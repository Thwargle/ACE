#pragma once
#include "CoreMinimal.h"

class UWorld;

namespace ACEClientBuild
{
	inline constexpr const TCHAR* DesktopProductName = TEXT("AC:Unreal");
	inline constexpr const TCHAR* VRProductName = TEXT("AC:VR");
	inline const TCHAR* ProductName(bool bVR = false)
	{
		return PLATFORM_ANDROID || bVR ? VRProductName : DesktopProductName;
	}
	ACECLIENT_API void UpdateWindowTitle(UWorld* World, bool bVR = false);
#if PLATFORM_ANDROID
	inline constexpr const TCHAR* Version = TEXT("2026.09.21.quest.60");
#else
	inline constexpr const TCHAR* Version = TEXT("2026.09.21.66");
#endif
}
