#include "ACEUpdateSubsystem.h"
#include "ACEClientBuild.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/GameInstance.h"
#define UI UI_ST
THIRD_PARTY_INCLUDES_START
#include <openssl/evp.h>
THIRD_PARTY_INCLUDES_END
#undef UI
#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"
#include "Android/AndroidJavaEnv.h"
#endif

// HTTP writes on a worker thread. A bounded, locked stream keeps full installers out of RAM
// and lets cancellation close the file safely before a retry opens the same cache path.
class FACEUpdateStream
{
public:
    explicit FACEUpdateStream(int64 InLimit, const FString& Path = FString()) : Limit(InLimit)
    {
        if (!Path.IsEmpty()) { File.Reset(IFileManager::Get().CreateFileWriter(*Path)); bFailed = !File; }
    }
    void Receive(void* Data, int64& Length)
    {
        FScopeLock Lock(&Mutex);
        if (bClosed || bFailed || Length < 0 || Length > Limit - Count) { bFailed = true; Length = 0; return; }
        if (File) { File->Serialize(Data, Length); bFailed = File->IsError(); }
        else Memory.Append(static_cast<uint8*>(Data), int32(Length));
        if (bFailed) { Length = 0; return; }
        Count += Length;
    }
    bool Close()
    {
        FScopeLock Lock(&Mutex);
        if (File) { bFailed |= !File->Close(); File.Reset(); }
        bClosed = true;
        return !bFailed;
    }
    int64 Received() const { FScopeLock Lock(&Mutex); return Count; }
    FString Text() const { FScopeLock Lock(&Mutex); FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Memory.GetData()), Memory.Num()); return FString(Text.Length(), Text.Get()); }
private:
    mutable FCriticalSection Mutex;
    TUniquePtr<FArchive> File;
    TArray<uint8> Memory;
    int64 Limit, Count = 0;
    bool bFailed = false, bClosed = false;
};

FString FACEUpdateRelease::DownloadURL() const
{
    return FString::Printf(TEXT("https://thwargle.com/downloads/ac/v%d/%s"), Number, *File);
}

bool ACEUpdates::ParseManifest(const FString& Json, bool bQuest, FACEUpdateRelease& Out, FString& Error)
{
    Error = TEXT("The update information is invalid. Please try again later.");
    if (Json.Len() > 65536) return false;
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) return false;
    double Number = 0, Schema = 0, Bytes = 0;
    const TSharedPtr<FJsonObject>* Updates = nullptr;
    const TSharedPtr<FJsonObject>* Artifact = nullptr;
    if (!Root->TryGetNumberField(TEXT("version"), Number) || !FMath::IsFinite(Number) || Number < 1 || Number > 100000 || Number != FMath::FloorToDouble(Number)) return false;
    if (!Root->TryGetObjectField(TEXT("updates"), Updates) || !(*Updates)->TryGetNumberField(TEXT("schema"), Schema) || Schema != 1)
    { Error = TEXT("This release does not offer in-game updates yet. Downloads are available on thwargle.com."); return false; }
    if (!(*Updates)->TryGetObjectField(bQuest ? TEXT("quest") : TEXT("windows"), Artifact)) return false;
    FACEUpdateRelease Parsed; Parsed.Number = int32(Number);
    if (!Root->TryGetStringField(bQuest ? TEXT("questVersion") : TEXT("windowsVersion"), Parsed.Version)
        || !(*Artifact)->TryGetStringField(TEXT("file"), Parsed.File)
        || !(*Artifact)->TryGetStringField(TEXT("sha256"), Parsed.Sha256)
        || !(*Artifact)->TryGetNumberField(TEXT("bytes"), Bytes)) return false;
    if (!FMath::IsFinite(Bytes) || Bytes < 1 || Bytes > 2147483648. || Bytes != FMath::FloorToDouble(Bytes)) return false;
    Parsed.Bytes = int64(Bytes);
    const FString Expected = (bQuest ? FString::Printf(TEXT("AC-VR-Quest-v%d.apk"), Parsed.Number) : FString::Printf(TEXT("AC-Unreal-Setup-v%d.exe"), Parsed.Number));
    if (Parsed.File != Expected || Parsed.Sha256.Len() != 64 || Parsed.Version.IsEmpty() || Parsed.Version.Len() > 48) return false;
    for (TCHAR C : Parsed.Sha256) if (!FChar::IsHexDigit(C)) return false;
    for (TCHAR C : Parsed.Version) if (!FChar::IsDigit(C) && C != '.' && !(bQuest && FString(TEXT("quest")).Contains(FString::Chr(C)))) return false;
    if (bQuest && !Parsed.Version.EndsWith(FString::Printf(TEXT(".quest.%d"), Parsed.Number))) return false;
    Parsed.Sha256.ToUpperInline(); Out = MoveTemp(Parsed); Error.Reset(); return true;
}

