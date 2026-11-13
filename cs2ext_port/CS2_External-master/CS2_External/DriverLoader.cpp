#include "DriverLoader.h"
#include "FrameShm.h"

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>

#include "resource.h"   // res/resource.h, added to the include path by the build

namespace DriverLoader
{
    static char  g_err[512] = { 0 };

    static void SetErr(const char* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(g_err, sizeof(g_err), fmt, ap);
        va_end(ap);
    }

    const char* LastError() { return g_err; }

    // RT_RCDATA is MAKEINTRESOURCE(10) (an ANSI macro); the W APIs need the
    // wide form, otherwise C2664.
    static const LPCWSTR kRcData = MAKEINTRESOURCEW(10);

    static unsigned ResourceSize(int id)
    {
        HRSRC hr = FindResourceW(nullptr, MAKEINTRESOURCEW(id), kRcData);
        if (!hr) return 0;
        return SizeofResource(nullptr, hr);
    }

    unsigned MapperBytes() { return ResourceSize(IDR_KDMAPPER); }
    unsigned DriverBytes() { return ResourceSize(IDR_DRIVER); }

    bool IsElevated()
    {
        HANDLE tok = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
        TOKEN_ELEVATION elev{};
        DWORD n = 0;
        const bool ok = GetTokenInformation(tok, TokenElevation, &elev, sizeof(elev), &n) != FALSE;
        CloseHandle(tok);
        return ok && elev.TokenIsElevated != 0;
    }

    bool Present()
    {
        // Detach so Ensure() re-resolves the pid and re-opens the mapping: after
        // the driver maps, the section name appears under a new pid.
        FrameShm::Detach();
        if (!FrameShm::Ensure()) return false;

        unsigned char hdr[32] = { 0 };
        if (!FrameShm::Read(hdr, (int)sizeof(hdr))) return false;

        unsigned magic = 0;
        memcpy(&magic, hdr, sizeof(magic));
        return magic == 0x45533243u;
    }

    static bool Extract(int id, const wchar_t* path)
    {
        HRSRC hr = FindResourceW(nullptr, MAKEINTRESOURCEW(id), kRcData);
        if (!hr) { SetErr("embedded resource %d missing (rebuild with tools\\make-resources.ps1)", id); return false; }

        const DWORD size = SizeofResource(nullptr, hr);
        HGLOBAL hg = LoadResource(nullptr, hr);
        const void* src = hg ? LockResource(hg) : nullptr;
        if (!src || size == 0) { SetErr("resource %d is empty", id); return false; }

        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) { SetErr("cannot create %ls (err %lu)", path, GetLastError()); return false; }

        DWORD wrote = 0;
        const BOOL ok = WriteFile(f, src, size, &wrote, nullptr);
        CloseHandle(f);
        if (!ok || wrote != size) { SetErr("short write to %ls", path); return false; }
        return true;
    }

    static bool MakeTempDir(wchar_t* out, size_t cch)
    {
        wchar_t base[MAX_PATH] = { 0 };
        if (GetTempPathW(MAX_PATH, base) == 0) return false;

        wchar_t name[64];
        swprintf_s(name, L"cs2esp_%08X%04X", (unsigned)GetTickCount64(),
                   (unsigned)(GetCurrentProcessId() & 0xFFFF));
        swprintf_s(out, cch, L"%s%s", base, name);
        return CreateDirectoryW(out, nullptr) != FALSE;
    }

    static bool RunMapper(const std::wstring& mapper, const std::wstring& driver, DWORD* exitCode)
    {
        std::wstring cmd = L"\"" + mapper + L"\" --indPages \"" + driver + L"\"";
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            SetErr("CreateProcess for the mapper failed (err %lu)", GetLastError());
            return false;
        }

        DWORD wr = WaitForSingleObject(pi.hProcess, 30000);
        DWORD code = 0xFFFFFFFF;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        if (exitCode) *exitCode = code;
        if (wr == WAIT_TIMEOUT) { SetErr("mapper timed out (driver blocked by AV?)"); return false; }
        if (code != 0) { SetErr("mapper exit 0x%X (driver blocked by AV / HVCI?)", code); return false; }
        return true;
    }

    bool Ensure(bool force)
    {
        if (!force && Present()) return true;

        if (!IsElevated())
        {
            SetErr("administrator required: mapping the driver needs SeLoadDriverPrivilege");
            return false;
        }
        if (ResourceSize(IDR_KDMAPPER) == 0 || ResourceSize(IDR_DRIVER) == 0)
        {
            SetErr("embedded payload missing; rebuild with tools\\make-resources.ps1");
            return false;
        }

        wchar_t dir[MAX_PATH] = { 0 };
        if (!MakeTempDir(dir, MAX_PATH))
        {
            SetErr("cannot create a temp directory (err %lu)", GetLastError());
            return false;
        }

        const std::wstring kdm = std::wstring(dir) + L"\\k.exe";
        const std::wstring sys = std::wstring(dir) + L"\\d.sys";

        bool ok = Extract(IDR_KDMAPPER, kdm.c_str()) && Extract(IDR_DRIVER, sys.c_str());
        if (ok)
        {
            DWORD code = 0;
            ok = RunMapper(kdm, sys, &code);
        }

        // The mapper already copied the image into kernel memory; the temp files
        // are only needed during the call.
        DeleteFileW(kdm.c_str());
        DeleteFileW(sys.c_str());
        RemoveDirectoryW(dir);

        if (!ok) return false;

        for (int i = 0; i < 24; i++)
        {
            if (Present()) return true;
            Sleep(150);
        }
        SetErr("driver loaded but the shared frame section never became readable");
        return false;
    }
}
