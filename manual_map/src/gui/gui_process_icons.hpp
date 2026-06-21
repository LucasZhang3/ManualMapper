#pragma once

#include <imgui.h>

#include <string>

ImTextureID process_icons_get( const std::wstring& exe_path );
void process_icons_shutdown( );
