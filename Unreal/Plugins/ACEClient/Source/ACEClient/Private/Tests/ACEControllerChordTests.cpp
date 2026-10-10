#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ACEInputBindings.h"
#include "ACEPlayerController.h"
#include "UI/ACERetailKeySelector.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEControllerChordTest,"ACE.RetailParity.ControllerChords",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEControllerChordTest::RunTest(const FString&)
{
 using namespace ACEInputBindings;
 const FString Original=GGameUserSettingsIni;
 GGameUserSettingsIni=FPaths::ProjectSavedDir()/TEXT("Automation/ControllerChordFixture.ini");
 FConfigFile Config;Config.bCanSaveAllSections=true;
 GConfig->SetFile(GGameUserSettingsIni,&Config);Reload();
 const auto Focus=FSlateApplication::Get().GetKeyboardFocusedWidget();
 FSlateApplication::Get().ClearKeyboardFocus();
 const auto Values=UWorld::InitializationValues().CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* PC=World->SpawnActor<APlayerController>();PC->PlayerInput=NewObject<UPlayerInput>(PC);
 TStrongObjectPtr<UACERetailKeySelector> Selector(NewObject<UACERetailKeySelector>());
 ON_SCOPE_EXIT
 {
  Selector->CancelCapture();World->DestroyWorld(false);
  FSlateApplication::Get().ClearKeyboardFocus();if(Focus)FSlateApplication::Get().SetKeyboardFocus(Focus);
  GGameUserSettingsIni=Original;Reload();
 };
 const FKey Toggle=Action(TEXT("ToggleUCM")),A=EKeys::Gamepad_FaceButton_Bottom,RT=EKeys::Gamepad_RightTrigger;
 Selector->InitializeBinding(Toggle,3);Selector->RetailLabel=NewObject<UTextBlock>(Selector.Get());
 const auto Widget=Selector->TakeWidget();
 auto Begin=[&]
 {
  BeginEdit();
  const FKeyEvent Enter(EKeys::Enter,FModifierKeysState(),0,false,0,0);
  Widget->OnKeyDown(Selector->GetCachedGeometry(),Enter);Widget->OnKeyUp(Selector->GetCachedGeometry(),Enter);
  TestTrue(TEXT("Controller binding capture starts"),Selector->GetIsSelectingKey());
 };
 auto Capture=[&](FKey Key,bool Down,bool Shift=false)
 {
  Selector->FilterCaptureKey(FKeyEvent(Key,FModifierKeysState(Shift,false,false,false,false,false,false,false,false),0,false,0,0),Down);
 };
 auto Send=[&](FKey Key,bool Down)
 {PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key,Down?IE_Pressed:IE_Released,Down?1.f:0.f));};
 auto Hold=[&](TArray<FKey> Keys)
 {
  PC->PlayerInput->FlushPressedKeys();for(FKey Key:Keys)Send(Key,true);
  PC->PlayerInput->ProcessInputStack({},.016f,false);
 };
 for(FKey Modifier:{RT,EKeys::Gamepad_LeftTrigger,EKeys::Gamepad_LeftShoulder,EKeys::Gamepad_RightShoulder,EKeys::Gamepad_Special_Left})
  for(bool ModifierFirst:{false,true})
 {
  Begin();Capture(Modifier,true);Capture(A,true);
  if(ModifierFirst){Capture(Modifier,false);TestTrue(TEXT("Releasing modifier first keeps action capture"),Selector->GetIsSelectingKey());}
  Capture(A,false);
  TestFalse(TEXT("Controller chord completes on action release"),Selector->GetIsSelectingKey());
  TestEqual(TEXT("Action button is preserved"),Get(Toggle,3),FInputChord(A));
  TestTrue(TEXT("Held controller modifier is stored"),GetControllerModifiers(Toggle,3)==TArray<FKey>{Modifier});
  TestEqual(TEXT("Capturing RT+A does not erase the plain A jump binding"),Get(EKeys::SpaceBar,3),FInputChord(A));
  Selector->RefreshBinding();
  if(Modifier==RT)TestTrue(TEXT("Label displays RT+A"),Selector->RetailLabel->GetText().ToString().Contains(TEXT("RT+A")));
  Commit();
  FConfigFile Saved;Saved.Read(GGameUserSettingsIni);GConfig->SetFile(GGameUserSettingsIni,&Saved);Reload();
  TestTrue(TEXT("Controller modifiers survive a settings-file reload"),GetControllerModifiers(Toggle,3)==TArray<FKey>{Modifier});
  Hold({Modifier,A});
  TestTrue(TEXT("Held combination triggers assigned action"),Pressed(PC,Toggle));
  TestFalse(TEXT("Combination suppresses plain A jump"),Down(PC,EKeys::SpaceBar));
  Hold({A});
  TestFalse(TEXT("A alone cannot trigger RT+A"),Pressed(PC,Toggle));
  TestTrue(TEXT("A alone still jumps"),Down(PC,EKeys::SpaceBar));
  Hold({Modifier});TestFalse(TEXT("Modifier alone does not invoke combination"),Down(PC,Toggle));
 }
 // A digital trigger can also emit analog pressure while held. Axis changes
 // must not finish capture before the action button is pressed.
 Begin();Capture(RT,true);
 for(float Pressure:{.15f,.4f,.9f})
  Selector->FilterCaptureAnalog(FAnalogInputEvent(EKeys::Gamepad_RightTriggerAxis,FModifierKeysState(),0,false,0,0,Pressure));
 TestTrue(TEXT("Trigger pressure does not prematurely finish button capture"),Selector->GetIsSelectingKey());
 Capture(A,true);Capture(RT,false);Capture(A,false);
 TestTrue(TEXT("Trigger chord retains RT after early release"),GetControllerModifiers(Toggle,3)==TArray<FKey>{RT});Commit();
 // The event-driven UCM/chat path runs before the engine processes input stacks.
 PC->PlayerInput->FlushPressedKeys();Send(RT,true);
 TestTrue(TEXT("Immediate matching sees RT pressed earlier in the same frame"),Matches(Toggle,FInputChord(A),PC));
 TestFalse(TEXT("Immediate matching suppresses plain button"),Matches(EKeys::SpaceBar,FInputChord(A),PC));
 Send(RT,false);
 TestFalse(TEXT("Immediate matching sees a queued modifier release"),Matches(Toggle,FInputChord(A),PC));
 // Multiple controller modifiers plus keyboard modifiers, released in any order.
 Begin();Capture(EKeys::Gamepad_LeftShoulder,true,true);Capture(RT,true,true);Capture(A,true,true);
 Capture(EKeys::Gamepad_LeftShoulder,false);Capture(RT,false);Capture(A,false);
 TestEqual(TEXT("Keyboard modifier survives alongside controller modifiers"),Get(Toggle,3),FInputChord(A,true,false,false,false));
 TestEqual(TEXT("Both held controller buttons survive capture"),GetControllerModifiers(Toggle,3).Num(),2);Commit();
 Hold({EKeys::LeftShift,EKeys::Gamepad_LeftShoulder,RT,A});TestTrue(TEXT("Full controller/keyboard combination matches"),Pressed(PC,Toggle));
 Hold({EKeys::LeftShift,RT,A});TestFalse(TEXT("Partial controller combination cannot invoke action"),Pressed(PC,Toggle));
 // Chords can also use stick directions and wheel pulses as the action.
 Begin();Capture(RT,true);
 Selector->FilterCaptureAnalog(FAnalogInputEvent(EKeys::Gamepad_LeftX,FModifierKeysState(),0,false,0,0,.8f));
 TestEqual(TEXT("Modified stick direction is captured"),Get(Toggle,3).Key,EKeys::Gamepad_LeftStick_Right);
 TestTrue(TEXT("Stick direction retains RT"),GetControllerModifiers(Toggle,3)==TArray<FKey>{RT});Cancel();
 Begin();Capture(RT,true);
 Selector->FilterCaptureWheel(FPointerEvent(0,FVector2D::ZeroVector,FVector2D::ZeroVector,TSet<FKey>(),EKeys::Invalid,1.f,FModifierKeysState()));
 TestEqual(TEXT("Modified wheel pulse is captured"),Get(Toggle,3).Key,EKeys::MouseScrollUp);
 TestTrue(TEXT("Wheel retains RT"),GetControllerModifiers(Toggle,3)==TArray<FKey>{RT});Cancel();
 // An exact duplicate conflicts; chords sharing only their primary button do not.
 BeginEdit();Set(Toggle,3,FInputChord(A),{RT});Set(EKeys::I,3,FInputChord(A),{EKeys::Gamepad_LeftTrigger});
 Set(EKeys::M,3,FInputChord(A),{RT});
 TestFalse(TEXT("Assigning exact combination removes previous assignment"),Get(Toggle,3).Key.IsValid());
 TestTrue(TEXT("Other controller modifier on A remains bound"),GetControllerModifiers(EKeys::I,3)==TArray<FKey>{EKeys::Gamepad_LeftTrigger});
 TestEqual(TEXT("Plain A remains bound after conflict resolution"),Get(EKeys::SpaceBar,3),FInputChord(A));Cancel();
 // Cancel/clear/reset cannot leave hidden modifiers behind.
 Begin();Capture(RT,true);Capture(EKeys::Escape,true);Capture(EKeys::Escape,false);
 TestFalse(TEXT("Escape cancels while controller modifier is held"),Selector->GetIsSelectingKey());Cancel();
 Begin();Capture(RT,true);Capture(RT,false);
 TestEqual(TEXT("Single trigger remains bindable"),Get(Toggle,3),FInputChord(RT));
 TestTrue(TEXT("Single button clears previous controller modifiers"),GetControllerModifiers(Toggle,3).IsEmpty());Cancel();
 Begin();Capture(EKeys::BackSpace,true);Capture(EKeys::BackSpace,false);
 TestFalse(TEXT("Backspace clears the entire combination"),Get(Toggle,3).Key.IsValid());
 TestTrue(TEXT("Cleared binding has no modifiers"),GetControllerModifiers(Toggle,3).IsEmpty());Cancel();
 BeginEdit();Defaults();TestTrue(TEXT("Defaults remove controller combinations"),GetControllerModifiers(Toggle,3).IsEmpty());Cancel();
 TestEqual(TEXT("Cancel restores controller combination"),GetControllerModifiers(Toggle,3).Num(),2);
 // Movement/camera axes retain their magnitude and priority with a held trigger.
 BeginEdit();Set(EKeys::W,3,FInputChord(EKeys::Gamepad_RightStick_Up),{RT});Commit();
 auto* Movement=World->SpawnActor<AACEPlayerController>();Movement->PlayerInput=NewObject<UPlayerInput>(Movement);
 Movement->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Gamepad_RightY,IE_Axis,.65f));
 Movement->PlayerInput->ProcessInputStack({},.016f,false);
 const float UnmodifiedSpeed=Value(Movement,EKeys::NumPadEight);
 TestTrue(TEXT("Unmodified stick has partial analog magnitude"),UnmodifiedSpeed>0.f&&UnmodifiedSpeed<1.f);
 Movement->InputKey(FInputKeyEventArgs::CreateSimulated(RT,IE_Pressed,1.f));
 Movement->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Gamepad_RightY,IE_Axis,.65f));
 Movement->PlayerInput->ProcessInputStack({},.016f,false);
 TestTrue(TEXT("Modified stick moves using its assigned action"),Down(Movement,EKeys::W));
 TestTrue(TEXT("Controller modifier does not change analog speed"),FMath::IsNearlyEqual(Value(Movement,EKeys::W),UnmodifiedSpeed,.001f));
 TestFalse(TEXT("Modified stick suppresses default camera action"),Down(Movement,EKeys::NumPadEight));
 Movement->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Gamepad_RightY,IE_Axis,0.f));
 Movement->PlayerInput->ProcessInputStack({},.016f,false);
 TestFalse(TEXT("Recenter ends modified stick movement"),Down(Movement,EKeys::W));
 // Modifier presses that arrive while typing must be available to the chat
 // chord, without turning into gameplay presses under the focused text box.
 BeginEdit();Set(Action(TEXT("ToggleChat")),3,FInputChord(A),{RT});Commit();
 Movement->PlayerInput->FlushPressedKeys();
 TSharedPtr<SEditableTextBox> Entry;
 auto Window=SNew(SWindow).ClientSize(FVector2D(300,80))[SAssignNew(Entry,SEditableTextBox)];
 FSlateApplication::Get().AddWindow(Window,false);FSlateApplication::Get().SetKeyboardFocus(Entry,EFocusCause::SetDirectly);
 Movement->InputKey(FInputKeyEventArgs::CreateSimulated(RT,IE_Pressed,1.f));
 TestTrue(TEXT("Controller modifier recorded while typing can match the exit-chat chord"),Matches(Action(TEXT("ToggleChat")),FInputChord(A),Movement));
 SetCombatContext(8);Movement->PlayerInput->ProcessInputStack({},.016f,false);
 TestFalse(TEXT("Typing suppresses trigger gameplay even when it is a chat modifier"),Pressed(Movement,Action(TEXT("SpellCast"))));
 FSlateApplication::Get().ClearKeyboardFocus();FSlateApplication::Get().RequestDestroyWindow(Window);
 // Retail files cannot represent controller modifiers in keyboard columns.
 BeginEdit();Set(EKeys::W,0,FInputChord(EKeys::R),{RT});
 FString ExportError;
 TestFalse(TEXT("Retail export cannot silently turn a controller chord into plain R"),ExportRetailKeymapFile(FPaths::ProjectSavedDir()/TEXT("Automation/ControllerChord.keymap"),ExportError));
 TestTrue(TEXT("Retail export explains controller column"),ExportError.Contains(TEXT("controller column")));Cancel();
 return !HasAnyErrors();
}
#endif
