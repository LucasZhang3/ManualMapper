#include <include/core.hpp>

#include <psapi.h>

#include <cstring>
#include <sstream>
#include <string>

typedef NTSTATUS( NTAPI* nt_duplicate_object_t )( HANDLE , HANDLE , HANDLE , PHANDLE , ACCESS_MASK , ULONG , ULONG );

namespace
{
    std::wstring hex_u64( uint64_t value )
    {
        std::wstringstream stream;
        stream << L"0x" << std::hex << std::uppercase << value;
        return stream.str( );
    }

    std::wstring protect_name( DWORD protect )
    {
        switch ( protect )
        {
        case PAGE_NOACCESS: return L"PAGE_NOACCESS";
        case PAGE_READONLY: return L"PAGE_READONLY";
        case PAGE_READWRITE: return L"PAGE_READWRITE";
        case PAGE_WRITECOPY: return L"PAGE_WRITECOPY";
        case PAGE_EXECUTE: return L"PAGE_EXECUTE";
        case PAGE_EXECUTE_READ: return L"PAGE_EXECUTE_READ";
        case PAGE_EXECUTE_READWRITE: return L"PAGE_EXECUTE_READWRITE";
        case PAGE_EXECUTE_WRITECOPY: return L"PAGE_EXECUTE_WRITECOPY";
        default:
            return L"PAGE_0x" + hex_u64( protect );
        }
    }

    std::wstring section_name( const IMAGE_SECTION_HEADER* section )
    {
        char raw [ 9 ] = {};
        std::memcpy( raw , section->Name , 8 );
        return std::wstring( raw , raw + std::strlen( raw ) );
    }

    std::wstring loader_status_text( LONG status )
    {
        switch ( status )
        {
        case 1: return L"complete (DllMain invoked)";
        case 10: return L"base relocations applied";
        case 20: return L"import table resolved";
        case 30: return L"TLS callbacks executed, calling DllMain";
        case -1: return L"invalid DOS header in target";
        case -2: return L"invalid NT header in target";
        case -3: return L"LoadLibrary failed for dependency";
        case -4: return L"GetProcAddress failed for import";
        case -5: return L"DllMain returned FALSE";
        default: return L"code " + std::to_wstring( status );
        }
    }
}

struct system_handle_entry
{
    USHORT process_id;
    USHORT creator_back_trace_index;
    UCHAR object_type_index;
    UCHAR handle_attributes;
    USHORT handle_value;
    PVOID object;
    ULONG granted_access;
};

struct system_handle_information
{
    ULONG handle_count;
    system_handle_entry handles [ 1 ];
};

uint32_t c_manual_map::find_pid( const wchar_t* name ) const
{
    ULONG size = 0;
    NtQuerySystemInformation( SystemProcessInformation , nullptr , 0 , &size );

    if ( !size )
    {
        return 0;
    }

    std::vector< uint8_t > buffer( size );
    auto info = reinterpret_cast< SYSTEM_PROCESS_INFORMATION* >( buffer.data( ) );

    if ( !NT_SUCCESS( NtQuerySystemInformation( SystemProcessInformation , info , size , &size ) ) )
    {
        return 0;
    }

    while ( true )
    {
        if ( info->ImageName.Buffer && info->ImageName.Length > 0 )
        {
            std::wstring image( info->ImageName.Buffer , info->ImageName.Length / sizeof( wchar_t ) );

            if ( _wcsicmp( image.c_str( ) , name ) == 0 )
            {
                return static_cast< uint32_t >( reinterpret_cast< uint64_t >( info->UniqueProcessId ) );
            }
        }

        if ( !info->NextEntryOffset )
        {
            break;
        }

        info = reinterpret_cast< SYSTEM_PROCESS_INFORMATION* >( reinterpret_cast< uint8_t* >( info ) + info->NextEntryOffset );
    }

    return 0;
}

