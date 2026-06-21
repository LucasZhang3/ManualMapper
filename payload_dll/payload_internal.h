#pragma once

#include <payload/payload_shared.hpp>

#include <windows.h>

struct payload_runtime_state
{
    HMODULE module = nullptr;
    payload_config config {};
    payload_shared_status* status = nullptr;
    HANDLE status_mapping = nullptr;
    HANDLE heartbeat_thread = nullptr;
    HANDLE ipc_thread = nullptr;
    HANDLE hotkey_thread = nullptr;
    HANDLE overlay_thread = nullptr;
    volatile LONG shutdown = 0;
    volatile LONG unload_requested = 0;
    PVOID dll_notification_cookie = nullptr;
    void* loadlibrary_trampoline = nullptr;
    uint8_t loadlibrary_backup [ 16 ] = {};
    HWND overlay_window = nullptr;
};

payload_runtime_state& payload_state( );
bool payload_runtime_init( HMODULE module , void* reserved );
void payload_runtime_shutdown( );
DWORD WINAPI payload_init_thread( LPVOID parameter );

void payload_log_a( const char* message );
void payload_log_w( const wchar_t* message );
void payload_set_status( uint32_t state , const char* message = nullptr );
void payload_set_error( uint64_t code , const char* message );

bool payload_open_status_mapping( );
void payload_close_status_mapping( );
void payload_write_proof_file( );
void payload_write_host_snapshot( );
bool payload_start_heartbeat( );
bool payload_start_ipc_server( );
bool payload_start_module_watch( );
bool payload_install_loadlibrary_hook( );
bool payload_start_hotkeys( );
bool payload_start_overlay( );
bool payload_load_plugin( );
void payload_stop_workers( );

uint32_t payload_dump_modules_to_buffer( wchar_t* buffer , uint32_t buffer_chars );
uint32_t payload_snapshot_memory( uint64_t address , uint32_t size , const wchar_t* path );
