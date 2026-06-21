#include <app/config.hpp>

#include <shlobj.h>

#include <algorithm>
#include <fstream>
#include <string>

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

    int parse_int( const std::wstring& value , int fallback )
    {
        try
        {
            return std::stoi( value );
        }
        catch ( ... )
        {
            return fallback;
        }
    }

    float parse_float( const std::wstring& value , float fallback )
    {
        try
        {
            return std::stof( value );
        }
        catch ( ... )
        {
            return fallback;
        }
    }

    bool parse_bool( const std::wstring& value , bool fallback )
    {
        if ( value == L"1" || value == L"true" || value == L"yes" )
        {
            return true;
        }

        if ( value == L"0" || value == L"false" || value == L"no" )
        {
            return false;
        }

        return fallback;
    }

    void apply_line( app_config& config , const std::wstring& key , const std::wstring& value )
    {
        if ( key == L"last_dll" )
        {
            config.last_dll = value;
        }
        else if ( key == L"recent_dll" )
        {
            push_recent( config.recent_dlls , value , 8 );
        }
        else if ( key == L"window_x" )
        {
            config.window_x = parse_int( value , config.window_x );
        }
        else if ( key == L"window_y" )
        {
            config.window_y = parse_int( value , config.window_y );
        }
        else if ( key == L"window_w" )
        {
            config.window_w = parse_int( value , config.window_w );
        }
        else if ( key == L"window_h" )
        {
            config.window_h = parse_int( value , config.window_h );
        }
        else if ( key == L"panel_split" )
        {
            config.panel_split = parse_float( value , config.panel_split );
        }
        else if ( key == L"confirm_inject" )
        {
            config.confirm_inject = parse_bool( value , config.confirm_inject );
        }
        else if ( key == L"log_timestamps" )
        {
            config.log_timestamps = parse_bool( value , config.log_timestamps );
        }
        else if ( key == L"stealth_capture" )
        {
            config.stealth_capture = parse_bool( value , config.stealth_capture );
        }
        else if ( key == L"use_allowlist" )
        {
            config.use_allowlist = parse_bool( value , config.use_allowlist );
        }
        else if ( key == L"process_rule" )
        {
            if ( !value.empty( ) )
            {
                config.process_rules.push_back( value );
            }
        }
        else if ( key == L"cli_notes" )
        {
            config.cli_notes = value;
        }
        else if ( key == L"language" )
        {
            config.language = value.empty( ) ? L"en" : value;
        }
    }

    bool load_config_stream( std::wistream& file , app_config& config )
    {
        config = {};
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
            apply_line( config , key , value );
        }

        return true;
    }

    void write_config_stream( std::wostream& file , const app_config& config )
    {
        file << L"last_dll=" << config.last_dll << L"\n";
        file << L"window_x=" << config.window_x << L"\n";
        file << L"window_y=" << config.window_y << L"\n";
        file << L"window_w=" << config.window_w << L"\n";
        file << L"window_h=" << config.window_h << L"\n";
        file << L"panel_split=" << config.panel_split << L"\n";
        file << L"confirm_inject=" << ( config.confirm_inject ? L"1" : L"0" ) << L"\n";
        file << L"log_timestamps=" << ( config.log_timestamps ? L"1" : L"0" ) << L"\n";
        file << L"stealth_capture=" << ( config.stealth_capture ? L"1" : L"0" ) << L"\n";
        file << L"use_allowlist=" << ( config.use_allowlist ? L"1" : L"0" ) << L"\n";
        file << L"cli_notes=" << config.cli_notes << L"\n";
        file << L"language=" << config.language << L"\n";

        for ( const auto& item : config.recent_dlls )
        {
            file << L"recent_dll=" << item << L"\n";
        }

        for ( const auto& rule : config.process_rules )
        {
            file << L"process_rule=" << rule << L"\n";
        }
    }
}

std::wstring default_config_path( )
{
    return config_path( );
}

bool load_config( app_config& config )
{
    std::wifstream file( config_path( ) );

    if ( !file.is_open( ) )
    {
        return false;
    }

    return load_config_stream( file , config );
}

bool load_config_from_path( const std::wstring& path , app_config& config )
{
    std::wifstream file( path );

    if ( !file.is_open( ) )
    {
        return false;
    }

    return load_config_stream( file , config );
}

void save_config( const app_config& config )
{
    std::wofstream file( config_path( ) );

    if ( !file.is_open( ) )
    {
        return;
    }

    write_config_stream( file , config );
}

bool save_config_to_path( const std::wstring& path , const app_config& config )
{
    std::wofstream file( path );

    if ( !file.is_open( ) )
    {
        return false;
    }

    write_config_stream( file , config );
    return true;
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

bool is_process_allowed( const app_config& config , const std::wstring& process_name )
{
    if ( config.process_rules.empty( ) )
    {
        return true;
    }

    const auto matches_rule = [ & ] ( const std::wstring& rule ) -> bool
    {
        return _wcsicmp( process_name.c_str( ) , rule.c_str( ) ) == 0;
    };

    const bool listed = std::any_of( config.process_rules.begin( ) , config.process_rules.end( ) , matches_rule );
    return config.use_allowlist ? listed : !listed;
}
