// dllmain.cpp
#include "pch.h"
#include <windows.h>
#include <stdint.h>
#include <cmath>
#include <stdio.h>   // for atof
#include <string.h>

int toggleKey = 0;

// configurable default values
uint16_t cfg_initial_fov = 15000;
uint16_t cfg_max_fov = 24000;
float    cfg_max_speed = 80.0f;
int      cfg_graph_type = 1;

// original bytes for instruction at 0x0047DC9E
BYTE originalBytes[7] = { 0x66, 0x89, 0x81, 0xC4, 0x00, 0x00, 0x00 };
BYTE nopBytes[7] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

// patch target (nfsmw)
uintptr_t patchAddr = 0x0047DC9E;

// memory
volatile float* speed_ptr = (float*)0x009142C8;
volatile uint16_t* fov_ptr = (uint16_t*)0x00986934;


void PatchBytes(bool enable)
{
    DWORD oldProtect;
    VirtualProtect((LPVOID)patchAddr, 7, PAGE_EXECUTE_READWRITE, &oldProtect);

    if (enable)
        memcpy((void*)patchAddr, nopBytes, 7);
    else
        memcpy((void*)patchAddr, originalBytes, 7);

    VirtualProtect((LPVOID)patchAddr, 7, oldProtect, &oldProtect);
}

void LoadConfig()
{
    // hotkey
    toggleKey = GetPrivateProfileIntA(
        "hotkeys",
        "toggle_fov",
        118,
        ".\\DFconfig.ini"
    );

    // fov settings
    cfg_initial_fov = (uint16_t)GetPrivateProfileIntA(
        "settings",
        "initial_fov",
        15000,
        ".\\DFconfig.ini"
    );

    cfg_max_fov = (uint16_t)GetPrivateProfileIntA(
        "settings",
        "max_fov",
        24000,
        ".\\DFconfig.ini"
    );

    cfg_graph_type = GetPrivateProfileIntA(
        "settings",
        "graph_type",
        1,
        ".\\DFconfig.ini"
    );

    // speed limit: allow floats in ini (better than cast-int)
    char buf[64] = { 0 };
    GetPrivateProfileStringA("speed", "max_speed", "80.0", buf, sizeof(buf), ".\\DFconfig.ini");
    cfg_max_speed = (float)atof(buf);
    if (cfg_max_speed <= 0.0f) cfg_max_speed = 80.0f;
}

bool effectEnabled = false;

float ApplyGraph(float t)
{
    // clamp t 0..1
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    switch (cfg_graph_type)
    {
    case 0:
        // linear
        return t;

    case 1:
        // cubic in
        return t * t * t;

    case 2:
        // cubic out
        return 1.0f - powf(1.0f - t, 3.0f);

    default:
        return t;
    }
}

DWORD WINAPI FovThread(void*)
{
    // use the addresses provided above
    volatile float* speed = speed_ptr;
    volatile uint16_t* fov = fov_ptr;

    // sanity check: if pointers are null, stop
    if (!speed || !fov) return 0;

    while (true)
    {
        if (effectEnabled)
        {
            float s = *speed;
            if (s < 0.0f) s = 0.0f;
            if (s > cfg_max_speed) s = cfg_max_speed;

            float t = s / cfg_max_speed;
            float eased = ApplyGraph(t);

            float ff = (float)cfg_initial_fov + (float)(cfg_max_fov - cfg_initial_fov) * eased;

            if (ff > cfg_max_fov) ff = (float)cfg_max_fov;
            if (ff < cfg_initial_fov) ff = (float)cfg_initial_fov;

            *fov = (uint16_t)ff;
        }

        SwitchToThread();
    }
}

DWORD WINAPI HotkeyThread(void*)
{
    LoadConfig();

    while (true)
    {
        if (GetAsyncKeyState(toggleKey) & 1)
        {
            effectEnabled = !effectEnabled;
            PatchBytes(effectEnabled);
        }
        Sleep(1);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        CreateThread(nullptr, 0, FovThread, nullptr, 0, nullptr);
        CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
