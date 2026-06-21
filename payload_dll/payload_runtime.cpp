#include "payload_internal.h"

#include <psapi.h>
#include <tlhelp32.h>

#include <stdio.h>

#pragma comment( lib , "psapi.lib" )
#pragma comment( lib , "advapi32.lib" )

#ifndef LDR_DLL_NOTIFICATION_REASON_LOADED
#define LDR_DLL_NOTIFICATION_REASON_LOADED 1
#endif

using PLDR_DLL_NOTIFICATION_FUNCTION = void( CALLBACK* )( ULONG , const struct _LDR_DLL_NOTIFICATION_DATA* , void* );

struct _LDR_DLL_LOADED_NOTIFICATION_DATA
{
    ULONG Flags;
    const struct { USHORT Length; USHORT MaximumLength; PWSTR Buffer; }* FullDllName;
    const struct { USHORT Length; USHORT MaximumLength; PWSTR Buffer; }* BaseDllName;
    PVOID DllBase;
    ULONG SizeOfImage;
};

struct _LDR_DLL_NOTIFICATION_DATA
{
    union
    {
        _LDR_DLL_LOADED_NOTIFICATION_DATA Loaded;
    };
};

namespace
{
    payload_runtime_state g_state {};

    using loadlibraryw_fn = HMODULE( WINAPI* )( LPCWSTR );

    loadlibraryw_fn g_original_loadlibraryw = nullptr;

    HMODULE WINAPI loadlibraryw_detour( LPCWSTR name )
    {
        if ( name )
        {
            wchar_t line [ 512 ] = {};
            swprintf_s( line , L"[hook] LoadLibraryW: %s" , name );
            payload_log_w( line );
        }

        return g_original_loadlibraryw ? g_original_loadlibraryw( name ) : nullptr;
    }

    void CALLBACK dll_notification(
        ULONG reason ,
        const _LDR_DLL_NOTIFICATION_DATA* data ,
        void* /*context*/ )
    {
        if ( reason != LDR_DLL_NOTIFICATION_REASON_LOADED || !data || !data->Loaded.BaseDllName )
        {
            return;
        }

        if ( data->Loaded.BaseDllName->Buffer && data->Loaded.BaseDllName->Length )
        {
            const size_t chars = data->Loaded.BaseDllName->Length / sizeof( wchar_t );
            wchar_t line [ 512 ] = {};
            swprintf_s( line , L"[watch] Module loaded: %.*s" , static_cast< int >( chars ) , data->Loaded.BaseDllName->Buffer );
            payload_log_w( line );
        }
    }

    bool write_file_bytes( const wchar_t* path , const void* data , DWORD size )
    {
        HANDLE file = CreateFileW( path , GENERIC_WRITE , 0 , nullptr , CREATE_ALWAYS , FILE_ATTRIBUTE_NORMAL , nullptr );

        if ( file == INVALID_HANDLE_VALUE )
        {
            return false;
        }

        DWORD written = 0;
        const BOOL ok = WriteFile( file , data , size , &written , nullptr );
        CloseHandle( file );
        return ok && written == size;
    }

    void append_file_line( const wchar_t* path , const wchar_t* line )
    {
        HANDLE file = CreateFileW( path , FILE_APPEND_DATA , FILE_SHARE_READ , nullptr , OPEN_ALWAYS , FILE_ATTRIBUTE_NORMAL , nullptr );

        if ( file == INVALID_HANDLE_VALUE )
        {
            return;
        }

        DWORD written = 0;
        WriteFile( file , line , static_cast< DWORD >( lstrlenW( line ) * sizeof( wchar_t ) ) , &written , nullptr );
        const wchar_t newline [ ] = L"\r\n";
        WriteFile( file , newline , sizeof( newline ) - sizeof( wchar_t ) , &written , nullptr );
        CloseHandle( file );
    }
}

payload_runtime_state& payload_state( )
{
    return g_state;
}

