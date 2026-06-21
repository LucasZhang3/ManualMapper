#pragma once

#include <string>

bool capture_stealth_supported( );
bool capture_stealth_active( void* hwnd );
bool apply_capture_stealth( void* hwnd , bool enabled , std::wstring* error_out = nullptr );
void sync_capture_stealth( void* hwnd , bool enabled );