uint64_t c_manual_map::get_module_base( HANDLE process , const char* name )
{
    HMODULE modules [ 1024 ] = {};
    DWORD needed = 0;

    if ( !EnumProcessModules( process , modules , sizeof( modules ) , &needed ) )
    {
        return 0;
    }

    const auto count = needed / sizeof( HMODULE );

    for ( DWORD idx = 0; idx < count; ++idx )
    {
        char module_name [ MAX_PATH ] = {};

        if ( !GetModuleBaseNameA( process , modules [ idx ] , module_name , MAX_PATH ) )
        {
            continue;
        }

        if ( _stricmp( module_name , name ) == 0 )
        {
            return reinterpret_cast< uint64_t >( modules [ idx ] );
        }
    }

    return 0;
}

void* c_manual_map::resolve_remote_export( HANDLE process , const char* module_name , const char* export_name )
{
    const auto remote_module = get_module_base( process , module_name );
    const auto local_module = reinterpret_cast< uint64_t >( GetModuleHandleA( module_name ) );

    if ( !remote_module || !local_module )
    {
        return nullptr;
    }

    const auto local_export = reinterpret_cast< uint64_t >(
        GetProcAddress( reinterpret_cast< HMODULE >( local_module ) , export_name ) );

    if ( !local_export )
    {
        return nullptr;
    }

    return reinterpret_cast< void* >( remote_module + ( local_export - local_module ) );
}

bool c_manual_map::run_loader( uint64_t shellcode_addr , uint64_t shellcode_data_addr , uint32_t timeout_ms , uint32_t& error , manual_map_options options ) const
{
    log( options , L"[loader] CreateRemoteThread( start=" + hex_u64( shellcode_addr )
         + L", param=" + hex_u64( shellcode_data_addr ) + L" )" );

    const auto remote_thread = CreateRemoteThread(
        m_process ,
        nullptr ,
        0 ,
        reinterpret_cast< LPTHREAD_START_ROUTINE >( shellcode_addr ) ,
        reinterpret_cast< void* >( shellcode_data_addr ) ,
        0 ,
        nullptr );

    if ( !remote_thread )
    {
        error = 0x1018;
        log( options , L"[loader] CreateRemoteThread failed (GLE=" + hex_u64( GetLastError( ) ) + L")" );
        return false;
    }

    const auto thread_id = GetThreadId( remote_thread );
    log( options , L"[loader] Remote thread started (TID=" + std::to_wstring( thread_id ) + L"), polling shellcode status..." );

    const auto start = GetTickCount64( );
    bool completed = false;
    LONG last_status = 0;

    while ( GetTickCount64( ) - start < timeout_ms )
    {
        const auto status = read< LONG >( shellcode_data_addr + offsetof( map_shellcode_data , status ) );

        if ( status != last_status && status != 0 )
        {
            log( options , L"[loader] Status -> " + loader_status_text( status ) );
            last_status = status;
        }

        if ( status == 1 )
        {
            completed = true;
            break;
        }

        if ( status >= 30 )
        {
            completed = true;
            break;
        }

        if ( status < 0 )
        {
            error = static_cast< uint32_t >( 0x101A0000u | static_cast< uint16_t >( -status ) );
            log( options , L"[loader] Remote loader reported failure: " + loader_status_text( status ) );
            CloseHandle( remote_thread );
            return false;
        }

        if ( WaitForSingleObject( remote_thread , 50 ) == WAIT_OBJECT_0 )
        {
            const auto final_status = read< LONG >( shellcode_data_addr + offsetof( map_shellcode_data , status ) );
            completed = final_status == 1 || final_status >= 30;

            if ( final_status != last_status && final_status != 0 )
            {
                log( options , L"[loader] Thread exited, final status: " + loader_status_text( final_status ) );
            }

            if ( !completed && final_status < 0 )
            {
                error = static_cast< uint32_t >( 0x101A0000u | static_cast< uint16_t >( -final_status ) );
            }

            break;
        }
    }

    CloseHandle( remote_thread );

    if ( !completed )
    {
        error = error ? error : 0x1017;
        log( options , L"[loader] Timed out after " + std::to_wstring( timeout_ms ) + L" ms (last status: "
             + loader_status_text( last_status ) + L")" );
        return false;
    }

    log( options , L"[loader] Remote PE mapping finished successfully." );
    return true;
}

