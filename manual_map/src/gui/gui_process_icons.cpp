#include "gui_process_icons.hpp"
#include "gui_app.hpp"

#include <d3d11.h>

#include <shellapi.h>
#include <windows.h>

#include <cstring>
#include <list>
#include <unordered_map>

namespace
{
    constexpr int k_icon_size = 32;
    constexpr size_t k_max_cache_entries = 256;

    struct cache_entry
    {
        ID3D11ShaderResourceView* srv = nullptr;
        std::list< std::wstring >::iterator lru_it;
    };

    std::unordered_map< std::wstring , cache_entry > g_cache;
    std::list< std::wstring > g_lru;

    std::wstring basename( const std::wstring& path )
    {
        const auto slash = path.find_last_of( L"\\/" );

        if ( slash == std::wstring::npos )
        {
            return path;
        }

        return path.substr( slash + 1 );
    }

    ID3D11ShaderResourceView* create_srv_from_hicon( HICON icon )
    {
        if ( !icon )
        {
            return nullptr;
        }

        ID3D11Device* device = gui_app_device( );

        if ( !device )
        {
            return nullptr;
        }

        BITMAPINFO bitmap_info {};
        bitmap_info.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
        bitmap_info.bmiHeader.biWidth = k_icon_size;
        bitmap_info.bmiHeader.biHeight = -k_icon_size;
        bitmap_info.bmiHeader.biPlanes = 1;
        bitmap_info.bmiHeader.biBitCount = 32;
        bitmap_info.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HDC screen_dc = GetDC( nullptr );
        HDC memory_dc = CreateCompatibleDC( screen_dc );
        HBITMAP dib = CreateDIBSection( memory_dc , &bitmap_info , DIB_RGB_COLORS , &bits , nullptr , 0 );

        if ( !dib || !bits )
        {
            if ( dib )
            {
                DeleteObject( dib );
            }

            DeleteDC( memory_dc );
            ReleaseDC( nullptr , screen_dc );
            return nullptr;
        }

        HGDIOBJ previous = SelectObject( memory_dc , dib );
        std::memset( bits , 0 , static_cast< size_t >( k_icon_size ) * static_cast< size_t >( k_icon_size ) * 4U );
        DrawIconEx( memory_dc , 0 , 0 , icon , k_icon_size , k_icon_size , 0 , nullptr , DI_NORMAL );
        SelectObject( memory_dc , previous );

        D3D11_TEXTURE2D_DESC texture_desc {};
        texture_desc.Width = k_icon_size;
        texture_desc.Height = k_icon_size;
        texture_desc.MipLevels = 1;
        texture_desc.ArraySize = 1;
        texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texture_desc.SampleDesc.Count = 1;
        texture_desc.Usage = D3D11_USAGE_DEFAULT;
        texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initial_data {};
        initial_data.pSysMem = bits;
        initial_data.SysMemPitch = k_icon_size * 4;

        ID3D11Texture2D* texture = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;

        if ( SUCCEEDED( device->CreateTexture2D( &texture_desc , &initial_data , &texture ) ) )
        {
            device->CreateShaderResourceView( texture , nullptr , &srv );
            texture->Release( );
        }

        DeleteObject( dib );
        DeleteDC( memory_dc );
        ReleaseDC( nullptr , screen_dc );
        return srv;
    }

    HICON load_file_icon( const std::wstring& path )
    {
        if ( path.empty( ) )
        {
            return nullptr;
        }

        SHFILEINFOW file_info {};
        const UINT flags = SHGFI_ICON | SHGFI_LARGEICON;

        if ( SHGetFileInfoW( path.c_str( ) , 0 , &file_info , sizeof( file_info ) , flags ) )
        {
            return file_info.hIcon;
        }

        return nullptr;
    }

    HICON load_icon_for_path( const std::wstring& exe_path )
    {
        HICON icon = load_file_icon( exe_path );

        if ( icon )
        {
            return icon;
        }

        const std::wstring file_name = basename( exe_path );

        if ( file_name.empty( ) )
        {
            return nullptr;
        }

        const std::wstring system32_path = L"C:\\Windows\\System32\\" + file_name;
        return load_file_icon( system32_path );
    }

    void evict_oldest( )
    {
        if ( g_lru.empty( ) )
        {
            return;
        }

        const std::wstring& key = g_lru.back( );
        const auto it = g_cache.find( key );

        if ( it != g_cache.end( ) )
        {
            if ( it->second.srv )
            {
                it->second.srv->Release( );
            }

            g_cache.erase( it );
        }

        g_lru.pop_back( );
    }

    void touch_lru( const std::wstring& key , cache_entry& entry )
    {
        g_lru.erase( entry.lru_it );
        g_lru.push_front( key );
        entry.lru_it = g_lru.begin( );
    }
}

ImTextureID process_icons_get( const std::wstring& exe_path )
{
    if ( exe_path.empty( ) )
    {
        return ImTextureID( 0 );
    }

    const auto cached = g_cache.find( exe_path );

    if ( cached != g_cache.end( ) )
    {
        touch_lru( exe_path , cached->second );
        return reinterpret_cast< ImTextureID >( cached->second.srv );
    }

    const HICON icon = load_icon_for_path( exe_path );

    if ( !icon )
    {
        return ImTextureID( 0 );
    }

    ID3D11ShaderResourceView* srv = create_srv_from_hicon( icon );
    DestroyIcon( icon );

    if ( !srv )
    {
        return ImTextureID( 0 );
    }

    while ( g_cache.size( ) >= k_max_cache_entries )
    {
        evict_oldest( );
    }

    g_lru.push_front( exe_path );
    cache_entry entry {};
    entry.srv = srv;
    entry.lru_it = g_lru.begin( );
    g_cache.emplace( exe_path , entry );
    return reinterpret_cast< ImTextureID >( srv );
}

void process_icons_shutdown( )
{
    for ( auto& pair : g_cache )
    {
        if ( pair.second.srv )
        {
            pair.second.srv->Release( );
            pair.second.srv = nullptr;
        }
    }

    g_cache.clear( );
    g_lru.clear( );
}