bool ACEUpdates::VerifyFile(const FString& Path, const FACEUpdateRelease& Release)
{
    TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Path));
    if (!File || File->TotalSize() != Release.Bytes) return false;
    EVP_MD_CTX* Hash = EVP_MD_CTX_new();
    if (!Hash) return false;
    bool Valid = EVP_DigestInit_ex(Hash, EVP_sha256(), nullptr) == 1;
    TArray<uint8> Buffer; Buffer.SetNumUninitialized(256 * 1024);
    int64 Left = Release.Bytes;
    while (Valid && Left > 0)
    {
        const int64 Size = FMath::Min<int64>(Left, Buffer.Num()); File->Serialize(Buffer.GetData(), Size);
        Valid = !File->IsError() && EVP_DigestUpdate(Hash, Buffer.GetData(), SIZE_T(Size)) == 1; Left -= Size;
    }
    uint8 Digest[EVP_MAX_MD_SIZE]; unsigned int Length = 0;
    Valid = Valid && EVP_DigestFinal_ex(Hash, Digest, &Length) == 1 && Length == 32;
    EVP_MD_CTX_free(Hash);
    return Valid && BytesToHex(Digest, Length).Equals(Release.Sha256, ESearchCase::IgnoreCase);
}

bool UACEUpdateSubsystem::IsBusy() const
{
    return State == EACEUpdateState::Downloading || State == EACEUpdateState::Verifying || State == EACEUpdateState::Installing;
}
bool UACEUpdateSubsystem::CanUseLobby() const
{
    const auto* Client = GetGameInstance() ? GetGameInstance()->GetSubsystem<UACEClientSubsystem>() : nullptr;
    const auto Session = Client ? Client->GetSession() : nullptr;
    return !Session || Session->GetState() == EACESessionState::Disconnected || Session->GetState() == EACESessionState::Failed;
}
float UACEUpdateSubsystem::Progress() const { return Stream && Release.Bytes > 0 ? FMath::Clamp(float(double(Stream->Received()) / Release.Bytes), 0.f, 1.f) : 0.f; }
void UACEUpdateSubsystem::Fail(const FString& Reason) { State = EACEUpdateState::Error; Message = Reason; }
void UACEUpdateSubsystem::Cancel()
{
    ++Generation;
    if (Request) { Request->OnProcessRequestComplete().Unbind(); Request->CancelRequest(); Request.Reset(); }
    if (Stream) { Stream->Close(); Stream.Reset(); }
    // Keep an already verified payload for another install attempt; partial downloads never install.
    State = Release.Number > ACEClientBuild::ReleaseNumber ? EACEUpdateState::Available : EACEUpdateState::Idle;
    Message = TEXT("Update cancelled. You can keep playing or try again.");
}
void UACEUpdateSubsystem::Deinitialize() { Cancel(); Super::Deinitialize(); }

FString UACEUpdateSubsystem::CacheDirectory() const
{
#if PLATFORM_ANDROID
    if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
    {
        static jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_ACEUpdateDirectory", "()Ljava/lang/String;", false);
        return FJavaHelper::FStringFromLocalRef(Env, static_cast<jstring>(FJavaWrapper::CallObjectMethod(Env, FJavaWrapper::GameActivityThis, Method)));
    }
#endif
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Updates"));
}
FString UACEUpdateSubsystem::PayloadPath() const { return CacheDirectory() / (PLATFORM_ANDROID ? TEXT("update.apk") : TEXT("update.exe")); }

