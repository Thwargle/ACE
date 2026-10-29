#include "ACEClientBuild.h"
#include "Mobile/ACEMobileSettings.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Widgets/SWindow.h"

const TCHAR* ACEClientBuild::ProductName(bool bVR)
{
    return UACEMobileSettings::IsEnabled() && !bVR ? TEXT("AC:Unreal Mobile") : PLATFORM_ANDROID || bVR ? VRProductName : DesktopProductName;
}

void ACEClientBuild::UpdateWindowTitle(UWorld* World, bool bVR)
{
	// Embedded PIE shares the editor's window. Only rename standalone game windows.
	if (!World || World->WorldType != EWorldType::Game) return;
	if (const auto* Viewport = World->GetGameViewport())
	{
		if (const auto Window = Viewport->GetWindow())
			Window->SetTitle(FText::FromString(FString::Printf(TEXT("%s - Build %s"), ProductName(bVR), Version)));
	}
}
