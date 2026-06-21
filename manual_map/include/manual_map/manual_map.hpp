#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>

struct map_shellcode_data
{
    void* module_base;
    void* reserved_data;
    volatile LONG status;
    HINSTANCE( *load_library )( const char* );
    FARPROC( *get_proc_address )( HMODULE , LPCSTR );
};

struct map_reserved_data
{
    uint64_t module_base;
    uint64_t module_size;
};

void __stdcall map_shellcode( map_shellcode_data* data );
void __stdcall map_shellcode_end( );
size_t map_shellcode_size( );

struct manual_map_options
{
    std::function< void( const std::wstring& ) > log;
};

class c_manual_map
{
public:
    uint32_t inject(
        const wchar_t* process_name ,
        uint8_t* data ,
        size_t size ,
        void* reserved = nullptr ,
        size_t reserved_size = 0 ,
        manual_map_options options = {} );

    uint32_t inject_pid(
        uint32_t pid ,
        uint8_t* data ,
        size_t size ,
        void* reserved = nullptr ,
        size_t reserved_size = 0 ,
        manual_map_options options = {} );

private:
    uint32_t find_pid( const wchar_t* name ) const;
    static bool enable_debug_privilege( );
    static uint64_t get_module_base( HANDLE process , const char* name );
    static void* resolve_remote_export( HANDLE process , const char* module_name , const char* export_name );
    HANDLE hijack_handle( uint32_t target_pid , ACCESS_MASK access ) const;
    HANDLE acquire_process_handle( uint32_t target_pid , ACCESS_MASK access ) const;
    bool run_loader( uint64_t shellcode_addr , uint64_t shellcode_data_addr , uint32_t timeout_ms , uint32_t& error , manual_map_options options ) const;
    uint32_t map_image( uint8_t* data , size_t size , void* reserved , size_t reserved_size , manual_map_options options );
    uint64_t alloc( size_t size , uint32_t protect ) const;
    void dealloc( uint64_t addr ) const;
    bool write( uint64_t addr , const void* data , size_t size ) const;
    bool protect( uint64_t addr , size_t size , uint32_t flags ) const;
    void log( manual_map_options options , const std::wstring& message ) const;

    template < typename t >
    t read( uint64_t addr ) const
    {
        t out {};
        ReadProcessMemory( m_process , reinterpret_cast< void* >( addr ) , &out , sizeof( t ) , nullptr );
        return out;
    }

    uint32_t m_pid = 0;
    HANDLE m_process = nullptr;
};