void payload_log_a( const char* message )
{
    auto& state = payload_state( );

    if ( !message )
    {
        return;
    }

    if ( state.config.feature_flags & PAYLOAD_FEATURE_DEBUG_LOG )
    {
        OutputDebugStringA( message );
        OutputDebugStringA( "\n" );
    }

    if ( ( state.config.feature_flags & PAYLOAD_FEATURE_FILE_LOG ) && state.config.log_path [ 0 ] )
    {
        wchar_t wide [ 512 ] = {};
        MultiByteToWideChar( CP_UTF8 , 0 , message , -1 , wide , 512 );
        append_file_line( state.config.log_path , wide );
    }
}

void payload_log_w( const wchar_t* message )
{
    if ( !message )
    {
        return;
    }

    char narrow [ 512 ] = {};
    WideCharToMultiByte( CP_UTF8 , 0 , message , -1 , narrow , 512 , nullptr , nullptr );
    payload_log_a( narrow );
}

void payload_set_status( uint32_t status_state , const char* message )
{
    auto& state = payload_state( );

    if ( state.status )
    {
        state.status->state = status_state;

        if ( message )
        {
            strncpy_s( state.status->message , message , _TRUNCATE );
        }
    }
}

void payload_set_error( uint64_t code , const char* message )
{
    auto& state = payload_state( );

    if ( state.status )
    {
        state.status->last_error = code;
    }

    payload_set_status( PAYLOAD_STATUS_ERROR , message );
}

bool payload_open_status_mapping( )
{
    auto& state = payload_state( );

    if ( !state.config.status_mapping_name [ 0 ] )
    {
        return false;
    }

    state.status_mapping = OpenFileMappingW( FILE_MAP_ALL_ACCESS , FALSE , state.config.status_mapping_name );

    if ( !state.status_mapping )
    {
        state.status_mapping = CreateFileMappingW(
            INVALID_HANDLE_VALUE ,
            nullptr ,
            PAGE_READWRITE ,
            0 ,
            sizeof( payload_shared_status ) ,
            state.config.status_mapping_name );
    }

    if ( !state.status_mapping )
    {
        return false;
    }

    state.status = static_cast< payload_shared_status* >( MapViewOfFile(
        state.status_mapping ,
        FILE_MAP_ALL_ACCESS ,
        0 ,
        0 ,
        sizeof( payload_shared_status ) ) );

    return state.status != nullptr;
}

void payload_close_status_mapping( )
{
    auto& state = payload_state( );

    if ( state.status )
    {
        UnmapViewOfFile( state.status );
        state.status = nullptr;
    }

    if ( state.status_mapping )
    {
        CloseHandle( state.status_mapping );
        state.status_mapping = nullptr;
    }
}

void payload_write_proof_file( )
{
    auto& state = payload_state( );

    if ( !( state.config.feature_flags & PAYLOAD_FEATURE_PROOF_FILE ) )
    {
        return;
    }

    CreateDirectoryW( state.config.proof_dir , nullptr );

    wchar_t path [ MAX_PATH ] = {};
    swprintf_s( path , L"%s\\manual_map_%u.json" , state.config.proof_dir , GetCurrentProcessId( ) );

    wchar_t content [ 1024 ] = {};
    swprintf_s(
        content ,
        L"{\"pid\":%u,\"base\":\"0x%llX\",\"size\":%llu,\"tick\":%llu,\"heartbeat\":%llu}" ,
        GetCurrentProcessId( ) ,
        state.status ? state.status->module_base : 0 ,
        state.status ? state.status->module_size : 0 ,
        state.status ? state.status->attach_tick : 0 ,
        state.status ? state.status->heartbeat_count : 0 );

    write_file_bytes( path , content , static_cast< DWORD >( lstrlenW( content ) * sizeof( wchar_t ) ) );
    payload_log_w( L"Proof file written." );
}

