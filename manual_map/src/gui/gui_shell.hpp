#pragma once

#include <windows.h>

#include <functional>

struct gui_app_state;
struct ImGuiIO;

void gui_shell_render(
    gui_app_state& state ,
    void* hwnd ,
    ImGuiIO& io ,
    const std::function< void( ) >& render_page ,
    const std::function< void( ) >& render_overlays );

LRESULT gui_shell_hit_test( HWND hwnd , LPARAM lparam );
