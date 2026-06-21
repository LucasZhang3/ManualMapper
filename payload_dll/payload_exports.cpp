#include "payload_internal.h"

#include <payload/payload_shared.hpp>

uint32_t PayloadGetVersion( )
{
    return PAYLOAD_API_VERSION;
}

uint32_t PayloadGetStatus( payload_shared_status* out )
{
    if ( !out )
    {
        return 1;
    }

    auto& state = payload_state( );

    if ( state.status )
    {
        *out = *state.status;
        return 0;
    }

    return 2;
}

uint32_t PayloadRequestUnload( )
{
    InterlockedExchange( &payload_state( ).unload_requested , 1 );
    return 0;
}

uint32_t PayloadDumpModules( wchar_t* buffer , uint32_t buffer_chars )
{
    return payload_dump_modules_to_buffer( buffer , buffer_chars );
}

uint32_t PayloadSnapshotMemory( uint64_t address , uint32_t size , const wchar_t* path )
{
    return payload_snapshot_memory( address , size , path );
}