struct system_handle_entry_ex
{
    PVOID object;
    ULONG_PTR unique_process_id;
    ULONG_PTR handle_value;
    ULONG granted_access;
    USHORT creator_back_trace_index;
    USHORT object_type_index;
    ULONG handle_attributes;
    ULONG reserved;
};

struct system_handle_information_ex
{
    ULONG_PTR handle_count;
    ULONG_PTR reserved;
    system_handle_entry_ex handles [ 1 ];
};

bool c_manual_map::enable_debug_privilege( )
{
    HANDLE token = nullptr;

    if ( !OpenProcessToken( GetCurrentProcess( ) , TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY , &token ) )
    {
        return false;
    }

    LUID luid {};

    if ( !LookupPrivilegeValueW( nullptr , SE_DEBUG_NAME , &luid ) )
    {
        CloseHandle( token );
        return false;
    }

    TOKEN_PRIVILEGES privileges {};
    privileges.PrivilegeCount = 1;
    privileges.Privileges [ 0 ].Luid = luid;
    privileges.Privileges [ 0 ].Attributes = SE_PRIVILEGE_ENABLED;

    const auto ok = AdjustTokenPrivileges( token , FALSE , &privileges , sizeof( privileges ) , nullptr , nullptr );
    const auto err = GetLastError( );
    CloseHandle( token );

    return ok && err == ERROR_SUCCESS;
}

HANDLE c_manual_map::hijack_handle( uint32_t target_pid , ACCESS_MASK access ) const
{
    auto ntdll = GetModuleHandleA( "ntdll.dll" );
    auto nt_duplicate_object = reinterpret_cast< nt_duplicate_object_t >( GetProcAddress( ntdll , "NtDuplicateObject" ) );

    if ( !nt_duplicate_object )
    {
        return nullptr;
    }

    ULONG size = 0;
    auto status = NtQuerySystemInformation( static_cast< SYSTEM_INFORMATION_CLASS >( 64 ) , nullptr , 0 , &size );

    if ( !size )
    {
        return nullptr;
    }

    std::vector< uint8_t > buffer;

    for ( int attempt = 0; attempt < 8; ++attempt )
    {
        buffer.resize( size );
        status = NtQuerySystemInformation( static_cast< SYSTEM_INFORMATION_CLASS >( 64 ) , buffer.data( ) , size , &size );

        if ( NT_SUCCESS( status ) )
        {
            break;
        }

        if ( status != static_cast< NTSTATUS >( 0xC0000004 ) )
        {
            return nullptr;
        }

        size *= 2;
    }

    if ( !NT_SUCCESS( status ) )
    {
        return nullptr;
    }

    auto handle_info = reinterpret_cast< system_handle_information_ex* >( buffer.data( ) );
    const uint32_t csrss_pid = find_pid( L"csrss.exe" );
    const uint32_t self_pid = GetCurrentProcessId( );
    HANDLE result = nullptr;

    for ( int pass = 0; pass < 2 && !result; ++pass )
    {
        for ( ULONG_PTR idx = 0; idx < handle_info->handle_count; ++idx )
        {
            const auto& entry = handle_info->handles [ idx ];
            const auto owner_pid = static_cast< uint32_t >( entry.unique_process_id );

            if ( !owner_pid || owner_pid == self_pid || owner_pid == target_pid )
            {
                continue;
            }

            if ( pass == 0 && owner_pid != csrss_pid )
            {
                continue;
            }

            const auto source_handle = OpenProcess( PROCESS_DUP_HANDLE , FALSE , owner_pid );

            if ( !source_handle || source_handle == INVALID_HANDLE_VALUE )
            {
                continue;
            }

            HANDLE duped_handle = nullptr;
            const auto dup_status = nt_duplicate_object(
                source_handle ,
                reinterpret_cast< HANDLE >( entry.handle_value ) ,
                GetCurrentProcess( ) ,
                &duped_handle ,
                access ,
                0 ,
                0 );

            CloseHandle( source_handle );

            if ( !NT_SUCCESS( dup_status ) || !duped_handle )
            {
                if ( duped_handle )
                {
                    CloseHandle( duped_handle );
                }

                continue;
            }

            if ( GetProcessId( duped_handle ) != target_pid )
            {
                CloseHandle( duped_handle );
                continue;
            }

            result = duped_handle;
            break;
        }
    }

    return result;
}

