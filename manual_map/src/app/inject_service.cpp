#include <app/inject_service.hpp>

#include <app/errors.hpp>
#include <app/process_list.hpp>
#include <manual_map/manual_map.hpp>

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#pragma comment( lib , "comdlg32.lib" )

namespace
{
    inline auto g_manual_map = std::make_shared< c_manual_map >( );

    void log_line( const inject_request& request , const std::wstring& message )
    {
        if ( request.log )
        {
            request.log( message );
        }
    }
}

std::vector< uint8_t > read_file_bytes( const std::wstring& path )
{
    std::ifstream file( std::filesystem::path( path ) , std::ios::binary | std::ios::ate );

    if ( !file.is_open( ) )
    {
        return {};
    }

    const auto size = file.tellg( );
    file.seekg( 0 , std::ios::beg );

    std::vector< uint8_t > bytes( static_cast< size_t >( size ) );
    file.read( reinterpret_cast< char* >( bytes.data( ) ) , size );
    return bytes;
}

std::wstring pick_dll_path( const std::wstring& initial )
{
    wchar_t path [ MAX_PATH ] = {};
    initial._Copy_s( path , MAX_PATH , initial.size( ) );

    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof( dialog );
    dialog.lpstrFilter = L"DLL Files (*.dll)\0*.dll\0All Files (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = L"Select DLL to inject";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if ( !GetOpenFileNameW( &dialog ) )
    {
        return {};
    }

    return path;
}

bool is_process_elevated( )
{
    HANDLE token = nullptr;

    if ( !OpenProcessToken( GetCurrentProcess( ) , TOKEN_QUERY , &token ) )
    {
        return false;
    }

    TOKEN_ELEVATION elevation {};
    DWORD size = 0;
    const auto ok = GetTokenInformation( token , TokenElevation , &elevation , sizeof( elevation ) , &size );
    CloseHandle( token );
    return ok && elevation.TokenIsElevated;
}

bool relaunch_as_admin( const wchar_t* args )
{
    wchar_t module_path [ MAX_PATH ] = {};
    GetModuleFileNameW( nullptr , module_path , MAX_PATH );

    SHELLEXECUTEINFOW info {};
    info.cbSize = sizeof( info );
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = module_path;
    info.lpParameters = args;
    info.nShow = SW_SHOWNORMAL;

    if ( !ShellExecuteExW( &info ) )
    {
        return false;
    }

    if ( info.hProcess )
    {
        CloseHandle( info.hProcess );
    }

    return true;
}

inject_result run_injection( inject_request request , app_config& config )
{
    inject_result result {};

    if ( request.dll_path.empty( ) )
    {
        result.code = 0x1001;
        log_line( request , L"Error: no DLL path provided." );
        return result;
    }

    auto payload = read_file_bytes( request.dll_path );

    if ( payload.empty( ) )
    {
        result.code = 0x1001;
        log_line( request , L"Error: failed to read DLL file." );
        return result;
    }

    log_line( request , L"[inject] Read " + std::to_wstring( payload.size( ) ) + L" bytes from disk into local buffer." );

    manual_map_options options {};
    options.log = request.log;

    if ( request.process_id )
    {
        result.target_pid = request.process_id;
        log_line( request , L"Using PID " + std::to_wstring( request.process_id ) + L"..." );
        result.code = g_manual_map->inject_pid( request.process_id , payload.data( ) , payload.size( ) , nullptr , 0 , options );
    }
    else
    {
        if ( request.process_name.empty( ) )
        {
            result.code = 0x1000;
            log_line( request , L"Error: no process name or PID provided." );
            return result;
        }

        uint32_t pid = 0;

        if ( request.wait_for_ms )
        {
            log_line( request , L"Waiting for " + request.process_name + L"..." );

            if ( !wait_for_process( request.process_name.c_str( ) , request.wait_for_ms , pid ) )
            {
                result.code = 0x1020;
                log_line( request , L"Timed out waiting for process." );
                return result;
            }

            log_line( request , L"Found " + request.process_name + L" (PID " + std::to_wstring( pid ) + L")." );
            result.target_pid = pid;
            result.code = g_manual_map->inject_pid( pid , payload.data( ) , payload.size( ) , nullptr , 0 , options );
        }
        else
        {
            const auto matches = find_processes_by_name( request.process_name.c_str( ) );

            if ( matches.empty( ) )
            {
                result.code = 0x1000;
                log_line( request , L"Process not found: " + request.process_name );
                return result;
            }

            if ( matches.size( ) > 1 )
            {
                log_line( request , L"Warning: multiple instances found. Using PID " + std::to_wstring( matches.front( ).pid ) + L"." );
            }

            result.target_pid = matches.front( ).pid;
            log_line( request , L"Target: " + request.process_name + L" (PID " + std::to_wstring( result.target_pid ) + L")" );
            result.code = g_manual_map->inject( request.process_name.c_str( ) , payload.data( ) , payload.size( ) , nullptr , 0 , options );
        }
    }

    if ( result.code == 0 )
    {
        remember_dll( config , request.dll_path );
        save_config( config );
        log_line( request , L"Injection succeeded." );
    }
    else
    {
        std::wstringstream stream;
        stream << L"Injection failed (0x" << std::hex << result.code << std::dec << L"): "
               << inject_error_message_w( result.code );
        log_line( request , stream.str( ) );
    }

    return result;
}
