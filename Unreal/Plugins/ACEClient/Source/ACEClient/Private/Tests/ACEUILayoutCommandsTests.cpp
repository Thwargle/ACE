#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Engine/GameInstance.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "UI/ACERetailUILayout.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIGameplayBinder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUILayoutCommandsTest, "ACE.RetailParity.UILayoutCommands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEUILayoutCommandsTest::RunTest(const FString&)
{
    using namespace ACERetailUILayout;
    TGuardValue<FString> SettingsPath(GGameUserSettingsIni, FPaths::ProjectSavedDir()/TEXT("Automation/UILayoutPreferencesFixture.ini"));
    FConfigFile Preferences; Preferences.NoSave = true; GConfig->SetFile(GGameUserSettingsIni, &Preferences);
    FString Name, Error;
    TestTrue(TEXT("No argument means retail default file"), NamedFile(TEXT(""), Name, Error));
    TestEqual(TEXT("Default file"), Name, FString(TEXT("UI-Default.txt")));
    TestTrue(TEXT("Quoted retail filename"), NamedFile(TEXT("\"my layout\""), Name, Error));
    TestEqual(TEXT("Quoted filename"), Name, FString(TEXT("my layout.txt")));
    for (const FString Bad : {TEXT("../layout"), TEXT("C:\\layout"), TEXT("one two"), TEXT("NUL"), TEXT("12345678901234567"), TEXT("\"unterminated")})
        TestFalse(TEXT("Invalid filename rejected"), NamedFile(Bad, Name, Error));
    TestNotEqual(TEXT("Automatic identity escaping avoids collisions"), AutoFile(TEXT("Server"), TEXT("A/B"), {1920,1080}),
        AutoFile(TEXT("Server"), TEXT("A%002FB"), {1920,1080}));

    auto* GI = NewObject<UGameInstance>();
    auto* Dat = NewObject<UACEDatSubsystem>(GI);
    if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    auto* Manager = NewObject<UACEUIElementManager>(); Manager->Initialize();
    auto* Layout = NewObject<UACEUILayoutResolver>(); Layout->Initialize(Dat, Manager);
    if (!Layout->LoadLayout(ACEUI::LayoutId::ClassicGameplay)) return false;
    Manager->ApplyEdgeAnchoredLayout(1920, 1080);
    const auto Chat = Manager->FindElementByName(TEXT("RootGameplay_FloatyMainChat_Field"));
    const auto Toolbar = Manager->FindElementByName(TEXT("RootGameplay_FloatyToolbar_Field"));
    const auto Hidden = Manager->FindElementByName(TEXT("RootGameplay_FloatyChat1_Field"));
    const auto Vitals = Manager->FindElementByName(TEXT("RootGameplay_FloatyVitals_Field"));
    const auto Radar = Manager->FindElementByName(TEXT("RootGameplay_Radar_Field"));
    if (!Chat || !Toolbar || !Hidden || !Vitals || !Radar) return false;
    // The jump meter is a retail floaty despite its different embedded root name.
    const auto Jump=Manager->FindElementByName(TEXT("RootGameplay_PowerBar_Field"));
    const auto Grip=Manager->FindElementUnder(TEXT("RootGameplay_PowerBar_Field"),TEXT("PowerbarTopBorder"));
    TestTrue(TEXT("Jump meter has an authored drag strip"),Jump && Grip);
    if (Jump && Grip)
    {
        Manager->SetUiLocked(false);Jump->bVisible=true;
        const FVector2D View(1920,1080);
        const FVector2D Start=FVector2D(Grip->GetScreenOrigin())+FVector2D(10,2);
        TestEqual(TEXT("Jump strip shows the move cursor"),Manager->GetWindowCursor(Start,View),EMouseCursor::CardinalCross);
        const FIntPoint Old(Jump->UserDragX,Jump->UserDragY);
        Manager->NotifyMouseDown(Start,View,EKeys::LeftMouseButton);
        Manager->NotifyMouseMove(Start+FVector2D(45,-70),View);
        Manager->NotifyMouseUp(Start+FVector2D(45,-70),View,EKeys::LeftMouseButton);
        TestEqual(TEXT("Dragging jump meter moves the entire window"),FIntPoint(Jump->UserDragX,Jump->UserDragY),Old+FIntPoint(45,-70));
        const FVector2D Body = FVector2D(Jump->GetScreenOrigin())+FVector2D(Jump->Width*.5,12);
        TestEqual(TEXT("Jump fill is also an easy drag target"),Manager->GetWindowCursor(Body,View),EMouseCursor::CardinalCross);
        Manager->NotifyMouseDown(Body,View,EKeys::LeftMouseButton);
        Manager->NotifyMouseMove(Body+FVector2D(20,30),View);
        Manager->NotifyMouseUp(Body+FVector2D(20,30),View,EKeys::LeftMouseButton);
        TestEqual(TEXT("Dragging the jump fill moves the complete window"),FIntPoint(Jump->UserDragX,Jump->UserDragY),Old+FIntPoint(65,-40));
        const FString SavedJump=Manager->ExportScreenLayout();
        Jump->UserDragX=Jump->UserDragY=0;Jump->RecomputeLayoutOffset();
        Manager->ImportScreenLayout(SavedJump,Error);
        TestEqual(TEXT("Jump position round-trips through retail saveui/loadui"),Manager->ExportScreenLayout(),SavedJump);
        Manager->SetUiLocked(true);
        const FVector2D LockedStart=FVector2D(Grip->GetScreenOrigin())+FVector2D(10,2);
        TestEqual(TEXT("Locked jump meter has no drag cursor"),Manager->GetWindowCursor(LockedStart,View),EMouseCursor::Default);
        Jump->bVisible=false;
    }
    Manager->SetUiLocked(true); Hidden->bVisible = false;
    const FString Retail = TEXT("<CHAT> X:35 Y: 640 W: 520 H: 240\n<TBAR> X:1500 Y: 920 W: 310 H: 132\n")
        TEXT("<FCH1> X:5000 Y: -40 W: 450 H: 180\n<VITS> X:90 Y: 35 W: 160 H: 500\n<RADA> X:1600 Y: 25 W: 120 H: 140\n")
        TEXT("<SBOX> X:300 Y: 50 W: 700 H: 500\n<UNKNOWN> X:0 Y:0 W:1 H:1\n");
    TestTrue(TEXT("Retail layout imports"), Manager->ImportScreenLayout(Retail, Error));
    TestEqual(TEXT("Chat placement"), FIntPoint(Chat->GetDrawX(), Chat->GetDrawY()), FIntPoint(35,640));
    TestEqual(TEXT("Chat resize"), FIntPoint(Chat->Width, Chat->Height), FIntPoint(520,240));
    TestEqual(TEXT("Two shortcut rows restored"), Toolbar->Height, 132);
    TestEqual(TEXT("Fixed-height vitals cannot be stretched vertically"), Vitals->Height, 58);
    TestEqual(TEXT("Hidden window clamped horizontally"), Hidden->GetDrawX(), 1470);
    TestEqual(TEXT("Hidden window clamped vertically"), Hidden->GetDrawY(), 0);
    TestFalse(TEXT("Layout load does not open hidden chat"), bool(Hidden->bVisible));
    TestTrue(TEXT("Loading does not change lock preference"), Manager->IsUiLocked());
    TestEqual(TEXT("SmartBox record does not crop VR world view"), Manager->FindElementByName(TEXT("RootGameplay_SmartBox_Field"))->GetDrawX(), 0);
    const FString Snapshot = Manager->ExportScreenLayout();
    TArray<FRect> Rects;
    TestTrue(TEXT("Export uses readable retail format"), Parse(Snapshot, Rects, Error));
    TestEqual(TEXT("All retail window tags exported"), Rects.Num(), 16);
    TestFalse(TEXT("Malformed later line prevents partial application"), Manager->ImportScreenLayout(TEXT("<CHAT> X:0 Y:0 W:500 H:200\n<TBAR> X:1 Y:2 W:no H:132\n"), Error));
    TestEqual(TEXT("Invalid load leaves all windows untouched"), Manager->ExportScreenLayout(), Snapshot);
    TestFalse(TEXT("Overflow rejected before mutation"), Manager->ImportScreenLayout(TEXT("<CHAT> X:9999999999999999999 Y:0 W:500 H:200"), Error));
    Manager->ImportScreenLayout(TEXT("<TBAR> X:1500 Y:920 W:310 H:100"), Error);
    TestEqual(TEXT("One shortcut row restored"), Toolbar->Height, 100);
    TestTrue(TEXT("Named snapshot round trip"), Manager->ImportScreenLayout(Snapshot, Error));
    TestEqual(TEXT("Round trip preserves geometry"), Manager->ExportScreenLayout(), Snapshot);
    Manager->ApplyEdgeAnchoredLayout(1920,1080);
    TestEqual(TEXT("Next layout tick preserves loaded geometry"), Manager->ExportScreenLayout(), Snapshot);
    Radar->UserDragX = 0; Radar->RecomputeLayoutOffset();
    Manager->LoadFloatyLayout();
    TestEqual(TEXT("Radar position persists in ordinary layout cache"), Radar->GetDrawX(), 1600);
    auto* FreshManager = NewObject<UACEUIElementManager>(); FreshManager->Initialize();
    auto* FreshLayout = NewObject<UACEUILayoutResolver>(); FreshLayout->Initialize(Dat, FreshManager);
    TestTrue(TEXT("Fresh gameplay layout loads"), FreshLayout->LoadLayout(ACEUI::LayoutId::ClassicGameplay));
    FreshManager->ApplyEdgeAnchoredLayout(1920,1080);
    const auto FreshChat = FreshManager->FindElementByName(TEXT("RootGameplay_FloatyMainChat_Field"));
    TestEqual(TEXT("Fresh session keeps saved chat size"), FIntPoint(FreshChat->Width,FreshChat->Height), FIntPoint(520,240));
    TestEqual(TEXT("Fresh session keeps saved chat position"), FIntPoint(FreshChat->GetDrawX(),FreshChat->GetDrawY()), FIntPoint(35,640));
    TestEqual(TEXT("Fresh session keeps two-row hotbar"), FreshManager->FindElementByName(TEXT("RootGameplay_FloatyToolbar_Field"))->Height,132);
    FreshManager->Shutdown();

    auto* Client = NewObject<UACEClientSubsystem>(GI); Client->Session = MakeShared<FACESession>();
    auto& Session = *Client->Session; Session.State = EACESessionState::InWorld; Session.PlayerGuid = 0x50001000;
    Session.ServerName = TEXT("UILayoutRegression") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FACEWorldObject Player; Player.Guid = Session.PlayerGuid; Player.Name = TEXT("TestCharacter");
    Session.WorldObjects.Add(Session.PlayerGuid, Player);
    auto* Binder = NewObject<UACEUIGameplayBinder>(); Binder->Client = Client; Binder->Manager = Manager;
    const FString Stem = TEXT("uitest") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
    const FString NamedPath = FPaths::ProjectSavedDir()/TEXT("UILayouts")/(Stem + TEXT(".txt"));
    FString AutoPath; TestTrue(TEXT("Automatic path available"), Binder->GetAutoUILayoutPath(AutoPath));
    ON_SCOPE_EXIT { IFileManager::Get().Delete(*NamedPath); IFileManager::Get().Delete(*AutoPath); Manager->Shutdown(); Dat->Deinitialize(); };
    TestTrue(TEXT("Slash save consumed locally"), Binder->TryDispatchChatCommand(TEXT("/saveui ") + Stem));
    TestTrue(TEXT("Named snapshot saved"), IFileManager::Get().FileExists(*NamedPath));
    Chat->UserDragX += 55; Chat->RecomputeLayoutOffset();
    TestTrue(TEXT("At-sign load consumed locally"), Binder->TryDispatchChatCommand(TEXT("@loadui ") + Stem));
    TestEqual(TEXT("Named command restored chat"), Chat->GetDrawX(), 35);
    Binder->TryDispatchChatCommand(TEXT("/saveautoui"));
    TestTrue(TEXT("Automatic snapshot saved"), IFileManager::Get().FileExists(*AutoPath));
    FString Before; Load(AutoPath, Before, Error);
    Chat->UserDragX += 99; Chat->RecomputeLayoutOffset(); Manager->SaveFloatyLayout();
    FString After; Load(AutoPath, After, Error);
    TestEqual(TEXT("Dragging does not overwrite explicit auto snapshot"), After, Before);
    Binder->UpdateAutoUILayout();
    TestEqual(TEXT("First character context automatically restores snapshot"), Chat->GetDrawX(), 35);
    Chat->UserDragX += 20; Chat->RecomputeLayoutOffset(); Binder->UpdateAutoUILayout();
    TestEqual(TEXT("Ordinary HUD ticks do not keep reloading"), Chat->GetDrawX(), 55);
    Binder->TryDispatchChatCommand(TEXT("/loadautoui"));
    TestEqual(TEXT("Manual auto reload"), Chat->GetDrawX(), 35);
    Manager->ApplyEdgeAnchoredLayout(1280,720); Binder->UpdateAutoUILayout();
    FString SmallPath; Binder->GetAutoUILayoutPath(SmallPath);
    TestNotEqual(TEXT("Effective UI resolution isolates auto snapshot"), SmallPath, AutoPath);
    Manager->ApplyEdgeAnchoredLayout(1920,1080); Binder->UpdateAutoUILayout();
    TestEqual(TEXT("Returning to saved resolution restores chat"), Chat->GetDrawX(), 35);
    Session.WorldObjects[Session.PlayerGuid].Name = TEXT("OtherCharacter");
    FString OtherPath; Binder->GetAutoUILayoutPath(OtherPath);
    TestNotEqual(TEXT("Different character uses a different file"), OtherPath, AutoPath);
    Session.ServerName += TEXT("Other"); Binder->GetAutoUILayoutPath(SmallPath);
    TestNotEqual(TEXT("Different server uses a different file"), SmallPath, OtherPath);
    TestEqual(TEXT("UI commands create no server packets"), Session.CachedC2SPackets.Num(), 0);
    TestTrue(TEXT("Missing layout command is handled locally"), Binder->TryDispatchChatCommand(TEXT("/loadui no_such_layout")));
    Binder->TryDispatchChatCommand(TEXT("/lockui")); TestFalse(TEXT("Retail lock command toggles"), Manager->IsUiLocked());
    return true;
}
#endif
