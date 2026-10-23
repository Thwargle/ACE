#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerInput.h"
#include "Mods/ACEActionDeckSubsystem.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginDesktop.h"
#include "Mods/ACEPluginStyle.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "ACEInputBindings.h"
#include "ACEOpcodes.h"
#include "Protocol/ACEIsaac.h"
#include "Protocol/ACEBinaryReader.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Slate/SObjectWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Widgets/SVirtualWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SInputKeySelector.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SOverlay.h"
#include "Slate/WidgetRenderer.h"
#include "Input/HittestGrid.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
    class FDeckInputProbe final : public IInputProcessor
    {
    public:
        int32 Keys=0;
        virtual void Tick(float,FSlateApplication&,TSharedRef<ICursor>) override {}
        virtual bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent&) override {++Keys;return false;}
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEActionDeckTest,"ACE.Plugins.ActionDeck",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEActionDeckTest::RunTest(const FString&)
{
    const FString OriginalSettings=GGameUserSettingsIni;
    GGameUserSettingsIni=FPaths::ProjectSavedDir()/TEXT("Automation/ActionDeckKeys.ini");
    FConfigFile KeyConfig;KeyConfig.NoSave=false;KeyConfig.bCanSaveAllSections=true;
    GConfig->SetFile(GGameUserSettingsIni,&KeyConfig);ACEInputBindings::Reload();
    ON_SCOPE_EXIT{GGameUserSettingsIni=OriginalSettings;ACEInputBindings::Reload();};
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
    auto* GI=NewObject<UGameInstance>(GEngine);W->SetGameInstance(GI);
    Context.OwningGameInstance=GI;Context.SetCurrentWorld(W);GI->OnWorldChanged(nullptr,W);GI->Init();
    auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();
    Host->StorageRoot=FPaths::ProjectSavedDir()/TEXT("Automation/ActionDeck")/FGuid::NewGuid().ToString();
    Host->Settings=MakeShared<FJsonObject>();
    Host->Discover(); // Use isolated settings, not the user's enabled plugins.
    auto* C=GI->GetSubsystem<UACEClientSubsystem>();
    C->GetUIElementManager()->SetUiLocked(false);
    C->Session=MakeShared<FACESession>();auto& S=*C->Session;
    S.State=EACESessionState::InWorld;S.PlayerGuid=1234;S.PlayerVitals.Health=100;
    auto* Deck=GI->GetSubsystem<UACEActionDeckSubsystem>();
    auto* PC=W->SpawnActor<AACEPlayerController>();PC->Client=C;PC->PlayerInput=NewObject<UPlayerInput>(PC);
    // This isolated controller deliberately uses the same raw input path as
    // the client, without an Enhanced Input settings fixture.
    AddExpectedMessagePlain(TEXT("does not have a valid PlayerInput object"),ELogVerbosity::Warning);
    auto* LP=NewObject<ULocalPlayer>(GEngine);LP->PlayerController=PC;GI->AddLocalPlayer(LP,FPlatformUserId::CreateFromInternalId(0));PC->SetPlayer(LP);
    W->AddController(PC); // This fixture world has not run actor initialization.
    auto* Canvas=NewObject<UACEUICanvasWidget>();Canvas->Initialize();
    auto* Binder=NewObject<UACEUIGameplayBinder>();Binder->Client=C;Binder->Canvas=Canvas;
    PC->DatGameplayBinder=Binder;Binder->PlayerController=PC;
    ON_SCOPE_EXIT {PC->DatGameplayBinder=nullptr;Binder->Client=nullptr;PC->Client=nullptr;GI->Shutdown();GEngine->DestroyWorldContext(W);W->DestroyWorld(false);};
    auto Plugin=Host->Plugins.FindByPredicate([](const auto& P){return P->Id==TEXT("actiondeck");});
    if(!TestTrue(TEXT("Action Deck ships in the plugin manager"),Plugin!=nullptr))return false;
    TestFalse(TEXT("Action Deck is optional and initially disabled"),(*Plugin)->Enabled);
    (*Plugin)->Enabled=true;Deck->EnsureCharacter();
    TArray<FInputChord> Unique;
    for(int32 I=0;I<36;++I)
    {
        const auto K=Deck->EffectiveKey(I);
        TestEqual(TEXT("All 36 default keys resolve their exact slot"),Deck->MatchKey(K),I);
        TestFalse(TEXT("Default keys are unique"),Unique.Contains(K));Unique.Add(K);
        TestEqual(TEXT("Only top row uses Shift"),bool(K.bShift),I<12);
        TestEqual(TEXT("Only middle row uses Ctrl"),bool(K.bCtrl),I>=12&&I<24);
    }
    TestEqual(TEXT("Bottom row first key"),Deck->KeyLabel(24),FString(TEXT("1")));
    TestEqual(TEXT("Bottom row twelfth key"),Deck->KeyLabel(35),FString(TEXT("=")));
    TestFalse(TEXT("Duplicate per-slot binding rejected"),Deck->SetKey(0,Deck->EffectiveKey(24)));
    TestFalse(TEXT("Duplicate row modifiers rejected"),Deck->SetRowModifier(0,0));
    TestTrue(TEXT("Individual key supports a modifier combination"),Deck->SetKey(0,FInputChord(EKeys::F8,true,true,false,false)));
    TestTrue(TEXT("Row modifier changes"),Deck->SetRowModifier(0,4));
    TestEqual(TEXT("Individual chord survives row changes"),Deck->EffectiveKey(0),FInputChord(EKeys::F8,true,true,false,false));
    TestEqual(TEXT("Custom icon label retains the complete override"),Deck->KeyLabel(0),FString(TEXT("C+S+F8")));
    {
        // Load a pre-fourth-row profile, including customized row modifiers.
        auto Legacy=Deck->ExportState();Legacy->RemoveField(TEXT("fourth_row"));
        auto LegacyRows=Legacy->GetArrayField(TEXT("rows"));LegacyRows.SetNum(3);Legacy->SetArrayField(TEXT("rows"),LegacyRows);
        auto LegacySlots=Legacy->GetArrayField(TEXT("slots"));LegacySlots.SetNum(36);Legacy->SetArrayField(TEXT("slots"),LegacySlots);
        Deck->Reset();Deck->Deserialize(Legacy);
        TestEqual(TEXT("Legacy profile retains its three-row layout"),Deck->VisibleRows(),3);
        TestEqual(TEXT("Legacy custom row modifier survives"),Deck->RowModifiers[0],uint8(4));
        TestEqual(TEXT("Legacy custom key survives"),Deck->KeyLabel(0),FString(TEXT("C+S+F8")));
        TestFalse(TEXT("Enabling extra row does not steal an existing Alt shortcut"),Deck->SetFourthRowEnabled(true));
        TestFalse(TEXT("Conflicting optional row stays disabled"),Deck->IsFourthRowEnabled());
        TestTrue(TEXT("Existing row can free Alt shortcuts"),Deck->SetRowModifier(0,1));
        TestTrue(TEXT("Fourth row can be enabled"),Deck->SetFourthRowEnabled(true));
        TestEqual(TEXT("Fourth row defaults to Alt+1"),Deck->KeyLabel(36),FString(TEXT("A+1")));
        TestEqual(TEXT("Fourth row defaults through Alt+="),Deck->KeyLabel(47),FString(TEXT("A+=")));
        TestEqual(TEXT("Extra row is drawn above existing rows"),Deck->DisplayRow(36),0);
        TestEqual(TEXT("Unmodified row remains on the bottom"),Deck->DisplayRow(24),3);
        for(int32 I=0;I<48;++I)TestEqual(TEXT("All four rows have distinct usable keys"),Deck->MatchKey(Deck->EffectiveKey(I)),I);
        Deck->BindAttack(47,false,3);
        TestTrue(TEXT("Extra slot supports a custom chord"),Deck->SetKey(47,FInputChord(EKeys::F10,false,true,true,false)));
        auto Expanded=Deck->ExportState();Deck->Reset();Deck->Deserialize(Expanded);
        TestTrue(TEXT("Fourth row setting survives save/load"),Deck->IsFourthRowEnabled());
        TestEqual(TEXT("Fourth row custom hint survives save/load"),Deck->KeyLabel(47),FString(TEXT("C+A+F10")));
        TestTrue(TEXT("Fourth row action survives save/load"),Deck->Slot(47).Action==EACEDeckAction::Melee);
        Deck->SetFourthRowEnabled(false);
        TestEqual(TEXT("Hidden row cannot match input"),Deck->MatchKey(Deck->EffectiveKey(47)),INDEX_NONE);
        TestFalse(TEXT("Hidden row cannot activate"),Deck->Activate(47));
        auto Hidden=Deck->ExportState();Deck->Reset();Deck->Deserialize(Hidden);Deck->SetFourthRowEnabled(true);
        TestEqual(TEXT("Hiding and restoring retains extra-row shortcuts"),Deck->KeyLabel(47),FString(TEXT("C+A+F10")));
        TestTrue(TEXT("Hiding and restoring retains extra-row actions"),Deck->Slot(47).Action==EACEDeckAction::Melee);
        Deck->Reset();Deck->Deserialize(Legacy);Deck->Save();
    }
    TestFalse(TEXT("Escape remains available to dismiss UI"),Deck->SetKey(1,FInputChord(EKeys::Escape)));
    TestFalse(TEXT("Mouse/gamepad keys are not accepted as keyboard shortcuts"),UACEActionDeckSubsystem::ValidKey(EKeys::LeftMouseButton));
    FACEWorldObject Self;Self.Guid=1234;Self.bIsPlayer=true;Self.ItemType=ACEItemType::Creature;S.WorldObjects.Add(Self.Guid,Self);
    FACEWorldObject Pack;Pack.Guid=200;Pack.ContainerId=Self.Guid;Pack.ItemType=ACEItemType::Container;S.WorldObjects.Add(Pack.Guid,Pack);
    FACEWorldObject Kit;Kit.Guid=300;Kit.ContainerId=Pack.Guid;Kit.Name=TEXT("Treated Healing Kit");Kit.WeenieClassId=777;
    Kit.ItemType=ACEItemType::Misc;Kit.IconId=0x060013B8;Kit.ObjectDescriptionFlags=0x10000;
    Kit.Structure=10;Kit.MaxStructure=50;Kit.TargetType=ACEItemType::Creature;
    // Source-owned, target remote-or-self (0x20 | 0x02).
    Kit.ItemUseable=0x00220008;
    S.WorldObjects.Add(Kit.Guid,Kit);
    TestTrue(TEXT("Inventory item can be bound from a nested pack"),Deck->BindItem(24,Kit.Guid));
    TestTrue(TEXT("A charged kit is ready"),Deck->Availability(24).Ready);
    TestEqual(TEXT("Exact dragged GUID is preferred"),Deck->Availability(24).Guid,Kit.Guid);
    FACEWorldObject Spare=Kit;Spare.Guid=301;S.WorldObjects.Add(Spare.Guid,Spare);
    Deck->Refresh(true);TestEqual(TEXT("Both kits counted"),Deck->Availability(24).Count,int64(2));
    S.WorldObjects[Kit.Guid].Structure=0;Deck->Refresh(true);
    TestEqual(TEXT("Depleted preferred kit switches to spare"),Deck->Availability(24).Guid,Spare.Guid);
    S.WorldObjects.Remove(Spare.Guid);Deck->Refresh(true);
    TestFalse(TEXT("No remaining uses disables the shortcut"),Deck->Availability(24).Ready);
    TestEqual(TEXT("Empty slot keeps its item identity"),Deck->Slot(24).Item.Name,Kit.Name);
    S.WorldObjects.Add(Spare.Guid,Spare);Deck->Refresh(true);
    TestTrue(TEXT("Acquiring replacement reactivates the shortcut"),Deck->Availability(24).Ready);
    S.WorldObjects[Spare.Guid].ContainerId=987;Deck->Refresh(true);
    TestFalse(TEXT("A matching world item is never consumed"),Deck->Availability(24).Ready);
    S.WorldObjects[Spare.Guid]=Spare;
    FACEWorldObject Wrong=Spare;Wrong.Name=TEXT("Different kit");
    TestFalse(TEXT("Different item of same class cannot replace the binding"),UACEActionDeckSubsystem::MatchesItem(Deck->Slot(24),Wrong));
    FACEWorldObject Friend=Self;Friend.Guid=400;S.WorldObjects.Add(Friend.Guid,Friend);
    FACEWorldObject Monster=Self;Monster.Guid=500;Monster.bIsPlayer=false;S.WorldObjects.Add(Monster.Guid,Monster);
    TestEqual(TEXT("No selected target heals self"),UACEActionDeckSubsystem::HealingTarget(Spare,0,Self.Guid,S.WorldObjects),Self.Guid);
    TestEqual(TEXT("A monster selection heals self"),UACEActionDeckSubsystem::HealingTarget(Spare,Monster.Guid,Self.Guid,S.WorldObjects),Self.Guid);
    TestEqual(TEXT("Another live player is the healing target"),UACEActionDeckSubsystem::HealingTarget(Spare,Friend.Guid,Self.Guid,S.WorldObjects),Friend.Guid);
    S.WorldObjects[Friend.Guid].bDying=true;
    TestEqual(TEXT("Dead player is not a valid heal target"),UACEActionDeckSubsystem::HealingTarget(Spare,Friend.Guid,Self.Guid,S.WorldObjects),Self.Guid);
    S.WorldObjects[Friend.Guid].bDying=false;
    S.WorldObjects[Friend.Guid].ObjectDescriptionFlags=ACEObjectDescFlag::PlayerKiller;
    TestEqual(TEXT("Incompatible PK target falls back to self"),UACEActionDeckSubsystem::HealingTarget(Spare,Friend.Guid,Self.Guid,S.WorldObjects),Self.Guid);
    S.WorldObjects[Friend.Guid]=Friend;
    S.WorldObjects.Remove(Self.Guid);
    TestEqual(TEXT("Self heal works while the player's world descriptor is absent"),UACEActionDeckSubsystem::HealingTarget(Spare,0,Self.Guid,S.WorldObjects),Self.Guid);
    S.WorldObjects.Add(Self.Guid,Self);
    auto OtherOnly=Spare;OtherOnly.ItemUseable=0x00200008;
    TestEqual(TEXT("An item that forbids self use does not get a self target"),UACEActionDeckSubsystem::HealingTarget(OtherOnly,0,Self.Guid,S.WorldObjects),0);
    TestTrue(TEXT("The main backpack binds as a self action"),Deck->BindItem(23,Self.Guid));
    TestTrue(TEXT("Self selection is ready without a combat stance"),Deck->Availability(23).Ready);
    TestEqual(TEXT("Self action uses the retail closed backpack icon"),Deck->Slot(23).Item.Icon,0x06004CF7);

    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    FString DatDirectory=TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),DatDirectory);
    if(!TestTrue(TEXT("Retail DAT available"),Dat->LoadDatDirectory(DatDirectory)))return false;
    S.KnownSpells={1,7,13,19,25,31,37,43,49,55,61,67};
    TestTrue(TEXT("Known spell binds"),Deck->BindSpell(12,1));
    TestFalse(TEXT("Unknown spell cannot bind"),Deck->BindSpell(13,999999));
    const auto Theme=FACEPluginStyle::Use(Host,TEXT("actiondeck"));
    auto* Resources=C->GetUIResourceResolver();
    auto* Composed=Cast<UTexture2D>(Theme->SpellImage(1)->GetResourceObject());
    auto* Raw=Resources->ResolveIconTexture(Deck->Slot(12).Item.Icon);
    TestTrue(TEXT("Deck uses the native spell compositor"),Composed&&Composed==Resources->ResolveSpellIcon(1)&&Composed!=Raw);
    if(Raw&&Composed)
    {
        auto& RawMip=Raw->GetPlatformData()->Mips[0];auto& FinalMip=Composed->GetPlatformData()->Mips[0];
        const auto* RawPixels=static_cast<const FColor*>(RawMip.BulkData.LockReadOnly());
        const auto* FinalPixels=static_cast<const FColor*>(FinalMip.BulkData.LockReadOnly());
        int32 Mask=0,Changed=0;
        for(int32 P=0;P<1024;++P)if(RawPixels[P]==FColor::White){++Mask;Changed+=FinalPixels[P]!=FColor::White;}
        RawMip.BulkData.Unlock();FinalMip.BulkData.Unlock();
        TestTrue(TEXT("Spell white surround is replaced with retail artwork"),Mask>20&&Changed==Mask);
    }
    const auto& ItemArt=Deck->Slot(24).Item;
    TestTrue(TEXT("Item background matches inventory with its underlay"),
        Theme->ItemImage(ItemArt,false)->GetResourceObject()==Resources->ResolveItemBackground(ItemArt.ItemType,ItemArt.Underlay));
    TestTrue(TEXT("Item foreground matches inventory with overlays and enchantment effects"),
        Theme->ItemImage(ItemArt,true)->GetResourceObject()==Resources->ResolveItemForeground(ItemArt.Icon,ItemArt.Overlay,ItemArt.Effects));
    S.PlayerVitals.CombatMode=int32(ACECombatMode::NonCombat);S.PlayerVitals.StatQualityInts.Add(40,S.PlayerVitals.CombatMode);
    Deck->Refresh(true);TestFalse(TEXT("Spell is disabled in peace"),Deck->Availability(12).Ready);
    S.PlayerVitals.CombatMode=int32(ACECombatMode::Magic);S.PlayerVitals.StatQualityInts.Add(40,S.PlayerVitals.CombatMode);
    Deck->Refresh(true);TestTrue(TEXT("Spell becomes ready in confirmed magic stance"),Deck->Availability(12).Ready);
    S.PendingCombatMode=int32(ACECombatMode::Magic);Deck->Refresh(true);
    TestFalse(TEXT("Pending stance does not enable casting early"),Deck->Availability(12).Ready);S.PendingCombatMode=0;
    Deck->BindAttack(25,false,3);Deck->BindAttack(26,true,1);
    Deck->Refresh(true);TestFalse(TEXT("Physical attacks disabled in magic stance"),Deck->Availability(25).Ready);
    S.PlayerVitals.CombatMode=int32(ACECombatMode::Melee);S.PlayerVitals.StatQualityInts.Add(40,S.PlayerVitals.CombatMode);
    Deck->Refresh(true);
    TestTrue(TEXT("Melee enabled only in melee stance"),Deck->Availability(25).Ready);
    TestFalse(TEXT("Missile disabled in melee stance"),Deck->Availability(26).Ready);

    const auto Key=Deck->EffectiveKey(24);Deck->SwapSlots(24,35);
    TestEqual(TEXT("Swapping actions keeps physical slot keys"),Deck->EffectiveKey(24),Key);
    TestEqual(TEXT("Swapping moves item bindings"),Deck->Slot(35).Guid,Kit.Guid);
    Deck->SwapSlots(24,35);Deck->Override=true;
    Deck->HideSpellBar=Deck->HideCombatBar=Deck->ShowTargetTimers=true;
    const auto Persisted=Deck->ExportState();Deck->Reset();Deck->Deserialize(Persisted);
    TestTrue(TEXT("Override survives save/load"),Deck->Override);
    TestTrue(TEXT("Hotbar and target timer options survive save/load"),Deck->HideSpellBar&&Deck->HideCombatBar&&Deck->ShowTargetTimers);
    TestEqual(TEXT("Item survives save/load"),Deck->Slot(24).Item.Name,Kit.Name);
    TestTrue(TEXT("Backpack self action survives save/load"),Deck->Slot(23).Action==EACEDeckAction::Self);
    TestEqual(TEXT("Custom key survives save/load"),Deck->EffectiveKey(0),FInputChord(EKeys::F8,true,true,false,false));
    TestEqual(TEXT("Row modifiers survive save/load"),Deck->RowModifiers[0],uint8(4));
    TestTrue(TEXT("Atomic state save succeeds"),Deck->Save());
    TestTrue(TEXT("State file exists under character/server scope"),FPaths::FileExists(Deck->StatePath));
    S.PlayerGuid=1235;Deck->EnsureCharacter();
    TestTrue(TEXT("Another character starts with empty slots"),Deck->Slot(24).Action==EACEDeckAction::Empty);
    TestFalse(TEXT("Override does not leak between characters"),Deck->Override);
    TestFalse(TEXT("Display options do not leak between characters"),Deck->HideSpellBar||Deck->HideCombatBar||Deck->ShowTargetTimers);
    S.PlayerGuid=Self.Guid;Deck->EnsureCharacter();
    TestEqual(TEXT("Returning to the character restores its deck"),Deck->Slot(24).Item.Name,Kit.Name);
    Deck->HideSpellBar=Deck->HideCombatBar=Deck->ShowTargetTimers=false;

    for(const auto& P:Host->Plugins)P->Enabled=P->Id==TEXT("actiondeck");
    Host->DesktopDock=SNew(SACEPluginDesktop).Host(Host);
    Host->DesktopDock->Toggle(TEXT("actiondeck"));
    TestTrue(TEXT("Open enabled deck with Override captures keys"),Deck->IsOverrideActive());
    auto& Slate=FSlateApplication::Get();const auto OldFocus=Slate.GetKeyboardFocusedWidget();
    auto Window=SNew(SVirtualWindow).Size(FVector2D(900,700));Window->SetIsFocusable(true);
    auto ViewportContent=SNew(SOverlay);
    auto* Viewport=NewObject<UGameViewportClient>(GEngine);
    Viewport->Init(Context,GI,false);
    Context.GameViewport=Viewport;LP->ViewportClient=Viewport;
    // Offscreen fixture: use a virtual window without binding a scene viewport.
    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    Viewport->SetViewportOverlayWidget(Window,ViewportContent);
    PRAGMA_ENABLE_DEPRECATION_WARNINGS
    Canvas->SetOwningPlayer(PC);Canvas->SetResourceResolver(Resources);
    TestTrue(TEXT("Native canvas belongs to the fixture's local controller"),Canvas->GetOwningPlayer()==PC);
    TestTrue(TEXT("Controller resolves the fixture viewport"),PC->GetGameInstance()->GetGameViewportClient()==Viewport);
    Binder->Manager=C->GetUIElementManager();
    Viewport->AddViewportWidgetContent(Canvas->TakeWidget(),0);
    Viewport->AddViewportWidgetContent(Host->DesktopDock.ToSharedRef(),1500);
    TSharedPtr<SEditableTextBox> Entry;
    TSharedPtr<SBorder> Cover;
    auto Content=SNew(SVerticalBox)+SVerticalBox::Slot().FillHeight(1)
        [SNew(SOverlay)+SOverlay::Slot()[ViewportContent]
            +SOverlay::Slot()[SAssignNew(Cover,SBorder).Visibility(EVisibility::Collapsed)]]
        +SVerticalBox::Slot().AutoHeight()[SAssignNew(Entry,SEditableTextBox)];
    Window->SetContent(Content);Slate.RegisterVirtualWindow(Window);
    FWidgetRenderer Renderer(false,true);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(900,700),TF_Bilinear,true);
    ON_SCOPE_EXIT {Slate.ReleaseAllPointerCapture();Slate.ClearKeyboardFocus();if(OldFocus)Slate.SetKeyboardFocus(OldFocus);
        Canvas->HideDragIcon();Viewport->RemoveAllViewportWidgets();LP->ViewportClient=nullptr;Context.GameViewport=nullptr;
        Slate.UnregisterVirtualWindow(Window);Target->ReleaseResource();};
    auto Frame=[&]{++GFrameCounter;Renderer.DrawWindow(Target,Window->GetHittestGrid(),Window,1.f,FVector2D(900,700),.016f);FlushRenderingCommands();};
    Frame();Frame();
    auto Probe=MakeShared<FDeckInputProbe>();Slate.RegisterInputPreProcessor(Probe);
    ON_SCOPE_EXIT{Slate.UnregisterInputPreProcessor(Probe);};
    Slate.ProcessKeyDownEvent(FKeyEvent(EKeys::Nine,FModifierKeysState(),0,false,0,0));
    TestEqual(TEXT("Empty deck binding reaches downstream input"),Probe->Keys,1);
    Deck->Override=false;Slate.ProcessKeyDownEvent(FKeyEvent(EKeys::Nine,FModifierKeysState(),0,false,0,0));
    TestEqual(TEXT("Override off lets the original input pipeline receive keys"),Probe->Keys,2);Deck->Override=true;
    TestFalse(TEXT("Empty slot leaves native hotbar shortcut available"),Deck->HandleKey(Deck->EffectiveKey(34),false));
    TestTrue(TEXT("Repeated key is consumed without a second action"),Deck->HandleKey(Deck->EffectiveKey(24),true));
    TestFalse(TEXT("Extra modifier cannot trigger a plain binding"),Deck->HandleKey(FInputChord(EKeys::One,true,true,true,false),false));
    Slate.SetKeyboardFocus(Entry);
    TestTrue(TEXT("Typing fixture has text focus"),ACEInputBindings::IsTextEntryFocused());
    TestFalse(TEXT("Typing never triggers the deck"),Deck->HandleKey(Deck->EffectiveKey(24),false));
    Slate.ClearKeyboardFocus();Deck->ConfigOpen=true;
    TestTrue(TEXT("Settings keep native hotbars blocked"),Deck->HandleKey(Deck->EffectiveKey(24),false));Deck->ConfigOpen=false;
    Deck->Override=false;TestFalse(TEXT("Override off restores native hotbars"),Deck->HandleKey(Deck->EffectiveKey(24),false));Deck->Override=true;
    Host->DesktopDock->Toggle(TEXT("actiondeck"));
    TestFalse(TEXT("Closing deck restores native hotbars"),Deck->HandleKey(Deck->EffectiveKey(24),false));
    Host->DesktopDock->Toggle(TEXT("actiondeck"));
    (*Plugin)->Enabled=false;TestFalse(TEXT("Disabling plugin restores native hotbars"),Deck->HandleKey(Deck->EffectiveKey(24),false));(*Plugin)->Enabled=true;
    Frame();Frame();
    TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>,FName)> Find;
    Find=[&](TSharedRef<SWidget> Widget,FName Tag)->TSharedPtr<SWidget>
    {
        if(Widget->GetTag()==Tag)return Widget;
        auto* Children=Widget->GetChildren();for(int32 I=0;I<Children->Num();++I)if(auto Found=Find(Children->GetChildAt(I),Tag))return Found;
        return nullptr;
    };
    auto Grid=Find(Content,TEXT("ActionDeck.Grid"));
    if(!TestTrue(TEXT("Deck grid exists"),Grid.IsValid()))return false;
    auto Point=[&](int32 I){const auto& G=Grid->GetCachedGeometry();return G.LocalToAbsolute(FVector2D((I%12+.5)*G.GetLocalSize().X/12,(Deck->DisplayRow(I)+.5)*G.GetLocalSize().Y/Deck->VisibleRows()));};
    {
        auto ReadPixels=[&]
        {
            TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);return Pixels;
        };
        // Use the actual viewport stack: native canvas, plugin windows, decorator.
        // Both source types must remain visible over the plugin and not steal drops.
        for(bool Spell:{false,true})
        {
            const FVector2D At=Point(30);
            const FVector2D Local=Canvas->GetCachedGeometry().AbsoluteToLocal(At);
            Frame();const auto Before=ReadPixels();
            if(Spell)
            {
                Binder->SpellDragId=1;Binder->SpellDragSourceBarSlot=INDEX_NONE;
                Binder->bSpellDragPending=Binder->bSpellDragActive=true;Binder->UpdateSpellDrag(Local);
            }
            else
            {
                Binder->InvDragGuid=Spare.Guid;Binder->InvDragIconDid=Spare.IconId;
                Binder->bInvDragPending=Binder->bInvDragActive=true;Binder->UpdateInventoryDrag(Local);
            }
            Frame();Frame();const auto During=ReadPixels();
            TArray64<uint8> DragPNG;FImageUtils::PNGCompressImageArray(900,700,During,DragPNG);
            FFileHelper::SaveArrayToFile(DragPNG,*(FPaths::ProjectSavedDir()/
                (Spell?TEXT("ActionDeck-SpellDrag.png"):TEXT("ActionDeck-ItemDrag.png"))));
            int32 Changed=0;
            for(int32 Y=int32(At.Y)-14;Y<int32(At.Y)+14;++Y)
                for(int32 X=int32(At.X)-14;X<int32(At.X)+14;++X)
                {
                    const auto A=Before[Y*900+X],B=During[Y*900+X];
                    Changed+=FMath::Max3(FMath::Abs(int32(A.R)-B.R),FMath::Abs(int32(A.G)-B.G),FMath::Abs(int32(A.B)-B.B))>30;
                }
            TestTrue(Spell?TEXT("Spell drag preview paints above Action Deck"):TEXT("Item drag preview paints above Action Deck"),Changed>100);
            auto* Icon=Spell?Binder->SpellDragIcon.Get():Binder->InvDragIcon.Get();
            TestNull(TEXT("Desktop drag preview is not duplicated inside the native canvas"),Icon->GetParent());
            if(Spell)
                TestTrue(TEXT("Foreground spell preview does not intercept the drop"),Binder->TryFinishSpellDrag(Local));
            else
                TestTrue(TEXT("Foreground item preview does not intercept the drop"),Binder->TryFinishInventoryDrag(Local));
            Deck->ClearSlot(30);Frame();Frame();
            const auto After=ReadPixels();int32 Remaining=0;
            for(int32 Y=int32(At.Y)-14;Y<int32(At.Y)+14;++Y)
                for(int32 X=int32(At.X)-14;X<int32(At.X)+14;++X)
                    Remaining+=Before[Y*900+X]!=After[Y*900+X];
            TestEqual(TEXT("Dropping removes the foreground preview"),Remaining,0);
            TestTrue(TEXT("Released preview is hidden"),Icon->GetVisibility()==ESlateVisibility::Collapsed);
        }
        // A desktop without a viewport (and world-space VR) retains its native
        // canvas preview rather than losing the drag ghost altogether.
        Context.GameViewport=nullptr;
        Binder->InvDragGuid=Spare.Guid;Binder->InvDragIconDid=Spare.IconId;
        Binder->bInvDragPending=Binder->bInvDragActive=true;Binder->UpdateInventoryDrag(FVector2D(80,90));
        TestTrue(TEXT("Canvas-only drag preview remains attached"),Binder->InvDragIcon->GetParent()==Canvas->GetElementLayer());
        auto* Slot=Cast<UCanvasPanelSlot>(Binder->InvDragIcon->Slot);
        TestTrue(TEXT("Canvas-only preview retains cursor position"),Slot&&Slot->GetPosition().Equals(FVector2D(64,74)));
        Binder->CancelInventoryDrag();Context.GameViewport=Viewport;
    }
    {
        const auto SavedDeck=Deck->ExportState();
        auto PathAt=[&](FVector2D At){return Slate.LocateWindowUnderMouse(At,{Window},false,0);};
        auto Press=[&](int32 Index)
        {
            const auto At=Point(Index);
            Slate.RoutePointerDownEvent(PathAt(At),FPointerEvent(0,At,At,{EKeys::LeftMouseButton},
                EKeys::LeftMouseButton,0,FModifierKeysState()));
            TestTrue(TEXT("Deck slot captures the pointer"),Grid->HasMouseCapture());
        };
        auto Move=[&](FVector2D At)
        {
            Slate.RoutePointerMoveEvent(PathAt(At),FPointerEvent(0,At,At,{EKeys::LeftMouseButton},
                EKeys::Invalid,0,FModifierKeysState()),false);
            Frame();Frame();
        };
        auto Release=[&](FVector2D At)
        {
            Slate.RoutePointerUpEvent(PathAt(At),FPointerEvent(0,At,At,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
            Frame();
            TestFalse(TEXT("Drop releases pointer capture"),Grid->HasMouseCapture());
            TestFalse(TEXT("Drop removes the moving icon"),Find(ViewportContent,TEXT("ActionDeck.DragPreview")).IsValid());
        };
        auto Pixels=[&]
        {
            TArray<FColor> Result;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Result,Flags);return Result;
        };
        auto CheckPreview=[&](FVector2D At,const FString& Screenshot)
        {
            auto Preview=Find(ViewportContent,TEXT("ActionDeck.DragPreview"));
            if(!TestTrue(TEXT("Dragging creates a foreground icon"),Preview.IsValid()))return;
            TestTrue(TEXT("Drag icon cannot intercept the destination"),Preview->GetVisibility()==EVisibility::HitTestInvisible);
            const auto During=Pixels();
            Preview->SetVisibility(EVisibility::Collapsed);Frame();const auto Without=Pixels();
            Preview->SetVisibility(EVisibility::HitTestInvisible);Frame();
            int32 Changed=0;
            for(int32 Y=int32(At.Y)-14;Y<int32(At.Y)+14;++Y)
                for(int32 X=int32(At.X)-14;X<int32(At.X)+14;++X)
                {
                    const auto A=During[Y*900+X],B=Without[Y*900+X];
                    Changed+=FMath::Max3(FMath::Abs(int32(A.R)-B.R),FMath::Abs(int32(A.G)-B.G),FMath::Abs(int32(A.B)-B.B))>30;
                }
            TestTrue(TEXT("Composed icon is rendered at the pointer above the window"),Changed>100);
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(900,700,During,PNG);
            FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/Screenshot));
        };
        const FVector2D Outside(850,600);
        TestFalse(TEXT("Removal test ends outside the whole plugin window"),Host->IsPointInsidePluginWindow(TEXT("actiondeck"),Outside));
        for(bool Spell:{false,true})
        {
            if(Spell)Deck->BindSpell(28,1);else Deck->BindItem(28,Spare.Guid);
            Deck->ClearSlot(29);
            const auto SourceKey=Deck->EffectiveKey(28),TargetKey=Deck->EffectiveKey(29);
            Press(28);Move(Point(29));
            CheckPreview(Point(29),Spell?TEXT("ActionDeck-MoveSpell.png"):TEXT("ActionDeck-MoveItem.png"));
            TestTrue(TEXT("Action stays bound until the drag is committed"),Deck->Slot(28).Action!=EACEDeckAction::Empty);
            Release(Point(29));
            TestTrue(TEXT("Moving to empty slot clears the old slot"),Deck->Slot(28).Action==EACEDeckAction::Empty);
            TestTrue(TEXT("Moving transfers the chosen action"),Spell?Deck->Slot(29).Spell==1:Deck->Slot(29).Guid==Spare.Guid);
            TestTrue(TEXT("Moving preserves physical slot shortcuts"),Deck->EffectiveKey(28)==SourceKey&&Deck->EffectiveKey(29)==TargetKey);
            Press(29);Move(Outside);
            CheckPreview(Outside,Spell?TEXT("ActionDeck-RemoveSpell.png"):TEXT("ActionDeck-RemoveItem.png"));
            Release(Outside);
            TestTrue(TEXT("Dropping outside removes the action"),Deck->Slot(29).Action==EACEDeckAction::Empty);
            TestTrue(TEXT("Removing preserves the slot key"),Deck->EffectiveKey(29)==TargetKey);
            TestEqual(TEXT("Removing a shortcut never drops its inventory item"),S.WorldObjects[Spare.Guid].ContainerId,Pack.Guid);
        }
        Deck->BindItem(28,Spare.Guid);Deck->BindSpell(29,1);
        Press(28);Move(Point(29));Release(Point(29));
        TestTrue(TEXT("Dropping on an occupied slot swaps both actions"),Deck->Slot(28).Spell==1&&Deck->Slot(29).Guid==Spare.Guid);
        const auto& WindowG=Host->DesktopDock->Windows[TEXT("actiondeck")].Widget->GetCachedGeometry();
        const auto Chrome=WindowG.LocalToAbsolute(FVector2D(10,10));
        TestTrue(TEXT("Title belongs to the deck window"),Host->IsPointInsidePluginWindow(TEXT("actiondeck"),Chrome));
        Press(29);Move(Chrome);Release(Chrome);
        TestEqual(TEXT("Dropping on the title restores the item"),Deck->Slot(29).Guid,Spare.Guid);
        Press(29);Move(Outside);
        Slate.ReleaseAllPointerCapture();Frame();
        TestEqual(TEXT("Losing capture cancels removal"),Deck->Slot(29).Guid,Spare.Guid);
        TestFalse(TEXT("Canceled drag removes the decorator"),Find(ViewportContent,TEXT("ActionDeck.DragPreview")).IsValid());
        // Empty slots cannot move or activate another shortcut when released.
        Deck->ClearSlot(28);Press(28);Move(Point(29));Release(Point(29));
        TestEqual(TEXT("Dragging an empty slot leaves destination intact"),Deck->Slot(29).Guid,Spare.Guid);
        // Hidden chrome and scaled cells must not change the drop boundary.
        Host->SetPluginWindowPinned(TEXT("actiondeck"),true);Frame();Frame();
        Press(29);Move(Point(28));Release(Point(28));
        TestEqual(TEXT("Locked deck still allows slot rearrangement"),Deck->Slot(28).Guid,Spare.Guid);
        Press(28);Move(Outside);Release(Outside);
        TestTrue(TEXT("Locked deck allows removing shortcuts"),Deck->Slot(28).Action==EACEDeckAction::Empty);
        Host->SetPluginWindowPinned(TEXT("actiondeck"),false);
        Deck->Deserialize(SavedDeck);Deck->Save();Deck->Refresh(true);Frame();Frame();
    }
    TestTrue(TEXT("Drop onto scaled grid reaches the last slot"),Deck->DropAt(Point(35),Spare.Guid));
    TestEqual(TEXT("Dropped binding is in the right slot"),Deck->Slot(35).Guid,Spare.Guid);
    TestFalse(TEXT("Drop outside bar is left to native UI"),Deck->DropAt(FVector2D(-1000,-1000),Spare.Guid));
    Cover->SetVisibility(EVisibility::Visible);Frame();
    TestFalse(TEXT("A covering window prevents drops through to the deck"),Deck->DropAt(Point(35),Spare.Guid));
    Cover->SetVisibility(EVisibility::Collapsed);Frame();
    Binder->InvDragGuid=Spare.Guid;Binder->bInvDragActive=Binder->bInvDragPending=true;
    TestTrue(TEXT("Native inventory drag is consumed"),Binder->TryFinishInventoryDrag(Canvas->GetCachedGeometry().AbsoluteToLocal(Point(34))));
    TestEqual(TEXT("Native drag makes a binding"),Deck->Slot(34).Guid,Spare.Guid);
    TestEqual(TEXT("Binding never moves the inventory item"),S.WorldObjects[Spare.Guid].ContainerId,Pack.Guid);
    Binder->SpellDragId=1;Binder->SpellDragSourceBarSlot=INDEX_NONE;Binder->bSpellDragActive=Binder->bSpellDragPending=true;
    TestTrue(TEXT("Native spellbook drop is consumed"),Binder->TryFinishSpellDrag(Canvas->GetCachedGeometry().AbsoluteToLocal(Point(33))));
    TestEqual(TEXT("Native spellbook drop binds the spell"),Deck->Slot(33).Spell,1);
    Binder->InvDragGuid=Self.Guid;Binder->bInvDragActive=Binder->bInvDragPending=true;
    TestTrue(TEXT("Native main backpack drop is consumed"),Binder->TryFinishInventoryDrag(Canvas->GetCachedGeometry().AbsoluteToLocal(Point(23))));
    TestTrue(TEXT("Native backpack drop binds self selection"),Deck->Slot(23).Action==EACEDeckAction::Self);
    // Exercise network dispatch against a private loopback socket, never a game server.
    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);auto Address=Sockets->CreateInternetAddr();
    bool IPValid=false;Address->SetIp(TEXT("127.0.0.1"),IPValid);Address->SetPort(0);
    auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Action Deck receiver"),false);
    if(!TestTrue(TEXT("Loopback socket binds"),Receiver&&Receiver->Bind(*Address)))return false;
    Receiver->GetAddress(*Address);S.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Action Deck sender"),false);
    S.SocketC2S->SetNonBlocking(true);S.ServerC2SAddr=Address;S.IssacClient=MakeUnique<FACEIsaac>(123u);
    ON_SCOPE_EXIT{S.SocketC2S->Close();Sockets->DestroySocket(S.SocketC2S);S.SocketC2S=nullptr;Receiver->Close();Sockets->DestroySocket(Receiver);S.ServerC2SAddr.Reset();};
    auto UseTarget=[&]()->int32
    {
        for(const auto& Pair:S.CachedC2SPackets)
        {
            FACEBinaryReader R(Pair.Value.Payload);R.Skip(16);
            if(R.ReadUInt32()!=ACEOpcode::GameAction)continue;R.ReadUInt32();
            if(R.ReadUInt32()!=ACEGameAction::UseWithTarget)continue;R.ReadInt32();return R.ReadInt32();
        }
        return 0;
    };
    S.SelectedObject.Guid=Monster.Guid;S.SelectedObject.bValid=true;S.bUseBusy=false;S.CachedC2SPackets.Reset();
    TestTrue(TEXT("Healing kit dispatches with monster selected"),Deck->Activate(24));
    TestEqual(TEXT("Heal packet targets self"),UseTarget(),Self.Guid);
    TestEqual(TEXT("Healing does not change combat selection"),S.SelectedObject.Guid,Monster.Guid);
    S.SelectedObject.Guid=Friend.Guid;S.bUseBusy=false;S.CachedC2SPackets.Reset();
    TestTrue(TEXT("Healing kit dispatches on another player"),Deck->Activate(24));
    TestEqual(TEXT("Heal packet targets selected player"),UseTarget(),Friend.Guid);
    auto ClickSlot=[&](int32 Index)
    {
        Frame();const auto At=Point(Index);
        auto Path=Slate.LocateWindowUnderMouse(At,{Window},false,0);
        Slate.RoutePointerDownEvent(Path,FPointerEvent(0,At,At,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        Slate.RoutePointerUpEvent(Path,FPointerEvent(0,At,At,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
    };
    for(int32 Selected:{0,Monster.Guid,Friend.Guid,Spare.Guid,Self.Guid})
    {
        S.SelectedObject.Guid=Selected;S.bUseBusy=false;S.CachedC2SPackets.Reset();
        ClickSlot(24);
        TestEqual(TEXT("Single kit click sends healing to a friendly player or defaults to self"),UseTarget(),Selected==Friend.Guid?Friend.Guid:Self.Guid);
        TestEqual(TEXT("Single kit click preserves the selected object"),S.SelectedObject.Guid,Selected);
        TestEqual(TEXT("Single kit click does not leave a targeting cursor"),Binder->PendingUseWithSourceGuid,0);
    }
    S.SelectedObject.Guid=Monster.Guid;S.bUseBusy=true;S.CachedC2SPackets.Reset();
    ClickSlot(23);
    TestEqual(TEXT("Backpack click selects self even while busy"),S.SelectedObject.Guid,Self.Guid);
    TestEqual(TEXT("Selecting self does not use a kit"),UseTarget(),0);
    S.SelectedObject.Guid=Monster.Guid;S.bUseBusy=false;S.CachedC2SPackets.Reset();
    Binder->PendingUseWithSourceGuid=Spare.Guid;
    ClickSlot(23);
    TestEqual(TEXT("Backpack click completes pending kit use on self"),UseTarget(),Self.Guid);
    TestEqual(TEXT("Completing pending kit use preserves combat selection"),S.SelectedObject.Guid,Monster.Guid);
    S.bUseBusy=false;S.CachedC2SPackets.Reset();
    TestTrue(TEXT("Backpack hotkey is handled"),Deck->HandleKey(Deck->EffectiveKey(23),false));
    TestEqual(TEXT("Backpack hotkey selects self"),S.SelectedObject.Guid,Self.Guid);
    {
        // Real controller events and native polling must agree with Slate.
        // A downstream probe alone cannot detect a native item being used.
        const auto SavedDeck=Deck->ExportState();
        FACEWorldObject NativeItem;NativeItem.Guid=900;NativeItem.ContainerId=Self.Guid;
        NativeItem.Name=TEXT("Native hotbar food");NativeItem.ItemType=ACEItemType::Food;NativeItem.ItemUseable=8;
        S.WorldObjects.Add(NativeItem.Guid,NativeItem);
        S.ShortcutObjects.SetNumZeroed(18);
        for(int32 I=0;I<3;++I)S.ShortcutObjects[I]=NativeItem.Guid;
        Deck->RowModifiers[0]=1;Deck->RowModifiers[1]=2;Deck->RowModifiers[2]=0;
        for(auto& Slot:Deck->Slots){Slot.CustomKey=false;Slot.Key=FInputChord();}
        for(int32 I:{0,12,24})Deck->BindItem(I,Spare.Guid);
        const TArray<FInputChord> Keys={FInputChord(EKeys::One),FInputChord(EKeys::One,false,true,false,false),FInputChord(EKeys::One,true,false,false,false)};
        ACEInputBindings::BeginEdit();
        for(int32 I=0;I<3;++I)ACEInputBindings::Set(ACEInputBindings::Shortcut(I),0,Keys[I]);
        ACEInputBindings::Commit();
        auto CountAction=[&](uint32 Opcode,int32 Object)
        {
            int32 Count=0;
            for(const auto& Packet:S.CachedC2SPackets)
            {
                FACEBinaryReader R(Packet.Value.Payload);R.Skip(16);
                if(R.ReadUInt32()!=ACEOpcode::GameAction)continue;R.ReadUInt32();
                if(R.ReadUInt32()==Opcode&&R.ReadInt32()==Object)++Count;
            }
            return Count;
        };
        auto BeginInput=[&]
        {
            PC->PlayerInput->FlushPressedKeys();PC->PlayerInput->ProcessInputStack({},.016f,false);
            S.bUseBusy=false;S.CachedC2SPackets.Reset();Binder->CancelPendingUseWith();
            S.PlayerVitals.CombatMode=Binder->CombatMode=int32(ACECombatMode::NonCombat);
            S.PlayerVitals.StatQualityInts.Add(40,S.PlayerVitals.CombatMode);
        };
        auto Poll=[&]{PC->PlayerInput->ProcessInputStack({},.016f,false);Binder->PollKeyboardActions(PC);};
        BeginInput();Deck->Override=false;
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::One,IE_Pressed,1.f));Poll();
        TestEqual(TEXT("Control: override off actually uses the native hotbar item"),CountAction(ACEGameAction::Use,NativeItem.Guid),1);
        Deck->Override=true;BeginInput();
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::One,IE_Pressed,1.f));Poll();
        TestEqual(TEXT("Controller input dispatches the deck item once"),CountAction(ACEGameAction::UseWithTarget,Spare.Guid),1);
        TestEqual(TEXT("Controller input cannot also use the native item"),CountAction(ACEGameAction::Use,NativeItem.Guid),0);
        S.bUseBusy=false;
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::One,IE_Repeat,1.f));Poll();
        TestEqual(TEXT("Controller repeat never repeats the action"),CountAction(ACEGameAction::UseWithTarget,Spare.Guid),1);
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::One,IE_Released,0.f));Poll();
        TestFalse(TEXT("Consumed controller shortcut is not left held"),PC->IsInputKeyDown(EKeys::One));
        for(int32 I=0;I<Keys.Num();++I)
        {
            BeginInput();
            const auto& K=Keys[I];
            Slate.ProcessKeyDownEvent(FKeyEvent(K.Key,FModifierKeysState(K.bShift,false,K.bCtrl,false,false,false,false,false,false),0,false,0,0));
            Poll();
            TestEqual(TEXT("Each row dispatches its item through Slate once"),CountAction(ACEGameAction::UseWithTarget,Spare.Guid),1);
            TestEqual(TEXT("Slate dispatch never uses native food"),CountAction(ACEGameAction::Use,NativeItem.Guid),0);
            // Simulate input already queued before the override gained control.
            BeginInput();
            if(K.bCtrl)PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftControl,IE_Pressed,1.f));
            if(K.bShift)PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftShift,IE_Pressed,1.f));
            PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(K.Key,IE_Pressed,1.f));Poll();
            TestFalse(TEXT("Reserved chord cannot reach polled native shortcuts"),ACEInputBindings::Pressed(PC,ACEInputBindings::Shortcut(I)));
            TestEqual(TEXT("Queued input cannot dispatch a native item"),CountAction(ACEGameAction::Use,NativeItem.Guid),0);
            ACEInputBindings::SetCombatContext(int32(ACECombatMode::Magic));
            TestFalse(TEXT("Reserved chord cannot reach polled native spells"),ACEInputBindings::Pressed(PC,ACEInputBindings::SpellSlot(0)));
        }
        BeginInput();Deck->ConfigOpen=true;
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::One,IE_Pressed,1.f));Poll();
        TestTrue(TEXT("Settings pause actions while still blocking native hotbars"),S.CachedC2SPackets.IsEmpty());
        Deck->CapturingKey=true;BeginInput();
        TestFalse(TEXT("Key capture receives its own Slate events"),Deck->HandleKey(Keys[0],false));
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::One,IE_Pressed,1.f));Poll();
        TestTrue(TEXT("Capture key-down cannot leak to native hotbars"),S.CachedC2SPackets.IsEmpty());
        Deck->ConfigOpen=Deck->CapturingKey=false;
        for(bool Empty:{false,true})
        {
            BeginInput();S.WorldObjects[Spare.Guid].Structure=0;
            if(Empty)Deck->ClearSlot(24);
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::One,IE_Pressed,1.f));Poll();
            TestEqual(TEXT("Only empty slots fall through to the native item"),CountAction(ACEGameAction::Use,NativeItem.Guid),Empty?1:0);
            TestEqual(TEXT("Empty or depleted slot cannot dispatch a heal"),CountAction(ACEGameAction::UseWithTarget,Spare.Guid),0);
        }
        S.WorldObjects[Spare.Guid]=Spare;Deck->BindItem(24,Spare.Guid);
        Deck->SetKey(24,FInputChord(EKeys::F7));
        ACEInputBindings::BeginEdit();ACEInputBindings::Set(ACEInputBindings::Shortcut(0),0,FInputChord(EKeys::F7));ACEInputBindings::Commit();
        BeginInput();PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::F7,IE_Pressed,1.f));Poll();
        TestEqual(TEXT("Custom deck keys take precedence too"),CountAction(ACEGameAction::UseWithTarget,Spare.Guid),1);
        TestEqual(TEXT("Custom key cannot use native item"),CountAction(ACEGameAction::Use,NativeItem.Guid),0);
        BeginInput();PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));Poll();
        TestTrue(TEXT("Unclaimed gameplay keys still reach native input"),ACEInputBindings::Down(PC,EKeys::W));
        BeginInput();Binder->UseShortcutSlot(1);
        TestEqual(TEXT("Explicit native hotbar clicks still work with override"),CountAction(ACEGameAction::Use,NativeItem.Guid),1);
        BeginInput();S.ShortcutObjects.Reset();S.WorldObjects.Remove(NativeItem.Guid);
        Deck->Deserialize(SavedDeck);
        S.PlayerVitals.CombatMode=Binder->CombatMode=int32(ACECombatMode::Melee);
        S.PlayerVitals.StatQualityInts.Add(40,S.PlayerVitals.CombatMode);
    }
    S.bUseBusy=false;S.CachedC2SPackets.Reset();
    TestFalse(TEXT("Wrong-stance spell cannot dispatch"),Deck->Activate(12));
    TestTrue(TEXT("Wrong-stance spell sends no packets"),S.CachedC2SPackets.IsEmpty());
    S.SpellBars.SetNum(8);S.SpellBars[0]={7,1,13};
    S.SendRemoveSpellFromBar(1,0);
    Binder->SpellDragId=1;Binder->SpellDragSourceBarSlot=1;Binder->SpellDragSourceBar=0;
    Binder->bSpellDragActive=Binder->bSpellDragPending=true;
    TestTrue(TEXT("Native spell-bar drop copies to deck"),Binder->TryFinishSpellDrag(Canvas->GetCachedGeometry().AbsoluteToLocal(Point(32))));
    TestTrue(TEXT("Copying restores the native spell bar order"),S.SpellBars[0]==TArray<int32>{7,1,13});
    Binder->CombatMode=int32(ACECombatMode::Melee);
    TestTrue(TEXT("Melee slot requests the native charge/attack flow"),Deck->Activate(25));
    TestEqual(TEXT("Melee slot preserves high attack height"),Binder->CombatAttackHeight,uint32(3));
    TestTrue(TEXT("Native attack is queued once"),Binder->bCombatAttackRequestPending);
    Binder->CancelCombatAttack();
    Deck->RowModifiers[0]=1;Deck->RowModifiers[1]=2;Deck->RowModifiers[2]=0;
    for(auto& Slot:Deck->Slots){Slot.CustomKey=false;Slot.Key=FInputChord();}
    for(int32 I=0;I<12;++I)Deck->BindSpell(I,S.KnownSpells[I]);
    S.PlayerVitals.CombatMode=int32(ACECombatMode::Magic);S.PlayerVitals.StatQualityInts.Add(40,S.PlayerVitals.CombatMode);
    Deck->Notice.Empty();Deck->Refresh(true);Frame();Frame();
    auto Screenshot=[&](const TCHAR* Name)
    {
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);TArray64<uint8> PNG;
        FImageUtils::PNGCompressImageArray(900,700,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/Name));
    };
    {
        const auto BeforeMacros=Deck->ExportState();
        const FString Script=TEXT("/hom\n/wait 30000\n/f \"I'm home.\"");
        auto CountAction=[&](uint32 Opcode)
        {
            int32 Count=0;
            for(const auto& Pair:S.CachedC2SPackets)
            {FACEBinaryReader R(Pair.Value.Payload);R.Skip(16);
                if(R.ReadUInt32()!=ACEOpcode::GameAction)continue;R.ReadUInt32();
                if(R.ReadUInt32()==Opcode)++Count;}
            return Count;
        };
        S.CachedC2SPackets.Reset();
        TestTrue(TEXT("Macro can replace a slot and keep its current hotkey"),Deck->BindMacro(24,TEXT("Home"),0x06004CF7,Script,0,Deck->EffectiveKey(24)));
        TestEqual(TEXT("Saving a macro never executes it"),S.CachedC2SPackets.Num(),0);
        auto Saved=Deck->ExportState();Deck->ClearSlot(24);Deck->Deserialize(Saved);
        TestEqual(TEXT("Multiline script survives persistence"),Deck->Slot(24).MacroText,Script);
        TestEqual(TEXT("Chosen icon survives persistence"),Deck->Slot(24).Item.Icon,0x06004CF7);
        TestEqual(TEXT("Macro keeps default-key inheritance"),Deck->Slot(24).CustomKey,false);
        TestFalse(TEXT("Invalid edit leaves saved macro intact"),Deck->BindMacro(24,TEXT("Bad"),0x06004CF7,TEXT("/hom\n/wait no"),0,Deck->EffectiveKey(24)));
        TestEqual(TEXT("Failed validation did not alter the macro"),Deck->Slot(24).MacroText,Script);
        TestFalse(TEXT("Macro editor rejects another slot's key"),Deck->BindMacro(24,TEXT("Home"),0x06004CF7,Script,0,Deck->EffectiveKey(25)));
        Deck->ConfigOpen=false;Slate.ClearKeyboardFocus();
        S.bUseBusy=true; // Chat macros do not require a combat stance or idle equipment.
        TestTrue(TEXT("Macro hotkey dispatches"),Deck->HandleKey(Deck->EffectiveKey(24),false));
        TestEqual(TEXT("/hom uses native mansion recall action"),CountAction(ACEGameAction::TeleToMansion),1);
        TestEqual(TEXT("Fellowship message is initially withheld"),CountAction(ACEGameAction::ChatChannel),0);
        Frame();Frame();
        TestTrue(TEXT("Running macro shows its status row"),Find(Content,TEXT("ActionDeck.Status"))->GetVisibility().IsVisible());
        const double Due=Deck->MacroDue;
        TestTrue(TEXT("30,000 milliseconds schedules approximately 30 seconds"),Due-FPlatformTime::Seconds()>29.8&&Due-FPlatformTime::Seconds()<=30.);
        TestTrue(TEXT("Held hotkey is consumed"),Deck->HandleKey(Deck->EffectiveKey(24),true));
        TestEqual(TEXT("Held hotkey does not repeat recall"),CountAction(ACEGameAction::TeleToMansion),1);
        TestFalse(TEXT("Second activation cannot interleave another macro"),Deck->Activate(24));
        Deck->AdvanceMacro(Due-.001);
        TestEqual(TEXT("Delay does not send early"),CountAction(ACEGameAction::ChatChannel),0);
        PC->bWorldRevealActive=true;Deck->AdvanceMacro(Due+1);
        TestTrue(TEXT("Recall portal space preserves pending macro"),Deck->IsMacroRunning());
        TestEqual(TEXT("No chat send during portal loading"),CountAction(ACEGameAction::ChatChannel),0);
        PC->bWorldRevealActive=false;
        Entry->SetText(FText::FromString(TEXT("Unfinished player message")));Slate.SetKeyboardFocus(Entry);
        const auto TypingFocus=Slate.GetKeyboardFocusedWidget();
        TestTrue(TEXT("Macro fixture has active text entry"),ACEInputBindings::IsTextEntryFocused());
        Deck->AdvanceMacro(Due+1);
        TestFalse(TEXT("Macro completes after delay and portal arrival"),Deck->IsMacroRunning());
        Frame();Frame();
        TestTrue(TEXT("Completed macro collapses its status row"),Find(Content,TEXT("ActionDeck.Status"))->GetVisibility()==EVisibility::Collapsed);
        TestEqual(TEXT("Exactly one fellowship message sent"),CountAction(ACEGameAction::ChatChannel),1);
        for(const auto& Pair:S.CachedC2SPackets)
        {
            FACEBinaryReader R(Pair.Value.Payload);R.Skip(16);
            if(R.ReadUInt32()!=ACEOpcode::GameAction)continue;R.ReadUInt32();
            if(R.ReadUInt32()!=ACEGameAction::ChatChannel)continue;
            TestEqual(TEXT("/f routes to fellowship, not local speech"),R.ReadUInt32(),ACEChatChannel::Fellow);
            TestEqual(TEXT("Fellowship receives exactly the native command arguments"),R.ReadString16L(),FString(TEXT("\"I'm home.\"")));
        }
        TestEqual(TEXT("Timed command preserves unfinished typed chat"),Entry->GetText().ToString(),FString(TEXT("Unfinished player message")));
        TestTrue(TEXT("Timed command does not steal typing focus"),Slate.GetKeyboardFocusedWidget()==TypingFocus);
        Slate.ClearKeyboardFocus();
        for(int32 StopReason=0;StopReason<4;++StopReason)
        {
            TestTrue(TEXT("Macro restarts explicitly"),Deck->Activate(24));
            S.CachedC2SPackets.Reset();const double Deadline=Deck->MacroDue;
            if(StopReason==0)Deck->StopMacro();
            if(StopReason==1)(*Plugin)->Enabled=false;
            if(StopReason==2)S.State=EACESessionState::CharacterSelect;
            if(StopReason==3)S.PlayerGuid=5678;
            Deck->AdvanceMacro(Deadline+60);
            TestFalse(TEXT("Stop, disable, logout and character changes cancel pending sends"),Deck->IsMacroRunning());
            TestEqual(TEXT("Cancelled macros send no delayed messages"),S.CachedC2SPackets.Num(),0);
            (*Plugin)->Enabled=true;S.State=EACESessionState::InWorld;S.PlayerGuid=Self.Guid;
        }
        TestTrue(TEXT("Command spacing can be configured"),Deck->BindMacro(24,TEXT("Paced"),0x06004CF7,TEXT("/f one\n/f two\n/f three"),1000,Deck->EffectiveKey(24)));
        S.CachedC2SPackets.Reset();Deck->Activate(24);const double Spaced=Deck->MacroDue;
        Deck->AdvanceMacro(Spaced-.001);TestEqual(TEXT("Spacing keeps adjacent sends apart"),CountAction(ACEGameAction::ChatChannel),1);
        Deck->AdvanceMacro(Spaced+100);TestEqual(TEXT("Long frames never burst every queued send"),CountAction(ACEGameAction::ChatChannel),2);
        Deck->StopMacro();

        // Exercise the actual modal through its controls, and render its layout.
        Deck->BindMacro(24,TEXT("Home"),0x06004CF7,Script,0,Deck->EffectiveKey(24));
        S.CachedC2SPackets.Reset();Deck->OpenMacroEditor(24);Frame();Frame();
        auto Modal=Find(Content,TEXT("ActionDeck.Macro.Editor"));
        if(!TestTrue(TEXT("Macro editor opens above the viewport"),Modal.IsValid()))return false;
        auto MacroName=StaticCastSharedPtr<SEditableTextBox>(Find(Content,TEXT("ActionDeck.Macro.Name")));
        auto MacroCommands=StaticCastSharedPtr<SMultiLineEditableTextBox>(Find(Content,TEXT("ActionDeck.Macro.Commands")));
        TestEqual(TEXT("Editing loads multiline commands"),MacroCommands->GetText().ToString(),Script);
        TestFalse(TEXT("Modal prevents direct deck activation"),Deck->Activate(24));
        TestFalse(TEXT("Modal lets its key selector receive shortcuts"),Deck->HandleKey(Deck->EffectiveKey(24),false));
        TestFalse(TEXT("Modal blocks inventory drops into the deck"),Deck->DropAt(Point(35),Spare.Guid));
        MacroName->SetText(FText::FromString(TEXT("Unsaved name")));
        StaticCastSharedPtr<SButton>(Find(Content,TEXT("ActionDeck.Macro.Cancel")))->SimulateClick();Frame();
        TestFalse(TEXT("Cancel dismisses the editor"),Deck->IsMacroEditorOpen());
        TestEqual(TEXT("Cancel preserves the stored macro"),Deck->Slot(24).Item.Name,FString(TEXT("Home")));
        Deck->OpenMacroEditor(24);Frame();Frame();
        MacroCommands=StaticCastSharedPtr<SMultiLineEditableTextBox>(Find(Content,TEXT("ActionDeck.Macro.Commands")));
        MacroCommands->SetText(FText::FromString(TEXT("/hom\n/wait typo")));
        StaticCastSharedPtr<SButton>(Find(Content,TEXT("ActionDeck.Macro.Save")))->SimulateClick();Frame();
        TestTrue(TEXT("Invalid script stays open with a validation message"),Deck->IsMacroEditorOpen());
        MacroCommands->SetText(FText::FromString(Script));
        StaticCastSharedPtr<SButton>(Find(Content,TEXT("ActionDeck.Macro.AllIcons")))->SimulateClick();
        for(int32 I=0;I<8;++I)Frame();
        auto IconGrid=Find(Content,TEXT("ActionDeck.Macro.IconGrid"));
        auto FirstIcon=IconGrid->GetChildren()->GetChildAt(0)->GetChildren()->GetChildAt(0);
        TestTrue(TEXT("Catalog exposes an icon button"),FirstIcon->GetTag().ToString().StartsWith(TEXT("ActionDeck.Macro.Icon.")));
        TestTrue(TEXT("Catalog icons are laid out and visible"),FirstIcon->GetCachedGeometry().GetLocalSize().X>30&&FirstIcon->GetCachedGeometry().GetLocalSize().Y>30);
        for(int32 I=0;I<6;++I)Frame();
        TestTrue(TEXT("Catalog growth preserves visible icon buttons"),FirstIcon==IconGrid->GetChildren()->GetChildAt(0)->GetChildren()->GetChildAt(0));
        StaticCastSharedRef<SButton>(FirstIcon)->SimulateClick();Frame();Frame();
        const uint32 PickedIcon=FCString::Strtoui64(*FirstIcon->GetTag().ToString().Right(8),nullptr,16);
        Screenshot(TEXT("ActionDeck-MacroEditor.png"));
        // The modal scales down independently of the user's deck size.
        Window->Resize(FVector2D(480,360));
        for(int32 I=0;I<3;++I){++GFrameCounter;Renderer.DrawWindow(Target,Window->GetHittestGrid(),Window,1.f,FVector2D(480,360),.016f);FlushRenderingCommands();}
        auto SaveButton=Find(Content,TEXT("ActionDeck.Macro.Save"));
        const auto& SaveGeometry=SaveButton->GetCachedGeometry();
        const FVector2D Bottom=SaveGeometry.GetAbsolutePosition()+SaveGeometry.GetAbsoluteSize();
        TestTrue(TEXT("Save remains on-screen at a small resolution"),Bottom.X<=480&&Bottom.Y<=360&&SaveGeometry.GetAbsoluteSize().Y>10);
        Screenshot(TEXT("ActionDeck-MacroEditor-Small.png"));
        Window->Resize(FVector2D(900,700));Frame();Frame();
        StaticCastSharedPtr<SEditableTextBox>(Find(Content,TEXT("ActionDeck.Macro.Name")))->SetText(FText::FromString(TEXT("Home recall")));
        StaticCastSharedPtr<SButton>(Find(Content,TEXT("ActionDeck.Macro.Save")))->SimulateClick();Frame();
        TestFalse(TEXT("Valid Save closes the modal"),Deck->IsMacroEditorOpen());
        TestEqual(TEXT("Save updates the chosen slot"),Deck->Slot(24).Item.Name,FString(TEXT("Home recall")));
        TestEqual(TEXT("Save retains the icon picked from the catalog"),uint32(Deck->Slot(24).Item.Icon),PickedIcon);
        TestEqual(TEXT("Opening, editing and saving never sends a command"),S.CachedC2SPackets.Num(),0);
        Deck->OpenMacroEditor(24);Frame();
        Modal=Find(Content,TEXT("ActionDeck.Macro.Editor"));
        Modal->OnPreviewKeyDown(Modal->GetCachedGeometry(),FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
        TestFalse(TEXT("Escape cancels the modal"),Deck->IsMacroEditorOpen());
        Deck->Deserialize(BeforeMacros);Deck->Notice.Empty();Deck->Save();Deck->Refresh(true);S.bUseBusy=false;Frame();Frame();
    }
    Screenshot(TEXT("ActionDeck-Bar.png"));
    {
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        const auto P=Point(30);const auto Pixel=Pixels[FMath::FloorToInt(P.Y)*900+FMath::FloorToInt(P.X)];
        TestTrue(TEXT("Empty slots have a dark background"),Pixel.R<90&&Pixel.G<90&&Pixel.B<90);
    }
    const auto Gear=StaticCastSharedPtr<SButton>(Find(Content,TEXT("ActionDeck.Settings")));
    if(!TestTrue(TEXT("Settings gear exists"),Gear.IsValid()))return false;
    // Collapsing the toolbar suppresses child visibility updates in Slate.
    // Test the effective hit path, not a child's cached visibility attribute.
    auto ControlIsHitTestable=[&](const TSharedPtr<SWidget>& Control)
    {
        const auto& Geometry=Control->GetCachedGeometry();
        const auto At=Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5f);
        const auto Path=Slate.LocateWindowUnderMouse(At,{Window},false,0);
        for(int32 I=0;I<Path.Widgets.Num();++I)if(Path.Widgets[I].Widget==Control)return true;
        return false;
    };
    Gear->SimulateClick();Frame();Frame();Screenshot(TEXT("ActionDeck-Settings.png"));
    const auto Selector=StaticCastSharedPtr<SInputKeySelector>(Find(Content,TEXT("ActionDeck.Key")));
    if(!TestTrue(TEXT("Per-slot key selector exists"),Selector.IsValid()))return false;
    Slate.SetKeyboardFocus(Selector);
    StaticCastSharedRef<SButton>(Selector->GetChildren()->GetChildAt(0))->SimulateClick();
    TestTrue(TEXT("Key selector enters capture mode"),Selector->GetIsSelectingKey());
    Selector->OnKeyUp(Selector->GetCachedGeometry(),FKeyEvent(EKeys::F9,FModifierKeysState(),0,false,0,0));
    TestEqual(TEXT("Key selector assigns the selected slot"),Deck->EffectiveKey(24),FInputChord(EKeys::F9));
    Gear->SimulateClick();Frame();Frame();
    TestFalse(TEXT("Gear closes settings"),Deck->IsConfigOpen());
    TestFalse(TEXT("Closing settings releases the key selector focus"),Slate.GetKeyboardFocusedWidget()==Selector);
    TestTrue(TEXT("Deck input resumes immediately after closing settings"),Deck->HandleKey(Deck->EffectiveKey(24),true));
    auto Dock=Host->DesktopDock;
    for(double Width:{360.,760.,530.})
    {
        Dock->Resize(TEXT("actiondeck"),FVector2D(Width-Dock->Windows[TEXT("actiondeck")].Size.X,0),true);
        Frame();Frame();Frame();
        TestEqual(TEXT("Deck accepts the requested width"),Dock->DisplaySize(Dock->Windows[TEXT("actiondeck")]).X,Width);
        TestTrue(TEXT("Resizing preserves square cells in a three-row bar"),
            FMath::IsNearlyEqual(Grid->GetCachedGeometry().GetAbsoluteSize().X/12,Grid->GetCachedGeometry().GetAbsoluteSize().Y/3,.1));
        if(!TestTrue(FString::Printf(TEXT("Grid at width %.0f accepts drops into the final slot"),Width),Deck->DropAt(Point(35),Spare.Guid)))
        {
            Screenshot(TEXT("ActionDeck-ResizeFailure.png"));
            const auto Path=Slate.LocateWindowUnderMouse(Point(35),{Window},false,0);
            AddInfo(FString::Printf(TEXT("Drop point %s; grid absolute size %s"),*Point(35).ToString(),*Grid->GetCachedGeometry().GetAbsoluteSize().ToString()));
            for(int32 J=0;J<Path.Widgets.Num();++J)
                AddInfo(Path.Widgets[J].Widget->GetTypeAsString()+TEXT(" ")+Path.Widgets[J].Widget->GetTag().ToString());
        }
    }
    const FVector2D BeforeMove=Point(35);
    Dock->Move(TEXT("actiondeck"),FVector2D(25,35),true);Frame();Frame();
    TestTrue(TEXT("Moving the window moves the slot hit targets"),Point(35).Equals(BeforeMove+FVector2D(25,35),.1f));
    TestTrue(TEXT("Moved grid accepts drops"),Deck->DropAt(Point(35),Spare.Guid));
    const FVector2D BeforePin=Point(35);
    Host->SetPluginWindowPinned(TEXT("actiondeck"),true);Frame();Frame();
    TestTrue(TEXT("Pin locks the window"),Host->IsPluginWindowPinned(TEXT("actiondeck")));
    TestTrue(TEXT("Collapsing pinned chrome preserves slot positions"),Point(35).Equals(BeforePin,.1f));
    TestTrue(TEXT("Pinned frame hides but bar still occupies three rows"),Grid->GetCachedGeometry().GetLocalSize().Y>0);
    const FVector2D LockedPoint=Point(35);
    Dock->Move(TEXT("actiondeck"),FVector2D(100,100),true);
    Dock->Resize(TEXT("actiondeck"),FVector2D(100,100),true);Frame();Frame();
    TestTrue(TEXT("Locked deck cannot move or resize"),Point(35).Equals(LockedPoint,.1f));
    TestTrue(TEXT("Locked deck still accepts item assignments"),Deck->DropAt(Point(35),Spare.Guid));
    TestFalse(TEXT("Pinned override cannot receive clicks"),ControlIsHitTestable(Find(Content,TEXT("ActionDeck.Override"))));
    TestFalse(TEXT("Pinned lock cannot receive clicks"),ControlIsHitTestable(Find(Content,TEXT("ActionDeck.Lock"))));
    TestFalse(TEXT("Pinned settings gear cannot receive clicks"),ControlIsHitTestable(Gear));
    auto Toolbar=Find(Content,TEXT("ActionDeck.Toolbar"));
    auto Status=Find(Content,TEXT("ActionDeck.Status"));
    TestTrue(TEXT("Pinned deck collapses the whole empty toolbar"),Toolbar->GetVisibility()==EVisibility::Collapsed);
    TestTrue(TEXT("Idle pinned deck collapses the bottom information line"),Status->GetVisibility()==EVisibility::Collapsed);
    TestFalse(TEXT("Idle macro Stop button cannot receive clicks"),ControlIsHitTestable(Find(Content,TEXT("ActionDeck.StopMacro"))));
    Screenshot(TEXT("ActionDeck-Pinned.png"));
    {
        const auto BeforeMacro=Deck->ExportState();
        const auto IdleSize=Dock->DisplaySize(Dock->Windows[TEXT("actiondeck")]);
        Deck->BindMacro(0,TEXT("Pinned macro"),0x06004CF7,TEXT("/wait 60000\n/f done"),0,Deck->EffectiveKey(0));
        TestTrue(TEXT("Pinned macro can start"),Deck->Activate(0));Frame();Frame();
        auto Stop=StaticCastSharedPtr<SButton>(Find(Content,TEXT("ActionDeck.StopMacro")));
        TestTrue(TEXT("Pinned macro shows its status row"),Status->GetVisibility().IsVisible());
        TestTrue(TEXT("Running macro adds only the footer height"),FMath::IsNearlyEqual(
            Dock->DisplaySize(Dock->Windows[TEXT("actiondeck")]).Y-IdleSize.Y,Status->GetCachedGeometry().GetLocalSize().Y,.1));
        TestTrue(TEXT("Running macro keeps the pinned top strip collapsed"),Toolbar->GetVisibility()==EVisibility::Collapsed);
        TestTrue(TEXT("Stop macro remains visible below the grid"),Stop->GetVisibility().IsVisible()
            &&Stop->GetCachedGeometry().GetAbsolutePosition().Y>=Grid->GetCachedGeometry().GetAbsolutePosition().Y+Grid->GetCachedGeometry().GetAbsoluteSize().Y);
        Screenshot(TEXT("ActionDeck-PinnedMacro.png"));
        Stop->SimulateClick();TestFalse(TEXT("Bottom control stops a pinned macro"),Deck->IsMacroRunning());
        Frame();Frame();
        TestTrue(TEXT("Stopping collapses the whole footer"),Status->GetVisibility()==EVisibility::Collapsed);
        TestFalse(TEXT("Stopped macro leaves no clickable Stop button"),ControlIsHitTestable(Stop));
        TestTrue(TEXT("Stopping removes the footer's reserved space"),Dock->DisplaySize(Dock->Windows[TEXT("actiondeck")]).Equals(IdleSize,.1));
        Deck->Deserialize(BeforeMacro);Deck->Save();Deck->Refresh(true);Frame();Frame();
    }
    {
        const auto At=Point(3);
        const auto Path=Slate.LocateWindowUnderMouse(At,{Window},false,0);
        Slate.RoutePointerDownEvent(Path,FPointerEvent(0,At,At,{EKeys::RightMouseButton},EKeys::RightMouseButton,0,FModifierKeysState()));
        Slate.RoutePointerUpEvent(Path,FPointerEvent(0,At,At,{},EKeys::RightMouseButton,0,FModifierKeysState()));
        Frame();Frame();
        TestFalse(TEXT("Right-click editing unpins the deck"),Host->IsPluginWindowPinned(TEXT("actiondeck")));
        TestTrue(TEXT("Right-click restores toolbar and settings without Quick-show"),Deck->IsConfigOpen()&&Toolbar->GetVisibility().IsVisible()&&Gear->GetVisibility().IsVisible());
        auto SlotKey=StaticCastSharedPtr<SInputKeySelector>(Find(Content,TEXT("ActionDeck.Key")));
        TestEqual(TEXT("Right-click edits the clicked slot"),SlotKey->GetSelectedKey(),Deck->EffectiveKey(3));
        Screenshot(TEXT("ActionDeck-RightClickSettings.png"));
        Gear->SimulateClick();Frame();TestFalse(TEXT("Restored gear hides settings"),Deck->IsConfigOpen());
        Gear->SimulateClick();Frame();TestTrue(TEXT("Restored gear shows settings again"),Deck->IsConfigOpen());
        Gear->SimulateClick();Host->SetPluginWindowPinned(TEXT("actiondeck"),true);Frame();Frame();
    }
    C->GetUIElementManager()->SetQuickShowUi(true);Frame();Frame();
    TestTrue(TEXT("Quick-show restores the collapsed toolbar"),Toolbar->GetVisibility().IsVisible());
    TestTrue(TEXT("Quick-show reveals all three pinned deck controls"),ControlIsHitTestable(Gear)
        &&ControlIsHitTestable(Find(Content,TEXT("ActionDeck.Override")))
        &&ControlIsHitTestable(Find(Content,TEXT("ActionDeck.Lock"))));
    TestTrue(TEXT("Quick-show does not unpin the deck"),Host->IsPluginWindowPinned(TEXT("actiondeck")));
    Gear->SimulateClick();Frame();Frame();
    TestTrue(TEXT("Revealed settings gear opens configuration while pinned"),Deck->IsConfigOpen());
    Gear->SimulateClick();Frame();Frame();
    C->GetUIElementManager()->SetQuickShowUi(false);Frame();Frame();
    TestTrue(TEXT("Releasing quick-show hides all pinned deck controls again"),!ControlIsHitTestable(Gear)
        &&!ControlIsHitTestable(Find(Content,TEXT("ActionDeck.Override")))
        &&!ControlIsHitTestable(Find(Content,TEXT("ActionDeck.Lock"))));
    Host->SetPluginWindowPinned(TEXT("actiondeck"),false);Frame();Frame();
    TestTrue(TEXT("Unpin restores the settings gear"),Gear->GetVisibility().IsVisible());
    TestTrue(TEXT("Unpin restores override and lock controls"),Find(Content,TEXT("ActionDeck.Override"))->GetVisibility().IsVisible()
        &&Find(Content,TEXT("ActionDeck.Lock"))->GetVisibility().IsVisible());

    {
        const auto BeforeExtraRow=Deck->ExportState();
        Gear->SimulateClick();Frame();Frame();
        StaticCastSharedPtr<SCheckBox>(Find(Content,TEXT("ActionDeck.FourthRow")))->ToggleCheckedState();
        Frame();Frame();Frame();
        TestEqual(TEXT("Settings checkbox enables the fourth row"),Deck->VisibleRows(),4);
        TestTrue(TEXT("Four-row cells remain square"),FMath::IsNearlyEqual(
            Grid->GetCachedGeometry().GetAbsoluteSize().X/12,Grid->GetCachedGeometry().GetAbsoluteSize().Y/4,.1));
        TestTrue(TEXT("Extra top-left slot accepts a backpack"),Deck->DropAt(Point(36),Self.Guid));
        TestTrue(TEXT("Extra top-right slot accepts an inventory item"),Deck->DropAt(Point(47),Spare.Guid));
        TestTrue(TEXT("Top-left drop targets the extra row"),Deck->Slot(36).Action==EACEDeckAction::Self);
        TestEqual(TEXT("Top-right drop targets the extra row"),Deck->Slot(47).Guid,Spare.Guid);
        // Read the actual native tooltip text; change the binding without moving
        // the mouse to catch stale hover-text caches, not just label formatting.
        const auto At=Point(36);
        Grid->OnMouseMove(Grid->GetCachedGeometry(),FPointerEvent(0,At,At,{},EKeys::Invalid,0,FModifierKeysState()));
        auto HoverText=[&]()
        {
            const auto TipContent=Grid->GetToolTip()->GetContentWidget();
            if(TipContent->GetTypeAsString()!=TEXT("SObjectWidget"))return FString();
            auto* Object=StaticCastSharedRef<SObjectWidget>(TipContent)->GetWidgetObject();
            TArray<UWidget*> Widgets;Object->WidgetTree->GetAllWidgets(Widgets);
            FString Result;for(auto* Widget:Widgets)if(auto* Label=Cast<UTextBlock>(Widget))Result+=Label->GetText().ToString();
            return Result;
        };
        TestTrue(TEXT("Default Alt chord appears in the hovered icon hint"),HoverText().Contains(Deck->EffectiveKey(36).GetInputText().ToString()));
        const FInputChord OverrideKey(EKeys::F10,false,true,true,false);
        TestTrue(TEXT("Extra slot accepts a user-defined override"),Deck->SetKey(36,OverrideKey));
        TestEqual(TEXT("Icon overlay displays the user-defined key"),Deck->KeyLabel(36),FString(TEXT("C+A+F10")));
        TestTrue(TEXT("Hovered hint updates without another mouse move"),HoverText().Contains(OverrideKey.GetInputText().ToString()));
        TestTrue(TEXT("Extra row modifier can change"),Deck->SetRowModifier(3,8));
        TestTrue(TEXT("Row changes retain custom hover hint"),HoverText().Contains(OverrideKey.GetInputText().ToString()));
        Deck->SetRowModifier(3,4);Frame();Frame();
        Screenshot(TEXT("ActionDeck-FourRowsSettings.png"));
        Gear->SimulateClick();Slate.ClearKeyboardFocus();Frame();Frame();
        TestTrue(TEXT("Active extra-row assignment reserves its override"),Deck->ReservesKey(OverrideKey));
        Host->SetPluginWindowPinned(TEXT("actiondeck"),true);Frame();Frame();
        Screenshot(TEXT("ActionDeck-FourRowsPinned.png"));
        TestTrue(TEXT("Pinned fourth row accepts drops into its last column"),Deck->DropAt(Point(47),Spare.Guid));
        Host->SetPluginWindowPinned(TEXT("actiondeck"),false);Frame();Frame();Gear->SimulateClick();Frame();Frame();
        StaticCastSharedPtr<SCheckBox>(Find(Content,TEXT("ActionDeck.FourthRow")))->ToggleCheckedState();Frame();Frame();
        TestEqual(TEXT("Settings can return to three rows"),Deck->VisibleRows(),3);
        TestFalse(TEXT("Hidden extra row releases overridden input"),Deck->ReservesKey(OverrideKey));
        TestEqual(TEXT("Hiding the fourth row preserves its custom hint"),Deck->KeyLabel(36),FString(TEXT("C+A+F10")));
        Gear->SimulateClick();Deck->Deserialize(BeforeExtraRow);Deck->Save();Deck->Refresh(true);Frame();Frame();
    }

    // Real DAT layout and binder visibility, including live changes and restore.
    auto* Layout=NewObject<UACEUILayoutResolver>();Layout->Initialize(Dat,Binder->Manager);
    if(!TestTrue(TEXT("Native combat layout loads"),Layout->LoadLayout(0x21000005)))return false;
    const auto CombatRoot=Binder->Manager->FindElementByName(TEXT("RootGameplay_FloatyCombatPanel_Field"));
    if(!TestTrue(TEXT("Native combat bar exists"),CombatRoot.IsValid()))return false;
    Binder->CombatMode=int32(ACECombatMode::Magic);Binder->SetFloatyVisible(TEXT("RootGameplay_FloatyCombatPanel_Field"),true);
    Deck->HideSpellBar=true;Binder->RefreshNativeCombatBarVisibility();
    TestFalse(TEXT("Spell bar hides immediately"),bool(CombatRoot->bVisible));
    Binder->SetFloatyVisible(TEXT("RootGameplay_FloatyCombatPanel_Field"),true);
    TestFalse(TEXT("Stance updates cannot reopen the hidden bar"),bool(CombatRoot->bVisible));
    Binder->CombatMode=int32(ACECombatMode::Melee);Binder->RefreshNativeCombatBarVisibility();
    TestTrue(TEXT("Spell option leaves melee visible"),bool(CombatRoot->bVisible));
    Deck->HideCombatBar=true;Binder->RefreshNativeCombatBarVisibility();
    TestFalse(TEXT("Combat option hides melee"),bool(CombatRoot->bVisible));
    Binder->CombatMode=int32(ACECombatMode::Missile);Binder->SetFloatyVisible(TEXT("RootGameplay_FloatyCombatPanel_Field"),true);
    TestFalse(TEXT("Combat option also hides missile"),bool(CombatRoot->bVisible));
    (*Plugin)->Enabled=false;Binder->RefreshNativeCombatBarVisibility();
    TestTrue(TEXT("Disabling Action Deck restores native combat bar"),bool(CombatRoot->bVisible));
    (*Plugin)->Enabled=true;Binder->RefreshNativeCombatBarVisibility();
    Host->DesktopDock->Toggle(TEXT("actiondeck"));Binder->RefreshNativeCombatBarVisibility();
    TestTrue(TEXT("Closing Action Deck restores native combat bar"),bool(CombatRoot->bVisible));
    Host->DesktopDock->Toggle(TEXT("actiondeck"));
    Binder->ApplyCombatModeInternal(int32(ACECombatMode::NonCombat),false);Binder->RefreshNativeCombatBarVisibility();
    TestFalse(TEXT("Peace never reveals a native combat bar"),bool(CombatRoot->bVisible));
    Deck->HideSpellBar=Deck->HideCombatBar=false;
    Binder->Manager->ClearRoots(); // Keep the remaining render fixture plugin-only.

    // Use real spell metadata and the session send/confirmation events. UCM is off.
    int32 Debuff=0,Dot=0,Stronger=0;
    for(uint32 Id=1;Id<6200;++Id)
    {
        FString Name;uint32 Icon=0;
        if(!Dat->TryGetSpellInfo(Id,Name,Icon))continue;
        if(Name==TEXT("Imperil Other I"))Debuff=Id;
        if(Name==TEXT("Imperil Other II"))Stronger=Id;
        if(Name==TEXT("Corruption I"))Dot=Id;
    }
    if(!TestTrue(TEXT("Retail debuff and damage-over-time metadata available"),Debuff&&Dot&&Stronger))return false;
    S.WorldObjects[Monster.Guid].Name=TEXT("Deck target");S.WorldObjects[Monster.Guid].bDying=false;
    S.SelectedObject.Guid=Monster.Guid;S.SelectedObject.bValid=true;
    S.KnownSpells.AddUnique(Debuff);S.KnownSpells.AddUnique(Dot);S.KnownSpells.AddUnique(Stronger);
    Deck->BindSpell(0,Debuff);Deck->BindSpell(1,Dot);Deck->BindSpell(2,Stronger);Deck->ShowTargetTimers=true;
    Host->RefreshAutomationData();
    auto Confirm=[&](int32 Spell,const FString& Suffix=FString())
    {FString Name;uint32 Icon=0;Dat->TryGetSpellInfo(Spell,Name,Icon);
        S.OnChatMessage.Broadcast(TEXT("You cast ")+Name+TEXT(" on Deck target")+Suffix,TEXT(""),ACEChatMessageType::Magic);};
    S.SendCastSpell(Debuff,Monster.Guid);S.OnUseDone.Broadcast(0);
    TestEqual(TEXT("A request and UseDone do not invent a timer"),Deck->EffectTimer(0).Remaining,0.);
    S.OnChatMessage.Broadcast(TEXT("Deck target resists your spell."),TEXT(""),ACEChatMessageType::Magic);
    Confirm(Debuff);TestEqual(TEXT("Resisted cast cannot create a timer"),Deck->EffectTimer(0).Remaining,0.);
    S.SendCastSpell(Debuff,Monster.Guid);S.OnChatMessage.Broadcast(TEXT("Your spell fizzled."),TEXT(""),ACEChatMessageType::Magic);
    Confirm(Debuff);TestEqual(TEXT("Fizzled cast cannot create a timer"),Deck->EffectTimer(0).Remaining,0.);
    S.SendCastSpell(Debuff,Monster.Guid);Confirm(Debuff);
    TestTrue(TEXT("Manual confirmed debuff starts a full timer without UCM"),Deck->EffectTimer(0).Fraction()>.99f);
    TestEqual(TEXT("Lower-tier effect does not claim stronger-tier coverage"),Deck->EffectTimer(2).Remaining,0.);
    S.SendCastSpell(Dot,Monster.Guid);Confirm(Dot);
    TestTrue(TEXT("Confirmed DoT uses its effect duration"),Deck->EffectTimer(1).Remaining>30&&Deck->EffectTimer(1).Remaining<=31);
    for(auto& E:Host->Debuffs)E->AsObject()->SetNumberField(TEXT("expires"),FPlatformTime::Seconds()+E->AsObject()->GetNumberField(TEXT("duration"))*.5);
    TestTrue(TEXT("Timer progress drains across half the full slot"),FMath::IsNearlyEqual(Deck->EffectTimer(1).Fraction(),.5f,.01f));
    Deck->Refresh(true);const auto BeforeTimer=Deck->Availability(0).Ready;
    Host->SetPluginWindowPinned(TEXT("actiondeck"),true);Deck->Refresh(true);Frame();Frame();Screenshot(TEXT("ActionDeck-TargetTimers.png"));
    TestEqual(TEXT("Target duration never locks an otherwise ready action"),Deck->Availability(0).Ready,BeforeTimer);
    S.SelectedObject.Guid=Friend.Guid;TestEqual(TEXT("Switching target removes the previous target timer"),Deck->EffectTimer(0).Remaining,0.);
    S.SelectedObject.Guid=Monster.Guid;TestTrue(TEXT("Switching back preserves that target timer"),Deck->EffectTimer(0).Remaining>0);
    S.SendCastSpell(Debuff,Monster.Guid);Confirm(Debuff,TEXT(", refreshing Imperil Other I"));
    TestTrue(TEXT("Confirmed refresh restarts the duration"),Deck->EffectTimer(0).Fraction()>.99f);
    Deck->ShowTargetTimers=false;TestEqual(TEXT("Timer option off hides existing effects"),Deck->EffectTimer(0).Remaining,0.);Deck->ShowTargetTimers=true;
    S.WorldObjects[Monster.Guid].bDying=true;TestEqual(TEXT("Dead target has no timer"),Deck->EffectTimer(0).Remaining,0.);S.WorldObjects[Monster.Guid].bDying=false;
    for(auto& E:Host->Debuffs)E->AsObject()->SetNumberField(TEXT("expires"),FPlatformTime::Seconds()-1);
    TestEqual(TEXT("Expired effects disappear"),Deck->EffectTimer(0).Remaining,0.);
    S.SendCastSpell(Debuff,Monster.Guid);Confirm(Debuff);S.OnObjectDeleted.Broadcast(Monster.Guid);
    TestEqual(TEXT("Leaving awareness clears effects before an ID is reused"),Deck->EffectTimer(0).Remaining,0.);
    S.SendCastSpell(Debuff,Monster.Guid);Confirm(Debuff);S.PlayerGuid=1235;
    TestEqual(TEXT("Effects cannot leak to another character before maintenance"),Deck->EffectTimer(0).Remaining,0.);S.PlayerGuid=Self.Guid;
    FACEDeckEffectTimer Format;Format.Remaining=65;TestEqual(TEXT("Long timer reads minutes and seconds"),Format.Label(),FString(TEXT("1:05")));
    Format.Remaining=.1;TestEqual(TEXT("Last fraction reads one second until expiry"),Format.Label(),FString(TEXT("1")));
    return true;
}
#endif
