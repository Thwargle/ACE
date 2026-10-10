#include "ACEDesktopDisplay.h"
#include "ACEPlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Widgets/SWindow.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
constexpr const TCHAR* Section = TEXT("ACEDesktopDisplay");
class FDisplayInput final : public IInputProcessor
{
    TWeakObjectPtr<AACEPlayerController> Controller;
    bool Restored = false;
    int32 LastMode = -1;
    FVector2D LastPosition = FVector2D(-1,-1);
    double SaveAt = 0;
    double RestoreAt = 0;
    bool PendingRestore = false;

    void SavePosition(const FVector2D& Position)
    {
        if (!GConfig) return;
        GConfig->SetInt(Section,TEXT("WindowX"),FMath::RoundToInt(Position.X),GGameUserSettingsIni);
        GConfig->SetInt(Section,TEXT("WindowY"),FMath::RoundToInt(Position.Y),GGameUserSettingsIni);
        GConfig->Flush(false,GGameUserSettingsIni);
    }

    void RestorePosition(const TSharedPtr<SWindow>& Window)
    {
        int32 X, Y;
        if (!GConfig || !GConfig->GetInt(Section,TEXT("WindowX"),X,GGameUserSettingsIni)
            || !GConfig->GetInt(Section,TEXT("WindowY"),Y,GGameUserSettingsIni)) return;
        // Keep the title bar accessible after unplugging or rearranging monitors.
        const auto& Metrics = FSlateApplication::Get().GetCachedDisplayMetricsByRef();
        bool Visible = false;
        for (const auto& Monitor : Metrics.MonitorInfo)
            Visible |= X+80 > Monitor.WorkArea.Left && X < Monitor.WorkArea.Right-80
                && Y >= Monitor.WorkArea.Top && Y < Monitor.WorkArea.Bottom-32;
        if (Visible) Window->MoveWindowTo(FVector2D(X,Y));
    }
public:
    explicit FDisplayInput(AACEPlayerController* PC) : Controller(PC) {}
    virtual void Tick(float, FSlateApplication& App, TSharedRef<ICursor>) override
    {
        auto* PC=Controller.Get();
        auto* Viewport=PC && PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr;
        auto Window=Viewport ? Viewport->GetWindow() : nullptr;
        if (!Window || PC->IsVRActive()) return;
        const auto Mode=Window->GetWindowMode();
        const double Now=FPlatformTime::Seconds();
        if (!Restored)
        {
            Restored=true;
            if (Mode==EWindowMode::Windowed) RestorePosition(Window);
        }
        if (PendingRestore && Now>=RestoreAt && Mode==EWindowMode::Windowed)
        {
            RestorePosition(Window); PendingRestore=false;
        }
        if (LastMode!=int32(Mode))
        {
            if (LastMode>=0 && Mode==EWindowMode::Windowed)
            { PendingRestore=true; RestoreAt=Now+.1; }
            LastMode=int32(Mode);
            // Reapply confinement when switching at login as well as in world.
            // Updating GameUserSettings alone leaves Slate's previous lock active.
            PC->RefreshDesktopDisplayInputMode();
        }
        if (Mode!=EWindowMode::Windowed || Window->IsWindowMinimized() || PendingRestore) return;
        const FVector2D Position=Window->GetPositionInScreen();
        if (Position!=LastPosition) { LastPosition=Position; SaveAt=Now+.5; }
        if (SaveAt>0 && Now>=SaveAt && GConfig)
        {
            SaveAt=0;
            SavePosition(Position);
        }
    }
    virtual bool HandleKeyDownEvent(FSlateApplication& App,const FKeyEvent& Event) override
    {
        const bool Toggle=(Event.GetKey()==EKeys::Enter && Event.IsAltDown())
            || (Event.GetKey()==EKeys::F11 && !Event.IsControlDown() && !Event.IsAltDown());
        if (!Toggle) return false;
        auto* PC=Controller.Get();
        auto* Viewport=PC && PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr;
        const auto Window=Viewport ? Viewport->GetWindow() : nullptr;
        auto* Settings=UGameUserSettings::GetGameUserSettings();
        if (!Window || !Settings || PC->IsVRActive() || App.GetActiveTopLevelWindow()!=Window) return false;
        if (Event.IsRepeat()) return true;
        const auto Actual=Window->GetWindowMode();
        if (Actual==EWindowMode::Windowed) SavePosition(Window->GetPositionInScreen());
        const auto Next=FACEDesktopDisplay::ToggleMode(Actual,Settings->GetPreferredFullscreenMode());
        Settings->SetFullscreenMode(Next);
        // Preserve the chosen resolution. UE's default toggle replaces it with
        // desktop dimensions, then tests IsFullscreen (false for borderless).
        Settings->ApplyResolutionSettings(false);
        Settings->ConfirmVideoMode();
        Settings->SaveSettings();
        if (Next==EWindowMode::Windowed) { PendingRestore=true; RestoreAt=FPlatformTime::Seconds()+.1; }
        return true;
    }
};
class FDisplayLifetime final : public FACEDesktopDisplay
{
    TSharedPtr<FDisplayInput> Input;
public:
    explicit FDisplayLifetime(AACEPlayerController* PC) : Input(MakeShared<FDisplayInput>(PC))
    { FSlateApplication::Get().RegisterInputPreProcessor(Input,0); }
    virtual ~FDisplayLifetime() override
    { if (FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(Input); }
};
}
TSharedPtr<FACEDesktopDisplay> FACEDesktopDisplay::Create(AACEPlayerController* PC)
{
    if (!PC || !PC->GetWorld() || PC->GetWorld()->IsPlayInEditor() || !FSlateApplication::IsInitialized()
        || FParse::Param(FCommandLine::Get(),TEXT("unattended"))
        || FParse::Param(FCommandLine::Get(),TEXT("RenderOffscreen"))) return nullptr;
    return MakeShared<FDisplayLifetime>(PC);
}
