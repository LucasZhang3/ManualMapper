#include "gui_app.hpp"
#include "gui_tray.hpp"
#include "gui_state.hpp"
#include "resource.h"

#include <app/config.hpp>

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hwnd , UINT msg , WPARAM wparam , LPARAM lparam );

namespace
{
    LRESULT hit_test_borderless( HWND hwnd , LPARAM lparam )
    {
        constexpr LONG border = 8;
        POINT point { GET_X_LPARAM( lparam ) , GET_Y_LPARAM( lparam ) };
        ScreenToClient( hwnd , &point );

        RECT client_rect {};
        GetClientRect( hwnd , &client_rect );

        if ( point.y < border )
        {
            if ( point.x < border )
            {
                return HTTOPLEFT;
            }

            if ( point.x >= client_rect.right - border )
            {
                return HTTOPRIGHT;
            }

            return HTTOP;
        }

        if ( point.y >= client_rect.bottom - border )
        {
            if ( point.x < border )
            {
                return HTBOTTOMLEFT;
            }

            if ( point.x >= client_rect.right - border )
            {
                return HTBOTTOMRIGHT;
            }

            return HTBOTTOM;
        }

        if ( point.x < border )
        {
            return HTLEFT;
        }

        if ( point.x >= client_rect.right - border )
        {
            return HTRIGHT;
        }

        return HTCLIENT;
    }

    LRESULT WINAPI window_proc( HWND hwnd , UINT msg , WPARAM wparam , LPARAM lparam )
    {
        if ( gui_tray_handle_message( hwnd , msg , wparam , lparam ) )
        {
            return 0;
        }

        if ( ImGui_ImplWin32_WndProcHandler( hwnd , msg , wparam , lparam ) )
        {
            return true;
        }

        switch ( msg )
        {
        case WM_NCHITTEST:
            return hit_test_borderless( hwnd , lparam );
        case WM_GETMINMAXINFO:
        {
            auto* min_max = reinterpret_cast< MINMAXINFO* >( lparam );
            min_max->ptMinTrackSize.x = 800;
            min_max->ptMinTrackSize.y = 500;
            return 0;
        }
        case WM_SIZE:
            if ( wparam != SIZE_MINIMIZED )
            {
                gui_app_on_resize( LOWORD( lparam ) , HIWORD( lparam ) );

                if ( gui_app_state_ptr( ) )
                {
                    gui_app_state_ptr( )->window_maximized = wparam == SIZE_MAXIMIZED;
                }
            }

            return 0;
        case WM_DROPFILES:
        {
            HDROP drop = reinterpret_cast< HDROP >( lparam );
            wchar_t path [ MAX_PATH ] = {};

            if ( DragQueryFileW( drop , 0 , path , MAX_PATH ) )
            {
                gui_app_set_dll_path( path );

                if ( gui_app_state_ptr( ) )
                {
                    gui_state_on_drag_enter( *gui_app_state_ptr( ) );
                }
            }

            DragFinish( drop );
            return 0;
        }
        case WM_SYSCOMMAND:
            if ( ( wparam & 0xfff0 ) == SC_KEYMENU )
            {
                return 0;
            }

            break;
        case WM_DESTROY:
            gui_tray_shutdown( );
            PostQuitMessage( 0 );
            return 0;
        default:
            break;
        }

        return DefWindowProcW( hwnd , msg , wparam , lparam );
    }
}

int WINAPI wWinMain( HINSTANCE instance , HINSTANCE , PWSTR , int show_command )
{
    WNDCLASSEXW window_class {};
    window_class.cbSize = sizeof( window_class );
    window_class.style = CS_CLASSDC;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"ManualMapInjectorGui";
    window_class.hIcon = LoadIconW( instance , MAKEINTRESOURCEW( IDI_APP_ICON ) );
    window_class.hIconSm = LoadIconW( instance , MAKEINTRESOURCEW( IDI_APP_ICON ) );
    RegisterClassExW( &window_class );

    app_config config {};
    load_config( config );

    const int window_x = config.window_x >= 0 ? config.window_x : 100;
    const int window_y = config.window_y >= 0 ? config.window_y : 100;
    const int window_w = config.window_w > 0 ? config.window_w : 1280;
    const int window_h = config.window_h > 0 ? config.window_h : 720;

    const DWORD window_style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
    RECT window_rect { 0 , 0 , window_w , window_h };
    AdjustWindowRectEx( &window_rect , window_style , FALSE , 0 );
    const int outer_w = window_rect.right - window_rect.left;
    const int outer_h = window_rect.bottom - window_rect.top;

    HWND hwnd = CreateWindowW(
        window_class.lpszClassName ,
        L"Manual Map Injector" ,
        window_style ,
        window_x ,
        window_y ,
        outer_w ,
        outer_h ,
        nullptr ,
        nullptr ,
        instance ,
        nullptr );

    const HICON big_icon = LoadIconW( instance , MAKEINTRESOURCEW( IDI_APP_ICON ) );
    const HICON small_icon = LoadIconW( instance , MAKEINTRESOURCEW( IDI_APP_ICON ) );
    SendMessageW( hwnd , WM_SETICON , ICON_BIG , reinterpret_cast< LPARAM >( big_icon ) );
    SendMessageW( hwnd , WM_SETICON , ICON_SMALL , reinterpret_cast< LPARAM >( small_icon ) );

    DragAcceptFiles( hwnd , TRUE );

    if ( !gui_app_create_device( hwnd ) )
    {
        UnregisterClassW( window_class.lpszClassName , instance );
        return 1;
    }

    ShowWindow( hwnd , show_command );
    UpdateWindow( hwnd );

    if ( !gui_app_init( ) )
    {
        gui_app_destroy_device( );
        DestroyWindow( hwnd );
        UnregisterClassW( window_class.lpszClassName , instance );
        return 1;
    }

    gui_tray_init( hwnd , instance );

    bool running = true;

    while ( running )
    {
        MSG message {};

        while ( PeekMessageW( &message , nullptr , 0U , 0U , PM_REMOVE ) )
        {
            TranslateMessage( &message );
            DispatchMessageW( &message );

            if ( message.message == WM_QUIT )
            {
                running = false;
            }
        }

        if ( !running )
        {
            break;
        }

        gui_app_new_frame( );
        gui_app_render( );
        gui_app_present( );
    }

    gui_app_shutdown( );
    gui_app_destroy_device( );
    DestroyWindow( hwnd );
    UnregisterClassW( window_class.lpszClassName , instance );
    return 0;
}
