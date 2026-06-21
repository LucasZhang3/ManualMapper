#include <app/process_list.hpp>

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>

namespace
{
    void query_runtime_details( process_entry& process )
    {
        if ( process.details_loaded || !process.pid )
        {
            return;
        }

        process.details_loaded = true;

        HANDLE handle = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION , FALSE , process.pid );

        if ( !handle )
        {
            return;
        }

        BOOL wow64 = FALSE;

        if ( IsWow64Process( handle , &wow64 ) )
        {
            process.is_wow64 = wow64 == TRUE;
        }

        DWORD session = 0;

        if ( ProcessIdToSessionId( process.pid , &session ) )
        {
            process.session_id = session;
        }

        HANDLE token = nullptr;

        if ( OpenProcessToken( handle , TOKEN_QUERY , &token ) )
        {
            TOKEN_ELEVATION elevation {};
            DWORD size = 0;

            if ( GetTokenInformation( token , TokenElevation , &elevation , sizeof( elevation ) , &size ) )
            {
                process.is_elevated = elevation.TokenIsElevated != 0;
            }

            CloseHandle( token );
        }

        CloseHandle( handle );
    }
}

std::vector< process_entry > list_processes( )
{
    std::vector< process_entry > processes;
    const HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS , 0 );

    if ( snapshot == INVALID_HANDLE_VALUE )
    {
        return processes;
    }

    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof( entry );

    if ( Process32FirstW( snapshot , &entry ) )
    {
        do
        {
            process_entry process {};
            process.pid = entry.th32ProcessID;
            process.parent_pid = entry.th32ParentProcessID;
            process.name = entry.szExeFile;

            if ( process.pid && !process.name.empty( ) )
            {
                processes.push_back( std::move( process ) );
            }
        }
        while ( Process32NextW( snapshot , &entry ) );
    }

    CloseHandle( snapshot );
    sort_processes( processes , process_sort_mode::name );
    return processes;
}

void enrich_process_details( process_entry& process )
{
    query_runtime_details( process );
}

void sort_processes( std::vector< process_entry >& processes , process_sort_mode mode )
{
    switch ( mode )
    {
    case process_sort_mode::pid:
        std::sort( processes.begin( ) , processes.end( ) , [ ] ( const process_entry& a , const process_entry& b )
        {
            return a.pid < b.pid;
        } );
        break;
    case process_sort_mode::session:
        std::sort( processes.begin( ) , processes.end( ) , [ ] ( const process_entry& a , const process_entry& b )
        {
            if ( a.session_id != b.session_id )
            {
                return a.session_id < b.session_id;
            }

            return _wcsicmp( a.name.c_str( ) , b.name.c_str( ) ) < 0;
        } );
        break;
    case process_sort_mode::name:
    default:
        std::sort( processes.begin( ) , processes.end( ) , [ ] ( const process_entry& a , const process_entry& b )
        {
            return _wcsicmp( a.name.c_str( ) , b.name.c_str( ) ) < 0;
        } );
        break;
    }
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
        const auto parent_text = std::to_wstring( process.parent_pid );

        if ( name.find( needle ) != std::wstring::npos
            || pid_text.find( needle ) != std::wstring::npos
            || parent_text.find( needle ) != std::wstring::npos )
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
