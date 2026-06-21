#include "gui_app.hpp"
#include "gui_tray.hpp"
#include "gui_state.hpp"
#include "resource.h"

#include <app/config.hpp>

#include <windows.h>
#include <shellapi.h>

#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hwnd , UINT msg , WPARAM wparam , LPARAM lparam );

namespace
{
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

    HWND hwnd = CreateWindowW(
        window_class.lpszClassName ,
        L"Manual Map Injector" ,
        WS_OVERLAPPEDWINDOW ,
        window_x ,
        window_y ,
        window_w ,
        window_h ,
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
