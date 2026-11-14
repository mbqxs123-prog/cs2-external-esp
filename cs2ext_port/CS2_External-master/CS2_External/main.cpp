#include "Offsets.h"
#include "Cheats.h"
#include "EspFrame.h"
#include "Game.h"
#include "FrameData.h"
#include "FrameShm.h"
#include "DriverLoader.h"
#include "MenuConfig.hpp"
#include "Utils/Format.hpp"
#include <iostream>
#include <cstdio>
#include <string>
#include <filesystem>
#include <KnownFolders.h>
#include <ShlObj.h>

namespace fs = std::filesystem;

static bool HasArg(int argc, char** argv, const char* want)
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], want) == 0) return true;
    return false;
}

// Offline frame validation: parse one frame file and print what the renderer
// would see. Without a path it reads the live shared section instead.
static int FrameCheck(const char* path)
{
    const bool ok = path ? FrameData::Reload(path) : FrameData::Reload();
    if (!ok)
    {
        printf("FRAME FAIL stage=%d (1 open 2 read 3 size 4 magic 5 version 6 seq 7 checksum)\n",
               FrameData::Stage());
        return 2;
    }

    const FrameData::Frame& f = FrameData::Get();
    printf("SOURCE %s pid=%u\n", FrameShm::Alive() ? "shm" : "file", FrameShm::Pid());
    printf("FRAME ok seq=%u flags=0x%X screen=%dx%d local=%d players=%u map=%s fov=%.1f\n",
           f.seq, f.flags, f.width, f.height, f.localSlot, f.playerCount, f.map, f.fov);
    printf("TAIL items=%s(%u) gren=%s(%u)\n",
           FrameData::TailOk() ? "ok" : "none", (unsigned)f.items.size(),
           FrameData::GrenOk() ? "ok" : "none", (unsigned)f.grenades.size());

    CView view;
    FrameData::ApplyView(view);
    view.ViewportOverride.x = (float)(f.width > 0 ? f.width : 1920);
    view.ViewportOverride.y = (float)(f.height > 0 ? f.height : 1080);

    for (int i = 0; i < FrameData::MAX_SLOTS; i++)
    {
        const FrameData::PlayerRec& p = f.players[i];
        if (p.flags == 0 && p.health == 0 && p.name[0] == 0) continue;
        printf("P%02d team=%d hp=%d armor=%d gear=0x%02X flags=0x%02X w=%u ammo=%u dist=%.1f "
               "org=(%.1f,%.1f,%.1f) name=%s bones=0x%X\n",
               i, p.team, p.health, p.armor, p.gear, p.flags, p.weapon, p.ammo, p.distance,
               p.origin.x, p.origin.y, p.origin.z, p.name, p.boneMask);

        Vec2 feet, head;
        if (view.WorldToScreen(p.origin, feet))
        {
            const bool haveHead = view.WorldToScreen(p.bones[0], head);
            const float hx = haveHead ? head.x : feet.x;
            const float hy = haveHead ? head.y : feet.y;
            float h = (feet.y - hy) * 1.14f;
            if (h < 4.f) h = 4.f;
            const float w = h * 0.56f;
            printf("     screen feet=(%.1f,%.1f) head=(%.1f,%.1f) box=(%.1f,%.1f,%.1f,%.1f)\n",
                   feet.x, feet.y, hx, hy, hx - w * 0.5f, hy - h * 0.12f, w, h);
        }
        else
        {
            printf("     screen off (behind camera)\n");
        }
    }

    for (size_t i = 0; i < f.items.size(); i++)
    {
        const FrameData::ItemRec& it = f.items[i];
        printf("I%02u cat=%u id=%u flags=0x%02X ammo=%u label=%s pos=(%.1f,%.1f,%.1f)\n",
               (unsigned)i, it.cat, it.id, it.flags, it.ammo,
               FrameData::ItemLabel(it.id, it.cat).c_str(), it.pos.x, it.pos.y, it.pos.z);
    }

    for (size_t i = 0; i < f.grenades.size(); i++)
    {
        const FrameData::GrenRec& g = f.grenades[i];
        printf("G%02u kind=%u id=%u flags=0x%02X name=%s r=%.1f pos=(%.1f,%.1f,%.1f) "
               "det=%.1f spawn=%.1f\n",
               (unsigned)i, g.kind, g.id, g.flags,
               FrameData::GrenadeName(g.kind, g.id), g.radius,
               g.pos.x, g.pos.y, g.pos.z, g.detonate, g.spawn);
    }

    Vec3 lo; float yaw = 0.f;
    if (FrameData::LocalOrigin(lo, yaw))
    {
        Vec2 sp;
        const bool on = view.WorldToScreen(lo, sp);
        printf("LOCAL screen=%s (%.1f,%.1f) yaw=%.1f\n", on ? "on" : "off", sp.x, sp.y, yaw);
    }
    return 0;
}

