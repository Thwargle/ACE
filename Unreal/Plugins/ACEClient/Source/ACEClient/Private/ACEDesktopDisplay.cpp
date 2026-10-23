#include "ACEDesktopDisplay.h"
#include "ACEPlayerController.h"
#include "ACERuntimeOptions.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Widgets/SWindow.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Containers/Ticker.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
constexpr const TCHAR* Section = TEXT("ACEDesktopDisplay");
struct FModePreview
{
    bool Pending = false;
    EWindowMode::Type PreviousMode = EWindowMode::Windowed;
    FIntPoint PreviousSize;
    double Deadline = 0;
    TWeakObjectPtr<AACEPlayerController> Controller;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    TSharedPtr<SWidget> Prompt;
    FTSTicker::FDelegateHandle Timer;
} Preview;
bool TickModePreview(float)
{
    if (!Preview.Pending) return false;
    if (!Preview.Controller.IsValid() || FPlatformTime::Seconds()>=Preview.Deadline)
    { FACEDesktopDisplay::FinishModeChange(false);return false; }
    return true;
}
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
        if (Preview.Pending && (Event.GetKey()==EKeys::Escape
            || (Event.GetKey()==EKeys::Enter && !Event.IsAltDown() && !Event.IsControlDown())))
        {
            if (!Event.IsRepeat()) FACEDesktopDisplay::FinishModeChange(Event.GetKey()==EKeys::Enter);
            return true;
        }
        const bool Toggle=(Event.GetKey()==EKeys::Enter && Event.IsAltDown())
            || (Event.GetKey()==EKeys::F11 && !Event.IsControlDown() && !Event.IsAltDown());
        if (!Toggle) return false;
        auto* PC=Controller.Get();
        auto* Viewport=PC && PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr;
        const auto Window=Viewport ? Viewport->GetWindow() : nullptr;
        auto* Settings=UGameUserSettings::GetGameUserSettings();
        if (!Window || !Settings || PC->IsVRActive() || App.GetActiveTopLevelWindow()!=Window) return false;
        if (Event.IsRepeat()) return true;
        if (Preview.Pending) { FACEDesktopDisplay::FinishModeChange(false); return true; }
        const auto Actual=Window->GetWindowMode();
        if (Actual==EWindowMode::Windowed) SavePosition(Window->GetPositionInScreen());
        const auto Next=FACEDesktopDisplay::ToggleMode(Actual,Settings->GetPreferredFullscreenMode());
        const auto Size=FACEDesktopDisplay::ResolveResolution(Actual,Next,Settings->GetScreenResolution(),
            FACEDesktopDisplay::GetMonitorResolution(PC),false);
        FACEDesktopDisplay::ApplyModeChange(PC,Next,Size);
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
    {
        FACEDesktopDisplay::FinishModeChange(false);
        if (FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(Input);
    }
};
}
FIntPoint FACEDesktopDisplay::ResolveResolution(EWindowMode::Type Previous,EWindowMode::Type Next,
    FIntPoint Requested,FIntPoint Desktop,bool ExplicitResolution)
{
    if (Desktop.X<=0 || Desktop.Y<=0) Desktop=FIntPoint(1280,720);
    if (Next==EWindowMode::WindowedFullscreen || Requested.X<800 || Requested.Y<600
        || (Previous==EWindowMode::Windowed && Next==EWindowMode::Fullscreen && !ExplicitResolution))
        return Desktop;
    return Requested;
}

FIntPoint FACEDesktopDisplay::GetMonitorResolution(AACEPlayerController* PC)
{
    if (FSlateApplication::IsInitialized())
    {
        auto* Viewport=PC && PC->GetWorld()?PC->GetWorld()->GetGameViewport():nullptr;
        auto Window=Viewport?Viewport->GetWindow():nullptr;
        const auto& Metrics=FSlateApplication::Get().GetCachedDisplayMetricsByRef();
        if (Window)
        {
            const FVector2D Center=Window->GetPositionInScreen()+Window->GetSizeInScreen()*.5;
            for (const auto& Monitor:Metrics.MonitorInfo)
                if (Center.X>=Monitor.DisplayRect.Left && Center.X<Monitor.DisplayRect.Right
                    && Center.Y>=Monitor.DisplayRect.Top && Center.Y<Monitor.DisplayRect.Bottom)
                    return FIntPoint(Monitor.NativeWidth,Monitor.NativeHeight);
        }
        return FIntPoint(Metrics.PrimaryDisplayWidth,Metrics.PrimaryDisplayHeight);
    }
    return FIntPoint(1280,720);
}

