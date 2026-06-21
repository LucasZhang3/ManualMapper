#include "gui_tray.hpp"
#include "resource.h"

#include <shellapi.h>
#include <string>

namespace
{
    constexpr UINT TRAY_ICON_ID = 1;
    NOTIFYICONDATAW g_tray {};
    bool g_tray_visible = false;
    HINSTANCE g_instance = nullptr;
    HMENU g_tray_menu = nullptr;

    void show_tray_menu( void* hwnd )
    {
        POINT cursor {};
        GetCursorPos( &cursor );
        SetForegroundWindow( static_cast< HWND >( hwnd ) );
        TrackPopupMenu( g_tray_menu , TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON , cursor.x , cursor.y , 0 , static_cast< HWND >( hwnd ) , nullptr );
        PostMessageW( static_cast< HWND >( hwnd ) , WM_NULL , 0 , 0 );
    }
}

bool gui_tray_init( void* hwnd , HINSTANCE instance )
{
    g_instance = instance;
    g_tray_menu = CreatePopupMenu( );
    AppendMenuW( g_tray_menu , MF_STRING , 1 , L"Restore" );
    AppendMenuW( g_tray_menu , MF_SEPARATOR , 0 , nullptr );
    AppendMenuW( g_tray_menu , MF_STRING , 2 , L"Quit" );

    ZeroMemory( &g_tray , sizeof( g_tray ) );
    g_tray.cbSize = sizeof( g_tray );
    g_tray.hWnd = static_cast< HWND >( hwnd );
    g_tray.uID = TRAY_ICON_ID;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_tray.uCallbackMessage = WM_TRAYICON;
    g_tray.hIcon = LoadIconW( instance , MAKEINTRESOURCEW( IDI_APP_ICON ) );
    wcscpy_s( g_tray.szTip , L"Manual Map Injector" );
    return Shell_NotifyIconW( NIM_ADD , &g_tray ) != FALSE;
}

void gui_tray_shutdown( )
{
    if ( g_tray_visible )
    {
        Shell_NotifyIconW( NIM_DELETE , &g_tray );
        g_tray_visible = false;
    }

    if ( g_tray_menu )
    {
        DestroyMenu( g_tray_menu );
        g_tray_menu = nullptr;
    }
}

void gui_tray_show( )
{
    if ( !g_tray_visible )
    {
        Shell_NotifyIconW( NIM_ADD , &g_tray );
        g_tray_visible = true;
    }
}

void gui_tray_hide( )
{
    if ( g_tray_visible )
    {
        Shell_NotifyIconW( NIM_DELETE , &g_tray );
        g_tray_visible = false;
    }
}

void gui_tray_minimize_to_tray( void* hwnd )
{
    ShowWindow( static_cast< HWND >( hwnd ) , SW_HIDE );
    gui_tray_show( );
}

void gui_tray_restore_from_tray( void* hwnd )
{
    gui_tray_hide( );
    ShowWindow( static_cast< HWND >( hwnd ) , SW_SHOW );
    ShowWindow( static_cast< HWND >( hwnd ) , SW_RESTORE );
    SetForegroundWindow( static_cast< HWND >( hwnd ) );
}

bool gui_tray_handle_message( void* hwnd , UINT msg , WPARAM wparam , LPARAM lparam )
{
    if ( msg == WM_TRAYICON )
    {
        if ( lparam == WM_LBUTTONDBLCLK )
        {
            gui_tray_restore_from_tray( hwnd );
            return true;
        }

        if ( lparam == WM_RBUTTONUP )
        {
            show_tray_menu( hwnd );
            return true;
        }
    }
    else if ( msg == WM_COMMAND )
    {
        switch ( LOWORD( wparam ) )
        {
        case 1:
            gui_tray_restore_from_tray( hwnd );
            return true;
        case 2:
            PostMessageW( static_cast< HWND >( hwnd ) , WM_CLOSE , 0 , 0 );
            return true;
        default:
            break;
        }
    }
    else if ( msg == WM_TRAY_RESTORE )
    {
        gui_tray_restore_from_tray( hwnd );
        return true;
    }
    else if ( msg == WM_TRAY_QUIT )
    {
        PostMessageW( static_cast< HWND >( hwnd ) , WM_CLOSE , 0 , 0 );
        return true;
    }

    return false;
}
