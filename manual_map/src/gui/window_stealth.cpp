#include "window_stealth.hpp"

#include <windows.h>

#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif

#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace
{
    using set_window_display_affinity_fn = BOOL ( WINAPI* )( HWND , DWORD );

    set_window_display_affinity_fn resolve_set_window_display_affinity( )
    {
        static const auto function = reinterpret_cast< set_window_display_affinity_fn >(
            GetProcAddress( GetModuleHandleW( L"user32.dll" ) , "SetWindowDisplayAffinity" ) );

        return function;
    }

    bool apply_affinity( HWND hwnd , DWORD affinity , std::wstring* error_out )
    {
        const auto set_affinity = resolve_set_window_display_affinity( );

        if ( !set_affinity )
        {
            if ( error_out )
            {
                *error_out = L"SetWindowDisplayAffinity is not available on this system.";
            }

            return false;
        }

        if ( set_affinity( hwnd , affinity ) )
        {
            return true;
        }

        if ( error_out )
        {
            *error_out = L"SetWindowDisplayAffinity failed (error " + std::to_wstring( GetLastError( ) ) + L").";
        }

        return false;
    }
}

bool capture_stealth_supported( )
{
    if ( !resolve_set_window_display_affinity( ) )
    {
        return false;
    }

    OSVERSIONINFOEXW version {};
    version.dwOSVersionInfoSize = sizeof( version );
#pragma warning( push )
#pragma warning( disable : 4996 )
    if ( !GetVersionExW( reinterpret_cast< OSVERSIONINFOW* >( &version ) ) )
#pragma warning( pop )
    {
        return true;
    }

    if ( version.dwMajorVersion > 10 )
    {
        return true;
    }

    if ( version.dwMajorVersion == 10 && version.dwBuildNumber >= 19041 )
    {
        return true;
    }

    return false;
}

bool apply_capture_stealth( void* hwnd , bool enabled , std::wstring* error_out )
{
    if ( !hwnd )
    {
        if ( error_out )
        {
            *error_out = L"No window handle.";
        }

        return false;
    }

    const auto window = static_cast< HWND >( hwnd );

    if ( !enabled )
    {
        return apply_affinity( window , WDA_NONE , error_out );
    }

    if ( apply_affinity( window , WDA_EXCLUDEFROMCAPTURE , nullptr ) )
    {
        return true;
    }

    if ( apply_affinity( window , WDA_MONITOR , error_out ) )
    {
        if ( error_out )
        {
            error_out->clear( );
        }

        return true;
    }

    return false;
}
