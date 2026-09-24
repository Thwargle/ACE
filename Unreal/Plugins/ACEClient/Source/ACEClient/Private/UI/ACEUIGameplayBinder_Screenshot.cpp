#include "UI/ACEUIGameplayBinder.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

void UACEUIGameplayBinder::RequestGameplayScreenshot()
{
	if (!PendingScreenshotFilename.IsEmpty() || FScreenshotRequest::IsScreenshotRequested())
	{
		PostInventorySystemMessage(TEXT("A screenshot is already being saved."));
		return;
	}
	if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport)
	{
		PostInventorySystemMessage(TEXT("Unable to take a screenshot: the game view is not available."));
		return;
	}
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir());
	if (!IFileManager::Get().MakeDirectory(*Folder, true))
	{
		PostInventorySystemMessage(FString::Printf(TEXT("Unable to create screenshot folder: %s"), *Folder));
		return;
	}
	// Request UE's unique numbered PNG, including the game UI but excluding editor chrome.
	FScreenshotRequest::RequestScreenshot(Folder / TEXT("AC-Screenshot.png"), true, true, false, FIntRect(), true);
	PendingScreenshotFilename = FScreenshotRequest::GetFilename();
	ScreenshotRequestTime = FPlatformTime::Seconds();
	ScreenshotProcessedHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(
		this, &UACEUIGameplayBinder::FinishGameplayScreenshot);
}

void UACEUIGameplayBinder::FinishGameplayScreenshot()
{
	if (PendingScreenshotFilename.IsEmpty()) return;
	const FString Filename = MoveTemp(PendingScreenshotFilename);
	CancelGameplayScreenshot();
	// The engine broadcasts after its image-write attempt, including failed attempts.
	const bool bSaved = IFileManager::Get().FileSize(*Filename) > 0;
	PostInventorySystemMessage(bSaved
		? FString::Printf(TEXT("Screenshot saved to file '%s'"), *Filename)
		: FString::Printf(TEXT("Unable to save screenshot to '%s'. Check available space and folder permissions."), *Filename));
}

void UACEUIGameplayBinder::CancelGameplayScreenshot()
{
	FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotProcessedHandle);
	ScreenshotProcessedHandle.Reset();
	PendingScreenshotFilename.Reset();
	ScreenshotRequestTime = 0;
}