uint32_t payload_dump_modules_to_buffer( wchar_t* buffer , uint32_t buffer_chars )
{
    if ( !buffer || buffer_chars < 2 )
    {
        return 1;
    }

    buffer [ 0 ] = L'\0';

    HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32 , GetCurrentProcessId( ) );

    if ( snapshot == INVALID_HANDLE_VALUE )
    {
        return 2;
    }

    MODULEENTRY32W entry {};
    entry.dwSize = sizeof( entry );
    uint32_t count = 0;

    if ( Module32FirstW( snapshot , &entry ) )
    {
        do
        {
            wchar_t line [ 320 ] = {};
            swprintf_s( line , L"%s (0x%p)\n" , entry.szModule , entry.modBaseAddr );
            wcsncat_s( buffer , buffer_chars , line , _TRUNCATE );
            ++count;
        } while ( Module32NextW( snapshot , &entry ) );
    }

    CloseHandle( snapshot );

    auto& state = payload_state( );

    if ( state.status )
    {
        state.status->host_module_count = count;
    }

    return 0;
}

void payload_write_host_snapshot( )
{
    auto& state = payload_state( );

    if ( !( state.config.feature_flags & PAYLOAD_FEATURE_HOST_SNAPSHOT ) )
    {
        return;
    }

    const uint32_t mode = state.config.snapshot_mode;

    if ( mode == PAYLOAD_SNAPSHOT_NONE )
    {
        return;
    }

    CreateDirectoryW( state.config.proof_dir , nullptr );

    if ( mode & PAYLOAD_SNAPSHOT_MODULES )
    {
        wchar_t modules [ 8192 ] = {};
        payload_dump_modules_to_buffer( modules , 8192 );

        wchar_t path [ MAX_PATH ] = {};
        swprintf_s( path , L"%s\\modules_%u.txt" , state.config.proof_dir , GetCurrentProcessId( ) );
        write_file_bytes( path , modules , static_cast< DWORD >( lstrlenW( modules ) * sizeof( wchar_t ) ) );
        payload_log_w( L"Module snapshot written." );
    }

    if ( mode & PAYLOAD_SNAPSHOT_THREADS )
    {
        wchar_t path [ MAX_PATH ] = {};
        swprintf_s( path , L"%s\\threads_%u.txt" , state.config.proof_dir , GetCurrentProcessId( ) );

        HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD , 0 );
        wchar_t content [ 4096 ] = {};

        if ( snapshot != INVALID_HANDLE_VALUE )
        {
            THREADENTRY32 entry {};
            entry.dwSize = sizeof( entry );

            if ( Thread32First( snapshot , &entry ) )
            {
                do
                {
                    if ( entry.th32OwnerProcessID == GetCurrentProcessId( ) )
                    {
                        wchar_t line [ 128 ] = {};
                        swprintf_s( line , L"TID %u\n" , entry.th32ThreadID );
                        wcsncat_s( content , line , _TRUNCATE );
                    }
                } while ( Thread32Next( snapshot , &entry ) );
            }

            CloseHandle( snapshot );
        }

        write_file_bytes( path , content , static_cast< DWORD >( lstrlenW( content ) * sizeof( wchar_t ) ) );
        payload_log_w( L"Thread snapshot written." );
    }
}

uint32_t payload_snapshot_memory( uint64_t address , uint32_t size , const wchar_t* path )
{
    if ( !path || !size || !address )
    {
        return 1;
    }

    void* buffer = VirtualAlloc( nullptr , size , MEM_COMMIT | MEM_RESERVE , PAGE_READWRITE );

    if ( !buffer )
    {
        return 2;
    }

    SIZE_T read = 0;

    if ( !ReadProcessMemory( GetCurrentProcess( ) , reinterpret_cast< void* >( address ) , buffer , size , &read ) || read != size )
    {
        VirtualFree( buffer , 0 , MEM_RELEASE );
        return 3;
    }

    const bool ok = write_file_bytes( path , buffer , size );
    VirtualFree( buffer , 0 , MEM_RELEASE );
    return ok ? 0u : 4u;
}

DWORD WINAPI payload_heartbeat_thread( LPVOID )
{
    auto& state = payload_state( );

    while ( !state.shutdown )
    {
        if ( state.status )
        {
            InterlockedIncrement64( reinterpret_cast< volatile LONGLONG* >( &state.status->heartbeat_count ) );
        }

        Sleep( state.config.heartbeat_interval_ms > 0 ? state.config.heartbeat_interval_ms : 1000 );
    }

    return 0;
}

