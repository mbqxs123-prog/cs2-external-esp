#pragma once
#include "Game.h"
#include "View.hpp"
#include "Bone.h"
#include "Globals.hpp"

class PlayerController
{
public:
	DWORD64 Address = 0;
	int m_iTeamNum = 0;
	int m_iHealth = 0;
	int m_bPawnIsAlive = 0;
	DWORD Pawn = 0;
	std::string PlayerName;
public:
	bool GetTeamID();
	bool GetHealth();
	bool GetIsAlive();
	bool GetPlayerName();
	DWORD64 GetPlayerPawnAddress();
};

class PlayerPawn
{
public:
	enum class Flags
	{
		NONE,
		IN_AIR = 1 << 0
	};

	DWORD64 Address = 0;
	CBone BoneData;
	Vec2 ViewAngle;
	Vec3 m_vOldOrigin;
	Vec2 ScreenPos;
	std::string WeaponName;
	DWORD ShotsFired;
	int m_iHealth;
	int m_iTeamNum;
	int Fov;
	DWORD64 m_bSpottedByMask;
	int m_fFlags;

	// ---- fed by the driver frame (no process reads) ----
	int   m_iArmor = 0;        // 0..100
	int   m_iGear = 0;         // CS2_GEAR_* bitmask
	int   m_iWeaponId = 0;     // item definition index
	int   m_iAmmo = 0;         // magazine
	float m_flDistance = 0.f;  // metres to local player
	Vec3  m_vVelocity{};       // world units / second
	bool  m_bDormant = false;
	bool  m_bSpotted = false;
public:
	bool GetPos();
	bool GetViewAngle();
	bool GetWeaponName();
	bool GetShotsFired();
	bool GetHealth();
	bool GetTeamID();
	bool GetFov();
	bool GetSpottedMask();
	bool GetFFlags();

	constexpr bool HasFlag(const Flags Flag) const noexcept {
		return m_fFlags & (int)Flag;
	}


};

class CEntity
{
public:
	PlayerController Controller;
	PlayerPawn Pawn;
public:

	bool UpdateController(const DWORD64& PlayerControllerAddress);
	bool UpdatePawn(const DWORD64& PlayerPawnAddress);

	bool IsAlive();
	bool IsInScreen();
	const CBone& GetBone() const;
};