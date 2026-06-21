#pragma once

#include <app/config.hpp>
#include <app/inject_service.hpp>
#include <app/pe_util.hpp>
#include <app/process_list.hpp>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

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

    int selected_pid = 0;
    int list_focus = -1;
    int inject_delay_sec = 0;
    uint32_t last_auto_inject_pid = 0;
    uint32_t last_error_code = 0;

    process_sort_mode sort_mode = process_sort_mode::name;
    float panel_split = 0.42f;
    pe_info dll_pe {};

    std::string log;
    std::mutex log_mutex;
    std::vector< std::string > pending_log;
    std::mutex pending_mutex;

    std::string status = "Ready";
    std::atomic< bool > injecting { false };
    std::atomic< bool > inject_done { false };
    inject_result inject_result {};
    float inject_progress = 0.0f;
};

void gui_state_init( gui_app_state& state );
void gui_state_shutdown( gui_app_state& state );
void gui_state_new_frame( gui_app_state& state );
void gui_state_render( gui_app_state& state );
void gui_state_set_dll_path( gui_app_state& state , const wchar_t* path );
void gui_state_save_window( gui_app_state& state , void* hwnd );
void gui_state_on_resize( gui_app_state& state , unsigned int width , unsigned int height );