bool FACEDesktopDisplay::IsModeChangePending() { return Preview.Pending; }

void FACEDesktopDisplay::FinishModeChange(bool Keep)
{
    if (!Preview.Pending) return;
    Preview.Pending=false;
    FTSTicker::GetCoreTicker().RemoveTicker(Preview.Timer);
    if (auto* Viewport=Preview.Viewport.Get();Viewport && Preview.Prompt)
        Viewport->RemoveViewportWidgetContent(Preview.Prompt.ToSharedRef());
    Preview.Prompt.Reset();
    if (auto* Settings=UGameUserSettings::GetGameUserSettings())
    {
        if (!Keep)
        {
            Settings->SetFullscreenMode(Preview.PreviousMode);
            Settings->SetScreenResolution(Preview.PreviousSize);
            Settings->ApplyResolutionSettings(false);
        }
        Settings->ConfirmVideoMode();Settings->SaveSettings();
    }
    if (auto* PC=Preview.Controller.Get()) PC->RefreshDesktopDisplayInputMode();
}

void FACEDesktopDisplay::ApplyModeChange(AACEPlayerController* PC,EWindowMode::Type Mode,FIntPoint Size)
{
    auto* Settings=UGameUserSettings::GetGameUserSettings();
    if (!Settings || (PC && PC->IsVRActive())) return;
    FinishModeChange(false);
    if (Settings->GetFullscreenMode()==Mode && Settings->GetScreenResolution()==Size)
    { Settings->SaveSettings();return; }
    auto* Viewport=PC && PC->GetWorld()?PC->GetWorld()->GetGameViewport():nullptr;
    // Never enter an unconfirmed mode without an independent, reachable prompt.
    if (!Viewport || !FSlateApplication::IsInitialized()) return;
    Preview.PreviousMode=Settings->GetFullscreenMode();Preview.PreviousSize=Settings->GetScreenResolution();
    Preview.Controller=PC;Preview.Viewport=Viewport;Preview.Pending=true;
    Preview.Deadline=FPlatformTime::Seconds()+15.;
    Preview.Prompt=SNew(SBorder).HAlign(HAlign_Center).VAlign(VAlign_Center)
        .BorderBackgroundColor(FLinearColor(0,0,0,.8f))
        [SNew(SBox).WidthOverride(360)
         [SNew(SVerticalBox)
          +SVerticalBox::Slot().AutoHeight().Padding(12)
           [SNew(STextBlock).AutoWrapText(true).Justification(ETextJustify::Center)
            .Text_Lambda([]{return FText::FromString(FString::Printf(TEXT("Keep these display settings?\nReverting in %d seconds."),
                FMath::Max(0,FMath::CeilToInt(Preview.Deadline-FPlatformTime::Seconds()))));})]
          +SVerticalBox::Slot().AutoHeight().Padding(12)
           [SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(4)
             [SNew(SButton).HAlign(HAlign_Center).Text(FText::FromString(TEXT("Keep (Enter)")))
              .OnClicked_Lambda([]{FinishModeChange(true);return FReply::Handled();})]
            +SHorizontalBox::Slot().FillWidth(1).Padding(4)
             [SNew(SButton).HAlign(HAlign_Center).Text(FText::FromString(TEXT("Revert (Esc)")))
              .OnClicked_Lambda([]{FinishModeChange(false);return FReply::Handled();})]]]];
    Viewport->AddViewportWidgetContent(Preview.Prompt.ToSharedRef(),10000);
    Preview.Timer=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickModePreview));
    Settings->SetScreenResolution(Size);Settings->SetFullscreenMode(Mode);
    Settings->ApplyResolutionSettings(false);
    PC->RefreshDesktopDisplayInputMode();
}

