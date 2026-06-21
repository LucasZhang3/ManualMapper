#include <windows.h>

#pragma comment( lib , "user32.lib" )

BOOL APIENTRY DllMain( HMODULE , DWORD reason , LPVOID )
{
    if ( reason == DLL_PROCESS_ATTACH )
    {
        MessageBoxA(
            nullptr ,
            "Hello World!" ,
            "payload_dll (manual map)" ,
            MB_OK | MB_ICONINFORMATION | MB_TOPMOST );
    }

    return TRUE;
}
