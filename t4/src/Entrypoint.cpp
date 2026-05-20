#include "StdInclude.hpp"
#include "Mod.hpp"

#include "T4Interface.hpp"

using namespace IWXMVM;
using namespace T4;

T4Interface gameInterface = T4Interface();

// We're loaded via dsound.dll proxy at process startup — well before the game
// has created its main window, set up dvars, or initialized its engine. If we
// run Mod::Initialize immediately, our hooks try to install against
// half-initialized game state and crash the game with unhandled exceptions.
//
// CoDMVM Launcher avoids this by injecting iw3.dll AFTER the game is at the
// main menu. We don't have a separate launcher, so we replicate that timing
// by sleeping inside the worker thread until the game's main window appears.
//
// WaW MP class name is "CoD-WaW".
static DWORD WINAPI WaitForGameThenInit(LPVOID arg)
{
    // Up to ~5 minutes wait window for game to finish booting.
    HWND hwnd = nullptr;
    for (int i = 0; i < 300 && !hwnd; ++i)
    {
        hwnd = ::FindWindowA("CoD-WaW", nullptr);
        if (hwnd && ::IsWindowVisible(hwnd))
            break;
        hwnd = nullptr;
        ::Sleep(1000);
    }
    // Give the engine a bit more time after the window is visible.
    ::Sleep(2000);

    Mod::Initialize(reinterpret_cast<GameInterface*>(arg));
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        ::CreateThread(nullptr, 0, WaitForGameThenInit, &gameInterface, 0, nullptr);
    }
    return TRUE;
}