static int SelfTest()
{
    printf("CS2ESP selftest\n");
    printf("  architecture : x64\n");
    printf("  elevated     : %s\n", DriverLoader::IsElevated() ? "yes" : "no");
    printf("  emb. mapper  : %u bytes\n", DriverLoader::MapperBytes());
    printf("  emb. driver  : %u bytes\n", DriverLoader::DriverBytes());
    const bool present = DriverLoader::Present();
    printf("  driver live  : %s\n", present ? "yes (shared section ok)" : "no");
    printf("  shm pid      : %u\n", FrameShm::Pid());
    if (present)
    {
        FrameData::Reload();
        const FrameData::Frame& f = FrameData::Get();
        printf("  frame        : seq=%u flags=0x%X players=%u items=%u gren=%u\n",
               f.seq, f.flags, f.playerCount,
               (unsigned)f.items.size(), (unsigned)f.grenades.size());
    }
    return (DriverLoader::MapperBytes() && DriverLoader::DriverBytes()) ? 0 : 2;
}

int main(int argc, char** argv)
{
    if (HasArg(argc, argv, "--frame-check"))
    {
        const char* path = nullptr;
        for (int i = 1; i + 1 < argc; i++)
            if (strcmp(argv[i], "--frame-check") == 0) path = argv[i + 1];
        return FrameCheck(path);
    }
    if (HasArg(argc, argv, "--selftest")) return SelfTest();

    const bool noDriver    = HasArg(argc, argv, "--no-driver");
    const bool driverOnly  = HasArg(argc, argv, "--driver-only");
    const bool forceDriver = HasArg(argc, argv, "--force-driver");
    if (HasArg(argc, argv, "--menu")) { MenuConfig::ShowMenu = true; }

    if (!noDriver)
    {
        printf("checking driver ...\n");
        if (DriverLoader::Ensure(forceDriver))
        {
            printf("driver live: shared frame section readable\n");
        }
        else
        {
            printf("driver not ready: %s\n", DriverLoader::LastError());
            MessageBoxW(nullptr, L"Driver load failed. Check HVCI off, blocklist off, AV whitelist.", L"CS2ESP", MB_ICONWARNING | MB_OK);
        }
    }
    if (driverOnly) return 0;

    char documentsPath[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, documentsPath) == S_OK)
    {
        MenuConfig::path = documentsPath;
        MenuConfig::path += "/CS2_External";
        if (!fs::exists(MenuConfig::path)) { fs::create_directory(MenuConfig::path); }
    }

    std::cout << "CS2ESP overlay starting (frame-fed, no process attach)" << std::endl;

    // The overlay may be started before the game: keep retrying for two minutes
    // instead of exiting on the first "window not found".
    bool attached = false;
    for (int attempt = 0; attempt < 60; attempt++)
    {
        try
        {
            Gui.AttachAnotherWindow("Counter-Strike 2", "SDL_app", FrameCheats_Run);
            attached = true;
            break;
        }
        catch (OSImGui::OSException& e)
        {
            if (attempt == 0)
            {
                std::cout << "waiting for the CS2 window ... (" << e.what() << ")" << std::endl;
            }
            Sleep(2000);
        }
    }

    if (!attached)
    {
        MessageBoxW(nullptr, L"CS2 window not found in 2 min. Use windowed/borderless mode.", L"CS2ESP", MB_ICONINFORMATION | MB_OK);
    }

    std::cout << "exit." << std::endl;
    return 0;
}
