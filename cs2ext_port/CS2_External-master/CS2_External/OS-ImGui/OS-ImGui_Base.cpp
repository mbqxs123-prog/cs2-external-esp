#include "OS-ImGui_Base.h"

/****************************************************
* Copyright (C)	: Liv
* @file			: OS-ImGui_Base.cpp
* @author		: Liv
* @email		: 1319923129@qq.com
* @version		: 1.0
* @date			: 2023/6/18	11:21
****************************************************/

namespace OSImGui
{
    bool OSImGui_Base::InitImGui(ID3D11Device* device, ID3D11DeviceContext* device_context)
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;

        ImGui::StyleColorsDark();
        io.LogFilename = nullptr;

        // CJK font: the default ProggyClean has no Chinese glyphs. msyh.ttc is
        // present on every Win10/11; simhei is the fallback, then the built-in.
        {
            ImFontConfig cfg;
            cfg.OversampleH = 2;
            cfg.OversampleV = 2;
            ImFont* f = io.Fonts->AddFontFromFileTTF(
                "C:\\Windows\\Fonts\\msyh.ttc", 16.0f, &cfg,
                io.Fonts->GetGlyphRangesChineseFull());
            if (!f)
            {
                f = io.Fonts->AddFontFromFileTTF(
                    "C:\\Windows\\Fonts\\simhei.ttf", 16.0f, &cfg,
                    io.Fonts->GetGlyphRangesChineseFull());
            }
            if (!f) { io.Fonts->AddFontDefault(); }
        }

        if (!ImGui_ImplWin32_Init(Window.hWnd))
            throw OSException("ImGui_ImplWin32_Init() call failed.");
        if (!ImGui_ImplDX11_Init(device, device_context))
            throw OSException("ImGui_ImplDX11_Init() call failed.");

        return true;
    }

    void OSImGui_Base::CleanImGui()
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        g_Device.CleanupDeviceD3D();
        DestroyWindow(Window.hWnd);
        UnregisterClassA(Window.ClassName.c_str(), Window.hInstance);
    }

    std::wstring OSImGui_Base::StringToWstring(std::string& str)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.from_bytes(str);
    }

}