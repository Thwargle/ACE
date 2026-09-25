#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEInputBindings.h"
#include "ACERuntimeOptions.h"
#include "ACEScreenshotSettings.h"
#include "ACESpellTargeting.h"
#include "HAL/PlatformProcess.h"
#include "Components/EditableTextBox.h"
#include "ACECharacterOptions.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEVideoSettingsWidget.h"
#include "Components/ComboBoxString.h"
#include "Components/ScrollBox.h"
#include "Blueprint/WidgetTree.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUIInteractionParityTest, "ACE.RetailParity.UIInteractions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEUIInteractionParityTest::RunTest(const FString&)
{
    const FString OriginalSettings=GGameUserSettingsIni;
    TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/InteractionPreferences.ini"));
    FConfigFile Preferences; Preferences.NoSave=true; GConfig->SetFile(GGameUserSettingsIni,&Preferences);
    auto* GI=NewObject<UGameInstance>();
    GI->Init();
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    const FString ArtDirectory=FPaths::ProjectSavedDir()/TEXT("Automation/UIInteractions");
    IFileManager::Get().MakeDirectory(*ArtDirectory,true);
    auto* Resources=NewObject<UACEUIResourceResolver>(); Resources->Initialize(Dat);
    auto* Manager=NewObject<UACEUIElementManager>(); Manager->Initialize();
    auto* Layout=NewObject<UACEUILayoutResolver>(); Layout->Initialize(Dat,Manager);
    if (!Layout->LoadLayout(0x21000005)) return false;
    auto* Canvas=NewObject<UACEUICanvasWidget>(); Canvas->Initialize();
    Canvas->InitializeCanvas(Manager); Canvas->SetResourceResolver(Resources);
    const auto Slate=Canvas->TakeWidget();
    auto* Client=NewObject<UACEClientSubsystem>(GI); Client->Session=MakeShared<FACESession>();
    auto& Session=*Client->Session; Session.State=EACESessionState::InWorld; Session.PlayerGuid=1234;
    FACEWorldObject Self; Self.Guid=1234; Self.Name=TEXT("UI regression"); Self.bIsPlayer=true; Self.ItemType=ACEItemType::Creature; Session.WorldObjects.Add(Self.Guid,Self);
    auto* Binder=NewObject<UACEUIGameplayBinder>(); Binder->Initialize(Client,Manager,Canvas,nullptr);
    Canvas->SetGameplayBinder(Binder);
    ON_SCOPE_EXIT { Binder->Shutdown(); Canvas->SetGameplayBinder(nullptr); Manager->Shutdown(); Dat->Deinitialize(); TGuardValue<FString> Restore(GGameUserSettingsIni,OriginalSettings); ACEInputBindings::Reload(); };
    FWidgetRenderer Renderer(true,true);
    auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(1600,900),TF_Bilinear,true);
    auto Draw=[&](const TCHAR* Name)
    {
        for (int32 Pass=0;Pass<4;++Pass)
        {
            Canvas->NativeTick(Canvas->GetCachedGeometry(),0.f);
            Renderer.DrawWidget(Target,Slate,FVector2D(1600,900),0.f); FlushRenderingCommands();
        }
        TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(1600,900,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Automation/UIInteractions")/(FString(Name)+TEXT(".png"))));
    };
    Manager->ApplyEdgeAnchoredLayout(1600,900);

    // Real tab input, hotkey navigation, and the VR selection entry point share state.
    Session.SpellBars.SetNum(8);
    for (int32 I=1;I<=20;++I) { Session.SpellBars[0].Add(I); Session.SpellBars[1].Add(I+20); }
    Binder->ApplyCombatMode(int32(ACECombatMode::Magic));
    Binder->SetCombatSpellBar(0); Binder->SelectVRSpell(7); Binder->RefreshSpellHotbarOverlays();
    Binder->HandleNamedClick(TEXT("Spellcast_Tab2"));
    TestEqual(TEXT("First visit selects the first spell"),Binder->SelectedCombatSpellSlot,0);
    Binder->SelectVRSpell(29); Binder->RefreshSpellHotbarOverlays();
    Binder->HandleNamedClick(TEXT("Spellcast_Tab1"));
    TestEqual(TEXT("Tab one restores seventh spell"),Binder->SelectedCombatSpellSlot,6);
    Binder->HandleNamedClick(TEXT("Spellcast_Tab2"));
    TestEqual(TEXT("Tab two restores ninth spell"),Binder->SelectedCombatSpellSlot,8);
    Binder->StepCombatSpellSelection(1,false,true); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Last spell selected"),Binder->SelectedCombatSpellSlot,19);
    TestTrue(TEXT("Last selection is inside viewport"),19>=Binder->SpellHotbarScrollOffset && 19<Binder->SpellHotbarScrollOffset+13);
    Binder->StepCombatSpellSelection(1,false,false); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Next wraps last to first"),Binder->SelectedCombatSpellSlot,0);
    TestEqual(TEXT("Wrapping scrolls left to first"),Binder->SpellHotbarScrollOffset,0);
    Binder->StepCombatSpellSelection(-1,false,false); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Previous wraps first to last"),Binder->SelectedCombatSpellSlot,19);
    Session.SpellBars[1].SetNumZeroed(56);
    Binder->StepCombatSpellSelection(1,false,false); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Padded 56-entry bar wraps directly to its first spell"),Binder->SelectedCombatSpellSlot,0);
    Binder->StepCombatSpellSelection(-1,false,false); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Reverse navigation skips every trailing empty entry"),Binder->SelectedCombatSpellSlot,19);
    Binder->SetCombatSpellBar(0); Binder->SetCombatSpellBar(1);
    TestEqual(TEXT("Returning restores scrolled selection"),Binder->SelectedCombatSpellSlot,19);
    TestTrue(TEXT("Restored cursor is visible"),19<Binder->SpellHotbarScrollOffset+13);
    Draw(TEXT("SpellSelectionLast"));
    Session.SpellBars[1].Swap(0,19); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Reordering preserves selected spell identity"),Binder->SelectedCombatSpellSlot,0);
    Binder->SpellHotbarScrollOffset=3; Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Manual scrolling is not forcibly undone"),Binder->SpellHotbarScrollOffset,3);
    Binder->SelectVRSpell(40); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Explicit re-selection reveals even the same spell"),Binder->SpellHotbarScrollOffset,0);
    Session.SpellBars[1].Reset(); Binder->RefreshSpellHotbarOverlays();
    Binder->StepCombatSpellSelection(1,false,false);
    TestEqual(TEXT("Empty bar has a safe cursor"),Binder->SelectedCombatSpellSlot,0);

    // Actual drag gesture: lift immediately, move the destination marker, then
    // insert exactly where shown, without a second removal on release.
    Binder->SetCombatSpellBar(1); Session.SpellBars[1]={1,2,3,4};
    Binder->RefreshSpellHotbarOverlays(); Draw(TEXT("SpellDragBefore"));
    auto SlotPoint=[&](int32 Index) { const auto& G=Binder->SpellBarSlotBgs[Index]->GetCachedGeometry();
        return Canvas->GetCachedGeometry().AbsoluteToLocal(G.LocalToAbsolute(G.GetLocalSize()*.5)); };
    TestTrue(TEXT("Spell press begins gesture"),Binder->TryBeginSpellDrag(SlotPoint(0)));
    Binder->UpdateSpellDrag(SlotPoint(2));
    TestFalse(TEXT("Dragged spell immediately leaves favorites"),Session.SpellBars[1].Contains(1));
    TestEqual(TEXT("Remaining spells close the source gap"),Session.SpellBars[1][0],2);
    TestTrue(TEXT("Insertion marker uses original retail green accept art"),Binder->SpellDropMarker
        && Binder->SpellDropMarker->Background.GetResourceObject()==Resources->ResolveIconTexture(0x060011F9)
        && Binder->SpellDropMarker->GetVisibility()!=ESlateVisibility::Collapsed);
    const auto PreviousCursor=FSlateApplication::Get().GetCursorPos();
    FSlateApplication::Get().SetCursorPos(Canvas->GetCachedGeometry().LocalToAbsolute(SlotPoint(2)));
    Draw(TEXT("SpellDragging")); Binder->TryFinishSpellDrag(SlotPoint(2));
    FSlateApplication::Get().SetCursorPos(PreviousCursor);
    TestTrue(TEXT("Drop inserts at indicated index after source removal"),Session.SpellBars[1]==TArray<int32>({2,3,1,4}));
    TestTrue(TEXT("Drop hides destination marker"),Binder->SpellDropMarker->GetVisibility()==ESlateVisibility::Collapsed);

    Session.SpellBars[2]={5,6};
    Draw(TEXT("SpellCrossTabStart"));
    Binder->TryBeginSpellDrag(SlotPoint(0));
    const auto DestinationTab=Manager->FindElementByName(TEXT("Spellcast_Tab3"));
    if(TestTrue(TEXT("Third spell tab exists"),DestinationTab.IsValid()))
    {
        const FVector2D TabPoint=(FVector2D(DestinationTab->GetScreenOrigin())+FVector2D(DestinationTab->Width,DestinationTab->Height)*.5)*FVector2D(Canvas->GetLastScaleX(),Canvas->GetLastScaleY());
        Binder->UpdateSpellDrag(TabPoint);
        TestEqual(TEXT("Hovering another tab during drag opens that bar"),Client->GetActiveSpellBar(),2);
        Binder->TryFinishSpellDrag(TabPoint);
        TestTrue(TEXT("Drop on a tab appends the lifted spell exactly once"),Session.SpellBars[2]==TArray<int32>({5,6,2}));
        TestFalse(TEXT("Cross-tab move removes the original"),Session.SpellBars[1].Contains(2));
        Binder->SetCombatSpellBar(1);Binder->RefreshSpellHotbarOverlays();Draw(TEXT("SpellCrossTabEnd"));
    }

    const auto Combat=Manager->FindElementByName(TEXT("RootGameplay_FloatyCombatPanel_Field"));
    Manager->SetUiLocked(false); Manager->BringFloatyToFront(Combat);
    Combat->UserDragX+=200-Combat->GetScreenOrigin().X; Combat->UserDragY+=200-Combat->GetScreenOrigin().Y; Combat->RecomputeLayoutOffset();
    Binder->RefreshSpellHotbarOverlays(); Draw(TEXT("SpellResizeBefore"));
    const auto Edge=Manager->FindElementByName(TEXT("CombatPanelRightBorder"));
    const FVector2D Start=Canvas->LayoutToViewport(FVector2D(Edge->GetScreenOrigin())+FVector2D(Edge->Width,Edge->Height)*.5);
    const auto ResizeCanvasSize=Canvas->GetCachedGeometry().GetLocalSize();
    TestTrue(TEXT("Spellbar right border advertises horizontal resize"),Manager->GetWindowCursor(Start,ResizeCanvasSize)==EMouseCursor::ResizeLeftRight);
    Manager->NotifyMouseDown(Start,ResizeCanvasSize,EKeys::LeftMouseButton);
    const int32 BeforeWidth=Combat->Width;
    Manager->NotifyMouseMove(Start+FVector2D(17*Canvas->GetLastScaleX(),0),ResizeCanvasSize); Binder->RefreshSpellHotbarOverlays();
    TestEqual(TEXT("Spellbar frame follows a 17-pixel drag without snapping"),Combat->Width,BeforeWidth+17);
    Manager->NotifyMouseMove(Start+FVector2D(-170*Canvas->GetLastScaleX(),0),ResizeCanvasSize); Binder->RefreshSpellHotbarOverlays();
    Manager->NotifyMouseUp(Start+FVector2D(-170*Canvas->GetLastScaleX(),0),ResizeCanvasSize,EKeys::LeftMouseButton);
    TestTrue(TEXT("Narrowing frame reduces visible spell slots"),Binder->SpellBarSpellIds.Num()<13);
    const auto CastButton=Manager->FindElementByName(TEXT("CastSpellButton"));
    TestTrue(TEXT("Cast stays inside narrowed frame"),CastButton->GetScreenOrigin().X+CastButton->Width<=Combat->GetScreenOrigin().X+Combat->Width);
    Draw(TEXT("SpellResizeNarrow"));
    TestEqual(TEXT("Right frame edge stays aligned after ticks"),Edge->GetScreenOrigin().X+Edge->Width,Combat->GetScreenOrigin().X+Combat->Width);
    for (auto SlotBackground : Binder->SpellBarSlotBgs)
        if (SlotBackground->GetVisibility()!=ESlateVisibility::Collapsed)
            TestTrue(TEXT("Cast does not overlap a visible slot after reflow"),CastButton->GetScreenOrigin().X>=SlotBackground->GetCachedGeometry().GetAbsolutePosition().X+32);
    Combat->UserResizeW=0; Binder->RefreshSpellHotbarOverlays(); Manager->SetUiLocked(true);

    // DAT-backed retail compatibility, not spell-name heuristics. Buffs,
    // debuffs and heals use the same formula target rules in every school.
    auto Other=Self; Other.Guid=1235; Other.Name=TEXT("Other player"); Session.WorldObjects.Add(Other.Guid,Other);
    int32 Resolved=0;
    uint32 SpellFlags=0, RetailType=0; bool Projectile=false;
    TestTrue(TEXT("DAT exposes retail formula targeting"),Dat->TryGetRetailSpellTargeting(37,SpellFlags,RetailType,Projectile));
    TestEqual(TEXT("Blade Bane uses retail equipment mask instead of narrow server target type"),RetailType,560015u);
    for (int32 Spell : {1,3,5,7,15,1237,27,5387})
    {
        TestFalse(TEXT("Other creature spell rejects caster"),Client->ResolveSpellCastTarget(Spell,Self.Guid,Resolved));
        TestTrue(TEXT("Other creature spell accepts another player"),Client->ResolveSpellCastTarget(Spell,Other.Guid,Resolved));
        TestEqual(TEXT("Accepted recipient is unchanged"),Resolved,Other.Guid);
    }
    TestTrue(TEXT("Self healing ignores selected enemy"),Client->ResolveSpellCastTarget(6,Other.Guid,Resolved) && Resolved==Self.Guid);
    TestTrue(TEXT("Item bane may target caster's worn equipment"),Client->ResolveSpellCastTarget(37,Self.Guid,Resolved));
    TestTrue(TEXT("Self portal recall remains valid without selection"),Client->ResolveSpellCastTarget(2645,0,Resolved));
    TestTrue(TEXT("VR Flame Bolt retains free aiming even without Projectile flag"),Client->ResolveSpellCastTarget(27,0,Resolved,true));
    TestFalse(TEXT("VR Other healing cannot fall back to self"),Client->ResolveSpellCastTarget(5,Self.Guid,Resolved,true));
    auto Npc=Other; Npc.Guid=1236; Npc.bIsPlayer=false; Session.WorldObjects.Add(Npc.Guid,Npc);
    TestFalse(TEXT("Protected NPC is not a spell target"),Client->ResolveSpellCastTarget(5,Npc.Guid,Resolved));
    Npc.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable; Session.WorldObjects.Add(Npc.Guid,Npc);
    TestTrue(TEXT("Attackable creature follows retail eligibility"),Client->ResolveSpellCastTarget(7,Npc.Guid,Resolved));
    Npc.PetOwnerId=Other.Guid; Session.WorldObjects.Add(Npc.Guid,Npc);
    TestFalse(TEXT("Combat pet is excluded like retail"),Client->ResolveSpellCastTarget(7,Npc.Guid,Resolved));
    auto Stack=Other; Stack.Guid=1237; Stack.bIsPlayer=false; Stack.ItemType=ACEItemType::MeleeWeapon;
    Stack.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable; Stack.StackSize=2; Session.WorldObjects.Add(Stack.Guid,Stack);
    TestFalse(TEXT("Stacked item cannot be enchanted"),Client->ResolveSpellCastTarget(37,Stack.Guid,Resolved));
    Session.SpellBars[1]={5}; Binder->SelectCombatSpellSlot(0); Client->SelectObject(Self.Guid); Binder->RefreshSpellHotbarOverlays();
    TestFalse(TEXT("Heal Other Cast button disabled for self"),CastButton->bActivatable);
    Session.CachedC2SPackets.Reset(); TestFalse(TEXT("Cast API cannot bypass disabled button"),Client->SendCastSpell(5));
    TestTrue(TEXT("Rejected cast sends no action"),Session.CachedC2SPackets.IsEmpty()); Draw(TEXT("HealOtherSelfDisabled"));
    Client->SelectObject(Other.Guid); Binder->RefreshSpellHotbarOverlays();
    TestTrue(TEXT("Heal Other Cast button enables for another player"),CastButton->bActivatable); Draw(TEXT("HealOtherPlayerEnabled"));
    Session.SpellBars[1].Reset();

    // Retail gmSpellbookUI double-click appends via AddFavorite(-1, false),
    // rather than casting, replacing an occupied slot, or relocating a duplicate.
    Session.KnownSpells={41,42};
    Binder->ShowPanelPage(TEXT("SpellManagementPanel_Field")); Binder->HandleNamedClick(TEXT("SpellbookTab"));
    Binder->SetCombatSpellBar(0); Binder->TickRefresh(); Draw(TEXT("SpellbookBeforeAppend"));
    auto ClickBook=[&](int32 SpellId,bool bDoubleClick)
    {
        const int32 Row=Binder->SpellbookRowIds.Find(SpellId);
        if (!TestTrue(TEXT("Known spell has a visible row"),Row!=INDEX_NONE)) return;
        const auto& RowGeometry=Binder->SpellbookRowBackgrounds[Row]->GetCachedGeometry();
        const FVector2D Position=RowGeometry.LocalToAbsolute(RowGeometry.GetLocalSize()*.5);
        const FPointerEvent Press(0,Position,Position,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
        const FPointerEvent Release(0,Position,Position,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState());
        if (bDoubleClick) Canvas->NativeOnMouseButtonDoubleClick(Canvas->GetCachedGeometry(),Press);
        else Canvas->NativeOnMouseButtonDown(Canvas->GetCachedGeometry(),Press);
        Canvas->NativeOnMouseButtonUp(Canvas->GetCachedGeometry(),Release);
    };
    const uint32 BeforeBook=Session.NextGameActionSequence;
    ClickBook(41,false);
    TestEqual(TEXT("Single spellbook click sends no cast or favorite action"),Session.NextGameActionSequence,BeforeBook);
    ClickBook(41,true);
    TestEqual(TEXT("Double-click appends after all twenty existing spells"),Session.SpellBars[0].Find(41),20);
    TestEqual(TEXT("Double-click sends exactly one favorite action"),Session.NextGameActionSequence,BeforeBook+1);
    TestEqual(TEXT("New shortcut is selected as in retail"),Binder->SelectedCombatSpellSlot,20);
    TestTrue(TEXT("Appended shortcut scrolls into view"),20>=Binder->SpellHotbarScrollOffset && 20<Binder->SpellHotbarScrollOffset+13);
    TestTrue(TEXT("Other tab is unchanged"),Session.SpellBars[1].IsEmpty());
    const auto BeforeDuplicate=Session.SpellBars[0];
    ClickBook(41,false); ClickBook(41,true);
    TestTrue(TEXT("An existing shortcut is not duplicated or moved"),Session.SpellBars[0]==BeforeDuplicate);
    TestEqual(TEXT("Duplicate sends no remove/add packets"),Session.NextGameActionSequence,BeforeBook+1);
    Binder->SetCombatSpellBar(1); Draw(TEXT("SpellbookEmptyTab"));
    ClickBook(41,false); ClickBook(41,true);
    TestEqual(TEXT("Double-click uses the currently open tab"),Session.SpellBars[1].Find(41),0);
    TestTrue(TEXT("Adding to another tab preserves the first tab"),Session.SpellBars[0]==BeforeDuplicate);
    TestEqual(TEXT("Adding a favorite leaves combat stance unchanged"),Binder->CombatMode,int32(ACECombatMode::Magic));
    const int32 DragRow=Binder->SpellbookRowIds.Find(42);
    const auto& DragGeometry=Binder->SpellbookRowBackgrounds[DragRow]->GetCachedGeometry();
    const FVector2D DragStart=Canvas->GetCachedGeometry().AbsoluteToLocal(DragGeometry.LocalToAbsolute(DragGeometry.GetLocalSize()*.5));
    Binder->TryBeginSpellDrag(DragStart); Binder->UpdateSpellDrag(DragStart+FVector2D(-80,0));
    Binder->TryFinishSpellDrag(DragStart+FVector2D(-80,0));
    ClickBook(42,false);
    TestFalse(TEXT("A completed drag cannot become the first half of a double-click"),Session.SpellBars[1].Contains(42));
    ClickBook(42,true);
    TestEqual(TEXT("Next double-click still appends at the end"),Session.SpellBars[1].Find(42),1);
    Draw(TEXT("SpellbookAppended"));

    // The original DAT edge anchors grow the list, not the fixed description.
    const auto SharedPanel=Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field"));
    UACEUIElementManager::ApplyFloatyResizeLayout(SharedPanel);
    for(const TCHAR* PageName:{TEXT("PositiveEffectsPanel_Field"),TEXT("NegativeEffectsPanel_Field"),TEXT("SpellManagementPanel_Field")})
    {
        Binder->ShowPanelPage(PageName); if(FString(PageName)==TEXT("SpellManagementPanel_Field")) Binder->HandleNamedClick(TEXT("SpellbookTab"));
        const bool Book=FString(PageName)==TEXT("SpellManagementPanel_Field");
        const auto ListEl=Manager->FindElementUnder(PageName,Book?TEXT("SpellBook_SpellList"):TEXT("Effects_SpellList"));
        const auto Footer=Manager->FindElementUnder(PageName,Book?TEXT("FilterBox"):TEXT("InfoBackground"));
        int32 SmallHeight=0;
        for(int32 H:{380,680,380})
        {
            SharedPanel->UserResizeH=H-SharedPanel->AuthoredHeight;
            UACEUIElementManager::ApplyFloatyResizeLayout(SharedPanel);Binder->TickRefresh();
            Draw(*(FString(PageName)+FString::FromInt(H)));
            AddInfo(FString::Printf(TEXT("Resize %s H=%d frame=%d list=%d footerY=%d"),PageName,H,SharedPanel->Height,ListEl->Height,Footer->GetScreenOrigin().Y));
            TestEqual(TEXT("Spell footer keeps its retail height"),Footer->Height,Book?113:88);
            TestEqual(TEXT("Footer stays against the bottom frame"),Footer->GetScreenOrigin().Y+Footer->Height,
                SharedPanel->GetScreenOrigin().Y+SharedPanel->Height-5);
            TestTrue(TEXT("List never overlaps its bottom controls"),ListEl->GetScreenOrigin().Y+ListEl->Height<=Footer->GetScreenOrigin().Y);
            if(H==380 && SmallHeight==0)SmallHeight=ListEl->Height;
            else TestEqual(TEXT("Additional panel height goes entirely to the spell list"),ListEl->Height,SmallHeight+H-380);
        }
    }
    // Character edits are staged; Chat and Config resets cannot change character options.
    Binder->ShowPanelPage(TEXT("OptionsPanel_Field"));Binder->SyncOptionsPanelTab(TEXT("CharacterSettingsPage"));
    Draw(TEXT("RetailCharacterOptions"));
    TestTrue(TEXT("Character starts with UI behavior in retail order"),Binder->OptionRowOptions.Num()>1 && Binder->OptionRowOptions[1]==0x07);
    TestEqual(TEXT("Retail display option belongs to Character"),ACECharacterOptions::Find(0x13)->Page,uint8(ACECharacterOptions::PageCharacter));
    TestEqual(TEXT("Channel subscription belongs to Character, not Chat routing"),ACECharacterOptions::Find(0x23)->Page,uint8(ACECharacterOptions::PageCharacter));
    const uint32 Original=Session.CharacterOptions1;
    const auto CharacterList=Manager->FindElementUnder(TEXT("CharacterSettingsPage"),TEXT("CharacterOptionsListBox"));
    Binder->TryHandleOptionsListClick(FVector2D(CharacterList->GetScreenOrigin())+FVector2D(12,24));
    TestEqual(TEXT("Editing a Character checkbox does not prematurely send it"),Session.CharacterOptions1,Original);
    TestTrue(TEXT("Checkbox toggles the draft bit"),Binder->OptionsDraft1!=(Original));
    Binder->HandleOptionsNamedClick(TEXT("ResetButton"));TestEqual(TEXT("Reset restores saved checkbox state"),Binder->OptionsDraft1,Original);
    Binder->TryHandleOptionsListClick(FVector2D(CharacterList->GetScreenOrigin())+FVector2D(12,24));
    Binder->HandleOptionsNamedClick(TEXT("ApplyButton"));TestEqual(TEXT("Apply saves the chosen checkbox"),Session.CharacterOptions1,Original^0x80u);
    Binder->SyncOptionsPanelTab(TEXT("ChatPage"));Binder->HandleOptionsNamedClick(TEXT("DefaultButton"));
    TestEqual(TEXT("Chat defaults leave character options alone"),Session.CharacterOptions1,Original^0x80u);

    Binder->SyncOptionsPanelTab(TEXT("ConfigPage"));Draw(TEXT("RetailConfigOptions"));
    auto* Video=Cast<UACEVideoSettingsWidget>(Binder->VideoSettings);
    auto* ScreenshotFolder=Video?Cast<UEditableTextBox>(Video->WidgetTree->FindWidget(TEXT("ScreenshotDirectory"))):nullptr;
    if(TestNotNull(TEXT("Config exposes screenshot save folder"),ScreenshotFolder))
    {
        const FString CustomFolder=FPaths::ConvertRelativePathToFull(ArtDirectory/TEXT("Custom Screenshots"));
        ScreenshotFolder->SetText(FText::FromString(CustomFolder)); Video->ApplyInterfaceOptions(); Video->ResetVideo();
        TestEqual(TEXT("Screenshot folder applies and reloads"),ScreenshotFolder->GetText().ToString(),CustomFolder);
        ScreenshotFolder->SetText(FText::GetEmpty()); Video->ApplyInterfaceOptions();
        TestEqual(TEXT("Empty setting restores default folder"),ACEScreenshotSettings::GetDirectory(),ACEScreenshotSettings::DefaultDirectory());
    }
    auto* Face=Video?Cast<UComboBoxString>(Video->WidgetTree->FindWidget(TEXT("ChatFontFace"))):nullptr;
    auto* FontSize=Video?Cast<UComboBoxString>(Video->WidgetTree->FindWidget(TEXT("ChatFontSize"))):nullptr;
    if(TestNotNull(TEXT("Retail chat font face control"),Face) && TestNotNull(TEXT("Retail chat font size control"),FontSize))
    {
        TestEqual(TEXT("All five retail font faces"),Face->GetOptionCount(),5);TestEqual(TEXT("All five retail font sizes"),FontSize->GetOptionCount(),5);
        Binder->AppendChatLineToLog(0,TEXT("Existing text changes font and wraps without losing its copy selection."),FLinearColor::White,FString());
        auto* Log=Binder->GetChatLogWidget(0);auto* Row=Cast<UACERetailTextBlock>(Log->GetChildAt(Log->GetChildrenCount()-1));
        for(int32 F=0;F<5;++F)for(int32 Size=0;Size<5;++Size)
        {
            Face->SetSelectedIndex(F);FontSize->SetSelectedIndex(Size);Video->ApplyInterfaceOptions();Binder->RefreshChatRowLayout(0);
            const auto* Font=Row->GetBitmapFont();
            TestTrue(TEXT("Every chosen face/size resolves a real retail atlas"),Font && Font->Id==ACERuntimeOptions::ChatFontId());
            TestTrue(TEXT("Changing fonts preserves selectable chat"),Row->IsSelectable());
        }
        Face->SetSelectedIndex(3);FontSize->SetSelectedIndex(4);Video->ApplyInterfaceOptions();
        Video->ResetVideo();TestEqual(TEXT("Reset retains applied Tahoma"),Face->GetSelectedIndex(),3);
        TestEqual(TEXT("Reset retains applied extra large size"),FontSize->GetSelectedIndex(),4);
        Draw(TEXT("ChatFontExtraLarge"));
        Video->DefaultsVideo();Video->ApplyInterfaceOptions();Binder->RefreshChatRowLayout(0);
        TestEqual(TEXT("Defaults restore original retail chat font"),Row->GetBitmapFont()->Id,0x40000000u);
    }

    // Resizing, wheel, arrow buttons, thumb dragging and row identity use the real DAT tree.
    Binder->ShowPanelPage(TEXT("SkillManagementPanel_Field")); Binder->SyncSkillPanelTab(TEXT("AttributePage"));
    const auto Floaty=Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field"));
    const auto List=Manager->FindElementUnder(TEXT("AttributePage"),TEXT("StatManagement_List"));
    const auto Bar=Manager->FindElementUnder(TEXT("AttributePage"),TEXT("StatManagement_List_Scrollbar"));
    if (!Floaty || !List || !Bar) return false;
    Floaty->UserResizeH=360-(Floaty->AuthoredHeight>=0?Floaty->AuthoredHeight:Floaty->Height);
    UACEUIElementManager::ApplyFloatyResizeLayout(Floaty);
    Draw(TEXT("AttributesSmall"));
    const int32 Page=FMath::Max(1,List->Height/18),MaxOffset=FMath::Max(0,9-Page);
    TestTrue(TEXT("Small attribute panel needs scrolling"),MaxOffset>0);
    Binder->ScrollStatList(-1); Draw(TEXT("AttributesScrolled"));
    TestTrue(TEXT("Wheel changes attribute offset"),Binder->AttributeScrollOffset>0);
    TestEqual(TEXT("Last visible attribute is Mana"),Binder->AttributeRows[FMath::Min(Page,9)-1]->GetText().ToString(),FString(TEXT("Mana")));
    const auto Up=Manager->FindElementUnder(TEXT("AttributePage"),TEXT("ScrollBar_Up"));
    Binder->OnElementActivated(Up);
    TestEqual(TEXT("Arrow steps one attribute"),Binder->AttributeScrollOffset,FMath::Max(0,MaxOffset-1));
    Draw(TEXT("AttributesArrow"));
    const FVector2D Bottom=FVector2D(Bar->GetScreenOrigin())+FVector2D(8,Bar->Height-18);
    TestTrue(TEXT("Attribute track starts a drag"),Binder->TryBeginScrollbarDrag(Bottom));
    Binder->UpdateScrollbarDrag(Bottom+FVector2D(0,500)); Binder->TryFinishScrollbarDrag();
    Draw(TEXT("AttributesDragEnd"));
    TestEqual(TEXT("Thumb reaches last attribute"),Binder->AttributeScrollOffset,MaxOffset);
    const auto* Paint=Canvas->PaintStates.Find(Bar->InstanceId);
    TestTrue(TEXT("Vertical chain track repeats instead of stretching"),Paint && Paint->Tiling==uint8(ESlateBrushTileType::Vertical));
    const int32 VisibleIndex=FMath::Min(Page,9)-1;
    const auto& Geometry=Binder->AttributeRowHighlights[VisibleIndex]->GetCachedGeometry();
    const auto ClickPoint=Canvas->GetElementLayer()->GetCachedGeometry().AbsoluteToLocal(Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5));
    TestTrue(TEXT("Scrolled attribute row can be clicked"),Binder->TryHandleOverlayClick(ClickPoint,false));
    TestEqual(TEXT("Scrolled click selects Mana, not the first-page row"),Binder->SelectedAttributeRow,8);
    Floaty->UserResizeH=600-Floaty->AuthoredHeight; UACEUIElementManager::ApplyFloatyResizeLayout(Floaty); Draw(TEXT("AttributesLarge"));
    TestEqual(TEXT("Expanding clamps the offset back to zero"),Binder->AttributeScrollOffset,0);
    TestFalse(TEXT("All nine attributes fit without scrollbar"),bool(Bar->bVisible));

    // No use packet or busy state for a quest-controlled door, including live changes.
    FACEWorldObject Door; Door.Guid=0x80001234; Door.ObjectDescriptionFlags=ACEObjectDescFlag::Door; Door.ItemUseable=1;
    Session.WorldObjects.Add(Door.Guid,Door); Session.bUseBusy=false;
    FString DoorMessage;
    const auto Chat=Session.OnChatMessage.AddLambda([&](const FString& Text,const FString&,int32){DoorMessage=Text;});
    const uint32 UseSequence=Session.NextGameActionSequence;
    Session.SendUseItem(Door.Guid);
    TestEqual(TEXT("Forbidden direct door use produces no action packet"),Session.NextGameActionSequence,UseSequence);
    TestFalse(TEXT("Forbidden door does not start use"),Session.bUseBusy);
    TestEqual(TEXT("Door feedback explains restriction"),DoorMessage,FString(TEXT("This door cannot be activated from here.")));
    for (int32 Use : {32,1})
    {
        FACEBinaryWriter W; W.WriteUInt8(1); W.WriteUInt32(Door.Guid); W.WriteUInt32(16); W.WriteInt32(Use);
        FACEBinaryReader R(W.GetData()); Session.HandlePublicUpdatePropertyInt(R);
        TestEqual(TEXT("Direct use follows live ItemUseable updates"),Session.WorldObjects[Door.Guid].IsDirectDoorUseBlocked(),Use==1);
        Session.bUseBusy=false; const uint32 BeforeUse=Session.NextGameActionSequence;
        Session.SendUseItem(Door.Guid);
        TestEqual(TEXT("Only an allowed door goes to the server for quest/lock checks"),Session.NextGameActionSequence,BeforeUse+(Use==32?1u:0u));
    }
    Door.ItemUseable=0; TestFalse(TEXT("Missing Undef property is not an explicit prohibition"),Door.IsDirectDoorUseBlocked());
    Session.OnChatMessage.Remove(Chat);

    // Existing keymap UI can rebind screenshot; exercise the actual engine save path.
    ACEInputBindings::Reload(); ACEInputBindings::BeginEdit(); ACEInputBindings::Defaults();
    const auto Screenshot=ACEInputBindings::Action(TEXT("CaptureScreenshot"));
    TestEqual(TEXT("Retail screenshot default is numpad multiply"),ACEInputBindings::Get(Screenshot,0).Key,EKeys::Multiply);
    ACEInputBindings::Set(Screenshot,0,FInputChord(EKeys::F12)); ACEInputBindings::Commit();
    TestTrue(TEXT("Rebound screenshot key is recognized"),ACEInputBindings::Matches(Screenshot,FInputChord(EKeys::F12)));
    TestFalse(TEXT("Old key is no longer active"),ACEInputBindings::Matches(Screenshot,FInputChord(EKeys::Multiply)));
#if PLATFORM_WINDOWS
    TestEqual(TEXT("Windows default is the redirected Documents/Asheron's Call folder"),ACEScreenshotSettings::DefaultDirectory(),FString(FPlatformProcess::UserDir())/TEXT("Asheron's Call"));
#endif
    const FString CaptureFolder=FPaths::ConvertRelativePathToFull(ArtDirectory/TEXT("Screenshot Output"));
    ACEScreenshotSettings::SetDirectory(CaptureFolder);
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
    Binder->RequestGameplayScreenshot();
    const FString Saved=Binder->PendingScreenshotFilename;
    TestFalse(TEXT("Screenshot request has a destination"),Saved.IsEmpty());
    TestTrue(TEXT("Capture uses configured path with spaces"),Saved.StartsWith(CaptureFolder));
    Binder->RequestGameplayScreenshot();
    TestEqual(TEXT("Repeated request cannot replace in-flight capture"),Binder->PendingScreenshotFilename,Saved);
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
        GEngine->GameViewport->ProcessScreenShots(GEngine->GameViewport->Viewport);
    TArray<uint8> Bytes; FFileHelper::LoadFileToArray(Bytes,*Saved);
    TestTrue(TEXT("Screenshot writes a real PNG"),Bytes.Num()>100 && Bytes[0]==137 && Bytes[1]=='P' && Bytes[2]=='N' && Bytes[3]=='G');
    TestTrue(TEXT("Saved screenshot feedback includes its path"),Binder->TransientInfoText && Binder->TransientInfoText->GetText().ToString().Contains(Saved));
    TestTrue(TEXT("Completion clears pending capture"),Binder->PendingScreenshotFilename.IsEmpty());
    Binder->PendingScreenshotFilename=FPaths::ProjectSavedDir()/TEXT("Automation/nonexistent-screenshot.png");
    Binder->FinishGameplayScreenshot();
    TestTrue(TEXT("Failed save is reported as failure"),Binder->TransientInfoText->GetText().ToString().StartsWith(TEXT("Unable to save screenshot")));
    if (!Saved.IsEmpty()) IFileManager::Get().Delete(*Saved);
    }
    else AddInfo(TEXT("Screenshot capture needs -game; editor-only run verifies folder settings."));
    Binder->Shutdown();
    TestEqual(TEXT("A new session starts tab memory from scratch"),Binder->SelectionSpellTab,INDEX_NONE);
    for (const auto& Tab:Binder->SpellTabSelections) TestEqual(TEXT("New tab default is first spell"),Tab.Slot,0);
    return true;
}
#endif
