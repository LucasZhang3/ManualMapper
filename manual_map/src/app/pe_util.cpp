#include <app/pe_util.hpp>

#include <windows.h>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstring>

namespace
{
    std::string fnv1a_hex( const uint8_t* data , size_t size )
    {
        uint64_t hash = 1469598103934665603ull;

        for ( size_t idx = 0; idx < size; ++idx )
        {
            hash ^= data [ idx ];
            hash *= 1099511628211ull;
        }

        std::ostringstream stream;
        stream << std::hex << std::uppercase << std::setfill( '0' ) << std::setw( 16 ) << hash;
        return stream.str( );
    }
}

pe_info analyze_pe_file( const std::wstring& path )
{
    pe_info info {};

    std::ifstream file( path , std::ios::binary | std::ios::ate );

    if ( !file.is_open( ) )
    {
        info.error = L"Unable to open file.";
        return info;
    }

    const auto file_size = static_cast< size_t >( file.tellg( ) );
    file.seekg( 0 , std::ios::beg );

    std::vector< uint8_t > bytes( file_size );
    file.read( reinterpret_cast< char* >( bytes.data( ) ) , static_cast< std::streamsize >( file_size ) );

    if ( file_size < sizeof( IMAGE_DOS_HEADER ) )
    {
        info.error = L"File too small for PE.";
        return info;
    }

    const auto* dos = reinterpret_cast< const IMAGE_DOS_HEADER* >( bytes.data( ) );

    if ( dos->e_magic != IMAGE_DOS_SIGNATURE )
    {
        info.error = L"Missing DOS header.";
        return info;
    }

    if ( static_cast< size_t >( dos->e_lfanew ) + sizeof( IMAGE_NT_HEADERS64 ) > file_size )
    {
        info.error = L"Invalid PE header offset.";
        return info;
    }

    const auto* nt = reinterpret_cast< const IMAGE_NT_HEADERS* >( bytes.data( ) + dos->e_lfanew );

    if ( nt->Signature != IMAGE_NT_SIGNATURE )
    {
        info.error = L"Missing PE signature.";
        return info;
    }

    info.valid = true;
    info.is_64bit = nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64;
    info.machine_label = info.is_64bit ? L"x64" : ( nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 ? L"x86" : L"other" );
    info.hash_hex = fnv1a_hex( bytes.data( ) , bytes.size( ) );

    const auto export_rva = nt->OptionalHeader.DataDirectory [ IMAGE_DIRECTORY_ENTRY_EXPORT ].VirtualAddress;

    if ( !export_rva )
    {
        return info;
    }

    const auto rva_to_offset = [ & ] ( uint32_t rva ) -> size_t
    {
        const auto* first_section = IMAGE_FIRST_SECTION( nt );

        for ( WORD idx = 0; idx < nt->FileHeader.NumberOfSections; ++idx )
        {
            const auto& section = first_section [ idx ];

            if ( rva >= section.VirtualAddress && rva < section.VirtualAddress + section.Misc.VirtualSize )
            {
                return static_cast< size_t >( rva - section.VirtualAddress + section.PointerToRawData );
            }
        }

        return SIZE_MAX;
    };

    const auto export_offset = rva_to_offset( export_rva );

    if ( export_offset == SIZE_MAX || export_offset + sizeof( IMAGE_EXPORT_DIRECTORY ) > file_size )
    {
        return info;
    }

    const auto* exports = reinterpret_cast< const IMAGE_EXPORT_DIRECTORY* >( bytes.data( ) + export_offset );
    const auto names_offset = rva_to_offset( exports->AddressOfNames );

    if ( names_offset == SIZE_MAX )
    {
        return info;
    }

    const auto max_exports = std::min< DWORD >( exports->NumberOfNames , 12 );

    for ( DWORD idx = 0; idx < max_exports; ++idx )
    {
        const auto name_rva_offset = names_offset + static_cast< size_t >( idx ) * sizeof( DWORD );

        if ( name_rva_offset + sizeof( DWORD ) > file_size )
        {
            break;
        }

        const auto name_rva = *reinterpret_cast< const DWORD* >( bytes.data( ) + name_rva_offset );
        const auto name_offset = rva_to_offset( name_rva );

        if ( name_offset == SIZE_MAX || name_offset >= file_size )
        {
            continue;
        }

        const char* name = reinterpret_cast< const char* >( bytes.data( ) + name_offset );
        std::wstring wide_name;

        for ( const char* cursor = name; *cursor; ++cursor )
        {
            wide_name.push_back( static_cast< wchar_t >( *cursor ) );
        }

        info.exports.push_back( wide_name );
    }

    if ( exports->NumberOfNames > max_exports )
    {
        info.exports.emplace_back( L"..." );
    }

    return info;
}