void UACEUpdateSubsystem::Check(bool bAutomatic)
{
    if (IsBusy() || State == EACEUpdateState::Checking || !CanUseLobby()) return;
    if (bAutomatic && (bChecked || GIsEditor || FApp::IsUnattended())) return;
    Cancel(); bChecked = true; Release = {}; State = EACEUpdateState::Checking; Message = TEXT("Checking for updates...");
    const uint32 Ticket = Generation;
    auto Body = MakeShared<FACEUpdateStream, ESPMode::ThreadSafe>(65536); Stream = Body;
    Request = FHttpModule::Get().CreateRequest(); Request->SetURL(ACEUpdates::ManifestURL); Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Cache-Control"), TEXT("no-cache")); Request->SetTimeout(15.f);
    Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    Request->SetResponseBodyReceiveStreamDelegateV2(FHttpRequestStreamDelegateV2::CreateLambda([Body](void* Data, int64& Length) { Body->Receive(Data, Length); }));
    Request->OnProcessRequestComplete().BindWeakLambda(this, [this, Ticket, Body](FHttpRequestPtr, FHttpResponsePtr Response, bool Success)
    {
        if (Ticket != Generation) return;
        Request.Reset(); Stream.Reset(); const bool Closed = Body->Close();
        if (!Success || !Closed || !Response || Response->GetResponseCode() != 200 || Response->GetEffectiveURL() != ACEUpdates::ManifestURL)
        { Fail(TEXT("Could not check for updates. You can still log in; try again later.")); return; }
        FString Error;
        if (!ACEUpdates::ParseManifest(Body->Text(), PLATFORM_ANDROID, Release, Error)) { Fail(Error); return; }
        State = Release.Number > ACEClientBuild::ReleaseNumber ? EACEUpdateState::Available : EACEUpdateState::Current;
        Message = State == EACEUpdateState::Available ? FString::Printf(TEXT("Version %s is available (%.0f MB)."), *Release.Version, Release.Bytes / 1048576.) : TEXT("You have the latest version.");
    });
    if (!Request->ProcessRequest()) { Request.Reset(); Fail(TEXT("Could not check for updates. You can still log in.")); }
}

void UACEUpdateSubsystem::Download()
{
    if (IsBusy() || !CanUseLobby() || Release.Number <= ACEClientBuild::ReleaseNumber) return;
    Cancel(); State = EACEUpdateState::Downloading; Message = TEXT("Downloading update...");
    IFileManager::Get().MakeDirectory(*CacheDirectory(), true);
    uint64 Total = 0, Free = 0;
    if (FPlatformMisc::GetDiskTotalAndFreeSpace(CacheDirectory(), Total, Free) && Free < uint64(Release.Bytes) * 2 + 1073741824ull)
    { Fail(TEXT("Not enough free storage for the update. Free some space and try again.")); return; }
    if (IFileManager::Get().FileSize(*PayloadPath()) == Release.Bytes) { VerifyDownload(PayloadPath(), true); return; }
    const uint32 Ticket = Generation;
    auto Body = MakeShared<FACEUpdateStream, ESPMode::ThreadSafe>(Release.Bytes, PayloadPath() + TEXT(".part")); Stream = Body;
    Request = FHttpModule::Get().CreateRequest(); Request->SetURL(Release.DownloadURL()); Request->SetVerb(TEXT("GET")); Request->SetTimeout(1800.f);
    Request->SetHeader(TEXT("Accept-Encoding"), TEXT("identity"));
    Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    Request->SetResponseBodyReceiveStreamDelegateV2(FHttpRequestStreamDelegateV2::CreateLambda([Body](void* Data, int64& Length) { Body->Receive(Data, Length); }));
    Request->OnProcessRequestComplete().BindWeakLambda(this, [this, Ticket, Body](FHttpRequestPtr, FHttpResponsePtr Response, bool Success)
    {
        if (Ticket != Generation) return;
        Request.Reset(); Stream.Reset(); const bool Closed = Body->Close();
        if (!Success || !Closed || !Response || Response->GetResponseCode() != 200 || Response->GetEffectiveURL() != Release.DownloadURL() || Body->Received() != Release.Bytes)
        { Fail(TEXT("Download interrupted or incomplete. Choose Download update to retry.")); return; }
        VerifyDownload(PayloadPath() + TEXT(".part"), false);
    });
    if (!Request->ProcessRequest()) { Body->Close(); Request.Reset(); Fail(TEXT("Could not start the download. Please try again.")); }
}

