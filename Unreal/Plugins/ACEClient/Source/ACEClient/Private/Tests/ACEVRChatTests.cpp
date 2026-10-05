#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "HAL/FileManager.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/WidgetComponent.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraComponent.h"
#include "Features/IModularFeatures.h"
#include "XRMotionControllerBase.h"
#include "MotionControllerComponent.h"
#include "ACEClientSubsystem.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "ACEPlayerController.h"
#include "UI/ACEChatEntry.h"
#include "UI/ACEUIGameplayBinder.h"
#include "VR/ACEVRChat.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "VR/ACEVRInputLayout.h"
#include "VR/ACEVRMenu.h"
#include "VR/ACEVRWidgetComponent.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
class FNativeChatTestController : public FXRMotionControllerBase
{
public:
    bool Connected = true;
    FNativeChatTestController() { IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this); }
    ~FNativeChatTestController() { IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this); }
    FName GetMotionControllerDeviceTypeName() const override { return TEXT("ACENativeChatTest"); }
    bool GetControllerOrientationAndPosition(int32 Index, FName Source, FRotator& Rotation, FVector& Position, float) const override
    {
        if (GetControllerTrackingStatus(Index, Source) != ETrackingStatus::Tracked) return false;
        Rotation = FRotator::ZeroRotator; Position = FVector::ZeroVector; return true;
    }
    ETrackingStatus GetControllerTrackingStatus(int32 Index, FName Source) const override
    { return Connected && Index == 0 && Source == TEXT("NativeChatTestAim") ? ETrackingStatus::Tracked : ETrackingStatus::NotTracked; }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRChatTest, "ACE.VR.NativeChat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEVRChatTest::RunTest(const FString&)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto* GI = NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
    auto* InitialWorld = GI->GetWorld();
    GI->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(GI); InitialWorld->DestroyWorld(false);
    auto* PC = World->SpawnActor<AACEPlayerController>();
    PC->SetPlayer(NewObject<ULocalPlayer>(GEngine)); World->AddController(PC);
    auto* Owner = World->SpawnActor<AActor>();
    Owner->SetOwner(PC); // Motion-controller polling requires a local player owner.
    auto* Root = NewObject<USceneComponent>(Owner); Owner->SetRootComponent(Root);
    ON_SCOPE_EXIT
    {
        FlushRenderingCommands();
        Owner->Destroy(); PC->Destroy(); GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        FlushRenderingCommands();
    };
    auto* Binder = NewObject<UACEUIGameplayBinder>();
    const FLinearColor SystemColor(.7f, .8f, .9f), TellColor(.95f, .7f, .4f);
    for (int32 I = 0; I < 127; ++I)
        Binder->AppendChatLineToLog(0, FString::Printf(TEXT("Already formatted line %d"), I), SystemColor, FString());
    TestEqual(TEXT("Native history shares the bounded retail main buffer"), Binder->ChatDisplayLines[0].Num(), 120);
    TestEqual(TEXT("Bounded history discards only the oldest lines"), Binder->ChatDisplayLines[0][0].Text, FString(TEXT("Already formatted line 7")));
    const FString Tell = TEXT("Old Friend tells you, \"Already formatted: /r is not a command here\"");
    Binder->AppendChatLineToLog(0, Tell, TellColor, TEXT("Old Friend"));
    Binder->AppendChatLineToLog(1, TEXT("Independent auxiliary chat"), FLinearColor::Green, TEXT("Other Sender"));
    const auto& Stored = Binder->ChatDisplayLines[0].Last();
    TestEqual(TEXT("Native source preserves retail formatting"), Stored.Text, Tell);
    TestEqual(TEXT("Native source retains the reply recipient"), Stored.Sender, FString(TEXT("Old Friend")));
    TestTrue(TEXT("Native source retains the retail channel color"), Stored.Color.Equals(TellColor));

    auto* Chat = CreateWidget<UACEVRChat>(PC);
    if (!TestNotNull(TEXT("Native chat is created with an initialized player context"), Chat)) return false;
    Chat->InitializeChat(nullptr, Binder);
    const auto ChatSlate = Chat->TakeWidget();
    if (!TestTrue(TEXT("Native chat Slate tree stays owned throughout the fixture"), Chat->Lines.IsValid())) return false;
    TestTrue(TEXT("Native chat recognizes its current source binder"), Chat->IsBoundTo(Binder));
    TestFalse(TEXT("A different gameplay binder requires rebinding native chat"), Chat->IsBoundTo(NewObject<UACEUIGameplayBinder>()));
    TestTrue(TEXT("First native refresh paints shared history"), Chat->Refresh());
    TestEqual(TEXT("Native view shows only the main chat buffer"), Chat->Lines->GetChildren()->Num(), 120);
    const auto FirstRow = Chat->Lines->GetChildren()->GetChildAt(0);
    const auto LastRow = Chat->Lines->GetChildren()->GetChildAt(119);
    if (!TestTrue(TEXT("A sender-bearing native line is clickable"), LastRow->GetType() == FName("SButton"))) return false;
    const auto SenderButton = StaticCastSharedRef<SButton>(LastRow);
    const auto Label = StaticCastSharedRef<STextBlock>(SenderButton->GetChildren()->GetChildAt(0));
    TestEqual(TEXT("Native rows do not reformat message text"), Label->GetText().ToString(), Tell);
    TestTrue(TEXT("Native rows paint the shared channel color"), Label->GetColorAndOpacity().GetSpecifiedColor().Equals(TellColor));
    bool Repainted = false;
    for (int32 I = 0; I < 100; ++I) Repainted |= Chat->Refresh();
    TestFalse(TEXT("Unchanged native chat does not rebuild at headset frame rate"), Repainted);
    TestTrue(TEXT("Unchanged native chat retains existing row widgets"), FirstRow == Chat->Lines->GetChildren()->GetChildAt(0));

    const auto RowGeometry = FGeometry::MakeRoot(FVector2D(800, 40), FSlateLayoutTransform());
    const FKeyEvent Accept(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
    SenderButton->OnKeyDown(RowGeometry, Accept); SenderButton->OnKeyUp(RowGeometry, Accept);
    TestEqual(TEXT("Activating a sender prepares a tell in the native entry"), Chat->Entry->GetText().ToString(), FString(TEXT("@tell Old Friend, ")));

    // New lines must preserve a reader's scroll position until Latest is chosen.
    Chat->bStickToBottom = false; Chat->Log->SetScrollOffset(37.f);
    const auto SurvivingRow = Chat->Lines->GetChildren()->GetChildAt(1);
    Binder->AppendChatLineToLog(0, TEXT("A newer line"), SystemColor, FString());
    TestTrue(TEXT("A new line invalidates the native view"), Chat->Refresh());
    TestTrue(TEXT("Appending at the history limit retains surviving row widgets"), SurvivingRow == Chat->Lines->GetChildren()->GetChildAt(0));
    TestEqual(TEXT("Reading older native chat preserves the scroll offset"), Chat->Log->GetScrollOffset(), 37.f);
    const uint64 BeforeClear = Binder->ChatDisplayRevision;
    Binder->ClearChatLog(0);
    TestTrue(TEXT("Clearing the main log increments its display revision"), Binder->ChatDisplayRevision > BeforeClear);
    TestTrue(TEXT("Clearing native chat invalidates its rows"), Chat->Refresh());
    TestEqual(TEXT("Clearing removes visible native rows"), Chat->Lines->GetChildren()->Num(), 0);
    TestEqual(TEXT("Main clear leaves auxiliary windows intact"), Binder->ChatDisplayLines[1].Num(), 1);
    TestFalse(TEXT("Cleared native chat becomes idle again"), Chat->Refresh());
    auto* Plugins=GI->GetSubsystem<UACEPluginSubsystem>();
    for(auto Plugin:Plugins->Plugins)if(Plugin->Id==TEXT("waypoint"))Plugin->Enabled=true;
    Binder->AppendChatLineToLog(0,TEXT("Meet at 42.0N, 33.6E and then 12.5S, 7.25W."),TellColor,TEXT("Friend"));
    TestTrue(TEXT("Coordinate-bearing chat refreshes"),Chat->Refresh());
    const auto CoordinateRow=Chat->Lines->GetChildren()->GetChildAt(0);
    TestEqual(TEXT("VR uses inline hyperlinks for coordinates"),CoordinateRow->GetType(),FName(TEXT("SRichTextBlock")));
    const auto Rich=StaticCastSharedRef<SRichTextBlock>(CoordinateRow);
    TestTrue(TEXT("Both VR destination links have independent metadata"),Rich->GetText().ToString().Contains(TEXT("href=\"42.00N, 33.60E\"")) && Rich->GetText().ToString().Contains(TEXT("href=\"12.50S, 7.25W\"")));
    Binder->ClearChatLog(0);Chat->Refresh();


    // Exercise the actual shared slash-command dispatcher without a network.
    Binder->Client = GI->GetSubsystem<UACEClientSubsystem>();
    auto* DesktopEntry = NewObject<UACEChatEntry>(); DesktopEntry->InitializeChat(Binder); Binder->ChatEntry = DesktopEntry;
    Binder->AppendChatLineToLog(0, TEXT("Clear me through /clear"), SystemColor, FString());
    Chat->Entry->SetChatText(TEXT("/clear")); Chat->Send();
    TestTrue(TEXT("Native slash commands use the retail command implementation"), Binder->ChatDisplayLines[0].IsEmpty());
    TestTrue(TEXT("Successful native command clears its entry"), Chat->Entry->GetText().IsEmpty());
    TestFalse(TEXT("Native submission cannot refocus the hidden desktop entry"), Binder->bPendingChatRefocus);
    Chat->Entry->NavigateHistory(true);
    TestEqual(TEXT("Native history remembers submitted slash commands"), Chat->Entry->GetText().ToString(), FString(TEXT("/clear")));
    Chat->Entry->NavigateHistory(false);
    TestTrue(TEXT("Native Next clears after the newest submission"), Chat->Entry->GetText().IsEmpty());
    Chat->Entry->SetChatText(TEXT("/clear all")); Chat->Send();
    TestTrue(TEXT("Native /clear all reaches auxiliary retail windows"), Binder->ChatDisplayLines[1].IsEmpty());
    for (int32 I = 0; I < 105; ++I) Chat->Entry->RememberSubmitted(FString::Printf(TEXT("Native history %d"), I));
    for (int32 I = 0; I < 110; ++I) Chat->Entry->NavigateHistory(true);
    TestEqual(TEXT("Native entry uses retail's hundred-command history limit"), Chat->Entry->GetText().ToString(), FString(TEXT("Native history 5")));
    Chat->Entry->SetChatText(TEXT("Unsent draft"));
    Chat->Committed(Chat->Entry->GetText(), ETextCommit::OnUserMovedFocus);
    TestEqual(TEXT("Losing native keyboard focus preserves an unsent draft"), Chat->Entry->GetText().ToString(), FString(TEXT("Unsent draft")));

    auto* Settings = NewObject<UACEVRSettings>(); Settings->ButtonBindings.Reset();
    TestEqual(TEXT("Left grip opens chat by default"), Settings->GetButtonAction("VRLeftGrip"), FName("VRChat"));
    TestTrue(TEXT("Chat is an available controller action"), ACEVRInputLayout::IsAction("VRChat"));
    Settings->ButtonBindings.Add("VRLeftGrip", "VRInventory"); Settings->ButtonBindings.Add("VRInventory", "VRChat");
    TestEqual(TEXT("An explicit left-grip rebind overrides chat"), Settings->GetButtonAction("VRLeftGrip"), FName("VRInventory"));
    TestEqual(TEXT("Chat can be rebound to another physical input"), Settings->GetButtonAction("VRInventory"), FName("VRChat"));
    Settings->bChatLocked = false; Settings->ChatScale = .12f; Settings->ChatAnchorMode = 1;
    Settings->ChatViewOffset = FVector(130, -70, 20); Settings->ChatViewRotation = FRotator(12, 18, -7);
    Settings->Sanitize();
    const FString Filename = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation")
        / (TEXT("NativeChatSettings-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ini")));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    ON_SCOPE_EXIT { IFileManager::Get().Delete(*Filename); };
    // Use a test-only file so validation does not replace the user's VR preferences.
    Settings->SaveConfig(CPF_Config, *Filename, GConfig, false);
    auto* Reloaded = NewObject<UACEVRSettings>(); Reloaded->LoadConfig(UACEVRSettings::StaticClass(), *Filename);
    TestFalse(TEXT("Chat unlock is saved"), Reloaded->bChatLocked);
    TestEqual(TEXT("Chat scale is saved"), Reloaded->ChatScale, .12f);
    TestEqual(TEXT("Chat anchor mode is saved"), Reloaded->ChatAnchorMode, 1);
    TestTrue(TEXT("Chat position and rotation are saved"), Reloaded->ChatViewOffset.Equals(Settings->ChatViewOffset)
        && Reloaded->ChatViewRotation.Equals(Settings->ChatViewRotation));
    TestEqual(TEXT("Rebound chat input survives reload"), Reloaded->GetButtonAction("VRInventory"), FName("VRChat"));
    auto* Rig = NewObject<UACEVRComponent>(Owner); Rig->Settings = Settings;
    Rig->ChatPanel = NewObject<UACEVRWidgetComponent>(Owner);
    TestTrue(TEXT("Native chat participates in the panel placement controls"), Rig->GetEditablePanel("Chat") == Rig->ChatPanel);
    TestFalse(TEXT("Chat move controls honor its own unlock setting"), Rig->IsPanelLocked("Chat"));
    Rig->GetPanelScale("Chat") = .11f;
    TestEqual(TEXT("Chat resizing modifies the chat scale independently"), Settings->ChatScale, .11f);
    Settings->bChatLocked = true;
    TestTrue(TEXT("Chat lock disables placement independently"), Rig->IsPanelLocked("Chat"));
    Rig->Head = NewObject<UCameraComponent>(Owner);
    Rig->Head->SetWorldLocationAndRotation(FVector(10, 20, 170), FRotator(4, 35, 0));
    Rig->ChatAnchorFrame = FTransform(FRotator(0, 10, 0), FVector(-20, 15, 160));
    Settings->ChatAnchorMode = 0;
    TestTrue(TEXT("Head-anchored chat uses the current head pose"), Rig->GetChatAnchorTransform().Equals(Rig->Head->GetComponentTransform()));
    for (int32 Anchor : {1, 2})
    {
        Settings->ChatAnchorMode = Anchor;
        TestTrue(TEXT("Body/world chat retains its independent anchor frame"), Rig->GetChatAnchorTransform().Equals(Rig->ChatAnchorFrame));
    }
    auto* PositionedChat = CreateWidget<UACEVRChat>(PC);
    if (!TestNotNull(TEXT("Positioned native chat has an initialized player context"), PositionedChat)) return false;
    PositionedChat->InitializeChat(Rig, Binder);
    const auto PositionedSlate = PositionedChat->TakeWidget();
    if (!TestTrue(TEXT("Positioned chat retains its Slate tree"), PositionedChat->Lines.IsValid())) return false;
    PositionedChat->Refresh();
    TestFalse(TEXT("Unchanged placement controls do not repaint native chat"), PositionedChat->Refresh());
    Settings->bChatLocked = false;
    TestTrue(TEXT("Unlocking placement repaints its controls without new chat"), PositionedChat->Refresh());
    TestFalse(TEXT("Placement repaint settles after the change"), PositionedChat->Refresh());
    TestEqual(TEXT("Native chat displays the saved anchor mode"), PositionedChat->AnchorLabel->GetText().ToString(), FString(TEXT("Anchor: World")));
    Binder->ChatFilterMode = 2;
    TestTrue(TEXT("Changing the shared filter refreshes its native label"), PositionedChat->Refresh());
    TestEqual(TEXT("Native chat displays the current filter"), PositionedChat->FilterLabel->GetText().ToString(), FString(TEXT("Filter: Combat")));
    TestFalse(TEXT("Unchanged filter labels do not repaint"), PositionedChat->Refresh());
    Binder->ChatFilterMode = 0; PositionedChat->Refresh();

    Settings->ChatAnchorMode = 2; Rig->bChatAnchorReady = false;
    Rig->UpdateChatAnchor(.016f);
    const FTransform WorldAnchor = Rig->GetChatAnchorTransform();
    Rig->Head->SetWorldLocationAndRotation(FVector(100, 80, 180), FRotator(0, 120, 0));
    Owner->SetActorLocation(FVector(50, 0, 0)); Rig->UpdateChatAnchor(.016f);
    TestTrue(TEXT("World-anchored chat remains stationary while the player moves"), Rig->GetChatAnchorTransform().Equals(WorldAnchor));
    Settings->ChatAnchorMode = 1; Rig->bChatAnchorReady = false;
    Rig->UpdateChatAnchor(.016f);
    TestTrue(TEXT("Changing chat anchoring refreshes its visible label"), PositionedChat->Refresh());
    TestEqual(TEXT("Native chat displays body-follow anchoring"), PositionedChat->AnchorLabel->GetText().ToString(), FString(TEXT("Anchor: Body")));
    const float OriginalYaw = Rig->GetChatAnchorTransform().Rotator().Yaw;
    Rig->Head->AddWorldRotation(FRotator(0, 10, 0)); Rig->UpdateChatAnchor(.016f);
    TestTrue(TEXT("Body-follow chat ignores small reading motions"), FMath::IsNearlyEqual(Rig->GetChatAnchorTransform().Rotator().Yaw, OriginalYaw));
    Rig->Head->AddWorldRotation(FRotator(0, 60, 0)); Rig->UpdateChatAnchor(.016f);
    const float FollowYaw = FMath::FindDeltaAngleDegrees(OriginalYaw, Rig->GetChatAnchorTransform().Rotator().Yaw);
    TestTrue(TEXT("Body-follow chat turns smoothly after leaving the reading cone"), FollowYaw > 0.f && FollowYaw < 70.f);

    Rig->ChatWidget = PositionedChat; Rig->ChatPanel->SetWidget(PositionedChat);
    Rig->ChatPanel->SetDrawSize(FVector2D(960, 780)); Rig->bChatOpen = true;
    Rig->GameplayMenu = CreateWidget<UACEVRMenu>(PC); Rig->GameplayMenu->OpenPage("Vendor");
    Rig->GameplayMenuPanel = NewObject<UACEVRWidgetComponent>(Owner);
    Rig->GameplayMenuPanel->SetDrawSize(FVector2D(1000, 800)); Rig->GameplayMenuPanel->SetWorldScale3D(FVector(.1f));
    Rig->GameplayMenuPanel->SetWorldLocationAndRotation(FVector(200, 40, 150), FRotator(0, 37, 0));
    Rig->GameplayMenuPanel->SetVisibility(true);
    Rig->MenuInspectionPanel = NewObject<UACEVRWidgetComponent>(Owner);
    Rig->MenuInspectionPanel->SetDrawSize(FVector2D(400, 700)); Rig->MenuInspectionPanel->SetWorldScale3D(FVector(.08f));
    Rig->MenuInspectionPanel->SetVisibility(true);
    const FVector SavedOffset = Settings->ChatViewOffset; const FRotator SavedRotation = Settings->ChatViewRotation;
    Rig->bChatDocked = true; Rig->UpdateChatPanel(true, .016f);
    const float Separation = FVector::DotProduct(Rig->GameplayMenuPanel->GetComponentLocation() - Rig->ChatPanel->GetComponentLocation(),
        Rig->GameplayMenuPanel->GetRightVector());
    const float ExpectedSeparation = 50.f + 32.f + 5.f + 960.f * Settings->ChatScale * .5f + 6.f;
    TestTrue(TEXT("Vendor docking leaves space for vendor, inspection, and chat without overlap"), FMath::IsNearlyEqual(Separation, ExpectedSeparation, .01f));
    TestTrue(TEXT("Docking preserves the saved free-chat placement"), Settings->ChatViewOffset.Equals(SavedOffset) && Settings->ChatViewRotation.Equals(SavedRotation));

    {
        FNativeChatTestController Controller;
        Rig->LeftAim = NewObject<UMotionControllerComponent>(Owner);
        // This fixture moves the panel with deterministic game-thread samples;
        // a global render-thread late update must not retain its scoped provider.
        Rig->LeftAim->bDisableLowLatencyUpdate = true;
        Rig->LeftAim->RegisterComponentWithWorld(World);
        ON_SCOPE_EXIT
        {
            Controller.Connected = false;
            Rig->LeftAim->TickComponent(.016f, LEVELTICK_All, nullptr);
            FlushRenderingCommands();
            Rig->LeftAim->DestroyComponent(); Rig->LeftAim = nullptr;
        };
        Rig->LeftAim->SetAssociatedPlayerIndex(0); Rig->LeftAim->SetTrackingMotionSource(TEXT("NativeChatTestAim"));
        Rig->LeftAim->Activate(true); Rig->LeftAim->TickComponent(.016f, LEVELTICK_All, nullptr);
        TestTrue(TEXT("Chat placement fixture provides a tracked hand"), Rig->LeftAim->IsTracked());
        Rig->bTracking = true;
        auto AimAtRadius = [&](const FTransform& Plane, float Radius)
        {
            Rig->LeftAim->SetWorldLocationAndRotation(Plane.TransformPosition(FVector(-80, Radius, 0)), Plane.Rotator());
        };
        const FTransform ResizePlane(Rig->ChatPanel->GetComponentQuat() * FRotator(0, 180, 0).Quaternion(), Rig->ChatPanel->GetComponentLocation());
        AimAtRadius(ResizePlane, 12.f);
        const float OriginalScale = Settings->ChatScale;
        Rig->BeginPanelEdit("Chat", true, true);
        TestEqual(TEXT("Tracked chat resize begins editing"), Rig->EditingPanel, FName("Chat"));
        TestTrue(TEXT("Resizing vendor chat preserves its dock"), Rig->bChatDocked);
        AimAtRadius(ResizePlane, 14.4f); Rig->UpdatePanelEdit(.016f); Rig->UpdateChatPanel(true, .016f);
        TestTrue(TEXT("Tracked resize changes chat scale"), FMath::IsNearlyEqual(Settings->ChatScale, OriginalScale * 1.2f, .001f));
        const float ResizedSeparation = FVector::DotProduct(Rig->GameplayMenuPanel->GetComponentLocation() - Rig->ChatPanel->GetComponentLocation(),
            Rig->GameplayMenuPanel->GetRightVector());
        TestTrue(TEXT("Resizing docked chat keeps a stable gap beside vendor and inspection"), FMath::IsNearlyEqual(ResizedSeparation,
            50.f + 32.f + 5.f + 960.f * Settings->ChatScale * .5f + 6.f, .01f) && ResizedSeparation > Separation);
        TestTrue(TEXT("Docked resize does not overwrite free-chat position"), Settings->ChatViewOffset.Equals(SavedOffset) && Settings->ChatViewRotation.Equals(SavedRotation));
        Rig->EndPanelEdit(false);
        const FTransform MovePlane(Rig->ChatPanel->GetComponentQuat() * FRotator(0, 180, 0).Quaternion(), Rig->ChatPanel->GetComponentLocation());
        AimAtRadius(MovePlane, 12.f); Rig->BeginPanelEdit("Chat", true, false);
        TestEqual(TEXT("Tracked chat move begins editing"), Rig->EditingPanel, FName("Chat"));
        TestFalse(TEXT("Moving vendor chat deliberately releases its dock"), Rig->bChatDocked);
        Rig->EndPanelEdit(false); Settings->ChatScale = OriginalScale;
    }
    Rig->bChatDocked = false; Rig->UpdateChatPanel(true, .016f);
    TestTrue(TEXT("Undocking returns chat to its saved anchor-relative placement"), Rig->ChatPanel->GetComponentLocation().Equals(
        Rig->GetChatAnchorTransform().TransformPosition(SavedOffset), .01));
    Rig->FocusPanelSurface(Rig->ChatPanel);
    TestTrue(TEXT("Focusing chat puts it above the overlapping vendor surface"), Rig->ChatPanel->TranslucencySortPriority > Rig->GameplayMenuPanel->TranslucencySortPriority);
    Rig->FocusPanelSurface(Rig->GameplayMenuPanel);
    TestTrue(TEXT("Focusing vendor returns it above chat"), Rig->GameplayMenuPanel->TranslucencySortPriority > Rig->ChatPanel->TranslucencySortPriority);
    Rig->UpdateChatPanel(false, .016f);
    TestTrue(TEXT("Unavailable chat stops rendering and pointer interception"), !Rig->ChatPanel->IsVisible()
        && !Rig->ChatPanel->IsComponentTickEnabled() && Rig->ChatPanel->GetCollisionEnabled() == ECollisionEnabled::NoCollision);

    if (FApp::CanEverRender())
    {
        // Render the actual shipping panel dimensions: layout must leave useful room
        // for messages while keeping the entry and controller targets on the surface.
        const FIntPoint Size(760, 640);
        FWidgetRenderer Renderer(true, true);
        TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(FVector2D(Size), TF_Bilinear, true));
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/VR");
        IFileManager::Get().MakeDirectory(*Directory, true);
        Binder->ClearChatLog(0);
        Binder->AppendChatLineToLog(0, TEXT("A long system message must wrap on its first paint so every complete row reserves its space before the next row starts."), SystemColor, FString());
        Binder->AppendChatLineToLog(0, TEXT("Old Friend tells you, \"This reply also wraps immediately, while keeping its sender target and the following row in their own space.\""), TellColor, TEXT("Old Friend"));
        PositionedChat->bStickToBottom = false; PositionedChat->Log->SetScrollOffset(0.f); PositionedChat->Refresh();
        Renderer.DrawWidget(Target.Get(), PositionedSlate, FVector2D(Size), 0.f); FlushRenderingCommands();
        const auto PlainRow = PositionedChat->Lines->GetChildren()->GetChildAt(0);
        const auto ReplyRow = PositionedChat->Lines->GetChildren()->GetChildAt(1);
        TestTrue(TEXT("A system message reserves multiline height on its first draw"), PlainRow->GetCachedGeometry().GetLocalSize().Y > 50.f);
        TestTrue(TEXT("A clickable sender message reserves multiline height on its first draw"), ReplyRow->GetCachedGeometry().GetLocalSize().Y > 50.f);
        const float PlainEnd = PlainRow->GetCachedGeometry().LocalToAbsolute(PlainRow->GetCachedGeometry().GetLocalSize()).Y;
        TestTrue(TEXT("Fresh wrapped chat rows do not overlap on their first draw"), PlainEnd <= ReplyRow->GetCachedGeometry().GetAbsolutePosition().Y);
        const float LogWidth = PositionedChat->Log->GetCachedGeometry().GetLocalSize().X;
        TestTrue(TEXT("Fresh wrapped chat rows fit the panel width"), PlainRow->GetDesiredSize().X <= LogWidth && ReplyRow->GetDesiredSize().X <= LogWidth);
        TArray<FColor> FirstPixels;
        if (!TestTrue(TEXT("Read first-paint native chat pixels"), Target->GameThread_GetRenderTargetResource()->ReadPixels(FirstPixels))) return false;
        TArray64<uint8> FirstPNG; FImageUtils::PNGCompressImageArray(Size.X, Size.Y, FirstPixels, FirstPNG);
        TestTrue(TEXT("Save first-paint native chat image"), FFileHelper::SaveArrayToFile(FirstPNG, *(Directory / TEXT("NativeChatFirstPaint.png"))));

        Binder->ClearChatLog(0);
        Binder->AppendChatLineToLog(0, TEXT("You have entered the world of Dereth."), FLinearColor(.85f, .9f, 1.f), FString());
        Binder->AppendChatLineToLog(0, TEXT("[General] Arcanum says, \"Meeting at the marketplace portal. Bring healing kits and room for salvage.\""),
            FLinearColor(.65f, .8f, 1.f), TEXT("Arcanum"));
        Binder->AppendChatLineToLog(0, TEXT("Old Friend tells you, \"I found a Copper Ring for you.\""), TellColor, TEXT("Old Friend"));
        Binder->AppendChatLineToLog(0, TEXT("You give Copper Ring to Old Friend."), FLinearColor(.9f, .85f, .6f), FString());
        Binder->AppendChatLineToLog(0, TEXT("[Fellowship] Ranger says, \"Ready when you are!\""), FLinearColor(.5f, 1.f, .6f), TEXT("Ranger"));
        Binder->AppendChatLineToLog(0, TEXT("You receive 1,250 experience."), FLinearColor(.85f, .9f, 1.f), FString());
        PositionedChat->Entry->SetChatText(TEXT("@tell Old Friend, Thank you!"));
        PositionedChat->bStickToBottom = false; PositionedChat->Log->SetScrollOffset(0.f);
        for (const bool Locked : {true, false})
        {
            Settings->bChatLocked = Locked; PositionedChat->Refresh();
            for (int32 Pass = 0; Pass < 4; ++Pass)
            {
                Renderer.DrawWidget(Target.Get(), PositionedSlate, FVector2D(Size), 0.f);
                FlushRenderingCommands();
            }
            const FVector2D LogSize = PositionedChat->Log->GetCachedGeometry().GetLocalSize();
            TestTrue(TEXT("Native chat keeps at least 280 pixels of message buffer at its shipping size"), LogSize.X >= 700.f && LogSize.Y >= 280.f);
            const auto& EntryGeometry = PositionedChat->Entry->GetCachedGeometry();
            TestTrue(TEXT("Native chat entry remains large enough to read and select"), EntryGeometry.GetLocalSize().X >= 500.f && EntryGeometry.GetLocalSize().Y >= 48.f);
            const FVector2D EntryEnd = PositionedSlate->GetCachedGeometry().AbsoluteToLocal(EntryGeometry.LocalToAbsolute(EntryGeometry.GetLocalSize()));
            TestTrue(TEXT("Native chat entry fits within the rendered panel"), EntryEnd.X <= Size.X && EntryEnd.Y <= Size.Y);
            TArray<FColor> Pixels;
            if (!TestTrue(TEXT("Read native chat render pixels"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))) return false;
            TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
            const FString ImagePath = Directory / (Locked ? TEXT("NativeChatLocked.png") : TEXT("NativeChatUnlocked.png"));
            TestTrue(TEXT("Save native chat visual regression image"), FFileHelper::SaveArrayToFile(PNG, *ImagePath));
            AddInfo(FString::Printf(TEXT("Native chat %s screenshot: %s; message buffer %.0fx%.0f"),
                Locked ? TEXT("locked") : TEXT("unlocked"), *ImagePath, LogSize.X, LogSize.Y));
        }
    }
    return !HasAnyErrors();
}
#endif
