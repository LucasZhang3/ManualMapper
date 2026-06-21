#pragma once

#include <imgui.h>

struct ImFont;
struct gui_app_state;

struct gui_theme_tokens
{
    float body_size = 15.0f;
    float label_size = 13.0f;
    float mono_size = 14.0f;
    float title_bar_height = 36.0f;
    float tab_bar_height = 36.0f;
    float status_bar_height = 26.0f;
    float row_height = 38.0f;
    float shell_padding = 16.0f;
    float card_padding = 12.0f;
    float card_inner_padding = 14.0f;
    float section_spacing = 10.0f;
};

void gui_theme_init( gui_app_state& state );
void gui_theme_apply( gui_app_state& state );
const gui_theme_tokens& gui_theme_tokens_for( const gui_app_state& state );
ImVec4 gui_theme_accent( int accent_index );
ImVec4 gui_theme_accent_muted( int accent_index , bool light_mode );

inline ImGuiWindowFlags gui_child_scroll_flags( )
{
    return ImGuiWindowFlags_NoScrollbar;
}
