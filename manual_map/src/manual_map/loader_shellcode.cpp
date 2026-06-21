#include <include/core.hpp>

#pragma runtime_checks( "" , off )
#pragma optimize( "" , off )
#pragma code_seg( ".loader" , read )

size_t map_shellcode_size( )
{
    const auto start = reinterpret_cast< const uint8_t* >( map_shellcode );
    const auto end = reinterpret_cast< const uint8_t* >( map_shellcode_end );

    if ( end > start )
    {
        return static_cast< size_t >( end - start );
    }

    return 0x2000;
}

void __stdcall map_shellcode( map_shellcode_data* data )
{
    const auto image_base = reinterpret_cast< uint64_t >( data->module_base );
    const auto dos = reinterpret_cast< IMAGE_DOS_HEADER* >( image_base );

    if ( dos->e_magic != IMAGE_DOS_SIGNATURE )
    {
        data->status = -1;
        return;
    }

    const auto nt_headers = reinterpret_cast< IMAGE_NT_HEADERS* >( image_base + dos->e_lfanew );

    if ( nt_headers->Signature != IMAGE_NT_SIGNATURE )
    {
        data->status = -2;
        return;
    }

    const auto preferred_base = nt_headers->OptionalHeader.ImageBase;
    const auto delta = static_cast< int64_t >( image_base - preferred_base );
    const auto entry = reinterpret_cast< BOOL( WINAPI* )( void* , DWORD , void* ) >(
        image_base + nt_headers->OptionalHeader.AddressOfEntryPoint );

    const auto& reloc_dir = nt_headers->OptionalHeader.DataDirectory [ IMAGE_DIRECTORY_ENTRY_BASERELOC ];

    if ( reloc_dir.Size )
    {
        auto block = reinterpret_cast< IMAGE_BASE_RELOCATION* >( image_base + reloc_dir.VirtualAddress );
        const auto reloc_end = reinterpret_cast< uint8_t* >( block ) + reloc_dir.Size;

        while ( reinterpret_cast< uint8_t* >( block ) < reloc_end && block->SizeOfBlock )
        {
            const auto count = ( block->SizeOfBlock - sizeof( IMAGE_BASE_RELOCATION ) ) / sizeof( uint16_t );
            const auto items = reinterpret_cast< uint16_t* >( block + 1 );

            for ( DWORD idx = 0; idx < count; ++idx )
            {
                const auto type = items [ idx ] >> 12;
                const auto offset = items [ idx ] & 0xFFF;

                if ( type == IMAGE_REL_BASED_DIR64 )
                {
                    *reinterpret_cast< uint64_t* >( image_base + block->VirtualAddress + offset ) += delta;
                }
                else if ( type == IMAGE_REL_BASED_HIGHLOW )
                {
                    *reinterpret_cast< uint32_t* >( image_base + block->VirtualAddress + offset ) += static_cast< uint32_t >( delta );
                }
            }

            block = reinterpret_cast< IMAGE_BASE_RELOCATION* >(
                reinterpret_cast< uint8_t* >( block ) + block->SizeOfBlock );
        }
    }

    data->status = 10;

    const auto& import_dir = nt_headers->OptionalHeader.DataDirectory [ IMAGE_DIRECTORY_ENTRY_IMPORT ];

    if ( import_dir.Size )
    {
        auto descriptor = reinterpret_cast< IMAGE_IMPORT_DESCRIPTOR* >( image_base + import_dir.VirtualAddress );

        while ( descriptor->Name )
        {
            const auto module_name = reinterpret_cast< char* >( image_base + descriptor->Name );
            const auto module_handle = data->load_library( module_name );

            if ( !module_handle )
            {
                data->status = -3;
                return;
            }

            const auto lookup_rva = descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk : descriptor->FirstThunk;
            auto original_thunk = reinterpret_cast< IMAGE_THUNK_DATA* >( image_base + lookup_rva );
            auto first_thunk = reinterpret_cast< IMAGE_THUNK_DATA* >( image_base + descriptor->FirstThunk );

            while ( original_thunk->u1.AddressOfData )
            {
                FARPROC function = nullptr;

                if ( IMAGE_SNAP_BY_ORDINAL( original_thunk->u1.Ordinal ) )
                {
                    function = data->get_proc_address(
                        module_handle ,
                        MAKEINTRESOURCEA( IMAGE_ORDINAL( original_thunk->u1.Ordinal ) ) );
                }
                else
                {
                    const auto import_name = reinterpret_cast< IMAGE_IMPORT_BY_NAME* >(
                        image_base + original_thunk->u1.AddressOfData );
                    function = data->get_proc_address( module_handle , import_name->Name );
                }

                if ( !function )
                {
                    data->status = -4;
                    return;
                }

                first_thunk->u1.Function = reinterpret_cast< uint64_t >( function );
                ++original_thunk;
                ++first_thunk;
            }

            ++descriptor;
        }
    }

    data->status = 20;

    const auto& tls_dir = nt_headers->OptionalHeader.DataDirectory [ IMAGE_DIRECTORY_ENTRY_TLS ];

    if ( tls_dir.Size )
    {
        const auto tls = reinterpret_cast< IMAGE_TLS_DIRECTORY* >( image_base + tls_dir.VirtualAddress );

        if ( tls->AddressOfCallBacks )
        {
            auto callbacks = reinterpret_cast< PIMAGE_TLS_CALLBACK* >(
                static_cast< uint64_t >( image_base + ( tls->AddressOfCallBacks - preferred_base ) ) );

            while ( callbacks && *callbacks )
            {
                auto callback = *callbacks;

                if ( delta )
                {
                    callback = reinterpret_cast< PIMAGE_TLS_CALLBACK >(
                        static_cast< uint64_t >( reinterpret_cast< uint64_t >( callback ) + delta ) );
                }

                callback( reinterpret_cast< void* >( image_base ) , DLL_PROCESS_ATTACH , nullptr );
                ++callbacks;
            }
        }
    }

    data->status = 30;

    data->status = 1;

    if ( !entry( reinterpret_cast< void* >( image_base ) , DLL_PROCESS_ATTACH , data->reserved_data ) )
    {
        data->status = -5;
        return;
    }
}

#pragma optimize( "" , on )
#pragma runtime_checks( "" , restore )

void __stdcall map_shellcode_end( )
{
}
