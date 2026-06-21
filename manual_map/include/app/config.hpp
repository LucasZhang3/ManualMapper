#pragma once

#include <string>
#include <vector>

struct app_config
{
    std::wstring last_dll;
    std::vector< std::wstring > recent_dlls;
};

bool load_config( app_config& config );
void save_config( const app_config& config );
void remember_dll( app_config& config , const std::wstring& dll_path );
void remove_recent_dll( app_config& config , const std::wstring& dll_path );