HANDLE c_manual_map::acquire_process_handle( uint32_t target_pid , ACCESS_MASK access ) const
{
    enable_debug_privilege( );

    if ( HANDLE direct = OpenProcess( access , FALSE , target_pid ) )
    {
        return direct;
    }

    return hijack_handle( target_pid , access );
}

uint64_t c_manual_map::alloc( size_t size , uint32_t prot ) const
{
    return reinterpret_cast< uint64_t >( VirtualAllocEx( m_process , nullptr , size , MEM_COMMIT | MEM_RESERVE , prot ) );
}

void c_manual_map::dealloc( uint64_t addr ) const
{
    VirtualFreeEx( m_process , reinterpret_cast< void* >( addr ) , 0 , MEM_RELEASE );
}

bool c_manual_map::write( uint64_t addr , const void* data , size_t size ) const
{
    SIZE_T written = 0;
    return WriteProcessMemory( m_process , reinterpret_cast< void* >( addr ) , data , size , &written ) && written == size;
}

bool c_manual_map::protect( uint64_t addr , size_t size , uint32_t flags ) const
{
    DWORD old = 0;
    return VirtualProtectEx( m_process , reinterpret_cast< void* >( addr ) , size , flags , &old );
}

void c_manual_map::log( manual_map_options options , const std::wstring& message ) const
{
    if ( options.log )
    {
        options.log( message );
    }
}

uint32_t c_manual_map::inject(
    const wchar_t* process_name ,
    uint8_t* data ,
    size_t size ,
    void* reserved ,
    size_t reserved_size ,
    manual_map_options options )
{
    m_pid = find_pid( process_name );

    if ( !m_pid )
    {
        return 0x1000;
    }

    log( options , L"Resolved PID " + std::to_wstring( m_pid ) + L" for " + process_name + L"." );
    return map_image( data , size , reserved , reserved_size , options );
}

uint32_t c_manual_map::inject_pid(
    uint32_t pid ,
    uint8_t* data ,
    size_t size ,
    void* reserved ,
    size_t reserved_size ,
    manual_map_options options )
{
    if ( !pid )
    {
        return 0x1000;
    }

    m_pid = pid;
    log( options , L"Using PID " + std::to_wstring( m_pid ) + L"." );
    return map_image( data , size , reserved , reserved_size , options );
}

