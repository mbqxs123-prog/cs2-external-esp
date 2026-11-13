#pragma once
#include "OS-ImGui/OS-ImGui.h"
#include "Utils/ProcessManager.hpp"

class CView
{
public:
	float Matrix[4][4]{};
	Vec3  Origin{};
	Vec3  Angles{};
	float Fov = 0.f;
	// Offline/frame checks have no overlay window yet; when set, projection uses
	// this size instead of Gui.Window.Size (which is 0 before the window exists).
	Vec2  ViewportOverride{ 0.f, 0.f };

	// CViewRender singleton lookup. The original m_vecLastClipCameraPos field on
	// C_CSPlayerPawn was removed from the schema, so the local camera origin is
	// pulled from the CViewRender instance the engine writes per frame.
	// Layout (matches catalyst/view.cpp): instance + 0x10 + { 0x0 origin, 0xC angles, 0x18 fov }.
	DWORD64 ViewRenderInstance = 0;

	bool ResolveViewRender(DWORD64 ClientDLLBase)
	{
		if (ViewRenderInstance != 0)
			return true;
		if (ClientDLLBase == 0)
			return false;
		ViewRenderInstance = ProcessMgr.FindVTableInstance(ClientDLLBase, "CViewRender");
		return ViewRenderInstance != 0;
	}

	bool UpdateCamera(DWORD64 ClientDLLBase)
	{
		if (!ResolveViewRender(ClientDLLBase))
			return false;

		const DWORD64 ViewBase = ViewRenderInstance + 0x10;
		if (!ProcessMgr.ReadMemory<Vec3>(ViewBase + 0x0, Origin))
			return false;
		ProcessMgr.ReadMemory<Vec3>(ViewBase + 0xC, Angles);
		ProcessMgr.ReadMemory<float>(ViewBase + 0x18, Fov);
		return true;
	}

	bool WorldToScreen(const Vec3& Pos, Vec2& ToPos)
	{
		float View = 0.f;
		const float vw = (ViewportOverride.x > 1.f) ? ViewportOverride.x : Gui.Window.Size.x;
		const float vh = (ViewportOverride.y > 1.f) ? ViewportOverride.y : Gui.Window.Size.y;
		float SightX = vw / 2, SightY = vh / 2;

		View = Matrix[3][0] * Pos.x + Matrix[3][1] * Pos.y + Matrix[3][2] * Pos.z + Matrix[3][3];

		if (View <= 0.01)
			return false;

		ToPos.x = SightX + (Matrix[0][0] * Pos.x + Matrix[0][1] * Pos.y + Matrix[0][2] * Pos.z + Matrix[0][3]) / View * SightX;
		ToPos.y = SightY - (Matrix[1][0] * Pos.x + Matrix[1][1] * Pos.y + Matrix[1][2] * Pos.z + Matrix[1][3]) / View * SightY;

		return true;
	}
};
