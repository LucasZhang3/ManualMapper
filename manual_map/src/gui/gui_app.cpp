#include "gui_app.hpp"
#include "gui_process_icons.hpp"
#include "gui_state.hpp"
#include "gui_theme.hpp"

#include <app/config.hpp>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <d3d11.h>
#include <wincodec.h>

#include <vector>
#include <windows.h>

#pragma comment( lib , "d3d11.lib" )
#pragma comment( lib , "dxgi.lib" )
#pragma comment( lib , "windowscodecs.lib" )

void* g_gui_hwnd = nullptr;

namespace
{
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    IDXGISwapChain* g_swap_chain = nullptr;
    ID3D11RenderTargetView* g_render_target = nullptr;
    ID3D11ShaderResourceView* g_icon_srv = nullptr;
    bool g_swap_occluded = false;
    unsigned int g_resize_w = 0;
    unsigned int g_resize_h = 0;

    gui_app_state g_state {};

    void create_render_target( )
    {
        ID3D11Texture2D* back_buffer = nullptr;
        g_swap_chain->GetBuffer( 0 , IID_PPV_ARGS( &back_buffer ) );
        g_device->CreateRenderTargetView( back_buffer , nullptr , &g_render_target );
        back_buffer->Release( );
    }

    void cleanup_render_target( )
    {
        if ( g_render_target )
        {
            g_render_target->Release( );
            g_render_target = nullptr;
        }
    }

    void get_clear_color( float out [ 4 ] )
    {
        if ( g_state.light_mode )
        {
            out [ 0 ] = 0.94f;
            out [ 1 ] = 0.94f;
            out [ 2 ] = 0.95f;
            out [ 3 ] = 1.0f;
        }
        else
        {
            out [ 0 ] = 0.08f;
            out [ 1 ] = 0.08f;
            out [ 2 ] = 0.09f;
            out [ 3 ] = 1.0f;
        }
    }

    bool load_png_texture( const wchar_t* path , ID3D11ShaderResourceView** out_srv )
    {
        IWICImagingFactory* factory = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;

        if ( FAILED( CoCreateInstance( CLSID_WICImagingFactory , nullptr , CLSCTX_INPROC_SERVER , IID_PPV_ARGS( &factory ) ) ) )
        {
            return false;
        }

        if ( FAILED( factory->CreateDecoderFromFilename( path , nullptr , GENERIC_READ , WICDecodeMetadataCacheOnLoad , &decoder ) ) )
        {
            factory->Release( );
            return false;
        }

        decoder->GetFrame( 0 , &frame );
        factory->CreateFormatConverter( &converter );
        converter->Initialize( frame , GUID_WICPixelFormat32bppRGBA , WICBitmapDitherTypeNone , nullptr , 0.0 , WICBitmapPaletteTypeCustom );

        UINT width = 0;
        UINT height = 0;
        converter->GetSize( &width , &height );

        const UINT stride = width * 4;
        const UINT image_size = stride * height;
        std::vector< uint8_t > pixels( image_size );
        converter->CopyPixels( nullptr , stride , image_size , pixels.data( ) );

        D3D11_TEXTURE2D_DESC desc {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA subresource {};
        subresource.pSysMem = pixels.data( );
        subresource.SysMemPitch = stride;

        ID3D11Texture2D* texture = nullptr;

        if ( FAILED( g_device->CreateTexture2D( &desc , &subresource , &texture ) ) )
        {
            converter->Release( );
            frame->Release( );
            decoder->Release( );
            factory->Release( );
            return false;
        }

        g_device->CreateShaderResourceView( texture , nullptr , out_srv );
        texture->Release( );
        converter->Release( );
        frame->Release( );
        decoder->Release( );
        factory->Release( );
        return true;
    }
}

bool gui_app_create_device( void* hwnd )
{
    g_gui_hwnd = hwnd;

    DXGI_SWAP_CHAIN_DESC desc {};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = static_cast< HWND >( hwnd );
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL feature_level {};
    const D3D_FEATURE_LEVEL levels [ ] = { D3D_FEATURE_LEVEL_11_0 , D3D_FEATURE_LEVEL_10_0 };

    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr ,
        D3D_DRIVER_TYPE_HARDWARE ,
        nullptr ,
        0 ,
        levels ,
        2 ,
        D3D11_SDK_VERSION ,
        &desc ,
        &g_swap_chain ,
        &g_device ,
        &feature_level ,
        &g_context );

    if ( result == DXGI_ERROR_UNSUPPORTED )
    {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr ,
            D3D_DRIVER_TYPE_WARP ,
            nullptr ,
            0 ,
            levels ,
            2 ,
            D3D11_SDK_VERSION ,
            &desc ,
            &g_swap_chain ,
            &g_device ,
            &feature_level ,
            &g_context );
    }

    if ( result != S_OK )
    {
        return false;
    }

    create_render_target( );
    return true;
}

