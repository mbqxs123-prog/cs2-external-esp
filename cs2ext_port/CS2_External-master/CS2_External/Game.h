#pragma once
#include <optional>
#include "Utils/ProcessManager.hpp"
#include "Offsets.h"
#include "View.hpp"

class CGame
{
private:
	struct
	{
		DWORD64 ClientDLL;
		DWORD64 EntityList;
		DWORD64 Matrix;
		DWORD64 ViewAngle;
		DWORD64 EntityListEntry;
		DWORD64 EntityListEntrySize = 0x70;
		DWORD64 LocalController;
		DWORD64 LocalPawn;
		DWORD64 ForceJump;
		DWORD64 GlobalVars;
		DWORD64 Sensitivity;
	}Address;

public:
	CView View;

public:	

	bool InitAddress();

	DWORD64 GetClientDLLAddress();

	DWORD64 GetEntityListAddress();

	DWORD64 GetMatrixAddress();

	DWORD64 GetViewAngleAddress();

	DWORD64 GetEntityListEntry();

	DWORD64 GetEntityListEntrySize();

	DWORD64 GetLocalControllerAddress();

	DWORD64 GetLocalPawnAddress();

	DWORD64 GetGlobalVarsAddress();

	DWORD64 GetSensitivityAddress();

	bool UpdateEntityListEntry();

	// Frame-fed build: display side never writes game memory. Legacy Cheats::Run
	// still references this name; keep a no-op so the old path compiles.
	void SetForceJump(int) {}

};

inline CGame gGame;