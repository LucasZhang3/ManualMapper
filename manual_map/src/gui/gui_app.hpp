#pragma once

struct ID3D11Device;
struct ID3D11DeviceContext;

bool gui_app_init( );
void gui_app_shutdown( );
void gui_app_new_frame( );
void gui_app_render( );
void gui_app_on_resize( unsigned int width , unsigned int height );

bool gui_app_create_device( void* hwnd );
void gui_app_destroy_device( );
void gui_app_present( );

extern void* g_gui_hwnd;
