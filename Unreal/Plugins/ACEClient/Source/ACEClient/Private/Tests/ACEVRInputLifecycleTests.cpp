#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "UI/ACEUIElementManager.h"
#include "VR/ACEVRPlatformTextEntry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRInputLifecycleTest,"ACE.VR.InputLifecycle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEVRInputLifecycleTest::RunTest(const FString&)
{
    TGuardValue<FString> TestConfig(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/UILockTest.ini"));
    auto* Manager=NewObject<UACEUIElementManager>(); Manager->Initialize();
    auto Make=[](const TCHAR* Name,int X,int Y,int W,int H)
    { auto E=MakeShared<FACEUIElement>(); E->ElementName=Name; E->X=X;E->Y=Y;E->Width=W;E->Height=H;return E; };
    auto Root=Make(TEXT("RootGameplay_Field"),0,0,800,600);
    auto Window=Make(TEXT("RootGameplay_FloatyPanel_Field"),200,100,100,100);
    auto Header=Make(TEXT("TitleDragArea"),0,0,100,20);
    Header->Type=ACEUI::ElementType::Dragbar;
    auto Grip=Make(TEXT("BottomBorder"),0,95,100,5); Grip->bResizeBottom=true; Grip->Type=ACEUI::ElementType::Resizebar;
    Root->AddChild(Window);Window->AddChild(Header);Window->AddChild(Grip);Manager->AddRoot(Root);
    Manager->SetUiLocked(true);
    TestTrue(TEXT("Locked title is window background, not a drag target"),Manager->HitTestCanvas(225,110)==Window);
    TestTrue(TEXT("Locked resize edge is window background"),Manager->HitTestCanvas(225,198)==Window);
    const FVector2D Size(800,600);
    Manager->NotifyMouseDown({225,198},Size,EKeys::LeftMouseButton);
    Manager->NotifyMouseMove({225,240},Size);
    TestEqual(TEXT("Locked window cannot resize"),Window->UserResizeH,0);
    Manager->NotifyMouseUp({225,240},Size,EKeys::LeftMouseButton);
    Manager->SetUiLocked(false);
    TestTrue(TEXT("Unlocked title accepts dragging"),Manager->HitTestCanvas(225,110)==Header);
    Manager->NotifyMouseDown({225,110},Size,EKeys::LeftMouseButton);
    Manager->NotifyMouseMove({255,130},Size);
    TestEqual(TEXT("Unlocked window follows drag"),Window->UserDragX,30);
    Manager->NotifyMouseUp({255,130},Size,EKeys::LeftMouseButton);
    Manager->NotifyMouseDown({255,218},Size,EKeys::LeftMouseButton);
    Manager->SetUiLocked(true);
    Manager->NotifyMouseMove({255,280},Size);
    TestEqual(TEXT("Locking cancels an existing resize capture"),Window->UserResizeH,0);
    Manager->Shutdown();

    auto* Entry=NewObject<UEditableTextBox>(); Entry->SetText(FText::FromString(TEXT("draft")));
    int Completed=0;
    auto First=MakeShared<FACEVRPlatformTextEntry>(Entry,[&]{++Completed;});
    First->SetTextFromVirtualKeyboard(FText::FromString(TEXT("hello")),ETextEntryType::TextEntryUpdated);
    TestEqual(TEXT("Native typing updates chat"),Entry->GetText().ToString(),FString(TEXT("hello")));
    First->SetTextFromVirtualKeyboard(FText::FromString(TEXT("sent")),ETextEntryType::TextEntryAccepted);
    First->SetTextFromVirtualKeyboard(FText::FromString(TEXT("late callback")),ETextEntryType::TextEntryAccepted);
    TestEqual(TEXT("A native session finishes once"),Completed,1);
    TestEqual(TEXT("Late native callback cannot overwrite accepted text"),Entry->GetText().ToString(),FString(TEXT("sent")));
    auto Second=MakeShared<FACEVRPlatformTextEntry>(Entry,[&]{++Completed;});
    TestTrue(TEXT("Reopening chat supplies a distinct native session identity"),&First.Get()!=&Second.Get());
    Second->SetTextFromVirtualKeyboard(FText::FromString(TEXT("cancelled edit")),ETextEntryType::TextEntryCanceled);
    TestEqual(TEXT("Cancel preserves original draft"),Entry->GetText().ToString(),FString(TEXT("sent")));
    TestEqual(TEXT("Reopened native session can finish independently"),Completed,2);
    auto* RetailEntry=NewObject<UACERetailTextEntry>();RetailEntry->SetText(FText::FromString(TEXT("retail draft")));
    auto RetailFirst=MakeShared<FACEVRPlatformTextEntry>(RetailEntry,[&]{++Completed;});
    RetailFirst->SetTextFromVirtualKeyboard(FText::FromString(TEXT("first chat")),ETextEntryType::TextEntryAccepted);
    auto RetailSecond=MakeShared<FACEVRPlatformTextEntry>(RetailEntry,[&]{++Completed;});
    RetailFirst->SetTextFromVirtualKeyboard(FText::FromString(TEXT("stale chat")),ETextEntryType::TextEntryAccepted);
    RetailSecond->SetTextFromVirtualKeyboard(FText::FromString(TEXT("second chat")),ETextEntryType::TextEntryAccepted);
    TestEqual(TEXT("Retail chat accepts repeated native keyboard sessions without stale callbacks"),RetailEntry->GetText().ToString(),FString(TEXT("second chat")));
    TestEqual(TEXT("Both retail keyboard sessions finish exactly once"),Completed,4);
    RetailEntry->bDigitsOnly=true;
    TestEqual(TEXT("Retail quantity fields retain the numeric native keyboard"),RetailSecond->GetVirtualKeyboardType(),Keyboard_Number);
    Manager->Initialize();Manager->AddRoot(Root);
    auto Other=Make(TEXT("OtherWindow"),400,100,100,100);
    auto FirstName=Make(TEXT("RepeatedLabel"),0,0,10,10);
    auto SecondName=Make(TEXT("RepeatedLabel"),0,0,10,10);
    Window->AddChild(FirstName);Root->AddChild(Other);Other->AddChild(SecondName);
    const auto Uncached=Manager->FindElementByName(TEXT("RepeatedLabel"));
    Manager->BeginNameLookupPass();
    TestTrue(TEXT("Indexed lookup preserves the tree's current Z-sorted duplicate order"),Manager->FindElementByName(TEXT("RepeatedLabel"))==Uncached);
    Window->bVisible=false;
    TestTrue(TEXT("Indexed lookup evaluates changing visibility"),Manager->FindElementByName(TEXT("RepeatedLabel"))==SecondName);
    TestTrue(TEXT("Ancestor lookup retains hidden fallback"),Manager->FindElementUnder(Window->ElementName,TEXT("RepeatedLabel"))==FirstName);
    auto Added=Make(TEXT("NewDynamicRow"),0,0,10,10);Other->AddChild(Added);Manager->InvalidateNameLookupIndex();
    TestTrue(TEXT("A dynamic row is queryable in the same refresh"),Manager->FindElementByName(TEXT("NewDynamicRow"))==Added);
    Manager->EndNameLookupPass();
    Manager->ClearRoots();
    TestFalse(TEXT("Reload cannot return a stale indexed element"),Manager->FindElementByName(TEXT("RepeatedLabel")).IsValid());
    Window->bVisible=true;Manager->AddRoot(Window);Manager->SetUiLocked(true);
    const FIntPoint At=Window->GetScreenOrigin();
    TestTrue(TEXT("Locked chrome is also blocked in a standalone floaty root"),Manager->HitTestCanvas(At.X+5,At.Y+5)==Window);
    Manager->Shutdown();
    return true;
}
#endif
