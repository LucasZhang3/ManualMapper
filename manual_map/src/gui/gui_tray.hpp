#pragma once

#include <windows.h>

#define WM_TRAYICON ( WM_USER + 100 )
#define WM_TRAY_RESTORE ( WM_USER + 101 )
#define WM_TRAY_QUIT ( WM_USER + 102 )

bool gui_tray_init( void* hwnd , HINSTANCE instance );
void gui_tray_shutdown( );
void gui_tray_show( );
void gui_tray_hide( );
void gui_tray_minimize_to_tray( void* hwnd );
void gui_tray_restore_from_tray( void* hwnd );
bool gui_tray_handle_message( void* hwnd , UINT msg , WPARAM wparam , LPARAM lparam );
