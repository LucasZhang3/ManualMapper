#include "gui_app.hpp"

#include <imgui_impl_win32.h>

#include <windows.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hwnd , UINT msg , WPARAM wparam , LPARAM lparam );

namespace
{
    LRESULT WINAPI window_proc( HWND hwnd , UINT msg , WPARAM wparam , LPARAM lparam )
    {
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
            }

            return 0;
        case WM_SYSCOMMAND:
            if ( ( wparam & 0xfff0 ) == SC_KEYMENU )
            {
                return 0;
            }

            break;
        case WM_DESTROY:
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
    RegisterClassExW( &window_class );

    HWND hwnd = CreateWindowW(
        window_class.lpszClassName ,
        L"Manual Map Injector" ,
        WS_OVERLAPPEDWINDOW ,
        100 ,
        100 ,
        1280 ,
        720 ,
        nullptr ,
        nullptr ,
        instance ,
        nullptr );

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