bool payload_start_heartbeat( )
{
    auto& state = payload_state( );

    if ( !( state.config.feature_flags & PAYLOAD_FEATURE_HEARTBEAT ) )
    {
        return false;
    }

    state.heartbeat_thread = CreateThread( nullptr , 0 , payload_heartbeat_thread , nullptr , 0 , nullptr );
    return state.heartbeat_thread != nullptr;
}

DWORD WINAPI payload_ipc_thread( LPVOID )
{
    auto& state = payload_state( );

    while ( !state.shutdown )
    {
        HANDLE pipe = CreateNamedPipeW(
            state.config.ipc_pipe_name ,
            PIPE_ACCESS_DUPLEX ,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT ,
            1 ,
            sizeof( payload_ipc_response ) ,
            sizeof( payload_ipc_request ) ,
            1000 ,
            nullptr );

        if ( pipe == INVALID_HANDLE_VALUE )
        {
            Sleep( 500 );
            continue;
        }

        const BOOL connected = ConnectNamedPipe( pipe , nullptr ) ? TRUE : ( GetLastError( ) == ERROR_PIPE_CONNECTED );

        if ( connected )
        {
            payload_ipc_request request {};
            DWORD read = 0;

            if ( ReadFile( pipe , &request , sizeof( request ) , &read , nullptr ) && read == sizeof( request ) )
            {
                payload_ipc_response response {};

                switch ( request.command )
                {
                case PAYLOAD_IPC_PING:
                    swprintf_s( response.message , L"pong pid=%u heartbeat=%llu" , GetCurrentProcessId( ) , state.status ? state.status->heartbeat_count : 0 );
                    response.result = 0;
                    break;
                case PAYLOAD_IPC_UNLOAD:
                    InterlockedExchange( &state.unload_requested , 1 );
                    swprintf_s( response.message , L"unload requested" );
                    response.result = 0;
                    break;
                case PAYLOAD_IPC_DUMP_MODULES:
                    response.result = payload_dump_modules_to_buffer( response.message , 256 );
                    break;
                case PAYLOAD_IPC_STATUS:
                    if ( state.status )
                    {
                        swprintf_s(
                            response.message ,
                            L"state=%u base=0x%llX hb=%llu" ,
                            state.status->state ,
                            state.status->module_base ,
                            state.status->heartbeat_count );
                    }

                    response.result = 0;
                    break;
                case PAYLOAD_IPC_SNAPSHOT:
                    response.result = payload_snapshot_memory( request.address , static_cast< uint32_t >( request.size ) , request.path );
                    swprintf_s( response.message , L"snapshot result=%u" , response.result );
                    break;
                default:
                    swprintf_s( response.message , L"unknown command" );
                    response.result = 1;
                    break;
                }

                DWORD written = 0;
                WriteFile( pipe , &response , sizeof( response ) , &written , nullptr );
            }
        }

        DisconnectNamedPipe( pipe );
        CloseHandle( pipe );
    }

    return 0;
}

bool payload_start_ipc_server( )
{
    auto& state = payload_state( );

    if ( !( state.config.feature_flags & PAYLOAD_FEATURE_IPC_PIPE ) || !state.config.ipc_pipe_name [ 0 ] )
    {
        return false;
    }

    state.ipc_thread = CreateThread( nullptr , 0 , payload_ipc_thread , nullptr , 0 , nullptr );
    return state.ipc_thread != nullptr;
}

