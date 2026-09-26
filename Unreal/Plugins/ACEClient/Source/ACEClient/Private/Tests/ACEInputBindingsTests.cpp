#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ACEInputBindings.h"
#include "ACEPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "RetailCustomKeymap.inl"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEInputBindingsTest,"ACE.RetailParity.KeyboardBindings",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEInputBindingsTest::RunTest(const FString&)
{
 const FString Original=GGameUserSettingsIni;
 GGameUserSettingsIni=FPaths::ProjectSavedDir()/TEXT("Automation/KeyboardFixture.ini");
 FConfigFile Config;Config.NoSave=false;Config.bCanSaveAllSections=true;
 GConfig->SetFile(GGameUserSettingsIni,&Config);ACEInputBindings::Reload();
 const auto Values=UWorld::InitializationValues().CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* PC=World->SpawnActor<APlayerController>();PC->PlayerInput=NewObject<UPlayerInput>(PC);
 ON_SCOPE_EXIT{World->DestroyWorld(false);GGameUserSettingsIni=Original;ACEInputBindings::Reload();};
 auto* Movement=World->SpawnActor<AACEPlayerController>();
 auto Forward=[&](float Value,bool Toggle){Movement->UpdateKeyboardAutoRun(Value,Toggle);return Value;};
 Forward(1,false); Forward(1,true); Forward(1,true); Forward(1,false);
 TestTrue(TEXT("Autorun takes over while forward is already held"),Movement->bAutoRun);
 TestEqual(TEXT("Releasing forward keeps autorun moving"),Forward(0,false),1.f);
 Forward(1,false);TestFalse(TEXT("A new forward press cancels autorun"),Movement->bAutoRun);
 Forward(0,false);Forward(0,true);Forward(0,false);Forward(-1,false);
 TestFalse(TEXT("A new backward press cancels autorun"),Movement->bAutoRun);
 Forward(0,true);Forward(0,false);Forward(0,true);
 TestFalse(TEXT("Second autorun press toggles it off"),Movement->bAutoRun);
 auto Hold=[&](TArray<FKey> Keys)
 {
  PC->PlayerInput->FlushPressedKeys();
  for(FKey Key:Keys)PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key,IE_Pressed,1.f));
  PC->PlayerInput->ProcessInputStack({},.016f,false);
 };
 // Shipped DAT gmDefaultMap (14000000) and DefaultMap (14000002),
 // rather than the later WASD bindings that were invented by this client.
 Hold({EKeys::X});TestTrue(TEXT("Retail X moves backward"),ACEInputBindings::Down(PC,EKeys::S));
 Hold({EKeys::Z});TestTrue(TEXT("Retail Z strafes left"),ACEInputBindings::Down(PC,EKeys::Q));
 Hold({EKeys::C});TestTrue(TEXT("Retail C strafes right"),ACEInputBindings::Down(PC,EKeys::E));
 TestFalse(TEXT("Retail C no longer crouches"),ACEInputBindings::Down(PC,EKeys::C));
 Hold({EKeys::Q});TestTrue(TEXT("Retail Q autoruns"),ACEInputBindings::Down(PC,EKeys::NumLock));
 Hold({EKeys::LeftShift,EKeys::W});TestTrue(TEXT("Shift preserves forward movement"),ACEInputBindings::Down(PC,EKeys::W));
 TestTrue(TEXT("Shift supplies walk modifier"),ACEInputBindings::Down(PC,EKeys::LeftShift));
 Hold({EKeys::LeftAlt,EKeys::A});TestTrue(TEXT("Alt+A strafes left"),ACEInputBindings::Down(PC,EKeys::Q));
 TestFalse(TEXT("Alt+A cannot also turn left"),ACEInputBindings::Down(PC,EKeys::A));
 for(int32 I=0;I<9;++I)
 {
  const auto First=ACEInputBindings::Get(ACEInputBindings::Shortcut(I),0);
  Hold({EKeys::LeftAlt,First.Key});
  TestTrue(TEXT("Alt number reaches second shortcut row"),ACEInputBindings::Down(PC,ACEInputBindings::Shortcut(I+9)));
  TestFalse(TEXT("Alt number does not also fire first row"),ACEInputBindings::Down(PC,ACEInputBindings::Shortcut(I)));
 }
 TestEqual(TEXT("Retail use key"),ACEInputBindings::Get(EKeys::F,0).Key,EKeys::R);
 TestEqual(TEXT("Retail examine key"),ACEInputBindings::Get(ACEInputBindings::Action(TEXT("Examine")),0).Key,EKeys::E);
 TestEqual(TEXT("Retail inventory key"),ACEInputBindings::Get(EKeys::I,0).Key,EKeys::F12);
 TestEqual(TEXT("Retail zoom-in is numpad minus"),ACEInputBindings::Get(EKeys::Add,0).Key,EKeys::Subtract);
 ACEInputBindings::BeginEdit();ACEInputBindings::Set(EKeys::W,0,FInputChord(EKeys::F7));
 ACEInputBindings::Set(EKeys::W,1,FInputChord());ACEInputBindings::Commit();
 Hold({EKeys::W,EKeys::Up});TestFalse(TEXT("Rebinding removes old primary and secondary movement keys"),ACEInputBindings::Down(PC,EKeys::W));
 Hold({EKeys::F7});TestTrue(TEXT("New binding reaches actual input poll"),ACEInputBindings::Down(PC,EKeys::W));
 FConfigFile Saved;Saved.Read(GGameUserSettingsIni);GConfig->SetFile(GGameUserSettingsIni,&Saved);ACEInputBindings::Reload();
 TestTrue(TEXT("Reload retains the working rebind"),ACEInputBindings::Down(PC,EKeys::W));
 ACEInputBindings::BeginEdit();ACEInputBindings::Set(EKeys::S,2,FInputChord(EKeys::F7));
 TestFalse(TEXT("Duplicate chord removed from previous action"),ACEInputBindings::Get(EKeys::W,0).Key.IsValid());
 ACEInputBindings::Revert();TestEqual(TEXT("Revert restores active bindings"),ACEInputBindings::Get(EKeys::W,0).Key,EKeys::F7);
 ACEInputBindings::Defaults();TestEqual(TEXT("Defaults restores retail secondary arrow"),ACEInputBindings::Get(EKeys::W,1).Key,EKeys::Up);
 ACEInputBindings::Cancel();TestEqual(TEXT("Cancel leaves saved customization intact"),ACEInputBindings::Get(EKeys::W,0).Key,EKeys::F7);
 ACEInputBindings::BeginEdit();ACEInputBindings::Set(ACEInputBindings::Action(TEXT("Chat")),0,FInputChord(EKeys::F6));ACEInputBindings::Commit();
 TestTrue(TEXT("Slate chat uses the same rebind"),ACEInputBindings::Matches(ACEInputBindings::Action(TEXT("Chat")),FInputChord(EKeys::F6)));
 TestFalse(TEXT("Old chat primary can be removed"),ACEInputBindings::Matches(ACEInputBindings::Action(TEXT("Chat")),FInputChord(EKeys::Enter)));
 ACEInputBindings::BeginEdit();ACEInputBindings::Defaults();
 const FString Retail=TEXT(R"KEYMAP(
 # Representative retail CMasterInputMap export: device order is intentionally reversed.
 "User Defined Keymap" [ 00000000-0000-0000-0000-000000000000 ]
 Devices [ Mouse [ GUID_SysMouse ] Keyboard [ GUID_SysKeyboard ] Joystick [ Test ] ]
 MetaKeys [ 1 [ 1 DIK_LSHIFT ] 2 [ 1 DIK_LCONTROL ] 3 [ 1 DIK_LMENU ] 5 [ 0 DIMOFS_BUTTON3 ] ]
 Bindings [
 MovementCommands [
  MovementBackup [ "" [ 1 DIK_S ] ] MovementBackup [ "" [ 1 DIK_DOWNARROW ] ]
  MovementStrafeLeft [ "" [ 1 DIK_Q ] ] MovementStrafeLeft [ "" [ 1 DIK_A ] 0x00000004 ]
  MovementTurnLeft [ "" [ 1 DIK_A ] ] MovementTurnLeft [ "" [ 1 DIK_LEFT ] ]
  MovementRunLock [ "" [ 0 DIMOFS_BUTTON3 ] ]
  DoNothing [ "" [ 1 DIK_X ] ]
 ]
 CameraControls [ CameraRotateLeft [ "" [ 1 DIK_NUMPAD4 ] ] ]
 CameraAlternateControls [ CameraRotateLeft [ "" [ 1 DIK_LEFT ] ] ]
 MeleeCombat [ CombatLowAttack [ "" [ 1 DIK_F7 ] ] ]
 MagicCombat [ CombatCastCurrentSpell [ "" [ 1 DIK_F7 ] ] UseSpellSlot_1 [ "" [ 1 DIK_1 ] ] ]
 UICommands [ USE [ "" [ 1 DIK_R ] ] EscapeKey [ "" [ 1 DIK_ESCAPE ] ] ToggleHelp [ "" [ 1 DIK_F1 ] ] ]
 Emotes [ Wave [ "" [ 1 DIK_J ] 0x00000010 ] Cry [ "" [ 2 JOY_BUTTON1 ] ] ]
 EditControls [ EscapeKey [ "" [ 1 DIK_F10 ] ] ]
 ]
 )KEYMAP");
 const auto Imported=ACEInputBindings::ImportRetailKeymap(Retail);
 if(!Imported.bSuccess)AddError(Imported.Error);
 TestTrue(TEXT("Retail text keymap imports"),Imported.bSuccess);
 TestTrue(TEXT("Unsupported actions, devices and modifiers are reported"),Imported.Skipped.Num()>=3);
 TestEqual(TEXT("Separate native contexts are reported separately"),Imported.UnchangedContexts.Num(),2);
 TestEqual(TEXT("Import is a draft and keeps retail S backward"),ACEInputBindings::Get(EKeys::S,0).Key,EKeys::S);
 TestEqual(TEXT("Mouse autorun imports using the declared device index"),ACEInputBindings::Get(EKeys::NumLock,0).Key,EKeys::ThumbMouseButton);
 TestTrue(TEXT("Retail file mask bit 2 names MetaKeys ordinal 3, preserving Alt-A"),ACEInputBindings::Get(EKeys::Q,1)==FInputChord(EKeys::A,false,false,true,false));
 TestFalse(TEXT("Unlisted movement actions are unbound when their retail input map is replaced"),ACEInputBindings::Get(EKeys::W,0).Key.IsValid());
 TestFalse(TEXT("Imported backward binding removes default Stop conflict"),ACEInputBindings::Get(ACEInputBindings::Action(TEXT("Stop")),0).Key.IsValid());
 TestEqual(TEXT("Alternate camera context cannot steal Left from movement"),ACEInputBindings::Get(EKeys::A,1).Key,EKeys::Left);
 TestEqual(TEXT("Text-widget Escape does not override gameplay Escape"),ACEInputBindings::Get(EKeys::Escape,0).Key,EKeys::Escape);
 const auto Before=ACEInputBindings::Get(EKeys::S,0);
 TestFalse(TEXT("Truncated keymap rejected"),ACEInputBindings::ImportRetailKeymap(Retail.LeftChop(3)).bSuccess);
 TestTrue(TEXT("Malformed import leaves draft unchanged"),ACEInputBindings::Get(EKeys::S,0)==Before);
 ACEInputBindings::Commit();Hold({EKeys::F7});ACEInputBindings::SetCombatContext(2);
 TestTrue(TEXT("Melee context retains imported attack"),ACEInputBindings::Down(PC,ACEInputBindings::Action(TEXT("MeleeLow"))));
 TestFalse(TEXT("Magic binding inactive in melee"),ACEInputBindings::Down(PC,ACEInputBindings::Action(TEXT("SpellCast"))));
 ACEInputBindings::SetCombatContext(8);
 TestTrue(TEXT("Same physical key independently casts in magic"),ACEInputBindings::Down(PC,ACEInputBindings::Action(TEXT("SpellCast"))));
 Hold({EKeys::One});TestTrue(TEXT("Magic slot takes priority over ordinary shortcut"),ACEInputBindings::Down(PC,ACEInputBindings::SpellSlot(0)));
 TestFalse(TEXT("Spell slot cannot also use an inventory item"),ACEInputBindings::Down(PC,ACEInputBindings::Shortcut(0)));
 Hold({EKeys::LeftControl,EKeys::One});TestTrue(TEXT("Ctrl shortcut still works while in magic"),ACEInputBindings::Down(PC,ACEInputBindings::Shortcut(0)));
 TestFalse(TEXT("Ctrl shortcut cannot also cast"),ACEInputBindings::Down(PC,ACEInputBindings::SpellSlot(0)));
 ACEInputBindings::BeginEdit();ACEInputBindings::ImportRetailKeymap(Retail);ACEInputBindings::Cancel();
 TestEqual(TEXT("Cancel preserves prior live import"),ACEInputBindings::Get(EKeys::S,0).Key,EKeys::S);
 ACEInputBindings::BeginEdit();
 const FString ExportPath=FPaths::ProjectSavedDir()/TEXT("Automation/RoundTrip.keymap");FString ExportError;
 TestTrue(TEXT("Save As writes a retail-format keymap"),ACEInputBindings::ExportRetailKeymapFile(ExportPath,ExportError));
 ACEInputBindings::Defaults();
 const auto RoundTrip=ACEInputBindings::ImportRetailKeymapFile(ExportPath);
 TestTrue(TEXT("Exported file reloads"),RoundTrip.bSuccess);
 TestTrue(TEXT("Save and load preserve modifier bindings"),ACEInputBindings::Get(EKeys::Q,1)==FInputChord(EKeys::A,false,false,true,false));
 TestFalse(TEXT("Save and load preserve unbound actions without invalid empty control records"),ACEInputBindings::Get(EKeys::W,0).Key.IsValid());
 TestEqual(TEXT("Save and load preserve mouse bindings"),ACEInputBindings::Get(EKeys::NumLock,0).Key,EKeys::ThumbMouseButton);
 ACEInputBindings::Cancel();
 ACEInputBindings::BeginEdit();ACEInputBindings::Defaults();
 const auto Custom=ACEInputBindings::ImportRetailKeymap(RetailCustomKeymap);
 TestTrue(TEXT("User retail keymap imports"),Custom.bSuccess);
 TestEqual(TEXT("All supported user-file action bindings import"),Custom.BindingCount,157);
 TestEqual(TEXT("Remaining unsupported controls are reported individually"),Custom.Skipped.Num(),18);
 TestEqual(TEXT("Retail map view imports its keypad Enter"),ACEInputBindings::Get(ACEInputBindings::Action(TEXT("CameraViewMapMode")),0).Key,ACEInputBindings::NumpadEnterKey());
 TestEqual(TEXT("All explicit supported DoNothing overrides import"),ACEInputBindings::GetBlockedBindings().Num(),17);
 TestEqual(TEXT("Native input contexts are accounted for separately"),Custom.UnchangedContexts.Num(),9);
 AddInfo(FString::Printf(TEXT("Custom keymap: %d imported, %d diagnostics"),Custom.BindingCount,Custom.Skipped.Num()));
 for(const auto& Entry:Custom.Skipped)AddInfo(Entry);
 TestEqual(TEXT("Self binding from user file"),ACEInputBindings::Get(ACEInputBindings::Action(TEXT("SelectionSelf")),0).Key,EKeys::NumPadOne);
 TestEqual(TEXT("Windows applications key imports for contracts"),ACEInputBindings::Get(EKeys::U,0).Key,ACEInputBindings::ApplicationsKey());
 TestEqual(TEXT("Mouse wheel imports as directional zoom"),ACEInputBindings::Get(EKeys::Add,0).Key,EKeys::MouseScrollUp);
 TestEqual(TEXT("Custom friend panel does not become allegiance"),ACEInputBindings::Get(ACEInputBindings::Action(TEXT("ToggleFriendsPanel")),0).Key,EKeys::F3);
 ACEInputBindings::Commit();ACEInputBindings::SetCombatContext(1);
 Hold({EKeys::LeftControl,EKeys::Up});TestFalse(TEXT("DoNothing Ctrl+Up blocks forward fallback"),ACEInputBindings::Down(PC,EKeys::W));
 Hold({EKeys::LeftAlt,EKeys::A});TestFalse(TEXT("DoNothing Alt+A blocks turning"),ACEInputBindings::Down(PC,EKeys::A));
 TestFalse(TEXT("DoNothing Alt+A blocks old strafe default"),ACEInputBindings::Down(PC,EKeys::Q));
 Hold({EKeys::Up});TestTrue(TEXT("Plain Up still moves"),ACEInputBindings::Down(PC,EKeys::W));
 Hold({EKeys::LeftAlt,EKeys::Up});TestTrue(TEXT("Alt+Up still autoruns"),ACEInputBindings::Down(PC,EKeys::NumLock));
 TestFalse(TEXT("Alt+Up does not also move"),ACEInputBindings::Down(PC,EKeys::W));
 Hold({EKeys::Slash});TestTrue(TEXT("Custom slash selects a player"),ACEInputBindings::Down(PC,ACEInputBindings::Action(TEXT("SelectionClosestPlayer"))));
 TestFalse(TEXT("Custom slash no longer opens chat"),ACEInputBindings::Down(PC,ACEInputBindings::Action(TEXT("Chat"))));
 Hold({EKeys::LeftControl,EKeys::LeftShift,EKeys::One});TestTrue(TEXT("Custom second-row shortcut survives"),ACEInputBindings::Down(PC,ACEInputBindings::Shortcut(9)));
 Hold({EKeys::LeftAlt,EKeys::One});TestTrue(TEXT("Alt+1 opens floating chat from this file"),ACEInputBindings::Down(PC,ACEInputBindings::Action(TEXT("ToggleFloatingChatWindow1"))));
 TestFalse(TEXT("Floating chat binding does not also use a shortcut"),ACEInputBindings::Down(PC,ACEInputBindings::Shortcut(9)));
 ACEInputBindings::Reload();
 Hold({EKeys::LeftControl,EKeys::Up});TestFalse(TEXT("DoNothing survives settings reload"),ACEInputBindings::Down(PC,EKeys::W));
 ACEInputBindings::BeginEdit();
 TestTrue(TEXT("Custom bindings export"),ACEInputBindings::ExportRetailKeymapFile(ExportPath,ExportError));
 ACEInputBindings::Defaults();TestTrue(TEXT("Custom bindings reimport"),ACEInputBindings::ImportRetailKeymapFile(ExportPath).bSuccess);
 ACEInputBindings::Commit();Hold({EKeys::LeftControl,EKeys::Up});
 TestFalse(TEXT("DoNothing survives retail export/import"),ACEInputBindings::Down(PC,EKeys::W));
 TestTrue(TEXT("Mouse wheel survives retail export/import"),ACEInputBindings::Matches(EKeys::Add,FInputChord(EKeys::MouseScrollUp)));
 ACEInputBindings::BeginEdit();ACEInputBindings::Set(EKeys::W,2,FInputChord(EKeys::Up,false,true,false,false));ACEInputBindings::Commit();
 TestTrue(TEXT("Explicit rebind can replace DoNothing"),ACEInputBindings::Down(PC,EKeys::W));
 ACEInputBindings::Cancel();
 return !HasAnyErrors();
}
#endif
