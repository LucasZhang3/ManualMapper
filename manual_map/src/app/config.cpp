#include <app/config.hpp>

#include <shlobj.h>

#include <algorithm>
#include <fstream>

namespace
{
    std::wstring config_path( )
    {
        wchar_t app_data [ MAX_PATH ] = {};

        if ( FAILED( SHGetFolderPathW( nullptr , CSIDL_APPDATA , nullptr , SHGFP_TYPE_CURRENT , app_data ) ) )
        {
            return L"settings.ini";
        }

        std::wstring path = app_data;
        path += L"\\manual_map";

        CreateDirectoryW( path.c_str( ) , nullptr );
        path += L"\\settings.ini";
        return path;
    }

    void trim( std::wstring& value )
    {
        while ( !value.empty( ) && ( value.front( ) == L' ' || value.front( ) == L'\t' ) )
        {
            value.erase( value.begin( ) );
        }

        while ( !value.empty( ) && ( value.back( ) == L' ' || value.back( ) == L'\t' ) )
        {
            value.pop_back( );
        }
    }

    void push_recent( std::vector< std::wstring >& items , const std::wstring& value , size_t max_items )
    {
        if ( value.empty( ) )
        {
            return;
        }

        items.erase(
            std::remove_if( items.begin( ) , items.end( ) , [ & ] ( const std::wstring& item )
            {
                return _wcsicmp( item.c_str( ) , value.c_str( ) ) == 0;
            } ) ,
            items.end( ) );

        items.insert( items.begin( ) , value );

        if ( items.size( ) > max_items )
        {
            items.resize( max_items );
        }
    }
}

bool load_config( app_config& config )
{
    config = {};

    std::wifstream file( config_path( ) );

    if ( !file.is_open( ) )
    {
        return false;
    }

    std::wstring line;

    while ( std::getline( file , line ) )
    {
        trim( line );

        if ( line.empty( ) || line [ 0 ] == L';' || line [ 0 ] == L'#' )
        {
            continue;
        }

        const auto separator = line.find( L'=' );

        if ( separator == std::wstring::npos )
        {
            continue;
        }

        auto key = line.substr( 0 , separator );
        auto value = line.substr( separator + 1 );
        trim( key );
        trim( value );

        if ( key == L"last_dll" )
        {
            config.last_dll = value;
        }
        else if ( key == L"recent_dll" )
        {
            push_recent( config.recent_dlls , value , 8 );
        }
    }

    return true;
}

void save_config( const app_config& config )
{
    std::wofstream file( config_path( ) );

    if ( !file.is_open( ) )
    {
        return;
    }

    file << L"last_dll=" << config.last_dll << L"\n";

    for ( const auto& item : config.recent_dlls )
    {
        file << L"recent_dll=" << item << L"\n";
    }
}

void remember_dll( app_config& config , const std::wstring& dll_path )
{
    config.last_dll = dll_path;
    push_recent( config.recent_dlls , dll_path , 8 );
}

void remove_recent_dll( app_config& config , const std::wstring& dll_path )
{
    config.recent_dlls.erase(
        std::remove_if( config.recent_dlls.begin( ) , config.recent_dlls.end( ) , [ & ] ( const std::wstring& item )
        {
            return _wcsicmp( item.c_str( ) , dll_path.c_str( ) ) == 0;
        } ) ,
        config.recent_dlls.end( ) );

    if ( _wcsicmp( config.last_dll.c_str( ) , dll_path.c_str( ) ) == 0 )
    {
        config.last_dll.clear( );
    }
}
