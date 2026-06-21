#include <app/process_list.hpp>

#include <windows.h>
#include <winternl.h>

#include <algorithm>

#pragma comment( lib , "ntdll.lib" )

std::vector< process_entry > list_processes( )
{
    std::vector< process_entry > processes;

    ULONG size = 0;
    NtQuerySystemInformation( SystemProcessInformation , nullptr , 0 , &size );

    if ( !size )
    {
        return processes;
    }

    std::vector< uint8_t > buffer( size );
    auto info = reinterpret_cast< SYSTEM_PROCESS_INFORMATION* >( buffer.data( ) );

    if ( !NT_SUCCESS( NtQuerySystemInformation( SystemProcessInformation , info , size , &size ) ) )
    {
        return processes;
    }

    while ( true )
    {
        if ( info->ImageName.Buffer && info->ImageName.Length > 0 )
        {
            process_entry entry {};
            entry.pid = static_cast< uint32_t >( reinterpret_cast< uint64_t >( info->UniqueProcessId ) );
            entry.name.assign(
                info->ImageName.Buffer ,
                info->ImageName.Length / sizeof( wchar_t ) );

            if ( entry.pid && !entry.name.empty( ) )
            {
                processes.push_back( std::move( entry ) );
            }
        }

        if ( !info->NextEntryOffset )
        {
            break;
        }

        info = reinterpret_cast< SYSTEM_PROCESS_INFORMATION* >(
            reinterpret_cast< uint8_t* >( info ) + info->NextEntryOffset );
    }

    std::sort( processes.begin( ) , processes.end( ) , [ ] ( const process_entry& a , const process_entry& b )
    {
        return _wcsicmp( a.name.c_str( ) , b.name.c_str( ) ) < 0;
    } );

    return processes;
}

std::vector< process_entry > find_processes_by_name( const wchar_t* name )
{
    std::vector< process_entry > matches;

    for ( const auto& process : list_processes( ) )
    {
        if ( _wcsicmp( process.name.c_str( ) , name ) == 0 )
        {
            matches.push_back( process );
        }
    }

    return matches;
}

std::vector< process_entry > filter_processes( const std::vector< process_entry >& processes , const wchar_t* filter )
{
    if ( !filter || !filter [ 0 ] )
    {
        return processes;
    }

    std::wstring needle( filter );
    std::transform( needle.begin( ) , needle.end( ) , needle.begin( ) , towlower );

    std::vector< process_entry > filtered;

    for ( const auto& process : processes )
    {
        std::wstring name = process.name;
        std::transform( name.begin( ) , name.end( ) , name.begin( ) , towlower );

        const auto pid_text = std::to_wstring( process.pid );

        if ( name.find( needle ) != std::wstring::npos || pid_text.find( needle ) != std::wstring::npos )
        {
            filtered.push_back( process );
        }
    }

    return filtered;
}

bool wait_for_process( const wchar_t* name , uint32_t timeout_ms , uint32_t& out_pid )
{
    const auto start = GetTickCount64( );

    while ( GetTickCount64( ) - start < timeout_ms )
    {
        const auto matches = find_processes_by_name( name );

        if ( !matches.empty( ) )
        {
            out_pid = matches.front( ).pid;
            return true;
        }

        Sleep( 250 );
    }

    return false;
}
