#include "payload_internal.h"

#include <payload/payload_shared.hpp>

#include <stdio.h>

namespace
{
    payload_config g_fallback_config {};

    void build_default_config( payload_config& config )
    {
        config = {};
        config.magic = PAYLOAD_CONFIG_MAGIC;
        config.version = PAYLOAD_SHARED_VERSION;
        config.struct_size = sizeof( payload_config );
        config.feature_flags = PAYLOAD_FEATURE_DEFAULT;
        config.heartbeat_interval_ms = 1000;
        config.snapshot_mode = PAYLOAD_SNAPSHOT_ALL;
        swprintf_s( config.status_mapping_name , L"Local\\ManualMapPayloadStatus_%u" , GetCurrentProcessId( ) );
        swprintf_s( config.ipc_pipe_name , L"\\\\.\\pipe\\ManualMapPayload_%u" , GetCurrentProcessId( ) );
        swprintf_s(
            config.ui_message ,
            L"Injection successful!\r\n\r\nManual Map payload is active in this process (PID %u)." ,
            GetCurrentProcessId( ) );
    }
}

BOOL APIENTRY DllMain( HMODULE module , DWORD reason , LPVOID reserved )
{
    switch ( reason )
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls( module );

        if ( reserved )
        {
            const auto* incoming = static_cast< const payload_config* >( reserved );

            if ( incoming->magic == PAYLOAD_CONFIG_MAGIC && incoming->struct_size >= sizeof( payload_config ) )
            {
                return payload_runtime_init( module , reserved ) ? TRUE : FALSE;
            }
        }

        build_default_config( g_fallback_config );
        return payload_runtime_init( module , &g_fallback_config ) ? TRUE : FALSE;
    }
    case DLL_PROCESS_DETACH:
        payload_runtime_shutdown( );
        break;
    default:
        break;
    }

    return TRUE;
}
