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

        SetLastError( ERROR_SUCCESS );

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
    return resolve_set_window_display_affinity( ) != nullptr;
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

    if ( !capture_stealth_supported( ) )
    {
        if ( error_out )
        {
            *error_out = L"SetWindowDisplayAffinity is not available on this system.";
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

    std::wstring exclude_error;
    apply_affinity( window , WDA_EXCLUDEFROMCAPTURE , &exclude_error );

    if ( apply_affinity( window , WDA_MONITOR , error_out ) )
    {
        return true;
    }

    if ( error_out && error_out->empty( ) && !exclude_error.empty( ) )
    {
        *error_out = exclude_error;
    }

    return false;
}
