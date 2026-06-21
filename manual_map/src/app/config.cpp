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
            value.pop_back( ) ;
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

    uint32_t parse_uint( const std::wstring& value , uint32_t fallback )
    {
        try
        {
            return static_cast< uint32_t >( std::stoul( value ) );
        }
        catch ( ... )
        {
            return fallback;
        }
    }

    injection_history_entry* current_history( app_config& config )
    {
        if ( config.injection_history.empty( ) )
        {
            config.injection_history.push_back( {} );
        }

        return &config.injection_history.back( );
    }

    inject_profile* current_profile( app_config& config )
    {
        if ( config.profiles.empty( ) )
        {
            config.profiles.push_back( {} );
        }

        return &config.profiles.back( );
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
        else if ( key == L"light_mode" )
        {
            config.light_mode = parse_bool( value , config.light_mode );
        }
        else if ( key == L"compact_mode" )
        {
            config.compact_mode = parse_bool( value , config.compact_mode );
        }
        else if ( key == L"first_run_complete" )
        {
            config.first_run_complete = parse_bool( value , config.first_run_complete );
        }
        else if ( key == L"min_to_tray" )
        {
            config.min_to_tray = parse_bool( value , config.min_to_tray );
        }
        else if ( key == L"watch_folder" )
        {
            config.watch_folder = value;
        }
        else if ( key == L"favorite_pid" )
        {
            config.favorite_pids.push_back( parse_uint( value , 0 ) );
        }
        else if ( key == L"queue_dll" )
        {
            if ( !value.empty( ) )
            {
                config.dll_queue.push_back( value );
            }
        }
        else if ( key == L"show_process_tree" )
        {
            config.show_process_tree = parse_bool( value , config.show_process_tree );
        }
        else if ( key == L"settings_appearance_open" )
        {
            config.settings_appearance_open = parse_bool( value , config.settings_appearance_open );
        }
        else if ( key == L"settings_capture_open" )
        {
            config.settings_capture_open = parse_bool( value , config.settings_capture_open );
        }
        else if ( key == L"settings_injection_open" )
        {
            config.settings_injection_open = parse_bool( value , config.settings_injection_open );
        }
        else if ( key == L"settings_logging_open" )
        {
            config.settings_logging_open = parse_bool( value , config.settings_logging_open );
        }
        else if ( key == L"settings_safety_open" )
        {
            config.settings_safety_open = parse_bool( value , config.settings_safety_open );
        }
        else if ( key == L"settings_profiles_open" )
        {
            config.settings_profiles_open = parse_bool( value , config.settings_profiles_open );
        }
        else if ( key == L"settings_advanced_open" )
        {
            config.settings_advanced_open = parse_bool( value , config.settings_advanced_open );
        }
        else if ( key == L"settings_payload_open" )
        {
            config.settings_payload_open = parse_bool( value , config.settings_payload_open );
        }
        else if ( key == L"payload_enabled" )
        {
            config.payload_enabled = parse_bool( value , config.payload_enabled );
        }
        else if ( key == L"payload_silent" )
        {
            config.payload_silent = parse_bool( value , config.payload_silent );
        }
        else if ( key == L"payload_show_message" )
        {
            config.payload_show_message = parse_bool( value , config.payload_show_message );
        }
        else if ( key == L"payload_file_log" )
        {
            config.payload_file_log = parse_bool( value , config.payload_file_log );
        }
        else if ( key == L"payload_debug_log" )
        {
            config.payload_debug_log = parse_bool( value , config.payload_debug_log );
        }
        else if ( key == L"payload_attach_console" )
        {
            config.payload_attach_console = parse_bool( value , config.payload_attach_console );
        }
        else if ( key == L"payload_heartbeat" )
        {
            config.payload_heartbeat = parse_bool( value , config.payload_heartbeat );
        }
        else if ( key == L"payload_proof_file" )
        {
            config.payload_proof_file = parse_bool( value , config.payload_proof_file );
        }
        else if ( key == L"payload_module_watch" )
        {
            config.payload_module_watch = parse_bool( value , config.payload_module_watch );
        }
        else if ( key == L"payload_loadlib_hook" )
        {
            config.payload_loadlib_hook = parse_bool( value , config.payload_loadlib_hook );
        }
        else if ( key == L"payload_hotkeys" )
        {
            config.payload_hotkeys = parse_bool( value , config.payload_hotkeys );
        }
        else if ( key == L"payload_ipc_pipe" )
        {
            config.payload_ipc_pipe = parse_bool( value , config.payload_ipc_pipe );
        }
        else if ( key == L"payload_host_snapshot" )
        {
            config.payload_host_snapshot = parse_bool( value , config.payload_host_snapshot );
        }
        else if ( key == L"payload_plugin_loader" )
        {
            config.payload_plugin_loader = parse_bool( value , config.payload_plugin_loader );
        }
        else if ( key == L"payload_overlay" )
        {
            config.payload_overlay = parse_bool( value , config.payload_overlay );
        }
        else if ( key == L"payload_delay_ms" )
        {
            config.payload_delay_ms = parse_uint( value , config.payload_delay_ms );
        }
        else if ( key == L"payload_heartbeat_ms" )
        {
            config.payload_heartbeat_ms = parse_uint( value , config.payload_heartbeat_ms );
        }
        else if ( key == L"payload_snapshot_mode" )
        {
            config.payload_snapshot_mode = parse_uint( value , config.payload_snapshot_mode );
        }
        else if ( key == L"payload_ui_message" )
        {
            config.payload_ui_message = value;
        }
        else if ( key == L"payload_log_path" )
        {
            config.payload_log_path = value;
        }
        else if ( key == L"payload_proof_dir" )
        {
            config.payload_proof_dir = value;
        }
        else if ( key == L"payload_plugin_path" )
        {
            config.payload_plugin_path = value;
        }
        else if ( key == L"history_target" )
        {
            config.injection_history.push_back( {} );
            current_history( config )->target = value;
        }
        else if ( key == L"history_dll" )
        {
            current_history( config )->dll = value;
        }
        else if ( key == L"history_timestamp" )
        {
            current_history( config )->timestamp = value;
        }
        else if ( key == L"history_success" )
        {
            current_history( config )->success = parse_bool( value , false );
        }
        else if ( key == L"profile_name" )
        {
            config.profiles.push_back( {} );
            current_profile( config )->name = value;
        }
        else if ( key == L"profile_dll" )
        {
            current_profile( config )->dll_path = value;
        }
        else if ( key == L"profile_process" )
        {
            current_profile( config )->process_name = value;
        }
        else if ( key == L"profile_wait" )
        {
            current_profile( config )->wait_for_process = parse_bool( value , false );
        }
        else if ( key == L"profile_inject_all" )
        {
            current_profile( config )->inject_all = parse_bool( value , false );
        }
        else if ( key == L"profile_delay" )
        {
            current_profile( config )->inject_delay_sec = parse_int( value , 0 );
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

        config.profiles.erase(
            std::remove_if( config.profiles.begin( ) , config.profiles.end( ) , [ ] ( const inject_profile& profile )
            {
                return profile.name.empty( );
            } ) ,
            config.profiles.end( ) );

        config.injection_history.erase(
            std::remove_if( config.injection_history.begin( ) , config.injection_history.end( ) , [ ] ( const injection_history_entry& entry )
            {
                return entry.target.empty( ) && entry.dll.empty( );
            } ) ,
            config.injection_history.end( ) );

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
        file << L"light_mode=" << ( config.light_mode ? L"1" : L"0" ) << L"\n";
        file << L"compact_mode=" << ( config.compact_mode ? L"1" : L"0" ) << L"\n";
        file << L"first_run_complete=" << ( config.first_run_complete ? L"1" : L"0" ) << L"\n";
        file << L"min_to_tray=" << ( config.min_to_tray ? L"1" : L"0" ) << L"\n";
        file << L"watch_folder=" << config.watch_folder << L"\n";
        file << L"show_process_tree=" << ( config.show_process_tree ? L"1" : L"0" ) << L"\n";
        file << L"settings_appearance_open=" << ( config.settings_appearance_open ? L"1" : L"0" ) << L"\n";
        file << L"settings_capture_open=" << ( config.settings_capture_open ? L"1" : L"0" ) << L"\n";
        file << L"settings_injection_open=" << ( config.settings_injection_open ? L"1" : L"0" ) << L"\n";
        file << L"settings_logging_open=" << ( config.settings_logging_open ? L"1" : L"0" ) << L"\n";
        file << L"settings_safety_open=" << ( config.settings_safety_open ? L"1" : L"0" ) << L"\n";
        file << L"settings_profiles_open=" << ( config.settings_profiles_open ? L"1" : L"0" ) << L"\n";
        file << L"settings_advanced_open=" << ( config.settings_advanced_open ? L"1" : L"0" ) << L"\n";
        file << L"settings_payload_open=" << ( config.settings_payload_open ? L"1" : L"0" ) << L"\n";
        file << L"payload_enabled=" << ( config.payload_enabled ? L"1" : L"0" ) << L"\n";
        file << L"payload_silent=" << ( config.payload_silent ? L"1" : L"0" ) << L"\n";
        file << L"payload_show_message=" << ( config.payload_show_message ? L"1" : L"0" ) << L"\n";
        file << L"payload_file_log=" << ( config.payload_file_log ? L"1" : L"0" ) << L"\n";
        file << L"payload_debug_log=" << ( config.payload_debug_log ? L"1" : L"0" ) << L"\n";
        file << L"payload_attach_console=" << ( config.payload_attach_console ? L"1" : L"0" ) << L"\n";
        file << L"payload_heartbeat=" << ( config.payload_heartbeat ? L"1" : L"0" ) << L"\n";
        file << L"payload_proof_file=" << ( config.payload_proof_file ? L"1" : L"0" ) << L"\n";
        file << L"payload_module_watch=" << ( config.payload_module_watch ? L"1" : L"0" ) << L"\n";
        file << L"payload_loadlib_hook=" << ( config.payload_loadlib_hook ? L"1" : L"0" ) << L"\n";
        file << L"payload_hotkeys=" << ( config.payload_hotkeys ? L"1" : L"0" ) << L"\n";
        file << L"payload_ipc_pipe=" << ( config.payload_ipc_pipe ? L"1" : L"0" ) << L"\n";
        file << L"payload_host_snapshot=" << ( config.payload_host_snapshot ? L"1" : L"0" ) << L"\n";
        file << L"payload_plugin_loader=" << ( config.payload_plugin_loader ? L"1" : L"0" ) << L"\n";
        file << L"payload_overlay=" << ( config.payload_overlay ? L"1" : L"0" ) << L"\n";
        file << L"payload_delay_ms=" << config.payload_delay_ms << L"\n";
        file << L"payload_heartbeat_ms=" << config.payload_heartbeat_ms << L"\n";
        file << L"payload_snapshot_mode=" << config.payload_snapshot_mode << L"\n";
        file << L"payload_ui_message=" << config.payload_ui_message << L"\n";
        file << L"payload_log_path=" << config.payload_log_path << L"\n";
        file << L"payload_proof_dir=" << config.payload_proof_dir << L"\n";
        file << L"payload_plugin_path=" << config.payload_plugin_path << L"\n";

        for ( const auto& item : config.recent_dlls )
        {
            file << L"recent_dll=" << item << L"\n";
        }

        for ( const auto& rule : config.process_rules )
        {
            file << L"process_rule=" << rule << L"\n";
        }

        for ( const auto pid : config.favorite_pids )
        {
            file << L"favorite_pid=" << pid << L"\n";
        }

        for ( const auto& dll : config.dll_queue )
        {
            file << L"queue_dll=" << dll << L"\n";
        }

        for ( const auto& entry : config.injection_history )
        {
            file << L"history_timestamp=" << entry.timestamp << L"\n";
            file << L"history_target=" << entry.target << L"\n";
            file << L"history_dll=" << entry.dll << L"\n";
            file << L"history_success=" << ( entry.success ? L"1" : L"0" ) << L"\n";
        }

        for ( const auto& profile : config.profiles )
        {
            file << L"profile_name=" << profile.name << L"\n";
            file << L"profile_dll=" << profile.dll_path << L"\n";
            file << L"profile_process=" << profile.process_name << L"\n";
            file << L"profile_wait=" << ( profile.wait_for_process ? L"1" : L"0" ) << L"\n";
            file << L"profile_inject_all=" << ( profile.inject_all ? L"1" : L"0" ) << L"\n";
            file << L"profile_delay=" << profile.inject_delay_sec << L"\n";
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

void add_injection_history( app_config& config , const injection_history_entry& entry )
{
    config.injection_history.insert( config.injection_history.begin( ) , entry );

    if ( config.injection_history.size( ) > 20 )
    {
        config.injection_history.resize( 20 );
    }
}

void clear_injection_history( app_config& config )
{
    config.injection_history.clear( );
}
