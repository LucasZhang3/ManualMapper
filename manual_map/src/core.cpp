#include <include/core.hpp>

#include <commdlg.h>
#include <iostream>
#include <limits>
#include <string>

#pragma comment( lib , "comdlg32.lib" )

static std::wstring to_wide( const std::string& text )
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

static std::wstring prompt_process_name( )
{
    std::wstring name;

    std::wcout << L"Enter target process name (Task Manager name, e.g. notepad.exe): ";
    std::getline( std::wcin , name );

    while ( !name.empty( ) && ( name.front( ) == L' ' || name.front( ) == L'\t' ) )
    {
        name.erase( name.begin( ) );
    }

    while ( !name.empty( ) && ( name.back( ) == L' ' || name.back( ) == L'\t' ) )
    {
        name.pop_back( );
    }

    return name;
}

static std::wstring pick_dll_path( )
{
    wchar_t path [ MAX_PATH ] = {};

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

static std::vector< uint8_t > read_file_bytes( const std::wstring& path )
{
    std::ifstream file( path , std::ios::binary | std::ios::ate );

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

static const char* loader_stage_message( uint16_t stage )
{
    switch ( stage )
    {
    case 1: return "invalid DOS header in target";
    case 2: return "invalid NT header in target";
    case 3: return "LoadLibrary failed for a dependency";
    case 4: return "GetProcAddress failed for an import";
    case 5: return "DllMain returned FALSE";
    default: return "unknown loader stage";
    }
}

static const char* error_message( uint32_t code )
{
    switch ( code )
    {
    case 0: return "Success";
    case 0x1000: return "Target process not found";
    case 0x1001: return "Invalid DOS signature (not a valid PE file)";
    case 0x1002: return "Invalid NT signature (not a valid PE file)";
    case 0x1003: return "Failed to acquire target process handle";
    case 0x1004: return "Failed to find or hijack a suitable thread";
    case 0x1005: return "Failed to allocate memory for the mapped image";
    case 0x1006: return "Failed to write PE data into the target process";
    case 0x1008: return "Failed to allocate reserved data";
    case 0x1012: return "Failed to allocate shellcode data";
    case 0x1014: return "Failed to allocate or copy loader shellcode";
    case 0x1016: return "Failed to allocate execution stub";
    case 0x1017: return "Loader timed out in the target process";
    case 0x1018: return "Failed to create remote loader thread";
    case 0x1019: return "Failed to resolve loader imports in the target process";
    default:
        if ( ( code & 0xFFFF0000u ) == 0x101A0000u )
        {
            return loader_stage_message( static_cast< uint16_t >( code & 0xFFFFu ) );
        }

        return "Unknown error";
    }
}

int main( int argc , char* argv [ ] )
{
    std::wcout << L"Manual Map Injector\n";
    std::wcout << L"-------------------\n\n";

    std::wstring process_name;
    std::wstring dll_path;

    if ( argc >= 3 )
    {
        process_name = to_wide( argv [ 1 ] );
        dll_path = to_wide( argv [ 2 ] );
        std::wcout << L"Using CLI args.\n";
    }
    else
    {
        process_name = prompt_process_name( );

        if ( process_name.empty( ) )
        {
            std::wcerr << L"Error: process name cannot be empty.\n";
            std::wcout << L"\nPress Enter to exit...";
            std::wcin.ignore( std::numeric_limits< std::streamsize >::max( ) , L'\n' );
            std::wcin.get( );
            return 1;
        }

        std::wcout << L"\nSelect the DLL file to inject...\n";
        dll_path = pick_dll_path( );
    }

    if ( dll_path.empty( ) )
    {
        std::wcerr << L"Error: no DLL selected.\n";
        std::wcout << L"\nPress Enter to exit...";
        std::wcin.get( );
        return 1;
    }

    auto payload = read_file_bytes( dll_path );

    if ( payload.empty( ) )
    {
        std::wcerr << L"Error: failed to read DLL: " << dll_path << L"\n";
        std::wcout << L"\nPress Enter to exit...";
        std::wcin.get( );
        return 1;
    }

    std::wcout << L"\nInjecting " << dll_path << L" into " << process_name << L"...\n";

    const auto result = g_manual_map->inject(
        process_name.c_str( ) ,
        payload.data( ) ,
        payload.size( ) );

    if ( result == 0 )
    {
        std::wcout << L"Injection succeeded.\n";
    }
    else
    {
        std::wcerr << L"Injection failed (0x" << std::hex << result << std::dec << L"): "
                   << error_message( result ) << L"\n";
    }

    std::wcout << L"\nPress Enter to exit...";
    std::wcin.ignore( std::numeric_limits< std::streamsize >::max( ) , L'\n' );
    std::wcin.get( );

    return result;
}
