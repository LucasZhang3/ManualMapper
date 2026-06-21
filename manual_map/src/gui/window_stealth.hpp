#pragma once

#include <string>

bool capture_stealth_supported( );
bool apply_capture_stealth( void* hwnd , bool enabled , std::wstring* error_out = nullptr );
