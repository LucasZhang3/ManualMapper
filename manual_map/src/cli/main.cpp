#define NOMINMAX

#include <windows.h>
#include <shellapi.h>

#include <app/config.hpp>
#include <app/errors.hpp>
#include <app/inject_service.hpp>
#include <app/process_list.hpp>

#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace
{
    std::wstring to_wide( const std::string& text )
    {
        if ( text.empty( ) )
        {
            return {};
        }

        const auto length = MultiByteToWideChar( CP_UTF8 , 0 , text.c_str( ) , -1 , nullptr , 0 );

        if ( length <= 0 )
        {
            return {};
        }

        std::wstring wide( static_cast< size_t >( length ) , L'\0' );
        MultiByteToWideChar( CP_UTF8 , 0 , text.c_str( ) , -1 , wide.data( ) , length );

        if ( !wide.empty( ) && wide.back( ) == L'\0' )
        {
            wide.pop_back( );
        }

        return wide;
    }

    void print_help( )
    {
        std::wcout << L"Manual Map Injector (CLI)\n\n";
        std::wcout << L"Usage:\n";
        std::wcout << L"  manual_map.exe [options]\n\n";
        std::wcout << L"Options:\n";
        std::wcout << L"  --process <name>     Target process name (e.g. notepad.exe)\n";
        std::wcout << L"  --pid <id>           Target process ID\n";
        std::wcout << L"  --dll <path>         DLL file to inject\n";
        std::wcout << L"  --list               List running processes and exit\n";
        std::wcout << L"  --search <text>      Filter process list by name or PID\n";
        std::wcout << L"  --wait <seconds>     Wait for process to start before injecting\n";
        std::wcout << L"  --admin              Relaunch as administrator and exit\n";
        std::wcout << L"  --gui                Launch the GUI version\n";
        std::wcout << L"  --help               Show this help\n\n";
        std::wcout << L"If no options are given, interactive mode is used.\n";
    }

    void print_process_list( const std::wstring& filter = {} )
    {
        const auto processes = filter_processes( list_processes( ) , filter.c_str( ) );

        std::wcout << L"\nRunning processes";

        if ( !filter.empty( ) )
        {
            std::wcout << L" matching \"" << filter << L"\"";
        }

        std::wcout << L":\n";
        std::wcout << L"  PID       Name\n";
        std::wcout << L"  --------  ------------------------------\n";

        for ( const auto& process : processes )
        {
            std::wcout << L"  " << process.pid;

            for ( int pad = static_cast< int >( 10 - std::to_wstring( process.pid ).size( ) ); pad > 0; --pad )
            {
                std::wcout << L' ';
            }

            std::wcout << process.name << L'\n';
        }

        std::wcout << L"\nTotal: " << processes.size( ) << L"\n\n";
    }

    uint32_t pick_process_interactive( std::wstring& out_name , uint32_t& out_pid )
    {
        const auto processes = list_processes( );

        if ( processes.empty( ) )
        {
            std::wcerr << L"No processes found.\n";
            return 1;
        }

        std::wcout << L"\nSelect a process:\n";
        std::wcout << L"  0) Enter process name manually\n";

        for ( size_t idx = 0; idx < processes.size( ); ++idx )
        {
            std::wcout << L"  " << ( idx + 1 ) << L") [" << processes [ idx ].pid << L"] "
                       << processes [ idx ].name << L'\n';
        }

        std::wcout << L"\nChoice: ";
        size_t choice = 0;
        std::wcin >> choice;
        std::wcin.ignore( std::numeric_limits< std::streamsize >::max( ) , L'\n' );

        if ( choice == 0 )
        {
            std::wcout << L"Process name: ";
            std::getline( std::wcin , out_name );
            out_pid = 0;
            return 0;
        }

        if ( choice < 1 || choice > processes.size( ) )
        {
            std::wcerr << L"Invalid selection.\n";
            return 1;
        }

        out_name = processes [ choice - 1 ].name;
        out_pid = processes [ choice - 1 ].pid;
        return 0;
    }

    bool launch_gui( )
    {
        wchar_t module_path [ MAX_PATH ] = {};
        GetModuleFileNameW( nullptr , module_path , MAX_PATH );

        std::wstring gui_path = module_path;
        const auto slash = gui_path.find_last_of( L"\\/" );

        if ( slash != std::wstring::npos )
        {
            gui_path.replace( slash + 1 , std::wstring::npos , L"manual_map_gui.exe" );
        }
        else
        {
            gui_path = L"manual_map_gui.exe";
        }

        HINSTANCE result = ShellExecuteW( nullptr , L"open" , gui_path.c_str( ) , nullptr , nullptr , SW_SHOWNORMAL );
        return reinterpret_cast< INT_PTR >( result ) > 32;
    }
}

