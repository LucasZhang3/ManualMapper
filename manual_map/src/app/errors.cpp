#include <app/errors.hpp>

#include <windows.h>

#include <string>

const char* inject_error_message( uint32_t code )
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
    case 0x1020: return "Timed out waiting for the target process to start";
    default:
        if ( ( code & 0xFFFF0000u ) == 0x101A0000u )
        {
            switch ( static_cast< uint16_t >( code & 0xFFFFu ) )
            {
            case 1: return "Loader: invalid DOS header in target";
            case 2: return "Loader: invalid NT header in target";
            case 3: return "Loader: LoadLibrary failed for a dependency";
            case 4: return "Loader: GetProcAddress failed for an import";
            case 5: return "Loader: DllMain returned FALSE";
            default: return "Loader failed in the target process";
            }
        }

        return "Unknown error";
    }
}

std::wstring inject_error_message_w( uint32_t code )
{
    const char* message = inject_error_message( code );

    if ( !message || !message [ 0 ] )
    {
        return L"Unknown error";
    }

    const auto length = MultiByteToWideChar( CP_UTF8 , 0 , message , -1 , nullptr , 0 );

    if ( length <= 0 )
    {
        return L"Unknown error";
    }

    std::wstring wide( static_cast< size_t >( length ) , L'\0' );
    MultiByteToWideChar( CP_UTF8 , 0 , message , -1 , wide.data( ) , length );

    if ( !wide.empty( ) && wide.back( ) == L'\0' )
    {
        wide.pop_back( );
    }

    return wide;
}
