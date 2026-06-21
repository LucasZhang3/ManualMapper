#pragma once

#include <string>
#include <vector>

struct app_config
{
    std::wstring last_dll;
    std::vector< std::wstring > recent_dlls;

    int window_x = -1;
    int window_y = -1;
    int window_w = 1280;
    int window_h = 720;
    float panel_split = 0.42f;

    bool confirm_inject = true;
    bool log_timestamps = true;
    bool use_allowlist = false;
    bool stealth_capture = false;
    std::vector< std::wstring > process_rules;
    std::wstring cli_notes;
    std::wstring language = L"en";
};

bool load_config( app_config& config );
void save_config( const app_config& config );
bool load_config_from_path( const std::wstring& path , app_config& config );
bool save_config_to_path( const std::wstring& path , const app_config& config );
std::wstring default_config_path( );
void remember_dll( app_config& config , const std::wstring& dll_path );
void remove_recent_dll( app_config& config , const std::wstring& dll_path );
bool is_process_allowed( const app_config& config , const std::wstring& process_name );
