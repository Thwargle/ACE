#pragma once
#include "CoreMinimal.h"

namespace ACEClientBuild
{
#if PLATFORM_ANDROID
	inline constexpr const TCHAR* Version = TEXT("2026.09.17.quest.42");
#else
	inline constexpr const TCHAR* Version = TEXT("2026.09.17.52");
#endif
}
