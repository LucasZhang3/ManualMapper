#pragma once

#include <app/config.hpp>
#include <functional>
#include <string>
#include <vector>

struct inject_request
{
    std::wstring process_name;
    uint32_t process_id = 0;
    std::wstring dll_path;
    uint32_t wait_for_ms = 0;
    uint32_t delay_ms = 0;
    bool inject_all = false;
    std::function< void( const std::wstring& ) > log;
};

struct inject_result
{
    uint32_t code = 0;
    uint32_t target_pid = 0;
    uint32_t success_count = 0;
    bool payload_verified = false;
};

inject_result run_injection( inject_request request , app_config& config );
std::vector< uint8_t > read_file_bytes( const std::wstring& path );
std::wstring pick_dll_path( const std::wstring& initial = {} );
bool is_process_elevated( );
bool relaunch_as_admin( const wchar_t* args );
