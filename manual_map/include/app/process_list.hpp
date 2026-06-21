#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct process_entry
{
    uint32_t pid = 0;
    std::wstring name;
};

std::vector< process_entry > list_processes( );
std::vector< process_entry > find_processes_by_name( const wchar_t* name );
std::vector< process_entry > filter_processes( const std::vector< process_entry >& processes , const wchar_t* filter );
bool wait_for_process( const wchar_t* name , uint32_t timeout_ms , uint32_t& out_pid );
