#pragma once

#include <payload/payload_shared.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct injection_history_entry
{
    std::wstring timestamp;
    std::wstring target;
    std::wstring dll;
    bool success = false;
};

struct inject_profile
{
    std::wstring name;
    std::wstring dll_path;
    std::wstring process_name;
    bool wait_for_process = false;
    bool inject_all = false;
    int inject_delay_sec = 0;
};

struct app_config
{
    std::wstring last_dll;
    std::vector< std::wstring > recent_dlls;

    int window_x = -1;
    int window_y = -1;
    int window_w = 1280;
    int window_h = 720;
    float panel_split = 0.42f;

    bool confirm_inject = false;
    bool log_timestamps = true;
    bool use_allowlist = false;
    bool stealth_capture = false;
    std::vector< std::wstring > process_rules;
    std::wstring cli_notes;
    std::wstring language = L"en";

    bool light_mode = false;
    bool compact_mode = false;
    bool first_run_complete = false;
    bool min_to_tray = false;
    std::wstring watch_folder;
    std::vector< uint32_t > favorite_pids;
    std::vector< injection_history_entry > injection_history;
    std::vector< inject_profile > profiles;
    std::vector< std::wstring > dll_queue;
    bool show_process_tree = false;

    bool settings_appearance_open = true;
    bool settings_capture_open = true;
    bool settings_injection_open = true;
    bool settings_logging_open = true;
    bool settings_safety_open = true;
    bool settings_profiles_open = true;
    bool settings_advanced_open = true;

    bool payload_enabled = true;
    bool payload_silent = false;
    bool payload_show_message = true;
    bool payload_file_log = true;
    bool payload_debug_log = true;
    bool payload_attach_console = false;
    bool payload_heartbeat = true;
    bool payload_proof_file = true;
    bool payload_module_watch = true;
    bool payload_loadlib_hook = true;
    bool payload_hotkeys = true;
    bool payload_ipc_pipe = true;
    bool payload_host_snapshot = true;
    bool payload_plugin_loader = false;
    bool payload_overlay = true;
    uint32_t payload_feature_flags = PAYLOAD_FEATURE_DEFAULT;
    uint32_t payload_delay_ms = 0;
    uint32_t payload_heartbeat_ms = 1000;
    uint32_t payload_snapshot_mode = PAYLOAD_SNAPSHOT_ALL;
    std::wstring payload_ui_message;
    std::wstring payload_log_path;
    std::wstring payload_proof_dir;
    std::wstring payload_plugin_path;
    bool settings_payload_open = true;
};

bool load_config( app_config& config );
void save_config( const app_config& config );
bool load_config_from_path( const std::wstring& path , app_config& config );
bool save_config_to_path( const std::wstring& path , const app_config& config );
std::wstring default_config_path( );
void remember_dll( app_config& config , const std::wstring& dll_path );
void remove_recent_dll( app_config& config , const std::wstring& dll_path );
bool is_process_allowed( const app_config& config , const std::wstring& process_name );
void add_injection_history( app_config& config , const injection_history_entry& entry );
void clear_injection_history( app_config& config );
