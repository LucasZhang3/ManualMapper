#include <app/payload_bridge.hpp>

#include <app/pe_util.hpp>

#include <windows.h>

#include <chrono>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>

namespace
{
    void log_line( const std::function< void( const std::wstring& ) >& log , const std::wstring& message )
    {
        if ( log )
        {
            log( message );
        }
    }

    bool dll_supports_payload_protocol( const std::wstring& dll_path )
    {
        if ( pe_has_export( dll_path , "PayloadGetVersion" ) )
        {
            return true;
        }

        const auto filename = dll_path.substr( dll_path.find_last_of( L"\\/" ) + 1 );
        return _wcsicmp( filename.c_str( ) , L"payload_dll.dll" ) == 0;
    }

    void build_status_mapping_name( uint32_t pid , wchar_t* out , size_t out_count )
    {
        swprintf_s( out , out_count , L"Local\\ManualMapPayloadStatus_%u" , pid );
    }

    void build_ipc_pipe_name( uint32_t pid , wchar_t* out , size_t out_count )
    {
        swprintf_s( out , out_count , L"\\\\.\\pipe\\ManualMapPayload_%u" , pid );
    }

    std::wstring default_log_path( )
    {
        wchar_t temp [ MAX_PATH ] = {};
        GetTempPathW( MAX_PATH , temp );
        return std::wstring( temp ) + L"manual_map_payload.log";
    }

    std::wstring default_proof_dir( )
    {
        wchar_t temp [ MAX_PATH ] = {};
        GetTempPathW( MAX_PATH , temp );
        return std::wstring( temp ) + L"manual_map_proofs";
    }
}

payload_config build_payload_config( uint32_t target_pid , const app_config& config , const inject_request& request )
{
    payload_config payload_config_data {};
    payload_config_data.feature_flags = config.payload_feature_flags;

    if ( config.payload_silent )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_SILENT;
        payload_config_data.feature_flags &= ~PAYLOAD_FEATURE_SHOW_MESSAGE;
    }
    else if ( config.payload_show_message )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_SHOW_MESSAGE;
    }
    else
    {
        payload_config_data.feature_flags &= ~PAYLOAD_FEATURE_SHOW_MESSAGE;
    }

    if ( config.payload_file_log )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_FILE_LOG;
    }
    else
    {
        payload_config_data.feature_flags &= ~PAYLOAD_FEATURE_FILE_LOG;
    }

    if ( config.payload_debug_log )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_DEBUG_LOG;
    }
    else
    {
        payload_config_data.feature_flags &= ~PAYLOAD_FEATURE_DEBUG_LOG;
    }

    if ( config.payload_attach_console )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_ATTACH_CONSOLE;
    }

    if ( config.payload_heartbeat )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_HEARTBEAT;
    }
    else
    {
        payload_config_data.feature_flags &= ~PAYLOAD_FEATURE_HEARTBEAT;
    }

    if ( config.payload_proof_file )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_PROOF_FILE;
    }
    else
    {
        payload_config_data.feature_flags &= ~PAYLOAD_FEATURE_PROOF_FILE;
    }

    if ( config.payload_module_watch )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_MODULE_WATCH;
    }
    else
    {
        payload_config_data.feature_flags &= ~PAYLOAD_FEATURE_MODULE_WATCH;
    }

    if ( config.payload_loadlib_hook )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_LOADLIB_HOOK;
    }
    else
    {
        payload_config_data.feature_flags &= ~PAYLOAD_FEATURE_LOADLIB_HOOK;
    }

    if ( config.payload_hotkeys )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_HOTKEYS;
    }
    else
    {
        payload_config_data.feature_flags &= ~PAYLOAD_FEATURE_HOTKEYS;
    }

    if ( config.payload_ipc_pipe )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_IPC_PIPE;
    }
    else
    {
        payload_config_data.feature_flags &= ~PAYLOAD_FEATURE_IPC_PIPE;
    }

    if ( config.payload_host_snapshot )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_HOST_SNAPSHOT;
    }
    else
    {
        payload_config_data.feature_flags &= ~PAYLOAD_FEATURE_HOST_SNAPSHOT;
    }

    if ( config.payload_plugin_loader && !config.payload_plugin_path.empty( ) )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_PLUGIN_LOADER;
    }
    else
    {
        payload_config_data.feature_flags &= ~PAYLOAD_FEATURE_PLUGIN_LOADER;
    }

    if ( config.payload_overlay )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_OVERLAY;
    }
    else
    {
        payload_config_data.feature_flags &= ~PAYLOAD_FEATURE_OVERLAY;
    }

    if ( config.payload_delay_ms > 0 )
    {
        payload_config_data.feature_flags |= PAYLOAD_FEATURE_DELAYED_INIT;
        payload_config_data.delay_ms = config.payload_delay_ms;
    }

    payload_config_data.heartbeat_interval_ms = config.payload_heartbeat_ms > 0 ? config.payload_heartbeat_ms : 1000;
    payload_config_data.snapshot_mode = config.payload_snapshot_mode;

    if ( !config.payload_ui_message.empty( ) )
    {
        wcsncpy_s( payload_config_data.ui_message , config.payload_ui_message.c_str( ) , _TRUNCATE );
    }
    else
    {
        swprintf_s(
            payload_config_data.ui_message ,
            L"Injection successful!\r\n\r\nManual Map payload is active in this process (PID %u)." ,
            target_pid );
    }

    const std::wstring log_path = config.payload_log_path.empty( ) ? default_log_path( ) : config.payload_log_path;
    wcsncpy_s( payload_config_data.log_path , log_path.c_str( ) , _TRUNCATE );

    const std::wstring proof_dir = config.payload_proof_dir.empty( ) ? default_proof_dir( ) : config.payload_proof_dir;
    wcsncpy_s( payload_config_data.proof_dir , proof_dir.c_str( ) , _TRUNCATE );

    if ( !config.payload_plugin_path.empty( ) )
    {
        wcsncpy_s( payload_config_data.plugin_path , config.payload_plugin_path.c_str( ) , _TRUNCATE );
    }

    if ( !config.cli_notes.empty( ) )
    {
        wcsncpy_s( payload_config_data.cli_notes , config.cli_notes.c_str( ) , _TRUNCATE );
    }

    build_ipc_pipe_name( target_pid , payload_config_data.ipc_pipe_name , 64 );
    build_status_mapping_name( target_pid , payload_config_data.status_mapping_name , 64 );

    ( void )request;
    return payload_config_data;
}