void gui_app_destroy_device( )
{
    if ( g_icon_srv )
    {
        g_icon_srv->Release( );
        g_icon_srv = nullptr;
    }

    cleanup_render_target( );

    if ( g_swap_chain )
    {
        g_swap_chain->Release( );
        g_swap_chain = nullptr;
    }

    if ( g_context )
    {
        g_context->Release( );
        g_context = nullptr;
    }

    if ( g_device )
    {
        g_device->Release( );
        g_device = nullptr;
    }
}

bool gui_app_init( )
{
    CoInitializeEx( nullptr , COINIT_APARTMENTTHREADED );

    IMGUI_CHECKVERSION( );
    ImGui::CreateContext( );
    ImGuiIO& io = ImGui::GetIO( );
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    load_config( g_state.config );
    g_state.light_mode = g_state.config.light_mode;

    const float body_size = g_state.config.compact_mode ? 14.0f : 15.0f;
    const float header_size = body_size + 2.0f;
    const float mono_size = g_state.config.compact_mode ? 13.0f : 14.0f;

    g_state.font_body = io.Fonts->AddFontFromFileTTF( "C:\\Windows\\Fonts\\segoeui.ttf" , body_size );
    g_state.font_header = io.Fonts->AddFontFromFileTTF( "C:\\Windows\\Fonts\\segoeuib.ttf" , header_size );
    g_state.font_mono = io.Fonts->AddFontFromFileTTF( "C:\\Windows\\Fonts\\cascadiamono.ttf" , mono_size );

    if ( !g_state.font_body )
    {
        g_state.font_body = io.Fonts->AddFontDefault( );
    }

    if ( !g_state.font_header )
    {
        g_state.font_header = g_state.font_body;
    }

    if ( !g_state.font_mono )
    {
        g_state.font_mono = g_state.font_body;
    }

    io.FontDefault = g_state.font_body;

    ImGui_ImplWin32_Init( g_gui_hwnd );
    ImGui_ImplDX11_Init( g_device , g_context );

    wchar_t icon_path [ MAX_PATH ] = {};
    GetModuleFileNameW( nullptr , icon_path , MAX_PATH );
    wchar_t* slash = wcsrchr( icon_path , L'\\' );

    if ( slash )
    {
        *( slash + 1 ) = L'\0';
        wcscat_s( icon_path , L"assets\\app_icon.png" );

        if ( !load_png_texture( icon_path , &g_icon_srv ) )
        {
            wcscpy_s( slash + 1 , MAX_PATH - ( slash + 1 - icon_path ) , L"src\\gui\\assets\\app_icon.png" );
            load_png_texture( icon_path , &g_icon_srv );
        }

        if ( g_icon_srv )
        {
            g_state.app_icon_texture = reinterpret_cast< ImTextureID >( g_icon_srv );
        }
    }

    gui_state_init( g_state );
    gui_theme_init( g_state );
    return true;
}

void gui_app_shutdown( )
{
    gui_state_save_window( g_state , g_gui_hwnd );
    process_icons_shutdown( );
    gui_state_shutdown( g_state );
    ImGui_ImplDX11_Shutdown( );
    ImGui_ImplWin32_Shutdown( );
    ImGui::DestroyContext( );
    CoUninitialize( );
}

void gui_app_on_resize( unsigned int width , unsigned int height )
{
    g_resize_w = width;
    g_resize_h = height;
    gui_state_on_resize( g_state , width , height );
}

void gui_app_new_frame( )
{
    if ( g_resize_w != 0 && g_resize_h != 0 )
    {
        cleanup_render_target( );
        g_swap_chain->ResizeBuffers( 0 , g_resize_w , g_resize_h , DXGI_FORMAT_UNKNOWN , 0 );
        g_resize_w = g_resize_h = 0;
        create_render_target( );
    }

    if ( g_swap_occluded && g_swap_chain->Present( 0 , DXGI_PRESENT_TEST ) == DXGI_STATUS_OCCLUDED )
    {
        Sleep( 10 );
    }

    g_swap_occluded = false;

    ImGui_ImplDX11_NewFrame( );
    ImGui_ImplWin32_NewFrame( );
    ImGui::NewFrame( );
    gui_state_new_frame( g_state );
}

void gui_app_render( )
{
    gui_state_render( g_state );

    ImGui::Render( );
    float clear [ 4 ] {};
    get_clear_color( clear );
    g_context->OMSetRenderTargets( 1 , &g_render_target , nullptr );
    g_context->ClearRenderTargetView( g_render_target , clear );
    ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData( ) );
}

void gui_app_present( )
{
    const HRESULT hr = g_swap_chain->Present( 1 , 0 );
    g_swap_occluded = ( hr == DXGI_STATUS_OCCLUDED );
}

void gui_app_set_dll_path( const wchar_t* path )
{
    gui_state_set_dll_path( g_state , path );
}

app_config* gui_app_config( )
{
    return &g_state.config;
}

gui_app_state* gui_app_state_ptr( )
{
    return &g_state;
}

ID3D11Device* gui_app_device( )
{
    return g_device;
}
