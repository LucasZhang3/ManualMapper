#pragma once

#include <app/config.hpp>
#include <app/inject_service.hpp>
#include <app/pe_util.hpp>
#include <app/process_list.hpp>

#include <imgui.h>

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

enum class gui_page
{
    injection ,
    history ,
    settings
};

enum class toast_type
{
    info ,
    success ,
    error_type
};

struct gui_toast
{
    std::string message;
    toast_type type;
    float remaining = 3.0f;
};

struct gui_app_state
{
    app_config config {};
    std::vector< process_entry > all_processes;
    std::vector< uint32_t > favorite_pids;

    char search [ 256 ] = {};
    char dll_path [ 1024 ] = {};
    char log_filter [ 128 ] = {};
    char process_rules_text [ 1024 ] = {};
    char cli_notes [ 512 ] = {};
    char watch_folder_text [ 512 ] = {};
    char new_profile_name [ 128 ] = {};
    char queue_dll_input [ 512 ] = {};

    bool wait_for_process = false;
    bool inject_all = false;
    bool auto_inject = false;
    bool confirm_inject = true;
    bool log_timestamps = true;
    bool use_allowlist = false;
    bool show_settings = false;
    bool show_confirm_popup = false;
    bool light_mode = false;
    bool stealth_capture = false;
    bool show_process_tree = false;

    gui_page current_page = gui_page::injection;
    std::vector< gui_toast > toasts;
    std::unordered_map< ImGuiID , float > pill_anim;
    bool command_palette_open = false;
    bool log_follow_tail = true;
    bool show_wizard = false;
    bool focus_search_next_frame = false;
    bool focus_log_filter_next_frame = false;
    bool window_maximized = false;
    bool pending_palette_refresh = false;
    bool pending_palette_inject = false;
    int wizard_step = 0;
    int selected_profile = -1;

    int selected_pid = 0;
    int list_focus = -1;
    int inject_delay_sec = 0;
    uint32_t last_auto_inject_pid = 0;
    uint32_t last_error_code = 0;

    process_sort_mode sort_mode = process_sort_mode::name;
    float panel_split = 0.42f;
    pe_info dll_pe {};
    std::unordered_map< std::wstring , pe_info > queue_pe_cache;

    std::string log;
    std::mutex log_mutex;
    std::vector< std::string > pending_log;
    std::mutex pending_mutex;

    std::string status = "Ready";
    std::atomic< bool > injecting { false };
    std::atomic< bool > inject_done { false };
    inject_result inject_result {};
    float inject_progress = 0.0f;

    std::vector< std::wstring > inject_queue;
    size_t inject_queue_index = 0;
    bool inject_queue_active = false;

    float drag_highlight_timer = 0.0f;
    float watch_folder_poll_timer = 0.0f;
    std::wstring last_watched_dll;

    ImFont* font_body = nullptr;
    ImFont* font_header = nullptr;
    ImFont* font_mono = nullptr;
    ImTextureID app_icon_texture = 0;
    float tab_bar_height = 36.0f;
    float title_bar_height = 36.0f;
};

void gui_state_init( gui_app_state& state );
void gui_state_shutdown( gui_app_state& state );
void gui_state_new_frame( gui_app_state& state );
void gui_state_render( gui_app_state& state );
void gui_state_set_dll_path( gui_app_state& state , const wchar_t* path );
void gui_state_save_window( gui_app_state& state , void* hwnd );
void gui_state_on_resize( gui_app_state& state , unsigned int width , unsigned int height );
void gui_state_on_drag_enter( gui_app_state& state );
bool gui_state_handle_shortcuts( gui_app_state& state );