payload_inject_session prepare_payload_session( uint32_t target_pid , const app_config& config , const inject_request& request )
{
    payload_inject_session session {};
    session.target_pid = target_pid;
    session.payload_enabled = dll_supports_payload_protocol( request.dll_path );

    if ( !session.payload_enabled || !config.payload_enabled )
    {
        return session;
    }

    session.config = build_payload_config( target_pid , config , request );

    session.status_mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE ,
        nullptr ,
        PAGE_READWRITE ,
        0 ,
        sizeof( payload_shared_status ) ,
        session.config.status_mapping_name );

    if ( !session.status_mapping )
    {
        session.payload_enabled = false;
        return session;
    }

    session.status_view = static_cast< payload_shared_status* >( MapViewOfFile(
        session.status_mapping ,
        FILE_MAP_ALL_ACCESS ,
        0 ,
        0 ,
        sizeof( payload_shared_status ) ) );

    if ( !session.status_view )
    {
        CloseHandle( session.status_mapping );
        session.status_mapping = nullptr;
        session.payload_enabled = false;
        return session;
    }

    ZeroMemory( session.status_view , sizeof( payload_shared_status ) );
    session.status_view->magic = PAYLOAD_SHARED_MAGIC;
    session.status_view->version = PAYLOAD_SHARED_VERSION;
    session.status_view->state = PAYLOAD_STATUS_INIT;
    session.status_view->pid = target_pid;
    return session;
}

void cleanup_payload_session( payload_inject_session& session )
{
    if ( session.status_view )
    {
        UnmapViewOfFile( session.status_view );
        session.status_view = nullptr;
    }

    if ( session.status_mapping )
    {
        CloseHandle( session.status_mapping );
        session.status_mapping = nullptr;
    }
}

bool verify_payload_handshake(
    payload_inject_session& session ,
    uint32_t timeout_ms ,
    const std::function< void( const std::wstring& ) >& log )
{
    if ( !session.payload_enabled || !session.status_view )
    {
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now( ) + std::chrono::milliseconds( timeout_ms );

    while ( std::chrono::steady_clock::now( ) < deadline )
    {
        const payload_shared_status snapshot = *session.status_view;

        if ( snapshot.magic == PAYLOAD_SHARED_MAGIC && snapshot.state == PAYLOAD_STATUS_RUNNING )
        {
            session.local_status = snapshot;
            std::wstringstream stream;
            stream << L"[payload] Handshake confirmed (heartbeat=" << snapshot.heartbeat_count
                   << L", modules=" << snapshot.host_module_count << L").";

            if ( snapshot.message [ 0 ] )
            {
                stream << L" " << snapshot.message;
            }

            log_line( log , stream.str( ) );
            return true;
        }

        if ( snapshot.state == PAYLOAD_STATUS_ERROR )
        {
            log_line( log , L"[payload] Payload reported error during attach." );
            return false;
        }

        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    }

    log_line( log , L"[payload] Handshake timed out — payload may still be initializing." );
    return false;
}

bool send_payload_ipc_command(
    const payload_inject_session& session ,
    payload_ipc_command command ,
    const std::function< void( const std::wstring& ) >& log )
{
    if ( !session.payload_enabled || session.config.ipc_pipe_name [ 0 ] == L'\0' )
    {
        return false;
    }

    HANDLE pipe = CreateFileW(
        session.config.ipc_pipe_name ,
        GENERIC_READ | GENERIC_WRITE ,
        0 ,
        nullptr ,
        OPEN_EXISTING ,
        0 ,
        nullptr );

    if ( pipe == INVALID_HANDLE_VALUE )
    {
        log_line( log , L"[payload] IPC pipe unavailable." );
        return false;
    }

    payload_ipc_request request {};
    request.command = static_cast< uint32_t >( command );
    DWORD written = 0;

    if ( !WriteFile( pipe , &request , sizeof( request ) , &written , nullptr ) || written != sizeof( request ) )
    {
        CloseHandle( pipe );
        return false;
    }

    payload_ipc_response response {};
    DWORD read = 0;

    if ( !ReadFile( pipe , &response , sizeof( response ) , &read , nullptr ) || read != sizeof( response ) )
    {
        CloseHandle( pipe );
        return false;
    }

    CloseHandle( pipe );

    if ( response.message [ 0 ] )
    {
        log_line( log , std::wstring( L"[payload] IPC: " ) + response.message );
    }

    return response.result == 0;
}