void FACEDesktopDisplay::ResetGraphics(AACEPlayerController* PC)
{
    if (PC && PC->IsVRActive()) return;
    FinishModeChange(false);
    auto* Settings=UGameUserSettings::GetGameUserSettings();if (!Settings) return;
    Settings->ValidateSettings();
    const auto Desktop=GetMonitorResolution(PC);
    const FIntPoint Size(FMath::Min(1280,FMath::Max(800,Desktop.X-80)),FMath::Min(720,FMath::Max(600,Desktop.Y-80)));
    Settings->SetFullscreenMode(EWindowMode::Windowed);Settings->SetScreenResolution(Size);
    Settings->SetOverallScalabilityLevel(2);Settings->SetResolutionScaleValueEx(100.f);
    Settings->SetDynamicResolutionEnabled(false);Settings->SetFrameRateLimit(0);Settings->SetVSyncEnabled(false);
    for (const auto& Option:ACERuntimeOptions::Values)
        if (FCString::Strcmp(Option.Key,TEXT("ScreenBrightness"))==0 || FCString::Strcmp(Option.Key,TEXT("Contrast"))==0
            || FCString::Strcmp(Option.Key,TEXT("FieldOfViewDegrees"))==0 || FCString::Strcmp(Option.Key,TEXT("Anisotropy"))==0
            || FCString::Strcmp(Option.Key,TEXT("DesktopUIScale"))==0 || FCString::Strcmp(Option.Key,TEXT("CursorScale"))==0
            || FCString::Strcmp(Option.Key,TEXT("ConstrainUIWindows"))==0 || FCString::Strcmp(Option.Key,TEXT("DynamicLightingQuality"))==0
            || FCString::Strcmp(Option.Key,TEXT("EffectLightLimit"))==0)
            ACERuntimeOptions::Set(Option.Key,Option.Default);
    Settings->ApplySettings(false);Settings->ConfirmVideoMode();Settings->SaveSettings();ACERuntimeOptions::Apply();
    if (PC) PC->RefreshDesktopDisplayInputMode();
    if (auto* Viewport=PC && PC->GetWorld()?PC->GetWorld()->GetGameViewport():nullptr)
        if (auto Window=Viewport->GetWindow())
        {
            const auto& Metrics=FSlateApplication::Get().GetCachedDisplayMetricsByRef();
            for (const auto& Monitor:Metrics.MonitorInfo) if (Monitor.bIsPrimary)
            {
                const FVector2D Position(Monitor.WorkArea.Left+20,Monitor.WorkArea.Top+20);
                Window->MoveWindowTo(Position);
                if (GConfig)
                {
                    GConfig->SetInt(Section,TEXT("WindowX"),int32(Position.X),GGameUserSettingsIni);
                    GConfig->SetInt(Section,TEXT("WindowY"),int32(Position.Y),GGameUserSettingsIni);
                    GConfig->Flush(false,GGameUserSettingsIni);
                }
                break;
            }
        }
}
TSharedPtr<FACEDesktopDisplay> FACEDesktopDisplay::Create(AACEPlayerController* PC)
{
    if (!PC || !PC->GetWorld() || PC->GetWorld()->IsPlayInEditor() || !FSlateApplication::IsInitialized()
        || FParse::Param(FCommandLine::Get(),TEXT("unattended"))
        || FParse::Param(FCommandLine::Get(),TEXT("RenderOffscreen"))) return nullptr;
    return MakeShared<FDisplayLifetime>(PC);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACELoginWidget.h"
#include "Engine/Engine.h"
#include "UObject/UObjectIterator.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEDisplayRecoveryTest,"ACE.Launcher.DisplayRecovery",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEDisplayRecoveryTest::RunTest(const FString&)
{
    // Run in a separate -game -RenderOffscreen process: exercise real viewport
    // ownership and settings persistence without changing the operator's display.
    if (!TestTrue(TEXT("Display recovery fixture requires an isolated offscreen client"),
        FParse::Param(FCommandLine::Get(),TEXT("RenderOffscreen")))) return false;
    auto* World=GEngine && GEngine->GameViewport?GEngine->GameViewport->GetWorld():nullptr;
    auto* PC=World?Cast<AACEPlayerController>(World->GetFirstPlayerController()):nullptr;
    if (!TestNotNull(TEXT("Live launcher player controller"),PC)) return false;
    auto* Settings=UGameUserSettings::GetGameUserSettings();
    const auto OldMode=Settings->GetFullscreenMode();const auto OldSize=Settings->GetScreenResolution();
    ON_SCOPE_EXIT {FACEDesktopDisplay::FinishModeChange(false);Settings->SetFullscreenMode(OldMode);
        Settings->SetScreenResolution(OldSize);Settings->ApplyResolutionSettings(false);};
    Settings->SetFullscreenMode(EWindowMode::Windowed);Settings->SetScreenResolution(FIntPoint(1024,768));
    Settings->ConfirmVideoMode();Settings->SaveSettings();
    FACEDesktopDisplay::ApplyModeChange(PC,EWindowMode::Windowed,FIntPoint(1280,720));
    TestTrue(TEXT("Display preview creates an independent viewport prompt"),Preview.Pending && Preview.Prompt.IsValid());
    int32 SavedX=0;GConfig->GetInt(TEXT("/Script/Engine.GameUserSettings"),TEXT("ResolutionSizeX"),SavedX,GGameUserSettingsIni);
    TestEqual(TEXT("Unconfirmed resolution is not saved"),SavedX,1024);
    Preview.Deadline=FPlatformTime::Seconds()-1.;TickModePreview(0);
    TestFalse(TEXT("Timeout dismisses the confirmation"),Preview.Pending);
    TestEqual(TEXT("Timeout restores the previous resolution"),Settings->GetScreenResolution(),FIntPoint(1024,768));
    FACEDesktopDisplay::ApplyModeChange(PC,EWindowMode::Fullscreen,FIntPoint(1920,1080));
    FACEDesktopDisplay::FinishModeChange(false);
    TestEqual(TEXT("Revert restores windowed mode"),Settings->GetFullscreenMode(),EWindowMode::Windowed);
    FACEDesktopDisplay::ApplyModeChange(PC,EWindowMode::Windowed,FIntPoint(1280,720));
    FACEDesktopDisplay::FinishModeChange(true);
    GConfig->GetInt(TEXT("/Script/Engine.GameUserSettings"),TEXT("ResolutionSizeX"),SavedX,GGameUserSettingsIni);
    TestEqual(TEXT("Keep persists the confirmed resolution"),SavedX,1280);
    ACERuntimeOptions::Set(TEXT("Contrast"),2);ACERuntimeOptions::Set(TEXT("DesktopUIScale"),3);
    ACERuntimeOptions::Set(TEXT("MasterVolume"),.25f);
    Settings->SetFullscreenMode(EWindowMode::Fullscreen);Settings->SetScreenResolution(FIntPoint(640,480));
    bool Clicked=false;
    for(TObjectIterator<UACELoginActionButton> It;It;++It)
        if(It->Action==TEXT("resetgraphics") && It->Owner.IsValid() && It->Owner->GetWorld()==World)
        { It->OnClicked.Broadcast();Clicked=true;break; }
    TestTrue(TEXT("Launcher exposes a working reset graphics button"),Clicked);
    TestEqual(TEXT("Launcher reset escapes broken fullscreen"),Settings->GetFullscreenMode(),EWindowMode::Windowed);
    TestTrue(TEXT("Launcher reset restores usable dimensions"),Settings->GetScreenResolution().X>=800 && Settings->GetScreenResolution().Y>=600);
    TestEqual(TEXT("Launcher reset restores contrast"),ACERuntimeOptions::Get(TEXT("Contrast")),1.f);
    TestEqual(TEXT("Launcher reset restores UI scale"),ACERuntimeOptions::Get(TEXT("DesktopUIScale")),1.f);
    TestEqual(TEXT("Graphics reset preserves unrelated preferences"),ACERuntimeOptions::Get(TEXT("MasterVolume")),.25f);
    return true;
}
#endif
