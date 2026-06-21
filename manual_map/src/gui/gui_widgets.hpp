#pragma once

struct gui_app_state;

enum class toast_type;
struct gui_toast;

void gui_push_toast( gui_app_state& state , const char* message , toast_type type );
void gui_draw_toasts( gui_app_state& state );
void gui_draw_help_marker( const char* description );
bool gui_draw_pill_toggle( gui_app_state& state , const char* id , bool* value , const char* label_on , const char* label_off , const char* tooltip = nullptr );
bool gui_begin_section_card( const char* id , const char* title , bool default_open , bool* open_state );
void gui_end_section_card( );
void gui_draw_command_palette( gui_app_state& state );
void gui_draw_first_run_wizard( gui_app_state& state );
void gui_draw_drag_overlay( gui_app_state& state );