bool payload_install_loadlibrary_hook( )
{
    auto& state = payload_state( );

    if ( !( state.config.feature_flags & PAYLOAD_FEATURE_LOADLIB_HOOK ) )
    {
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW( L"kernel32.dll" );

    if ( !kernel32 )
    {
        return false;
    }

    auto* target = reinterpret_cast< uint8_t* >( GetProcAddress( kernel32 , "LoadLibraryW" ) );

    if ( !target )
    {
        return false;
    }

    state.loadlibrary_trampoline = VirtualAlloc( nullptr , 64 , MEM_COMMIT | MEM_RESERVE , PAGE_EXECUTE_READWRITE );

    if ( !state.loadlibrary_trampoline )
    {
        return false;
    }

    memcpy( state.loadlibrary_backup , target , 14 );
    memcpy( state.loadlibrary_trampoline , target , 14 );

    auto* trampoline_jump = reinterpret_cast< uint8_t* >( state.loadlibrary_trampoline ) + 14;
    trampoline_jump [ 0 ] = 0xFF;
    trampoline_jump [ 1 ] = 0x25;
    *reinterpret_cast< uint32_t* >( trampoline_jump + 2 ) = 0;
    *reinterpret_cast< uint64_t* >( trampoline_jump + 6 ) = reinterpret_cast< uint64_t >( target + 14 );

    g_original_loadlibraryw = reinterpret_cast< loadlibraryw_fn >( state.loadlibrary_trampoline );

    DWORD old = 0;
    VirtualProtect( target , 14 , PAGE_EXECUTE_READWRITE , &old );

    target [ 0 ] = 0xFF;
    target [ 1 ] = 0x25;
    *reinterpret_cast< uint32_t* >( target + 2 ) = 0;
    *reinterpret_cast< uint64_t* >( target + 6 ) = reinterpret_cast< uint64_t >( loadlibraryw_detour );

    VirtualProtect( target , 14 , old , &old );
    FlushInstructionCache( GetCurrentProcess( ) , target , 14 );
    payload_log_a( "[hook] LoadLibraryW detour installed." );
    return true;
}

bool payload_start_module_watch( )
{
    auto& state = payload_state( );

    if ( !( state.config.feature_flags & PAYLOAD_FEATURE_MODULE_WATCH ) )
    {
        return false;
    }

    HMODULE ntdll = GetModuleHandleW( L"ntdll.dll" );

    if ( !ntdll )
    {
        return false;
    }

    using ldr_register_fn = NTSTATUS( NTAPI* )( ULONG , PLDR_DLL_NOTIFICATION_FUNCTION , void* , void** );
    auto register_notify = reinterpret_cast< ldr_register_fn >( GetProcAddress( ntdll , "LdrRegisterDllNotification" ) );

    if ( !register_notify )
    {
        return false;
    }

    return register_notify( 0 , dll_notification , nullptr , &state.dll_notification_cookie ) == 0;
}

LRESULT CALLBACK overlay_wndproc( HWND hwnd , UINT msg , WPARAM wparam , LPARAM lparam )
{
    switch ( msg )
    {
    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps {};
        HDC dc = BeginPaint( hwnd , &ps );
        SetBkMode( dc , TRANSPARENT );
        SetTextColor( dc , RGB( 220 , 240 , 255 ) );
        TextOutW( dc , 12 , 12 , L"Manual Map Payload Overlay (F8 toggle)" , 36 );
        EndPaint( hwnd , &ps );
        return 0;
    }
    default:
        break;
    }

    return DefWindowProcW( hwnd , msg , wparam , lparam );
}

DWORD WINAPI payload_overlay_thread( LPVOID )
{
    auto& state = payload_state( );
    WNDCLASSW wc {};
    wc.lpfnWndProc = overlay_wndproc;
    wc.hInstance = GetModuleHandleW( nullptr );
    wc.lpszClassName = L"ManualMapPayloadOverlay";
    RegisterClassW( &wc );

    state.overlay_window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW ,
        wc.lpszClassName ,
        L"Payload Overlay" ,
        WS_POPUP ,
        100 ,
        100 ,
        360 ,
        80 ,
        nullptr ,
        nullptr ,
        wc.hInstance ,
        nullptr );

    if ( !state.overlay_window )
    {
        return 0;
    }

    SetLayeredWindowAttributes( state.overlay_window , 0 , 200 , LWA_ALPHA );
    ShowWindow( state.overlay_window , SW_HIDE );

    MSG msg {};

    while ( !state.shutdown && GetMessageW( &msg , nullptr , 0 , 0 ) )
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }

    if ( state.overlay_window )
    {
        DestroyWindow( state.overlay_window );
        state.overlay_window = nullptr;
    }

    return 0;
}

