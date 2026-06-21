#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class process_sort_mode
{
    name ,
    pid ,
    session
};

struct process_entry
{
    uint32_t pid = 0;
    uint32_t parent_pid = 0;
    uint32_t session_id = 0;
    std::wstring name;
    bool is_wow64 = false;
    bool is_elevated = false;
    bool details_loaded = false;
};

std::vector< process_entry > list_processes( );
void enrich_process_details( process_entry& process );
void sort_processes( std::vector< process_entry >& processes , process_sort_mode mode );
std::vector< process_entry > find_processes_by_name( const wchar_t* name );
std::vector< process_entry > filter_processes( const std::vector< process_entry >& processes , const wchar_t* filter );
bool wait_for_process( const wchar_t* name , uint32_t timeout_ms , uint32_t& out_pid );