void UACEUpdateSubsystem::VerifyDownload(const FString& Path, bool bCached)
{
    State = EACEUpdateState::Verifying; Message = TEXT("Verifying the downloaded update...");
    const uint32 Ticket = Generation; const FACEUpdateRelease Expected = Release;
    const TWeakObjectPtr<UACEUpdateSubsystem> Weak(this);
    Async(EAsyncExecution::ThreadPool, [Weak, Ticket, Expected, Path, bCached]
    {
        const bool Valid = ACEUpdates::VerifyFile(Path, Expected);
        AsyncTask(ENamedThreads::GameThread, [Weak, Ticket, Path, Valid, bCached]
        {
            auto* Self = Weak.Get(); if (!Self || Self->Generation != Ticket) return;
            if (!Valid)
            {
                IFileManager::Get().Delete(*Path, false, true);
                if (bCached) { Self->State = EACEUpdateState::Available; Self->Download(); }
                else Self->Fail(TEXT("The download failed verification. Nothing was installed. Please download again."));
                return;
            }
            if (!bCached && !IFileManager::Get().Move(*Self->PayloadPath(), *Path, true, true))
            { Self->Fail(TEXT("Could not save the update. Check storage and try again.")); return; }
            Self->State = EACEUpdateState::Ready;
            Self->Message = PLATFORM_ANDROID ? TEXT("Update ready. Choose Install, then confirm in the headset. Your game data is kept.") : TEXT("Update ready. Install and restart keeps your accounts, settings, and game data.");
        });
    });
}

void UACEUpdateSubsystem::Install(bool bVR)
{
    if (State != EACEUpdateState::Ready || !CanUseLobby()) return;
#if PLATFORM_ANDROID
    if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
    {
        static jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_ACEInstallUpdate", "(Ljava/lang/String;I)Ljava/lang/String;", false);
        const auto Path = FJavaHelper::ToJavaString(Env, PayloadPath());
        const FString Result = FJavaHelper::FStringFromLocalRef(Env, static_cast<jstring>(FJavaWrapper::CallObjectMethod(Env, FJavaWrapper::GameActivityThis, Method, *Path, Release.Number)));
        if (Result == TEXT("started")) { State = EACEUpdateState::Installing; Message = TEXT("Preparing installation. Confirm the update in the headset when asked."); }
        else if (Result == TEXT("permission")) Message = TEXT("Allow AC:VR to install updates in the headset settings, return here, then choose Install again.");
        else Message = TEXT("The headset could not open the installer. Try again, or use the USB updater from thwargle.com.");
        return;
    }
#elif PLATFORM_WINDOWS
    if (GIsEditor || FApp::IsUnattended()) { Fail(TEXT("Install updates from the packaged game, not the editor or a test run.")); return; }
    const FString Root = FPaths::ConvertRelativePathToFull(FString(FPlatformProcess::BaseDir()) / TEXT("../../.."));
    const FString Script = Root / TEXT("Update-Client.ps1");
    const FString Helper = CacheDirectory() / TEXT("Update-Client.ps1");
    if (!FPaths::FileExists(Root / TEXT("ACUnreal.exe")) || IFileManager::Get().Copy(*Helper, *Script, true, true) != COPY_OK)
    { Fail(TEXT("The update helper is missing. Please use the installer from thwargle.com.")); return; }
    FString Shell = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")) / TEXT("System32/WindowsPowerShell/v1.0/powershell.exe");
    const FString Args = FString::Printf(TEXT("-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%s\" -Installer \"%s\" -ExpectedSha256 %s -InstallDirectory \"%s\" -GameProcessId %u -Mode %s"),
        *Helper, *PayloadPath(), *Release.Sha256, *Root, FPlatformProcess::GetCurrentProcessId(), bVR ? TEXT("VR") : TEXT("Desktop"));
    auto Handle = FPlatformProcess::CreateProc(*Shell, *Args, true, true, true, nullptr, 0, *CacheDirectory(), nullptr);
    if (!Handle.IsValid()) { Fail(TEXT("Could not start the updater. Please try again.")); return; }
    FPlatformProcess::CloseProc(Handle); State = EACEUpdateState::Installing;
    FPlatformMisc::RequestExit(false); return;
#endif
#if !PLATFORM_WINDOWS
    Fail(TEXT("In-game installation is not available on this platform."));
#endif
}