bool payload_start_overlay( )
{
    auto& state = payload_state( );

    if ( !( state.config.feature_flags & PAYLOAD_FEATURE_OVERLAY ) )
    {
        return false;
    }

    state.overlay_thread = CreateThread( nullptr , 0 , payload_overlay_thread , nullptr , 0 , nullptr );
    return state.overlay_thread != nullptr;
}

DWORD WINAPI payload_hotkey_thread( LPVOID )
{
    auto& state = payload_state( );

    RegisterHotKey( nullptr , 1 , 0 , VK_F9 );
    RegisterHotKey( nullptr , 2 , 0 , VK_F10 );
    RegisterHotKey( nullptr , 3 , 0 , VK_F8 );

    while ( !state.shutdown )
    {
        MSG msg {};

        while ( PeekMessageW( &msg , nullptr , 0 , 0 , PM_REMOVE ) )
        {
            if ( msg.message == WM_HOTKEY )
            {
                if ( msg.wParam == 1 )
                {
                    payload_write_host_snapshot( );
                    payload_log_a( "[hotkey] F9 host snapshot." );
                }
                else if ( msg.wParam == 2 )
                {
                    InterlockedExchange( &state.unload_requested , 1 );
                    payload_log_a( "[hotkey] F10 unload requested." );
                }
                else if ( msg.wParam == 3 && state.overlay_window )
                {
                    const BOOL visible = IsWindowVisible( state.overlay_window );
                    ShowWindow( state.overlay_window , visible ? SW_HIDE : SW_SHOW );
                }
            }
        }

        Sleep( 50 );
    }

    UnregisterHotKey( nullptr , 1 );
    UnregisterHotKey( nullptr , 2 );
    UnregisterHotKey( nullptr , 3 );
    return 0;
}

bool payload_start_hotkeys( )
{
    auto& state = payload_state( );

    if ( !( state.config.feature_flags & PAYLOAD_FEATURE_HOTKEYS ) )
    {
        return false;
    }

    state.hotkey_thread = CreateThread( nullptr , 0 , payload_hotkey_thread , nullptr , 0 , nullptr );
    return state.hotkey_thread != nullptr;
}

bool payload_load_plugin( )
{
    auto& state = payload_state( );

    if ( !( state.config.feature_flags & PAYLOAD_FEATURE_PLUGIN_LOADER ) || !state.config.plugin_path [ 0 ] )
    {
        return false;
    }

    const HMODULE plugin = LoadLibraryW( state.config.plugin_path );

    if ( !plugin )
    {
        payload_log_w( L"Plugin load failed." );
        return false;
    }

    payload_log_w( L"Plugin loaded." );
    return true;
}

void payload_stop_workers( )
{
    auto& state = payload_state( );
    InterlockedExchange( &state.shutdown , 1 );

    auto wait_for = [ ] ( HANDLE& handle )
    {
        if ( handle )
        {
            WaitForSingleObject( handle , 2000 );
            CloseHandle( handle );
            handle = nullptr;
        }
    };

    wait_for( state.heartbeat_thread );
    wait_for( state.ipc_thread );
    wait_for( state.hotkey_thread );

    if ( state.overlay_window )
    {
        PostMessageW( state.overlay_window , WM_CLOSE , 0 , 0 );
    }

    wait_for( state.overlay_thread );

    if ( state.dll_notification_cookie )
    {
        HMODULE ntdll = GetModuleHandleW( L"ntdll.dll" );

        if ( ntdll )
        {
            using ldr_unregister_fn = NTSTATUS( NTAPI* )( void* );
            auto unregister = reinterpret_cast< ldr_unregister_fn >( GetProcAddress( ntdll , "LdrUnregisterDllNotification" ) );

            if ( unregister )
            {
                unregister( state.dll_notification_cookie );
            }
        }

        state.dll_notification_cookie = nullptr;
    }

    if ( g_original_loadlibraryw )
    {
        HMODULE kernel32 = GetModuleHandleW( L"kernel32.dll" );

        if ( kernel32 )
        {
            auto* target = reinterpret_cast< uint8_t* >( GetProcAddress( kernel32 , "LoadLibraryW" ) );
            DWORD old = 0;
            VirtualProtect( target , 14 , PAGE_EXECUTE_READWRITE , &old );
            memcpy( target , state.loadlibrary_backup , 14 );
            VirtualProtect( target , 14 , old , &old );
            FlushInstructionCache( GetCurrentProcess( ) , target , 14 );
        }
    }

    if ( state.loadlibrary_trampoline )
    {
        VirtualFree( state.loadlibrary_trampoline , 0 , MEM_RELEASE );
        state.loadlibrary_trampoline = nullptr;
    }
}

