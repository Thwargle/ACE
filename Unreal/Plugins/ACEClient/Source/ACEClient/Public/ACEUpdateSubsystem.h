#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ACEUpdateSubsystem.generated.h"

struct FACEUpdateRelease
{
    int32 Number = 0;
    FString Version, File, Sha256;
    int64 Bytes = 0;
    FString DownloadURL() const;
};

namespace ACEUpdates
{
    inline constexpr const TCHAR* ManifestURL = TEXT("https://thwargle.com/assets/ac/release.json");
    // Only the fixed publisher and versioned filenames are accepted. JSON cannot supply a command or URL.
    ACECLIENT_API bool ParseManifest(const FString& Json, bool bQuest, FACEUpdateRelease& Out, FString& Error, bool bLinux = false);
    ACECLIENT_API bool VerifyFile(const FString& Path, const FACEUpdateRelease& Release);
    ACECLIENT_API const TCHAR* InstallationNotice(bool bQuest);
}

enum class EACEUpdateState : uint8 { Idle, Checking, Current, Available, Downloading, Verifying, Ready, InstallNotice, Installing, Error };
enum class EACEAutoUpdateAction : uint8 { None, Check, Download, Install };
class FACEUpdateStream;

/** Shared, optional lobby updater. It never starts an installation from gameplay. */
UCLASS()
class ACECLIENT_API UACEUpdateSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Deinitialize() override;
    void Check(bool bAutomatic = false);
    void Download();
    void Cancel();
    void Install(bool bVR);
    void PollInstall();
    void SetAutoUpdateEnabled(bool bEnabled);
    bool IsAutoUpdateEnabled() const { return bAutoUpdateEnabled; }
    /** Called only by the launcher. Never downloads or installs during a game session. */
    EACEAutoUpdateAction PollAutoUpdate(bool bVR);
    bool IsBusy() const;
    bool CanUseLobby() const;
    EACEUpdateState State = EACEUpdateState::Idle;
    FString Message = TEXT("Updates are checked when the launcher opens.");
    FACEUpdateRelease Release;
    float Progress() const;
private:
    friend class FACEUpdateManifestTest;
    friend class FACEAutoUpdateTest;
    friend class FACEUpdateNoticeTest;
    bool AdvanceInstallNotice(double Now);
    void LaunchInstaller();
    EACEAutoUpdateAction TakeAutomaticAction(double Now, bool bInLobby);
    void ResetRequest();
    void Fail(const FString& Reason);
    void VerifyDownload(const FString& Path, bool bCached);
    FString CacheDirectory() const;
    FString PayloadPath() const;
    bool bChecked = false;
    bool bAutoUpdateEnabled = false;
    bool bAutoUpdatePaused = false;
    bool bAutoDownloadStarted = false;
    bool bAutoInstallStarted = false;
    double NextAutomaticCheck = 0;
    double InstallAt = 0;
    bool bInstallVR = false;
    uint32 Generation = 0;
    TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> Request;
    TSharedPtr<FACEUpdateStream, ESPMode::ThreadSafe> Stream;
};
