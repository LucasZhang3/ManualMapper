#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct pe_info
{
    bool valid = false;
    bool is_64bit = false;
    std::wstring machine_label;
    std::vector< std::wstring > exports;
    std::string hash_hex;
    std::wstring error;
};

pe_info analyze_pe_file( const std::wstring& path );
