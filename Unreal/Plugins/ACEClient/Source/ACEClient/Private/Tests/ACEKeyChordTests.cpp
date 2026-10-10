#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ACEInputBindings.h"
#include "UI/ACERetailKeySelector.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEKeyChordTest,"ACE.RetailParity.KeyChordCapture",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEKeyChordTest::RunTest(const FString&)
{
 const FString Original=GGameUserSettingsIni;
 GGameUserSettingsIni=FPaths::ProjectSavedDir()/TEXT("Automation/KeyChordFixture.ini");
 FConfigFile Config;Config.bCanSaveAllSections=true;
 GConfig->SetFile(GGameUserSettingsIni,&Config);ACEInputBindings::Reload();
 const auto Focus=FSlateApplication::Get().GetKeyboardFocusedWidget();
 const auto Values=UWorld::InitializationValues().CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* PC=World->SpawnActor<APlayerController>();PC->PlayerInput=NewObject<UPlayerInput>(PC);
 TStrongObjectPtr<UACERetailKeySelector> Key(NewObject<UACERetailKeySelector>());
 ON_SCOPE_EXIT
 {
  Key->CancelCapture();World->DestroyWorld(false);
  FSlateApplication::Get().ClearKeyboardFocus();if(Focus)FSlateApplication::Get().SetKeyboardFocus(Focus);
  GGameUserSettingsIni=Original;ACEInputBindings::Reload();
 };
 const auto Action=ACEInputBindings::Action(TEXT("Pickup"));
 Key->InitializeBinding(Action,0);Key->RetailLabel=NewObject<UTextBlock>(Key.Get());
 const auto Widget=Key->TakeWidget();
 auto Begin=[&]
 {
  ACEInputBindings::BeginEdit();
  const FKeyEvent Enter(EKeys::Enter,FModifierKeysState(),0,false,0,0);
  Widget->OnKeyDown(Key->GetCachedGeometry(),Enter);Widget->OnKeyUp(Key->GetCachedGeometry(),Enter);
  TestTrue(TEXT("Mapping button starts capture"),Key->GetIsSelectingKey());
 };
 auto Mods=[](int32 Mask,bool Right=false)
 {
  return FModifierKeysState((Mask&1)&&!Right,(Mask&1)&&Right,(Mask&2)&&!Right,(Mask&2)&&Right,
   (Mask&4)&&!Right,(Mask&4)&&Right,(Mask&8)&&!Right,(Mask&8)&&Right,false);
 };
 const FKey Left[]={EKeys::LeftShift,EKeys::LeftControl,EKeys::LeftAlt,EKeys::LeftCommand};
 const FKey RightKeys[]={EKeys::RightShift,EKeys::RightControl,EKeys::RightAlt,EKeys::RightCommand};
 auto CheckCaptured=[&](FKey Physical,int32 Mask)
 {
  TestFalse(TEXT("Chord capture finishes"),Key->GetIsSelectingKey());
  const FInputChord Expected(Physical,(Mask&1)!=0,(Mask&2)!=0,(Mask&4)!=0,(Mask&8)!=0);
  TestEqual(TEXT("All modifiers survive capture"),ACEInputBindings::Get(Action,0),Expected);
  Key->RefreshBinding();
  if(Mask&1)TestTrue(TEXT("Binding label includes Shift"),Key->RetailLabel->GetText().ToString().Contains(TEXT("Shift")));
  ACEInputBindings::Commit();
  FConfigFile Saved;Saved.Read(GGameUserSettingsIni);GConfig->SetFile(GGameUserSettingsIni,&Saved);ACEInputBindings::Reload();
  TestEqual(TEXT("Full chord survives settings-file reload"),ACEInputBindings::Get(Action,0),Expected);
  PC->PlayerInput->FlushPressedKeys();
  for(int32 I=0;I<4;++I)if(Mask&(1<<I))PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(RightKeys[I],IE_Pressed,1.f));
  PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Physical,IE_Pressed,1.f));
  const bool Wheel=Physical==EKeys::MouseScrollUp||Physical==EKeys::MouseScrollDown;
  if(Wheel)PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Physical,IE_Released,0.f));
  PC->PlayerInput->ProcessInputStack({},.016f,false);
  TestTrue(TEXT("Captured chord triggers its assigned action using either side's modifiers"),ACEInputBindings::Pressed(PC,Action));
  if(Wheel)
  {
   TestTrue(TEXT("Wheel pulse works for held-input actions too"),ACEInputBindings::Down(PC,Action));
   TestFalse(TEXT("Modified wheel action suppresses default camera zoom"),ACEInputBindings::Matches(Physical==EKeys::MouseScrollUp?EKeys::Add:EKeys::Subtract,Expected));
   PC->PlayerInput->ProcessInputStack({},.016f,false);
   TestFalse(TEXT("Wheel cannot leave action held"),ACEInputBindings::Down(PC,Action));
  }
  PC->PlayerInput->FlushPressedKeys();PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Physical,IE_Pressed,1.f));
  PC->PlayerInput->ProcessInputStack({},.016f,false);
  if(Mask)TestFalse(TEXT("Plain input cannot invoke the modified binding"),ACEInputBindings::Pressed(PC,Action));
 };
 // Test every modifier combination, both physical sides, and both release orders.
 for(int32 Mask=1;Mask<16;++Mask)for(bool Right:{false,true})for(bool ModifierFirst:{false,true})
 {
  Begin();
  for(int32 I=0;I<4;++I)if(Mask&(1<<I))Key->FilterCaptureKey(FKeyEvent(Right?RightKeys[I]:Left[I],Mods(Mask,Right),0,false,0,0),true);
  Key->FilterCaptureKey(FKeyEvent(EKeys::R,Mods(Mask,Right),0,false,0,0),true);
  if(ModifierFirst)
  {
   for(int32 I=0;I<4;++I)if(Mask&(1<<I))Key->FilterCaptureKey(FKeyEvent(Right?RightKeys[I]:Left[I],FModifierKeysState(),0,false,0,0),false);
   TestTrue(TEXT("Releasing modifiers cannot prematurely bind Shift/Ctrl/Alt alone"),Key->GetIsSelectingKey());
  }
  Key->FilterCaptureKey(FKeyEvent(EKeys::R,ModifierFirst?FModifierKeysState():Mods(Mask,Right),0,false,0,0),false);
  CheckCaptured(EKeys::R,Mask);
  TestTrue(TEXT("Plain R retains retail Use alongside the new chord"),ACEInputBindings::Matches(EKeys::F,FInputChord(EKeys::R)));
  TestFalse(TEXT("Modified R cannot also use the selected object"),ACEInputBindings::Matches(EKeys::F,FInputChord(EKeys::R,(Mask&1)!=0,(Mask&2)!=0,(Mask&4)!=0,(Mask&8)!=0)));
 }
 for(int32 Mask:{0,1,2,4,7,15})for(float Delta:{-1.f,1.f})
 {
  Begin();
  const FPointerEvent Wheel(0,FVector2D::ZeroVector,FVector2D::ZeroVector,TSet<FKey>(),EKeys::Invalid,Delta,Mods(Mask));
  TestTrue(TEXT("Wheel capture consumes scrolling"),Key->FilterCaptureWheel(Wheel));
  CheckCaptured(Delta>0?EKeys::MouseScrollUp:EKeys::MouseScrollDown,Mask);
 }
 for(FKey Button:{EKeys::LeftMouseButton,EKeys::RightMouseButton,EKeys::MiddleMouseButton,EKeys::ThumbMouseButton,EKeys::ThumbMouseButton2})
 {
  Begin();
  const FPointerEvent Press(0,FVector2D::ZeroVector,FVector2D::ZeroVector,TSet<FKey>{Button},Button,0.f,Mods(7));
  const FPointerEvent Release(0,FVector2D::ZeroVector,FVector2D::ZeroVector,TSet<FKey>(),Button,0.f,FModifierKeysState());
  TestTrue(TEXT("Mouse chord press bypasses engine's modifier-stripping handler"),Key->FilterCaptureMouseButton(Press,true));
  TestTrue(TEXT("Mouse chord release is consumed"),Key->FilterCaptureMouseButton(Release,false));
  CheckCaptured(Button,7);
 }
 for(FKey Button:{EKeys::Gamepad_FaceButton_Left,EKeys::Gamepad_Special_Right,EKeys::BackSpace})
 {
  Begin();Key->FilterCaptureKey(FKeyEvent(Button,Mods(1),0,false,0,0),true);
  Key->FilterCaptureKey(FKeyEvent(Button,FModifierKeysState(),0,false,0,0),false);CheckCaptured(Button,1);
 }
 Begin();const auto Before=ACEInputBindings::Get(Action,0);
 Key->FilterCaptureKey(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0),true);
 Key->FilterCaptureKey(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0),false);
 TestFalse(TEXT("Escape ends capture"),Key->GetIsSelectingKey());TestEqual(TEXT("Escape preserves binding"),ACEInputBindings::Get(Action,0),Before);
 Begin();Key->FilterCaptureKey(FKeyEvent(EKeys::BackSpace,FModifierKeysState(),0,false,0,0),true);
 Key->FilterCaptureKey(FKeyEvent(EKeys::BackSpace,FModifierKeysState(),0,false,0,0),false);
 TestFalse(TEXT("Plain Backspace still clears binding"),ACEInputBindings::Get(Action,0).Key.IsValid());
 return !HasAnyErrors();
}
#endif