int wmain( int argc , wchar_t* argv [ ] )
{
    app_config config {};
    load_config( config );

    inject_request request {};
    request.dll_path = config.last_dll;
    request.log = [ ] ( const std::wstring& line )
    {
        std::wcout << line << L'\n';
    };

    bool interactive = argc <= 1;
    std::wstring list_search;

    for ( int idx = 1; idx < argc; ++idx )
    {
        const std::wstring arg = argv [ idx ];

        if ( arg == L"--help" || arg == L"-h" || arg == L"/?" )
        {
            print_help( );
            return 0;
        }

        if ( arg == L"--list" )
        {
            print_process_list( list_search );
            return 0;
        }

        if ( ( arg == L"--search" || arg == L"-s" ) && idx + 1 < argc )
        {
            list_search = argv [ ++idx ];
            continue;
        }

        if ( arg == L"--gui" )
        {
            if ( !launch_gui( ) )
            {
                std::wcerr << L"Failed to launch manual_map_gui.exe\n";
                return 1;
            }

            return 0;
        }

        if ( arg == L"--admin" )
        {
            std::wstring args;

            for ( int arg_idx = 1; arg_idx < argc; ++arg_idx )
            {
                if ( argv [ arg_idx ] != L"--admin" )
                {
                    if ( !args.empty( ) )
                    {
                        args += L' ';
                    }

                    args += argv [ arg_idx ];
                }
            }

            if ( !relaunch_as_admin( args.empty( ) ? nullptr : args.c_str( ) ) )
            {
                std::wcerr << L"Failed to relaunch as administrator.\n";
                return 1;
            }

            return 0;
        }

        if ( ( arg == L"--process" || arg == L"-p" ) && idx + 1 < argc )
        {
            request.process_name = argv [ ++idx ];
            interactive = false;
            continue;
        }

        if ( arg == L"--pid" && idx + 1 < argc )
        {
            request.process_id = static_cast< uint32_t >( std::wcstoul( argv [ ++idx ] , nullptr , 10 ) );
            interactive = false;
            continue;
        }

        if ( ( arg == L"--dll" || arg == L"-d" ) && idx + 1 < argc )
        {
            request.dll_path = argv [ ++idx ];
            interactive = false;
            continue;
        }

        if ( arg == L"--wait" && idx + 1 < argc )
        {
            request.wait_for_ms = static_cast< uint32_t >( std::wcstoul( argv [ ++idx ] , nullptr , 10 ) ) * 1000u;
            interactive = false;
            continue;
        }
    }

    if ( interactive )
    {
        std::wcout << L"Manual Map Injector\n";
        std::wcout << L"-------------------\n";

        if ( pick_process_interactive( request.process_name , request.process_id ) != 0 )
        {
            return 1;
        }

        if ( request.dll_path.empty( ) )
        {
            request.dll_path = pick_dll_path( config.last_dll );
        }
        else
        {
            std::wcout << L"\nUsing DLL: " << request.dll_path << L"\n";
            std::wcout << L"Press Enter to keep it, or type a new path: ";
            std::wstring override_path;
            std::getline( std::wcin , override_path );

            if ( !override_path.empty( ) )
            {
                request.dll_path = override_path;
            }
        }

        if ( request.dll_path.empty( ) )
        {
            request.dll_path = pick_dll_path( config.last_dll );
        }
    }

    if ( request.dll_path.empty( ) )
    {
        std::wcerr << L"Error: no DLL selected.\n";
        return 1;
    }

    if ( request.process_name.empty( ) && !request.process_id )
    {
        std::wcerr << L"Error: no process selected.\n";
        return 1;
    }

    if ( !is_process_elevated( ) )
    {
        std::wcout << L"Note: not running as administrator. Injection may fail for some targets.\n";
    }

    std::wcout << L"\nInjecting " << request.dll_path;

    if ( request.process_id )
    {
        std::wcout << L" into PID " << request.process_id;
    }
    else
    {
        std::wcout << L" into " << request.process_name;
    }

    std::wcout << L"...\n";

    const auto result = run_injection( request , config );

    if ( !interactive )
    {
        return result.code;
    }

    std::wcout << L"\nPress Enter to exit...";
    std::wcin.get( );
    return result.code;
}
