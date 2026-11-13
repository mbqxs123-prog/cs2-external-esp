#include "FrameShm.h"

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>

namespace FrameShm
{
    static HANDLE        g_map = nullptr;
    static const void*   g_view = nullptr;
    static unsigned      g_pid = 0;
    static ULONGLONG     g_lastTry = 0;
    static ULONGLONG     g_lastPidCheck = 0;
    static wchar_t       g_name[128] = { 0 };

    // Must match cs2_shm.c::build_name exactly. Salts are tried newest first so
    // that a driver instance mapped before a channel bump can still be read.
    static const unsigned kSalts[] = { 5u, 4u, 3u, 2u, 1u };

    void BuildName(unsigned pid, unsigned salt, wchar_t* out, int cch)
    {
        const unsigned a = (0x4C325343u ^ pid) ^ (salt << 24);
        const unsigned b = (0x9E3779B9u * (pid + 1u)) >> 16;
        const unsigned c = ((pid << 16) ^ 0x5F3Au) & 0xFFFFu;
        const unsigned d = ((pid >> 3) ^ 0xA1B2C3D4u) & 0xFFFFu;
        const unsigned e = 0x7F4A7C15u + pid;
        swprintf_s(out, (size_t)cch, L"Global\\{%08X-%04X-%04X-%04X-%08X%04X}",
                   a, b & 0xFFFFu, c, d, e, pid & 0xFFFFu);
    }

    // Name lookup only; the game process itself is never opened.
    static unsigned FindCs2Pid()
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;

        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        unsigned found = 0;
        if (Process32FirstW(snap, &pe))
        {
            do
            {
                if (_wcsicmp(pe.szExeFile, L"cs2.exe") == 0) { found = pe.th32ProcessID; break; }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return found;
    }

    static void CloseMapping()
    {
        if (g_view) { UnmapViewOfFile(g_view); g_view = nullptr; }
        if (g_map) { CloseHandle(g_map); g_map = nullptr; }
        g_name[0] = 0;
    }

    void Detach()
    {
        CloseMapping();
        g_pid = 0;
        g_lastTry = 0;
        g_lastPidCheck = 0;
    }

    bool Alive() { return g_view != nullptr; }
    unsigned Pid() { return g_pid; }

    bool Ensure()
    {
        const ULONGLONG now = GetTickCount64();

        if (g_view)
        {
            // The driver builds a new section when the target pid changes, so a
            // game restart invalidates the mapping. Re-check the pid every 2 s.
            if (now - g_lastPidCheck < 2000) return true;
            g_lastPidCheck = now;
            const unsigned p = FindCs2Pid();
            if (p == g_pid) return true;
            // Target changed (game restart): the driver rebuilds the section
            // under the new pid, so drop the old view and attach to the new one
            // in this same call.
            CloseMapping();
            g_pid = p;
            g_lastTry = 0;
        }
        else
        {
            if (now - g_lastTry < 500) return false;
            g_lastTry = now;

            g_pid = FindCs2Pid();
        }

        for (size_t i = 0; i < ARRAYSIZE(kSalts) && !g_map; i++)
        {
            BuildName(g_pid, kSalts[i], g_name, ARRAYSIZE(g_name));
            g_map = OpenFileMappingW(FILE_MAP_READ, FALSE, g_name);
        }
        if (!g_map) return false;

        g_view = MapViewOfFile(g_map, FILE_MAP_READ, 0, 0, 0);
        if (!g_view)
        {
            CloseHandle(g_map);
            g_map = nullptr;
            return false;
        }
        return true;
    }

    bool Read(void* dst, int len)
    {
        if (!dst || len <= 16) return false;
        if (!Ensure()) return false;

        const volatile unsigned* hdr = (const volatile unsigned*)g_view;
        const unsigned s1 = hdr[2];                 // seq word at offset 8
        if ((s1 & 1u) != 0u || s1 == 0xFFFFFFFFu) return false;

        // Header (magic/version/seq/flags) is plaintext; body is XOR'd by the
        // producer with a pid-derived byte, so a section dump is not readable.
        unsigned char* d = (unsigned char*)dst;
        const unsigned char* s = (const unsigned char*)g_view;
        memcpy(d, s, 16);
        const unsigned char key = (unsigned char)((g_pid * 2654435761u) >> 24);
        if (key == 0)
        {
            memcpy(d + 16, s + 16, (size_t)(len - 16));
        }
        else
        {
            for (int i = 16; i < len; i++) d[i] = (unsigned char)(s[i] ^ key);
        }

        const unsigned s2 = hdr[2];
        if (s1 != s2 || (s2 & 1u) != 0u) return false;
        return true;
    }
}
