#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACERetailUILayout.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEPlayerController.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

bool UACEUIGameplayBinder::GetAutoUILayoutPath(FString& Path) const
{
    const auto Session = Client ? Client->GetSession() : nullptr;
    const FIntPoint Size = Manager ? Manager->GetScreenLayoutSize() : FIntPoint::ZeroValue;
    if (!Session || Client->GetSessionState() != EACESessionState::InWorld || !Client->GetPlayerGuid()
        || Size.X <= 0 || Size.Y <= 0) return false;
    const FString Server = Session->GetServerName(), Character = Session->GetPlayerName();
    if (Server.IsEmpty() || Character.IsEmpty()) return false;
    const TCHAR* Mode = PLATFORM_ANDROID ? TEXT("Quest")
        : PlayerController && PlayerController->IsVRActive() ? TEXT("PCVR") : TEXT("Desktop");
    Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UILayouts/Auto"), Mode,
        ACERetailUILayout::AutoFile(Server, Character, Size));
    return true;
}

void UACEUIGameplayBinder::UpdateAutoUILayout()
{
    if (!Client || !Manager) return;
    if (Client->GetSessionState() != EACESessionState::InWorld)
    { AutoLayoutPlayer = 0; AutoLayoutSession.Reset(); return; }
    const auto Session = Client->GetSession();
    const int32 Player = Client->GetPlayerGuid();
    const FIntPoint Size = Manager->GetScreenLayoutSize();
    const bool VR = PlayerController && PlayerController->IsVRActive();
    // No filesystem access or name/path construction in steady-state HUD ticks.
    if (AutoLayoutSession.Pin() == Session && AutoLayoutPlayer == Player
        && AutoLayoutSize == Size && bAutoLayoutVR == VR) return;
    FString Path;
    if (!GetAutoUILayoutPath(Path)) return; // Wait for character identity and layout.
    AutoLayoutSession = Session; AutoLayoutPlayer = Player; AutoLayoutSize = Size; bAutoLayoutVR = VR;
    if (!IFileManager::Get().FileExists(*Path)) return; // An auto snapshot is optional.
    FString Text, Error;
    if (!ACERetailUILayout::Load(Path, Text, Error) || !Manager->ImportScreenLayout(Text, Error))
        AppendLocalChatLine(Error, ACEChatMessageType::ChatError);
}

bool UACEUIGameplayBinder::TryDispatchUILayoutCommand(const FString& Cmd, const FString& Args)
{
    if (Cmd == TEXT("lockui"))
    {
        if (!Args.IsEmpty()) AppendLocalChatLine(TEXT("Usage: /lockui"), ACEChatMessageType::ChatError);
        else if (Manager)
        {
            Manager->ToggleUiLocked();
            AppendLocalChatLine(Manager->IsUiLocked() ? TEXT("UI locked.") : TEXT("UI unlocked."), ACEChatMessageType::System);
        }
        return true;
    }
    const bool Auto = Cmd == TEXT("saveautoui") || Cmd == TEXT("loadautoui");
    const bool Saving = Cmd == TEXT("saveui") || Cmd == TEXT("saveautoui");
    if (!Auto && Cmd != TEXT("saveui") && Cmd != TEXT("loadui")) return false;
    auto ErrorLine = [this](const FString& Error) { AppendLocalChatLine(Error, ACEChatMessageType::ChatError); };
    if (!Manager || !Client || Client->GetSessionState() != EACESessionState::InWorld
        || Manager->GetScreenLayoutSize() == FIntPoint::ZeroValue)
    { ErrorLine(TEXT("UI layouts can only be saved or loaded while playing a character.")); return true; }
    FString Path, Error;
    if (Auto)
    {
        if (!Args.IsEmpty()) { ErrorLine(FString::Printf(TEXT("Usage: /%s (no filename)"), *Cmd)); return true; }
        if (!GetAutoUILayoutPath(Path)) { ErrorLine(TEXT("The character and server identity are not available yet.")); return true; }
    }
    else
    {
        FString Filename;
        if (!ACERetailUILayout::NamedFile(Args, Filename, Error)) { ErrorLine(Error); return true; }
        Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UILayouts"), Filename);
    }
    FString Text;
    const bool Success = Saving ? ACERetailUILayout::Save(Path, Manager->ExportScreenLayout(), Error)
        : ACERetailUILayout::Load(Path, Text, Error) && Manager->ImportScreenLayout(Text, Error);
    if (!Success) ErrorLine(Error);
    else AppendLocalChatLine(FString::Printf(TEXT("UI layout %s: %s"), Saving ? TEXT("saved") : TEXT("loaded"),
        *FPaths::ConvertRelativePathToFull(Path)), ACEChatMessageType::System);
    return true;
}
