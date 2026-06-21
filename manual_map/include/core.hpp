#pragma once

#define NOMINMAX

#include <windows.h>
#include <winternl.h>

#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#pragma comment( lib , "ntdll.lib" )
#pragma comment( lib , "psapi.lib" )

#include <manual_map/manual_map.hpp>