void UACEUpdateSubsystem::PollInstall()
{
#if PLATFORM_ANDROID
    if (State != EACEUpdateState::Installing) return;
    if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
    {
        static jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_ACEUpdateStatus", "()Ljava/lang/String;", false);
        const FString Result = FJavaHelper::FStringFromLocalRef(Env, static_cast<jstring>(FJavaWrapper::CallObjectMethod(Env, FJavaWrapper::GameActivityThis, Method)));
        if (Result == TEXT("confirm")) Message = TEXT("Confirm the update in the headset's installation window.");
        else if (Result.StartsWith(TEXT("failed"))) { State = EACEUpdateState::Ready; Message = TEXT("Installation was cancelled or failed. Your current version is unchanged. You can try Install again."); }
    }
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUpdateManifestTest, "ACE.Updates.ManifestIntegrityAndCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEUpdateManifestTest::RunTest(const FString&)
{
    const FString Hash=TEXT("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"); // SHA-256 of abc
    const FString Json=FString::Printf(TEXT("{\"version\":74,\"windowsVersion\":\"2026.09.25.80\",\"questVersion\":\"2026.09.25.quest.74\",\"updates\":{\"schema\":1,\"windows\":{\"file\":\"AC-Unreal-Setup-v74.exe\",\"bytes\":3,\"sha256\":\"%s\"},\"quest\":{\"file\":\"AC-VR-Quest-v74.apk\",\"bytes\":3,\"sha256\":\"%s\"}}}"),*Hash,*Hash);
    FACEUpdateRelease Win, Quest; FString Error;
    TestTrue(TEXT("Windows release parses"),ACEUpdates::ParseManifest(Json,false,Win,Error));
    TestTrue(TEXT("Quest direct APK parses"),ACEUpdates::ParseManifest(Json,true,Quest,Error));
    TestEqual(TEXT("APK uses the fixed HTTPS publisher"),Quest.DownloadURL(),FString(TEXT("https://thwargle.com/downloads/ac/v74/AC-VR-Quest-v74.apk")));
    for(const FString& Bad:TArray<FString>{Json.Replace(TEXT("AC-Unreal-Setup-v74.exe"),TEXT("../evil.exe")),
        Json.Replace(TEXT("AC-Unreal-Setup-v74.exe"),TEXT("https://other.invalid/a.exe")),
        Json.Replace(TEXT("\"version\":74"),TEXT("\"version\":74.5")),
        Json.Replace(TEXT("\"bytes\":3"),TEXT("\"bytes\":2147483649")),
        Json.Replace(TEXT("\"bytes\":3"),TEXT("\"bytes\":-1")),
        Json.Replace(TEXT("\"schema\":1"),TEXT("\"schema\":2")),
        Json.Replace(*Hash,TEXT("abc")),FString(TEXT("{\"version\":74}"))})
    { FACEUpdateRelease Invalid; TestFalse(TEXT("Malformed or unsafe metadata is rejected"),ACEUpdates::ParseManifest(Bad,false,Invalid,Error)); }
    const FString Path=FPaths::ProjectSavedDir()/TEXT("Automation/Updates/fixture.bin");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    ON_SCOPE_EXIT { IFileManager::Get().Delete(*Path,false,true); };
    uint8 Data[]={'a','b','c'};
    auto Body=MakeShared<FACEUpdateStream,ESPMode::ThreadSafe>(3,Path);
    int64 Length=3; Body->Receive(Data,Length); TestTrue(TEXT("Streaming write closes"),Body->Close());
    TestTrue(TEXT("Known SHA-256 passes without loading file into memory"),ACEUpdates::VerifyFile(Path,Win));
    Win.Bytes=4; TestFalse(TEXT("Truncation rejected"),ACEUpdates::VerifyFile(Path,Win)); Win.Bytes=3;
    auto Overflow=MakeShared<FACEUpdateStream,ESPMode::ThreadSafe>(2); Length=3; Overflow->Receive(Data,Length);
    TestEqual(TEXT("Oversized responses abort the stream"),Length,int64(0)); TestFalse(TEXT("Overflow fails completion"),Overflow->Close());
    auto Cancelled=MakeShared<FACEUpdateStream,ESPMode::ThreadSafe>(3); Cancelled->Close(); Length=3; Cancelled->Receive(Data,Length);
    TestEqual(TEXT("Late writes after cancellation cannot reach a new download"),Length,int64(0));
    Data[2]='d'; FFileHelper::SaveArrayToFile(TArrayView<const uint8>(Data,3),*Path);
    TestFalse(TEXT("Same-sized corrupt payload rejected"),ACEUpdates::VerifyFile(Path,Win));
    auto* Updater=NewObject<UACEUpdateSubsystem>(NewObject<UGameInstance>()); Updater->Release.Number=ACEClientBuild::ReleaseNumber-1;
    Updater->State=EACEUpdateState::Available; Updater->Download();
    TestFalse(TEXT("Old releases never start a download"),Updater->IsBusy());
    Updater->State=EACEUpdateState::Downloading; const uint32 Before=Updater->Generation; Updater->Cancel();
    TestTrue(TEXT("Cancelling invalidates queued completion callbacks"),Updater->Generation>Before && !Updater->IsBusy());
    return true;
}
#endif
