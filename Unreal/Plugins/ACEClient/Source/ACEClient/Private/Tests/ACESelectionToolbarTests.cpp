#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Protocol/ACEIsaac.h"
#include "ACESession.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACERetailTextEntry.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/ACERetailObjectNames.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACESelectionToolbarTest, "ACE.RetailParity.SelectionToolbar",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACESelectionToolbarTest::RunTest(const FString&)
{
    TGuardValue<uint64> FrameGuard(GFrameCounter, GFrameCounter);
    auto* GI = NewObject<UGameInstance>();
    auto* Dat = NewObject<UACEDatSubsystem>(GI);
    if (!TestTrue(TEXT("Retail DAT opens"), Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    ON_SCOPE_EXIT { Dat->Deinitialize(); };
    auto* Resources = NewObject<UACEUIResourceResolver>(); Resources->Initialize(Dat);
    auto* Manager = NewObject<UACEUIElementManager>(); Manager->Initialize();
    auto* Layout = NewObject<UACEUILayoutResolver>(); Layout->Initialize(Dat, Manager);
    if (!TestTrue(TEXT("Retail gameplay layout opens"), Layout->LoadLayout(0x21000005))) return false;
    auto* Canvas = NewObject<UACEUICanvasWidget>(); Canvas->Initialize();
    Canvas->InitializeCanvas(Manager); Canvas->SetResourceResolver(Resources);
    const auto Slate = Canvas->TakeWidget();
    auto* Client = NewObject<UACEClientSubsystem>(GI); Client->Session = MakeShared<FACESession>();
    auto& Session = *Client->Session;
    Session.State = EACESessionState::InWorld; Session.PlayerGuid = 1234;
    FACEWorldObject Self; Self.Guid = Session.PlayerGuid; Self.Name = TEXT("Toolbar regression");
    Session.WorldObjects.Add(Self.Guid, Self);
    FACEWorldObject Item; Item.Guid = 200; Item.ContainerId = Self.Guid; Item.Name = TEXT("Bracelet");
    Item.MaterialType = 59; Item.ItemType = ACEItemType::Jewelry; Item.IconId = 0x060011CB;
    // Deliberately has structure: these values must never fabricate item mana.
    Item.Structure = 75; Item.MaxStructure = 100;
    Session.WorldObjects.Add(Item.Guid, Item);
    auto* Binder = NewObject<UACEUIGameplayBinder>(); Binder->Initialize(Client, Manager, Canvas, nullptr);
    Canvas->SetGameplayBinder(Binder);
    ON_SCOPE_EXIT { Binder->Shutdown(); Canvas->SetGameplayBinder(nullptr); Manager->Shutdown(); };
    const auto SelectionHandle = Session.OnSelectionChanged.AddLambda(
        [Client](const FACESelectedObject& Selection) { Client->OnSelectionChanged.Broadcast(Selection); });
    ON_SCOPE_EXIT { Session.OnSelectionChanged.Remove(SelectionHandle); };
    Manager->ApplyEdgeAnchoredLayout(1600, 900);
    const auto Text = Manager->FindElementByName(TEXT("SelectedObjectText"));
    const auto Field = Manager->FindElementByName(TEXT("SelectedObjectField"));
    const auto Health = Manager->FindElementByName(TEXT("ToolbarHealthMeter"));
    const auto Mana = Manager->FindElementByName(TEXT("ToolbarManaMeter"));
    const auto Stack = Manager->FindElementByName(TEXT("StackSizeSlider"));
    if (!TestTrue(TEXT("Authored toolbar controls exist"), Text && Field && Health && Mana && Stack)) return false;

    FWidgetRenderer Renderer(true, true);
    auto* Target = FWidgetRenderer::CreateTargetFor(FVector2D(1600, 900), TF_Bilinear, true);
    const FString ArtDirectory = FPaths::ProjectSavedDir() / TEXT("Automation/SelectionToolbar");
    IFileManager::Get().MakeDirectory(*ArtDirectory, true);
    auto Draw = [&](const TCHAR* Name, float DPIScale = 1.f)
    {
        const TSharedRef<SWidget> Scaled = SNew(SDPIScaler).DPIScale(DPIScale)[Slate];
        for (int32 Pass = 0; Pass < 3; ++Pass)
        {
            ++GFrameCounter;
            Canvas->NativeTick(Canvas->GetCachedGeometry(), 0.f);
            Renderer.DrawWidget(Target, Scaled, FVector2D(1600, 900), 0.f);
            FlushRenderingCommands();
        }
        TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(1600, 900, Pixels, PNG);
        FFileHelper::SaveArrayToFile(PNG, *(ArtDirectory / (FString(Name) + TEXT(".png"))));
    };
    Client->SelectObject(Item.Guid); Binder->RefreshSelectionOverlay();
    TestEqual(TEXT("Raw network material produces full selection name"), Binder->SelectionText->GetText().ToString(), FString(TEXT("Copper Bracelet")));
    TestFalse(TEXT("Structure never opens a mana vial"), bool(Mana->bVisible));
    TestFalse(TEXT("Jewelry never opens a health vial"), bool(Health->bVisible));
    TestFalse(TEXT("One item hides split controls"), bool(Stack->bVisible));
    TestEqual(TEXT("Selected field uses retail ObjectSelected state"), Field->DefaultState, 0x1000000Bu);
    auto* Label = Cast<UACERetailTextBlock>(Binder->SelectionText);
    if (!TestNotNull(TEXT("Selected name uses the DAT bitmap text renderer"), Label)) return false;
    if (!TestNotNull(TEXT("Selected name resolves its bitmap font"), Label->GetBitmapFont())) return false;
    TestEqual(TEXT("Selection uses authored font 0x40000002"), Label->GetBitmapFont()->Id, 0x40000002u);
    TestTrue(TEXT("Selection uses authored white text"), Label->GetColorAndOpacity().GetSpecifiedColor().Equals(FLinearColor::White));
    TestEqual(TEXT("Ordinary selection is centered"), int32(Label->GetTextJustification()), int32(ETextJustify::Center));
    TestFalse(TEXT("Ordinary names wrap at authored width"), Text->bTextOneLine);
    const auto* TextSlot = Cast<UCanvasPanelSlot>(Label->Slot);
    if (!TestNotNull(TEXT("Name is attached to the toolbar canvas"), TextSlot)) return false;
    TestTrue(TEXT("Name retains full authored 140x31 bounds"), TextSlot->GetSize().Equals(FVector2D(140, 31)));
    Draw(TEXT("CopperBracelet"));

    Session.WorldObjects[Item.Guid].Name = TEXT("Copper Bracelet"); Binder->RefreshSelectionOverlay();
    TestEqual(TEXT("Already decorated material is not doubled"), Label->GetText().ToString(), FString(TEXT("Copper Bracelet")));
    Session.SelectedObject.bShowMana = true; Session.SelectedObject.ManaFraction = .4f;
    Binder->RefreshSelectionOverlay();
    TestTrue(TEXT("Successful item mana reply exposes the authored mana vial"), bool(Mana->bVisible));
    TestEqual(TEXT("Mana fill comes from response independently of structure"), Mana->MeterFillFraction, .4f);
    TestEqual(TEXT("Mana keeps the authored background image"), Mana->ImageFileId, 0x060022D5u);
    TestEqual(TEXT("Mana keeps the native 31px vial height"), Mana->Height, 31);
    Draw(TEXT("CopperBraceletMana"));
    Session.SelectedObject.ManaFraction = 0.f; Binder->RefreshSelectionOverlay();
    TestTrue(TEXT("Empty but valid item mana remains visible"), bool(Mana->bVisible));
    TestEqual(TEXT("Empty mana has zero fill"), Mana->MeterFillFraction, 0.f);

    Session.SelectedObject.bShowHealth = true; Session.SelectedObject.HealthFraction = .65f;
    Binder->RefreshSelectionOverlay();
    TestTrue(TEXT("Health selection exposes authored health vial"), bool(Health->bVisible));
    TestFalse(TEXT("Health takes precedence over stale mana"), bool(Mana->bVisible));
    TestEqual(TEXT("Health uses response fraction"), Health->MeterFillFraction, .65f);
    TestEqual(TEXT("Health retains authored background image"), Health->ImageFileId, 0x0600193Eu);
    Draw(TEXT("Health"));

    auto& StackItem = Session.WorldObjects[Item.Guid]; StackItem.Name = TEXT("Arrow");
    StackItem.PluralName = TEXT("Arrows"); StackItem.MaterialType = 64; StackItem.StackSize = StackItem.MaxStackSize = 50;
    Binder->SelectedStackAmount = 50; Binder->RefreshSelectionOverlay();
    TestTrue(TEXT("A stack always shows split controls"), bool(Stack->bVisible));
    TestFalse(TEXT("A stack suppresses even a stale health reply"), bool(Health->bVisible));
    TestFalse(TEXT("A stack suppresses even a stale mana reply"), bool(Mana->bVisible));
    TestEqual(TEXT("Stack name uses plural and material"), Label->GetText().ToString(), FString(TEXT("50 Steel Arrows")));
    TestTrue(TEXT("Stack name is one line above quantity controls"), Text->bTextOneLine);
    TestEqual(TEXT("Stack name aligns left in retail state"), int32(Label->GetTextJustification()), int32(ETextJustify::Left));
    TestEqual(TEXT("Stack uses retail selected field state"), Field->DefaultState, 0x1000000Cu);
    Draw(TEXT("Stack"));

    const auto Track = Stack->Children[0], Thumb = Stack->Children[1];
    TestEqual(TEXT("Retail stack track art"), Track->ImageFileId, 0x06004CF6u);
    TestEqual(TEXT("Retail stack thumb art"), Thumb->ImageFileId, 0x06005DC3u);
    TestEqual(TEXT("Native thumb is 16 pixels wide"), Thumb->Width, 16);
    if (!TestNotNull(TEXT("Stack quantity is editable DAT text"), Binder->StackAmountEntry.Get())) return false;
    auto* Entry = Binder->StackAmountEntry.Get();
    TestTrue(TEXT("Quantity only accepts digits"), Entry->bDigitsOnly);
    TestTrue(TEXT("Quantity uses authored right padding"), Entry->ContentMargins.IsSet() && Entry->ContentMargins->Right == 2.f);
    auto Commit = [&](const TCHAR* Value) { Entry->SetText(FText::FromString(Value)); Entry->Commit(ETextCommit::OnEnter); };
    Commit(TEXT("7"));
    TestEqual(TEXT("Typed amount immediately updates selected quantity"), Binder->SelectedStackAmount, 7);
    TestEqual(TEXT("Partial quantity does not shrink the track"), Track->Width, Stack->Width);
    TestEqual(TEXT("Typed quantity immediately repositions thumb"), Thumb->X, 10);
    TestEqual(TEXT("Typed quantity immediately updates name"), Label->GetText().ToString(), FString(TEXT("7 Steel Arrows (of 50)")));
    const auto Frame = Manager->FindElementByName(TEXT("SelectionBlinkField"));
    TestTrue(TEXT("Opaque selection frame paints below the slider"), Field->Children.IndexOfByKey(Frame) < Field->Children.IndexOfByKey(Stack));
    Draw(TEXT("StackPartial"));
    Draw(TEXT("StackPartial150"), 1.5f);
    Draw(TEXT("StackPartial"));
    Commit(TEXT("9999999999")); TestEqual(TEXT("Large quantity clamps to stack"), Binder->SelectedStackAmount, 50);
    Commit(TEXT("")); TestEqual(TEXT("Empty quantity clamps to one"), Binder->SelectedStackAmount, 1);
    Commit(TEXT("0")); TestEqual(TEXT("Zero quantity clamps to one"), Binder->SelectedStackAmount, 1);
    Commit(TEXT("25"));
    const auto Origin = Stack->GetScreenOrigin();
    const FVector2D Grab(Origin.X + Thumb->X + 3, Origin.Y + 7);
    TestTrue(TEXT("Thumb starts dragging"), Binder->TryBeginScrollbarDrag(Grab));
    Binder->UpdateScrollbarDrag(Grab);
    TestEqual(TEXT("Grabbing thumb does not change quantity"), Binder->SelectedStackAmount, 25);
    Binder->UpdateScrollbarDrag(Grab + FVector2D(-200, 0));
    TestEqual(TEXT("Dragging left clamps to one"), Binder->SelectedStackAmount, 1);
    Binder->UpdateScrollbarDrag(Grab + FVector2D(200, 0));
    TestEqual(TEXT("Dragging right selects full stack"), Binder->SelectedStackAmount, 50);
    Binder->TryFinishScrollbarDrag();
    TestTrue(TEXT("Track click starts dragging"), Binder->TryBeginScrollbarDrag(FVector2D(Origin.X + 44.5, Origin.Y + 7)));
    TestEqual(TEXT("Retail midpoint quantization selects 26 of 50"), Binder->SelectedStackAmount, 26);
    TestEqual(TEXT("Slider updates typed amount"), Entry->GetText().ToString(), FString(TEXT("26")));
    Binder->TryFinishScrollbarDrag();

    // Exercise the actual editable Slate widget, including hotkey focus/select-all.
    const auto EntrySlate = Entry->TakeWidget();
    EntrySlate->OnFocusReceived(Entry->GetCachedGeometry(), FFocusEvent(EFocusCause::SetDirectly, 0));
    EntrySlate->OnKeyChar(Entry->GetCachedGeometry(), FCharacterEvent('9', FModifierKeysState(), 0, false));
    EntrySlate->OnKeyChar(Entry->GetCachedGeometry(), FCharacterEvent('x', FModifierKeysState(), 0, false));
    TestEqual(TEXT("Typing replaces focused amount and rejects letters"), Entry->GetText().ToString(), FString(TEXT("9")));
    EntrySlate->OnKeyDown(Entry->GetCachedGeometry(), FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
    TestEqual(TEXT("Enter commits typed quantity"), Binder->SelectedStackAmount, 9);
    EntrySlate->OnFocusReceived(Entry->GetCachedGeometry(), FFocusEvent(EFocusCause::SetDirectly, 0));
    EntrySlate->OnKeyChar(Entry->GetCachedGeometry(), FCharacterEvent('3', FModifierKeysState(), 0, false));
    EntrySlate->OnKeyDown(Entry->GetCachedGeometry(), FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
    TestEqual(TEXT("Escape restores previous quantity"), Binder->SelectedStackAmount, 9);
    StackItem.StackSize = 5; Binder->RefreshSelectionOverlay();
    TestEqual(TEXT("Server stack decrease clamps quantity"), Binder->SelectedStackAmount, 5);
    TestEqual(TEXT("Server stack decrease updates entry"), Entry->GetText().ToString(), FString(TEXT("5")));

    StackItem.StackSize=12;Binder->RefreshSelectionOverlay();
    TestEqual(TEXT("A merged full stack selects the new full quantity"),Binder->SelectedStackAmount,12);
    TestEqual(TEXT("Merged toolbar count uses the current total"),Label->GetText().ToString(),FString(TEXT("12 Steel Arrows")));
    Commit(TEXT("7"));StackItem.StackSize=15;Binder->RefreshSelectionOverlay();
    TestEqual(TEXT("Inventory growth preserves an explicit partial quantity"),Binder->SelectedStackAmount,7);
    Commit(TEXT("15"));StackItem.StackSize=21;Session.OnSelectionChanged.Broadcast(Session.SelectedObject);
    TestEqual(TEXT("Selection notification also expands a full stack after merging"),Binder->SelectedStackAmount,21);
    TestEqual(TEXT("Selection notification refreshes combined stack label"),Label->GetText().ToString(),FString(TEXT("21 Steel Arrows")));

    StackItem.StackSize = 1; Binder->RefreshSelectionOverlay();
    TestEqual(TEXT("One remaining stackable item uses a singular uncounted name"), Label->GetText().ToString(), FString(TEXT("Steel Arrow")));
    TestFalse(TEXT("One remaining stackable item hides split controls"), bool(Stack->bVisible));
    StackItem.StackSize = StackItem.MaxStackSize = 1;
    StackItem.Name = TEXT("Ornately Engraved Bracelet"); StackItem.MaterialType = 59;
    Session.SelectedObject.bShowHealth = false; Session.SelectedObject.bShowMana = true;
    Session.SelectedObject.ManaFraction = .4f;
    Binder->RefreshSelectionOverlay();
    TestFalse(TEXT("Returning to a nonstack restores wrapped names"), Text->bTextOneLine);
    TestEqual(TEXT("Long selection names retain native bitmap font"), Label->GetBitmapFont()->Id, 0x40000002u);
    Draw(TEXT("LongName150"), 1.5f);
    TestTrue(TEXT("Scaled capture uses the real 150 percent Slate transform"),
        (Canvas->GetCachedGeometry().LocalToAbsolute(FVector2D(100, 0))
            - Canvas->GetCachedGeometry().LocalToAbsolute(FVector2D::ZeroVector)).Equals(FVector2D(150, 0), .1f));
    TestTrue(TEXT("DPI scale does not shrink the authored name field"), TextSlot->GetSize().Equals(FVector2D(140, 31)));

    Item.Name = TEXT("Compass"); Item.MaterialType = 0;
    TestEqual(TEXT("Missing explicit plural follows retail es fallback"), ACERetailObjectNames::Name(Item, true), FString(TEXT("Compasses")));
    Item.Name = TEXT("Arrow");
    TestEqual(TEXT("Missing explicit plural follows retail s fallback"), ACERetailObjectNames::Name(Item, true), FString(TEXT("Arrows")));
    Item.Name = TEXT("+Admin"); Item.ObjectDescriptionFlags = ACEObjectDescFlag::HiddenAdmin;
    TestEqual(TEXT("Retail hidden administrator prefix is omitted"), ACERetailObjectNames::Name(Item), FString(TEXT("Admin")));
    Client->SelectObject(0); Binder->RefreshSelectionOverlay();
    TestFalse(TEXT("Clearing selection hides health"), bool(Health->bVisible));
    TestFalse(TEXT("Clearing selection hides mana"), bool(Mana->bVisible));
    TestFalse(TEXT("Clearing selection hides split controls"), bool(Stack->bVisible));
    TestEqual(TEXT("Clearing restores empty field state"), Field->DefaultState, 0u);
    TestEqual(TEXT("Clearing hides name overlay"), Label->GetVisibility(), ESlateVisibility::Collapsed);
    FACEWorldObject Corpse;Corpse.Guid=400;Corpse.Name=TEXT("Corpse of rat");Corpse.ItemType=ACEItemType::Container;Corpse.ItemsCapacity=24;
    Session.WorldObjects.Add(Corpse.Guid,Corpse);Session.OpenExternalContainerGuid=Corpse.Guid;
    FACEWorldObject Loot=Item;Loot.Guid=401;Loot.ContainerId=Corpse.Guid;Loot.ObjectDescriptionFlags=0;Loot.Name=TEXT("Ring");
    Session.WorldObjects.Add(Loot.Guid,Loot);
    FACEContainerItemRef Ref;Ref.ItemGuid=Loot.Guid;Ref.ContainerType=0;Session.ContainerContents.Add(Corpse.Guid,{Ref});
    Binder->HandleExternalContainerOpened(Corpse.Guid);Draw(TEXT("CorpseBeforeID"));
    const int32 LootSlot=Binder->ExtItemGuids.IndexOfByKey(Loot.Guid);
    if(TestTrue(TEXT("Corpse loot is displayed"),Binder->ExtItemSlots.IsValidIndex(LootSlot)))
    {
        const auto& Geometry=Binder->ExtItemSlots[LootSlot]->GetCachedGeometry();
        const auto Point=Canvas->GetCachedGeometry().AbsoluteToLocal(Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5));
        Client->SelectObject(Corpse.Guid);
        TestTrue(TEXT("Right click is handled by the corpse slot"),Binder->TryHandleOverlayClick(Point,true));
        TestEqual(TEXT("Right click selects the inspected loot for the pickup binding"),Session.GetSelectedObject().Guid,Loot.Guid);
        TestEqual(TEXT("Binder pickup selection follows inspection"),Binder->LastSelection.Guid,Loot.Guid);
    }
    Binder->HandleExternalContainerClosed(Corpse.Guid);Session.OpenExternalContainerGuid=0;
    FACEWorldObject Ammo;Ammo.Guid=300;Ammo.WielderId=Self.Guid;Ammo.CurrentWieldedLocation=ACEEquipMask::MissileAmmo;Ammo.StackSize=868;
    Session.WorldObjects.Add(Ammo.Guid,Ammo);
    Binder->CombatMode=int32(ACECombatMode::Missile);Binder->SyncCombatModeButtons();Binder->SyncCombatAmmoCount();
    TestEqual(TEXT("Readied arrow count appears on missile button"),Binder->CombatAmmoLabel->GetText().ToString(),FString(TEXT("868")));
    Session.WorldObjects[Ammo.Guid].StackSize=867;Binder->SyncCombatAmmoCount();
    TestEqual(TEXT("Count updates after firing"),Binder->CombatAmmoLabel->GetText().ToString(),FString(TEXT("867")));
    FACEWorldObject Thrown=Ammo;Thrown.Guid=301;Thrown.CurrentWieldedLocation=ACEEquipMask::MissileWeapon;Thrown.MaxStackSize=100;Thrown.StackSize=20;
    Session.WorldObjects.Add(Thrown.Guid,Thrown);Binder->SyncCombatAmmoCount();
    TestEqual(TEXT("Thrown stack takes precedence over ammunition"),Binder->CombatAmmoLabel->GetText().ToString(),FString(TEXT("20")));
    Draw(TEXT("MissileAmmo"));
    Session.WorldObjects.Remove(Ammo.Guid);Session.WorldObjects.Remove(Thrown.Guid);Binder->SyncCombatAmmoCount();
    TestEqual(TEXT("No ammunition clears the count"),Binder->CombatAmmoLabel->GetVisibility(),ESlateVisibility::Collapsed);
    TestTrue(TEXT("UI regression has no network connection"), Session.CachedC2SPackets.IsEmpty());
    {
        const auto Bag=Manager->FindElementByName(TEXT("InventoryButton"));
        if(!TestTrue(TEXT("Toolbar backpack exists"),Bag.IsValid()))return false;
        Binder->ShowPanelPage(TEXT("InventoryPanel_Field"));Binder->SyncInventoryButtonVisual();
        const auto Open=Bag->ResolvePaintState(false,false,false);
        TestTrue(TEXT("Backpack retains retail open artwork after pointer leaves"),Open && Open->ImageFileId==0x06004CF8u);
        Draw(TEXT("InventoryOpenToolbar"));
        Binder->HidePanel();
        const auto Closed=Bag->ResolvePaintState(false,false,false);
        TestTrue(TEXT("Closing inventory restores the closed backpack"),Closed && Closed->ImageFileId==0x06004CF7u);

        FACEWorldObject First=Item;First.Guid=702;First.Name=TEXT("First vendor stock");First.VendorQuantityAvailable=-1;
        FACEWorldObject Second=First;Second.Guid=701;Second.Name=TEXT("Second vendor stock");
        Session.WorldObjects.Add(First.Guid,First);Session.WorldObjects.Add(Second.Guid,Second);
        Session.VendorMerchandise={First,Second};
        Binder->ShowVendorPanel(700);
        TestEqual(TEXT("Opening vendor selects leftmost stock in displayed order"),Session.SelectedObject.Guid,First.Guid);
        TestEqual(TEXT("Buy/Add to List use that initial selection"),Binder->VendorSelectedGuid,First.Guid);
        Binder->VendorSelectedGuid=Second.Guid;Client->SelectObject(Second.Guid);
        Binder->HandleVendorOpened(700);
        TestEqual(TEXT("Same-vendor updates retain the user's selection"),Session.SelectedObject.Guid,Second.Guid);
        Binder->HideVendorPanel();Binder->ShowVendorPanel(700);
        TestEqual(TEXT("Reopening vendor resets to first merchandise"),Session.SelectedObject.Guid,First.Guid);
        Binder->HideVendorPanel();Session.VendorMerchandise.Reset();
    }
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;PC->DatGameplayBinder=Binder;
    Binder->PlayerController=PC;
    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto Address=Sockets->CreateInternetAddr();bool Valid=false;Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(0);
    auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("UI pickup receiver"),false);
    if(!TestTrue(TEXT("Loopback binds"),Receiver && Receiver->Bind(*Address))) return false;
    Receiver->GetAddress(*Address);
    Session.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("UI pickup sender"),false);
    Session.SocketC2S->SetNonBlocking(true);Session.ServerC2SAddr=Address;Session.IssacClient=MakeUnique<FACEIsaac>(123u);
    ON_SCOPE_EXIT {
        Session.SocketC2S->Close();Sockets->DestroySocket(Session.SocketC2S);Session.SocketC2S=nullptr;
        Receiver->Close();Sockets->DestroySocket(Receiver);Session.ServerC2SAddr.Reset();
        Binder->PlayerController=nullptr;PC->Client=nullptr;PC->DatGameplayBinder=nullptr;World->DestroyWorld(false);
    };
    auto HasAction=[&](uint32 Action,int32 Object)
    {
        for(const auto& Pair:Session.CachedC2SPackets)
        {
            FACEBinaryReader Wire(Pair.Value.Payload);Wire.Skip(16);
            if(Wire.ReadUInt32()!=ACEOpcode::GameAction)continue;Wire.ReadUInt32();
            if(Wire.ReadUInt32()==Action && Wire.ReadInt32()==Object)return true;
        }
        return false;
    };
    Session.WorldObjects[Self.Guid].ItemsCapacity=24;
    Session.OpenExternalContainerGuid=Corpse.Guid;Client->SelectObject(Loot.Guid);
    PC->InteractWithSelectedObject();
    TestTrue(TEXT("F pickup sends inspected corpse item's GUID to server"),HasAction(ACEGameAction::PutItemInContainer,Loot.Guid));
    Session.OpenExternalContainerGuid=0;
    FACEWorldObject NPC;NPC.Guid=450;NPC.Name=TEXT("Previously selected NPC");NPC.ItemType=ACEItemType::Creature;
    Session.WorldObjects.Add(NPC.Guid,NPC);Client->SelectObject(NPC.Guid);
    Session.CachedC2SPackets.Reset();
    Session.WorldObjects[Item.Guid].ObjectDescriptionFlags=0;
    Binder->InvDragGuid=Item.Guid;Binder->InvDragSourcePack=Self.Guid;Binder->bInvDragPending=Binder->bInvDragActive=true;
    TestTrue(TEXT("Empty-world drag release is consumed"),Binder->TryFinishInventoryDrag(FVector2D(800,300)));
    TestTrue(TEXT("Empty release drops the item despite an old NPC selection"),HasAction(ACEGameAction::DropItem,Item.Guid));
    TestFalse(TEXT("Drag miss cannot give to the old selection"),HasAction(ACEGameAction::GiveObjectRequest,NPC.Guid));
    {
        FACEWorldObject Stone;Stone.Guid=800;Stone.ContainerId=Self.Guid;Stone.ItemType=ACEItemType::ManaStone;
        Stone.ItemUseable=0x00080008;Stone.TargetType=ACEItemType::Jewelry;Session.WorldObjects.Add(Stone.Guid,Stone);
        Session.bUseBusy=false;Session.CachedC2SPackets.Reset();Binder->PendingUseWithSourceGuid=Stone.Guid;
        TestFalse(TEXT("Corpse is not a compatible mana-stone cursor target"),Binder->IsPendingUseTargetCompatible(Corpse.Guid));
        TestTrue(TEXT("Clicking an invalid target is consumed"),Binder->TryCompletePendingUseWithTarget(Corpse.Guid));
        TestEqual(TEXT("Invalid corpse never offers destruction confirmation"),Binder->ManaStoneConfirmTarget,0);
        TestEqual(TEXT("Invalid target leaves targeting active"),Binder->PendingUseWithSourceGuid,Stone.Guid);
        TestFalse(TEXT("Invalid corpse sends no use packet"),HasAction(ACEGameAction::UseWithTarget,Stone.Guid));
        Binder->TryCompletePendingUseWithTarget(Loot.Guid);
        TestEqual(TEXT("An unowned item is also excluded from an inventory-only target mode"),Binder->ManaStoneConfirmTarget,0);
        Binder->TryCompletePendingUseWithTarget(Item.Guid);
        TestEqual(TEXT("Compatible owned item still asks for destruction confirmation"),Binder->ManaStoneConfirmTarget,Item.Guid);
        // Ownership can change between opening and accepting the confirmation.
        Session.WorldObjects[Item.Guid].ContainerId=Corpse.Guid;Binder->FinishManaStoneConfirmation(true);
        TestFalse(TEXT("Confirmation rechecks target eligibility"),HasAction(ACEGameAction::UseWithTarget,Stone.Guid));
        Session.WorldObjects[Item.Guid].ContainerId=Self.Guid;
    }
    return !HasAnyErrors();
}
#endif
