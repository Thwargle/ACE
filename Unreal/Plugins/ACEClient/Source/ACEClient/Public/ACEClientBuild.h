#pragma once
#include "CoreMinimal.h"

class UWorld;

namespace ACEClientBuild
{
	inline constexpr int32 ReleaseNumber = 79;
	inline constexpr const TCHAR* DesktopProductName = TEXT("AC:Unreal");
	inline constexpr const TCHAR* VRProductName = TEXT("AC:VR");
	inline const TCHAR* ProductName(bool bVR = false)
	{
		return PLATFORM_ANDROID || bVR ? VRProductName : DesktopProductName;
	}
	ACECLIENT_API void UpdateWindowTitle(UWorld* World, bool bVR = false);
	// One display version for desktop, PC VR, and standalone Quest.
	inline constexpr const TCHAR* Version = TEXT("2026.09.26.79");
}
