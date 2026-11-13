#pragma once
#include "Game.h"
#include "Bone.h"

namespace MenuConfig
{
	inline std::string path = "";

	inline bool ShowBoneESP = true;
	inline bool ShowBoxESP = true;
	inline bool ShowHealthBar = true;
	inline bool ShowWeaponESP = true;
	inline bool ShowDistance = true;
	inline bool ShowEyeRay = false;
	inline bool ShowPlayerName = true;
	inline bool ShowGlow = true;
	inline bool ShowSpotted = true;

	inline DWORD  TargetBoneIndex = BONEINDEX::HEAD;
	inline bool VisibleCheck = true;
	// 0: normal 1: dynamic
	inline int  BoxType = 0;
	// 0: Vertical 1: Horizontal
	inline int  HealthBarType = 0;

	inline ImColor BoneColor = ImColor(255, 255, 255, 255);
	inline ImColor BoxColor = ImColor(255, 255, 255, 255);
	inline ImColor GlowColor = ImColor(255, 255, 255, 255);
	inline ImColor EyeRayColor = ImColor(255, 0, 0, 255);

	inline bool ShowMenu = false;   // HOME toggles; start clean so the overlay is unobstructed

	inline bool ShowRadar = true;
	inline float RadarRange = 150;
	inline float RadarPointSizeProportion = 1.f;
	inline bool ShowRadarCrossLine = true;
	inline ImColor RadarCrossLineColor = ImColor(220, 220, 220, 255);
	// 0: circle 1: arrow 2: circle with arrow
	inline int RadarType = 2;
	inline float Proportion = 2230;


	inline bool TeamCheck = true;
	inline bool OBSBypass = false;

	inline bool BunnyHop = false;
	
	inline bool ShowHeadShootLine = true;
	inline ImColor HeadShootLineColor = ImColor(255, 255, 255, 255);

	inline bool ShowFovLine = true;
	inline ImColor FovLineColor = ImColor(55, 55, 55, 220);
	inline float FovLineSize = 60.f;

	inline bool ShowLineToEnemy = false;
	inline ImColor LineToEnemyColor = ImColor(255, 255, 255, 220);

	inline bool ShowCrossHair = true;
	inline ImColor CrossHairColor = ImColor(45, 45, 45, 255);
	inline float CrossHairSize = 150;

	inline bool ShowWhenSpec = true;

	inline bool AntiFlashbang = true;

	// ---- frame-fed extras ----
	inline bool ShowArmorBar = true;
	inline bool ShowGear = true;          // H helmet / K kit / Z scoped / R reload / D defusing
	inline bool ShowAmmo = true;          // magazine next to the weapon name
	inline bool ShowBombCarrier = true;   // red C4 tag over the carrier
	inline bool ShowItems = true;         // v6 tail: ground items / planted C4
	inline bool ShowGrenades = true;      // v7 tail: live grenades + fire areas
	inline bool ShowSnapline = false;
	inline bool ShowBoxFill = false;
	inline bool ShowTeam = false;         // draw teammates too
	inline bool ShowLocalInfo = true;     // frame status line (seq / counts)

	inline ImColor ArmorColor = ImColor(90, 165, 255, 255);
	inline ImColor GearColor = ImColor(230, 230, 230, 235);
	inline ImColor C4Color = ImColor(255, 70, 70, 255);
	inline ImColor GrenadeColor = ImColor(130, 255, 130, 255);
	inline ImColor FireColor = ImColor(255, 150, 40, 255);
	inline ImColor ItemColor = ImColor(255, 215, 0, 255);
	inline ImColor SnaplineColor = ImColor(255, 255, 255, 255);
}