DWORD WINAPI payload_init_thread( LPVOID parameter )
{
    auto& state = payload_state( );
    const HMODULE module = static_cast< HMODULE >( parameter );

    if ( state.config.feature_flags & PAYLOAD_FEATURE_DELAYED_INIT )
    {
        Sleep( state.config.delay_ms );
    }

    __try
    {
        if ( state.config.feature_flags & PAYLOAD_FEATURE_ATTACH_CONSOLE )
        {
            AllocConsole( );
            payload_log_a( "[payload] Console attached." );
        }

        if ( state.config.cli_notes [ 0 ] )
        {
            wchar_t line [ 600 ] = {};
            swprintf_s( line , L"[payload] CLI notes: %s" , state.config.cli_notes );
            payload_log_w( line );
        }

        payload_set_status( PAYLOAD_STATUS_ATTACHING , "initializing" );
        payload_write_host_snapshot( );
        payload_write_proof_file( );
        payload_start_module_watch( );
        payload_install_loadlibrary_hook( );
        payload_start_heartbeat( );
        payload_start_ipc_server( );
        payload_start_hotkeys( );
        payload_start_overlay( );
        payload_load_plugin( );

        if ( state.status )
        {
            state.status->module_base = reinterpret_cast< uint64_t >( module );
            state.status->module_size = 0;
            state.status->attach_tick = GetTickCount64( );
            state.status->pid = GetCurrentProcessId( );
            state.status->tls_ran = 1;
        }

        payload_set_status( PAYLOAD_STATUS_RUNNING , "running" );
        payload_log_a( "[payload] Initialization complete." );

        if ( state.config.feature_flags & PAYLOAD_FEATURE_SHOW_MESSAGE )
        {
            wchar_t message [ 256 ] = {};
            wcsncpy_s( message , state.config.ui_message , _TRUNCATE );

            if ( !message [ 0 ] )
            {
                swprintf_s(
                    message ,
                    L"Injection successful!\r\n\r\nManual Map payload is active in this process (PID %u)." ,
                    GetCurrentProcessId( ) );
            }

            MessageBoxW(
                nullptr ,
                message ,
                L"Manual Map — Injection Successful" ,
                MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND );
        }

        while ( !state.shutdown )
        {
            if ( state.unload_requested )
            {
                payload_set_status( PAYLOAD_STATUS_UNLOADING , "unloading" );
                break;
            }

            Sleep( 100 );
        }
    }
    __except ( EXCEPTION_EXECUTE_HANDLER )
    {
        payload_set_error( GetExceptionCode( ) , "structured exception during init" );
    }

    payload_stop_workers( );
    payload_set_status( PAYLOAD_STATUS_DEAD , "stopped" );
    return 0;
}

bool payload_runtime_init( HMODULE module , void* reserved )
{
    auto& state = payload_state( );
    state.module = module;

    if ( !reserved )
    {
        return false;
    }

    const auto* config = static_cast< const payload_config* >( reserved );
    state.config = *config;
    DisableThreadLibraryCalls( module );

    if ( !payload_open_status_mapping( ) )
    {
        return false;
    }

    if ( state.status )
    {
        state.status->pid = GetCurrentProcessId( );
        state.status->module_base = reinterpret_cast< uint64_t >( module );
    }

    payload_set_status( PAYLOAD_STATUS_INIT , "starting" );

    HANDLE thread = CreateThread( nullptr , 0 , payload_init_thread , module , 0 , nullptr );
    return thread != nullptr;
}

void payload_runtime_shutdown( )
{
    payload_stop_workers( );
    payload_close_status_mapping( );
}
