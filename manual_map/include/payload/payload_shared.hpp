#pragma once

#include <cstdint>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

constexpr uint32_t PAYLOAD_SHARED_MAGIC = 0x504D4D4Du; // PMMM
constexpr uint32_t PAYLOAD_CONFIG_MAGIC = 0x50444647u; // PDFG
constexpr uint32_t PAYLOAD_SHARED_VERSION = 2u;
constexpr uint32_t PAYLOAD_API_VERSION = 2u;

constexpr uint32_t PAYLOAD_STATUS_INIT = 0u;
constexpr uint32_t PAYLOAD_STATUS_ATTACHING = 1u;
constexpr uint32_t PAYLOAD_STATUS_RUNNING = 2u;
constexpr uint32_t PAYLOAD_STATUS_UNLOADING = 3u;
constexpr uint32_t PAYLOAD_STATUS_DEAD = 4u;
constexpr uint32_t PAYLOAD_STATUS_ERROR = 0x80000000u;

enum payload_feature_flags : uint32_t
{
    PAYLOAD_FEATURE_SILENT = 1u << 0 ,
    PAYLOAD_FEATURE_SHOW_MESSAGE = 1u << 1 ,
    PAYLOAD_FEATURE_FILE_LOG = 1u << 2 ,
    PAYLOAD_FEATURE_DEBUG_LOG = 1u << 3 ,
    PAYLOAD_FEATURE_ATTACH_CONSOLE = 1u << 4 ,
    PAYLOAD_FEATURE_HEARTBEAT = 1u << 5 ,
    PAYLOAD_FEATURE_PROOF_FILE = 1u << 6 ,
    PAYLOAD_FEATURE_MODULE_WATCH = 1u << 7 ,
    PAYLOAD_FEATURE_LOADLIB_HOOK = 1u << 8 ,
    PAYLOAD_FEATURE_HOTKEYS = 1u << 9 ,
    PAYLOAD_FEATURE_IPC_PIPE = 1u << 10 ,
    PAYLOAD_FEATURE_HOST_SNAPSHOT = 1u << 11 ,
    PAYLOAD_FEATURE_DELAYED_INIT = 1u << 12 ,
    PAYLOAD_FEATURE_PLUGIN_LOADER = 1u << 13 ,
    PAYLOAD_FEATURE_OVERLAY = 1u << 14 ,
};

constexpr uint32_t PAYLOAD_FEATURE_DEFAULT =
    PAYLOAD_FEATURE_SHOW_MESSAGE
    | PAYLOAD_FEATURE_FILE_LOG
    | PAYLOAD_FEATURE_DEBUG_LOG
    | PAYLOAD_FEATURE_HEARTBEAT
    | PAYLOAD_FEATURE_PROOF_FILE
    | PAYLOAD_FEATURE_MODULE_WATCH
    | PAYLOAD_FEATURE_LOADLIB_HOOK
    | PAYLOAD_FEATURE_HOTKEYS
    | PAYLOAD_FEATURE_IPC_PIPE
    | PAYLOAD_FEATURE_HOST_SNAPSHOT
    | PAYLOAD_FEATURE_PLUGIN_LOADER
    | PAYLOAD_FEATURE_OVERLAY;

enum payload_snapshot_mode : uint32_t
{
    PAYLOAD_SNAPSHOT_NONE = 0 ,
    PAYLOAD_SNAPSHOT_MODULES = 1 ,
    PAYLOAD_SNAPSHOT_THREADS = 2 ,
    PAYLOAD_SNAPSHOT_ALL = 3 ,
};

enum payload_ipc_command : uint32_t
{
    PAYLOAD_IPC_PING = 1 ,
    PAYLOAD_IPC_UNLOAD = 2 ,
    PAYLOAD_IPC_DUMP_MODULES = 3 ,
    PAYLOAD_IPC_SNAPSHOT = 4 ,
    PAYLOAD_IPC_STATUS = 5 ,
};

#pragma pack( push , 1 )

struct payload_shared_status
{
    uint32_t magic = PAYLOAD_SHARED_MAGIC;
    uint32_t version = PAYLOAD_SHARED_VERSION;
    uint32_t state = PAYLOAD_STATUS_INIT;
    uint32_t pid = 0;
    uint64_t module_base = 0;
    uint64_t module_size = 0;
    uint64_t heartbeat_count = 0;
    uint64_t attach_tick = 0;
    uint64_t last_error = 0;
    uint32_t host_module_count = 0;
    uint32_t tls_ran = 0;
    char message [ 256 ] = {};
};

struct payload_config
{
    uint32_t magic = PAYLOAD_CONFIG_MAGIC;
    uint32_t version = PAYLOAD_SHARED_VERSION;
    uint32_t struct_size = sizeof( payload_config );
    uint32_t feature_flags = PAYLOAD_FEATURE_DEFAULT;
    uint32_t delay_ms = 0;
    uint32_t heartbeat_interval_ms = 1000;
    uint32_t snapshot_mode = PAYLOAD_SNAPSHOT_ALL;
    wchar_t ui_message [ 128 ] = L"Manual Map payload attached successfully.";
    wchar_t log_path [ MAX_PATH ] = {};
    wchar_t proof_dir [ MAX_PATH ] = {};
    wchar_t plugin_path [ MAX_PATH ] = {};
    wchar_t cli_notes [ 512 ] = {};
    wchar_t ipc_pipe_name [ 64 ] = {};
    wchar_t status_mapping_name [ 64 ] = {};
};

struct payload_ipc_request
{
    uint32_t command = 0;
    uint32_t size = 0;
    uint64_t address = 0;
    wchar_t path [ MAX_PATH ] = {};
};

struct payload_ipc_response
{
    uint32_t result = 0;
    uint32_t bytes_written = 0;
    wchar_t message [ 256 ] = {};
};

#pragma pack( pop )

#ifdef PAYLOAD_DLL_EXPORTS
#define PAYLOAD_API extern "C" __declspec( dllexport )
#else
#define PAYLOAD_API extern "C"
#endif

PAYLOAD_API uint32_t PayloadGetVersion( );
PAYLOAD_API uint32_t PayloadGetStatus( payload_shared_status* out );
PAYLOAD_API uint32_t PayloadRequestUnload( );
PAYLOAD_API uint32_t PayloadDumpModules( wchar_t* buffer , uint32_t buffer_chars );
PAYLOAD_API uint32_t PayloadSnapshotMemory( uint64_t address , uint32_t size , const wchar_t* path );