uint32_t c_manual_map::map_image( uint8_t* data , size_t size , void* reserved , size_t reserved_size , manual_map_options options )
{
    auto dos = reinterpret_cast< IMAGE_DOS_HEADER* >( data );

    if ( dos->e_magic != IMAGE_DOS_SIGNATURE )
    {
        return 0x1001;
    }

    auto nt_headers = reinterpret_cast< IMAGE_NT_HEADERS* >( data + dos->e_lfanew );

    if ( nt_headers->Signature != IMAGE_NT_SIGNATURE )
    {
        return 0x1002;
    }

    log( options , L"[pe] Valid PE image — SizeOfImage=" + std::to_wstring( nt_headers->OptionalHeader.SizeOfImage )
         + L", EntryPoint RVA=" + hex_u64( nt_headers->OptionalHeader.AddressOfEntryPoint )
         + L", ImageBase (preferred)=" + hex_u64( nt_headers->OptionalHeader.ImageBase )
         + L", sections=" + std::to_wstring( nt_headers->FileHeader.NumberOfSections ) );

    log( options , L"[handle] Enabling SeDebugPrivilege and acquiring PROCESS_VM_* handle for PID "
         + std::to_wstring( m_pid ) + L"..." );

    enable_debug_privilege( );
    m_process = OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD ,
        FALSE ,
        m_pid );

    if ( m_process )
    {
        log( options , L"[handle] OpenProcess succeeded (direct handle)." );
    }
    else
    {
        log( options , L"[handle] OpenProcess failed (GLE=" + hex_u64( GetLastError( ) )
             + L"); attempting NtDuplicateObject handle hijack..." );
        m_process = hijack_handle(
            m_pid ,
            PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD );

        if ( m_process )
        {
            log( options , L"[handle] Handle hijack succeeded via duplicated system handle." );
        }
    }

    if ( !m_process )
    {
        return 0x1003;
    }

    uint32_t result = 0;
    uint64_t image_base = 0;
    uint64_t reserved_addr = 0;
    uint64_t shellcode_data_addr = 0;
    uint64_t shellcode_addr = 0;
    size_t loader_size = 0;

    log( options , L"[map] VirtualAllocEx RWX " + std::to_wstring( nt_headers->OptionalHeader.SizeOfImage )
         + L" bytes for mapped image..." );

    image_base = alloc( nt_headers->OptionalHeader.SizeOfImage , PAGE_EXECUTE_READWRITE );

    if ( !image_base )
    {
        result = 0x1005;
        goto cleanup;
    }

    log( options , L"[map] Remote image base = " + hex_u64( image_base ) );

    if ( !write( image_base , data , nt_headers->OptionalHeader.SizeOfHeaders ) )
    {
        result = 0x1006;
        goto cleanup;
    }

    log( options , L"[map] Wrote PE headers (" + std::to_wstring( nt_headers->OptionalHeader.SizeOfHeaders ) + L" bytes) to "
         + hex_u64( image_base ) );

    {
        auto section = IMAGE_FIRST_SECTION( nt_headers );

        for ( int idx = 0; idx < nt_headers->FileHeader.NumberOfSections; idx++ , section++ )
        {
            if ( section->SizeOfRawData )
            {
                if ( !write(
                        image_base + section->VirtualAddress ,
                        data + section->PointerToRawData ,
                        section->SizeOfRawData ) )
                {
                    result = 0x1006;
                    goto cleanup;
                }
            }

            if ( section->Misc.VirtualSize > section->SizeOfRawData )
            {
                const auto zero_size = section->Misc.VirtualSize - section->SizeOfRawData;
                std::vector< uint8_t > zeros( zero_size , 0 );

                if ( !write( image_base + section->VirtualAddress + section->SizeOfRawData , zeros.data( ) , zeros.size( ) ) )
                {
                    result = 0x1006;
                    goto cleanup;
                }
            }

            log( options , L"[map] Section [" + section_name( section ) + L"] -> VA " + hex_u64( image_base + section->VirtualAddress )
                 + L", VSize=" + std::to_wstring( section->Misc.VirtualSize )
                 + L", RawSize=" + std::to_wstring( section->SizeOfRawData )
                 + L", chars=0x" + hex_u64( section->Characteristics ) );
        }
    }

    if ( reserved && reserved_size )
    {
        reserved_addr = alloc( reserved_size , PAGE_READWRITE );

        if ( !reserved_addr )
        {
            result = 0x1008;
            goto cleanup;
        }

        write( reserved_addr , reserved , reserved_size );
    }
    else
    {
        map_reserved_data rd {};
        rd.module_base = image_base;
        rd.module_size = nt_headers->OptionalHeader.SizeOfImage;

        reserved_addr = alloc( sizeof( map_reserved_data ) , PAGE_READWRITE );

        if ( !reserved_addr )
        {
            result = 0x1008;
            goto cleanup;
        }

        write( reserved_addr , &rd , sizeof( map_reserved_data ) );
        log( options , L"[map] Allocated loader context at " + hex_u64( reserved_addr )
             + L" (module_base/size for DllMain lpReserved)." );
    }

    {
        map_shellcode_data sd {};
        sd.module_base = reinterpret_cast< void* >( image_base );
        sd.reserved_data = reinterpret_cast< void* >( reserved_addr );
        sd.status = 0;
        sd.load_library = reinterpret_cast< decltype( sd.load_library ) >(
            resolve_remote_export( m_process , "kernelbase.dll" , "LoadLibraryA" ) );

        if ( !sd.load_library )
        {
            sd.load_library = reinterpret_cast< decltype( sd.load_library ) >(
                resolve_remote_export( m_process , "kernel32.dll" , "LoadLibraryA" ) );
        }

        sd.get_proc_address = reinterpret_cast< decltype( sd.get_proc_address ) >(
            resolve_remote_export( m_process , "kernel32.dll" , "GetProcAddress" ) );

        if ( !sd.load_library || !sd.get_proc_address )
        {
            result = 0x1019;
            goto cleanup;
        }

        log( options , L"[imports] Remote LoadLibraryA = " + hex_u64( reinterpret_cast< uint64_t >( sd.load_library ) )
             + L", GetProcAddress = " + hex_u64( reinterpret_cast< uint64_t >( sd.get_proc_address ) ) );

        shellcode_data_addr = alloc( sizeof( map_shellcode_data ) , PAGE_READWRITE );

        if ( !shellcode_data_addr )
        {
            result = 0x1012;
            goto cleanup;
        }

        if ( !write( shellcode_data_addr , &sd , sizeof( map_shellcode_data ) ) )
        {
            result = 0x1012;
            goto cleanup;
        }

        log( options , L"[loader] Shellcode data struct written to " + hex_u64( shellcode_data_addr ) );
    }

    loader_size = map_shellcode_size( );

    if ( loader_size < 0x100 )
    {
        result = 0x1014;
        goto cleanup;
    }

    shellcode_addr = alloc( loader_size + 0x100 , PAGE_EXECUTE_READWRITE );

    if ( !shellcode_addr )
    {
        result = 0x1014;
        goto cleanup;
    }

    if ( !write( shellcode_addr , reinterpret_cast< const void* >( map_shellcode ) , loader_size ) )
    {
        result = 0x1014;
        goto cleanup;
    }

    log( options , L"[loader] Copied " + std::to_wstring( loader_size ) + L" bytes of map_shellcode to "
         + hex_u64( shellcode_addr ) );

    if ( !run_loader( shellcode_addr , shellcode_data_addr , 30000 , result , options ) )
    {
        goto cleanup;
    }

    log( options , L"[cleanup] Applying per-section VirtualProtectEx and erasing injection artifacts..." );

    {
        auto section = IMAGE_FIRST_SECTION( nt_headers );

        for ( int idx = 0; idx < nt_headers->FileHeader.NumberOfSections; idx++ , section++ )
        {
            DWORD section_prot = PAGE_READONLY;
            auto characteristics = section->Characteristics;

            if ( characteristics & IMAGE_SCN_MEM_EXECUTE )
            {
                section_prot = ( characteristics & IMAGE_SCN_MEM_WRITE ) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
            }
            else if ( characteristics & IMAGE_SCN_MEM_WRITE )
            {
                section_prot = PAGE_READWRITE;
            }

            protect( image_base + section->VirtualAddress , section->Misc.VirtualSize , section_prot );

            log( options , L"[cleanup] VirtualProtectEx [" + section_name( section ) + L"] "
                 + hex_u64( image_base + section->VirtualAddress ) + L" size="
                 + std::to_wstring( section->Misc.VirtualSize ) + L" -> " + protect_name( section_prot ) );
        }
    }

    {
        std::vector< uint8_t > empty( nt_headers->OptionalHeader.SizeOfHeaders , 0 );
        write( image_base , empty.data( ) , empty.size( ) );
        log( options , L"[cleanup] Zeroed PE headers at " + hex_u64( image_base ) + L" ("
             + std::to_wstring( nt_headers->OptionalHeader.SizeOfHeaders ) + L" bytes)." );

        std::vector< uint8_t > zero( loader_size + 0x100 , 0 );
        write( shellcode_addr , zero.data( ) , zero.size( ) );
        log( options , L"[cleanup] Zeroed remote shellcode stub at " + hex_u64( shellcode_addr ) + L"." );
    }

cleanup:

    if ( shellcode_data_addr )
    {
        dealloc( shellcode_data_addr );
        log( options , L"[cleanup] VirtualFreeEx shellcode data at " + hex_u64( shellcode_data_addr ) );
    }

    if ( shellcode_addr )
    {
        dealloc( shellcode_addr );
        log( options , L"[cleanup] VirtualFreeEx shellcode stub at " + hex_u64( shellcode_addr ) );
    }

    if ( result )
    {
        if ( reserved_addr )
        {
            dealloc( reserved_addr );
        }

        if ( image_base )
        {
            dealloc( image_base );
        }
    }

    CloseHandle( m_process );

    m_process = nullptr;

    return result;
}
