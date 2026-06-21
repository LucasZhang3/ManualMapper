#pragma once

struct ID3D11Device;
struct ID3D11DeviceContext;
struct gui_app_state;

struct app_config;

bool gui_app_init( );
void gui_app_shutdown( );
void gui_app_new_frame( );
void gui_app_render( );
void gui_app_on_resize( unsigned int width , unsigned int height );

bool gui_app_create_device( void* hwnd );
void gui_app_destroy_device( );
void gui_app_present( );

void gui_app_set_dll_path( const wchar_t* path );
app_config* gui_app_config( );
gui_app_state* gui_app_state_ptr( );
ID3D11Device* gui_app_device( );

extern void* g_gui_hwnd;
