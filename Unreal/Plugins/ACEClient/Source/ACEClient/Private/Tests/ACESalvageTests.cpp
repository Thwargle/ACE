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
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIGameplayBinder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACESalvageTest, "ACE.RetailParity.Salvage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACESalvageTest::RunTest(const FString&)
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
    FACEWorldObject Self; Self.Guid = Session.PlayerGuid; Self.Name = TEXT("Salvage regression");
    Session.WorldObjects.Add(Self.Guid, Self);
    auto AddItem = [&](int32 Guid, int32 Material, int32 Container = 1234)
    {
        FACEWorldObject Item; Item.Guid = Guid; Item.ContainerId = Container;
        Item.MaterialType = Material; Item.ItemType = ACEItemType::Misc; Item.IconId = 0x060011CB;
        Item.Name = FString::Printf(TEXT("Salvage item %d"), Guid);
        Session.WorldObjects.Add(Guid, Item);
    };
    AddItem(200, 0); Session.WorldObjects[200].ItemType = 0x20000000;
    Session.WorldObjects[200].WeenieClassId = 20630; Session.WorldObjects[200].Name = TEXT("Ust");
    auto* Binder = NewObject<UACEUIGameplayBinder>(); Binder->Initialize(Client, Manager, Canvas, nullptr);
    Canvas->SetGameplayBinder(Binder);
    ON_SCOPE_EXIT { Binder->Shutdown(); Canvas->SetGameplayBinder(nullptr); Manager->Shutdown(); };
    const auto SelectionHandle = Session.OnSelectionChanged.AddLambda(
        [Client](const FACESelectedObject& Selection) { Client->OnSelectionChanged.Broadcast(Selection); });
    ON_SCOPE_EXIT { Session.OnSelectionChanged.Remove(SelectionHandle); };
    FWidgetRenderer Renderer(true, true);
    auto* Target = FWidgetRenderer::CreateTargetFor(FVector2D(1600, 900), TF_Bilinear, true);
    const FString ArtDirectory = FPaths::ProjectSavedDir() / TEXT("Automation/Salvage");
    IFileManager::Get().MakeDirectory(*ArtDirectory, true);
    auto Draw = [&](const TCHAR* Name, float DPIScale = 1.f)
    {
        const TSharedRef<SWidget> Scaled = SNew(SDPIScaler).DPIScale(DPIScale)[Slate];
        for (int32 Pass = 0; Pass < 4; ++Pass)
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
    auto ClickPoint = [&](FVector2D LayoutPoint, FKey Button = EKeys::LeftMouseButton)
    {
        const auto& Geometry = Canvas->GetCachedGeometry();
        const FVector2D Point = Geometry.LocalToAbsolute(Canvas->LayoutToViewport(LayoutPoint));
        const FPointerEvent Down(0, Point, Point, TSet<FKey>{Button}, Button, 0, FModifierKeysState());
        const FPointerEvent Up(0, Point, Point, TSet<FKey>{}, Button, 0, FModifierKeysState());
        Canvas->NativeOnMouseButtonDown(Geometry, Down); Canvas->NativeOnMouseButtonUp(Geometry, Up);
    };
    Manager->ApplyEdgeAnchoredLayout(1600, 900);
    Binder->UseInventoryItem(200);
    Draw(TEXT("Empty"));
    TestEqual(TEXT("Using an owned Ust opens its actual salvage panel"), Binder->OpenSalvageToolGuid, 200);
    const auto Panel = Manager->FindElementByName(TEXT("SalvagePanel"));
    const auto List = Manager->FindElementUnder(TEXT("SalvagePanel"), TEXT("SalvageItemsList"));
    const auto Button = Manager->FindElementUnder(TEXT("SalvagePanel"), TEXT("Salvage_Button"));
    const auto Scrollbar = Manager->FindElementUnder(TEXT("SalvagePanel"), TEXT("Salvage_ItemListScroll"));
    if (!TestTrue(TEXT("All authored salvage controls exist"), Panel && List && Button && Scrollbar)) return false;
    TestTrue(TEXT("Empty salvage panel is visible"), bool(Panel->bVisible));
    TestTrue(TEXT("Empty Salvage button stays visible"), bool(Button->bVisible));
    TestFalse(TEXT("Empty Salvage button is disabled"), Button->bActivatable);
    TestFalse(TEXT("Retail scrollbar is hidden while the list fits"), bool(Scrollbar->bVisible));
    TestEqual(TEXT("Empty salvage exposes every authored slot square"), Binder->SalvageItemBackgrounds.Num(), Binder->SalvageVisibleSlots);
    for (const auto& Background : Binder->SalvageItemBackgrounds)
        TestTrue(TEXT("Empty salvage slots retain the retail inventory square"), Background
            && Background->GetVisibility() == ESlateVisibility::HitTestInvisible
            && Background->Background.GetResourceObject() == Resources->ResolveIconTexture(0x06004D20));
    TestEqual(TEXT("Retail destructive warning remains visible"), Binder->SalvageWarningLabel->GetText().ToString(),
        FString(TEXT("WARNING: Items in this panel will be destroyed!")));
    TestTrue(TEXT("Opening Ust does not send an action"), Session.CachedC2SPackets.IsEmpty());

    Binder->HideSalvagePanel(); Client->SelectObject(200);
    Binder->HandleNamedClick(TEXT("PanelButton_UseSelectedButton"));
    TestEqual(TEXT("Toolbar Use dispatches owned Ust to salvage"), Binder->OpenSalvageToolGuid, 200);
    TestEqual(TEXT("Toolbar Use does not start inventory sorting"), Client->SortSourceGuid, 0);
    Binder->HideSalvagePanel(); Binder->ShowPanelPage(TEXT("InventoryPanel_Field"));
    Draw(TEXT("InventoryBeforeDoubleClick"));
    const int32 ToolSlot = Binder->InventorySlotGuids.Find(200);
    if (TestTrue(TEXT("Owned Ust is present in actual inventory slots"), Binder->InventorySlots.IsValidIndex(ToolSlot)))
    {
        const auto* Slot = Cast<UCanvasPanelSlot>(Binder->InventorySlots[ToolSlot]->Slot);
        if (!TestNotNull(TEXT("Inventory Ust has a canvas slot"), Slot)) return false;
        const FVector2D Local = Slot->GetPosition() + Slot->GetSize() * .5;
        ClickPoint(Canvas->ViewportToLayout(Local));
        const auto& Geometry = Canvas->GetCachedGeometry();
        const FVector2D Point = Geometry.LocalToAbsolute(Local);
        const FPointerEvent DoubleClick(0, Point, Point, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0, FModifierKeysState());
        const FPointerEvent Release(0, Point, Point, TSet<FKey>{}, EKeys::LeftMouseButton, 0, FModifierKeysState());
        Canvas->NativeOnMouseButtonDoubleClick(Geometry, DoubleClick);
        Canvas->NativeOnMouseButtonUp(Geometry, Release);
        TestEqual(TEXT("Actual Slate double click on Ust opens salvage"), Binder->OpenSalvageToolGuid, 200);
    }

    AddItem(1000, 64); AddItem(1001, 61);
    Binder->RefreshInventoryOverlays();
    const int32 SourceSlot = Binder->InventorySlotGuids.Find(1000);
    if (!TestTrue(TEXT("Salvage source has a visible inventory slot"), Binder->InventorySlots.IsValidIndex(SourceSlot))) return false;
    UObject* const OriginalForeground = Binder->InventorySlots[SourceSlot]->Background.GetResourceObject();
    if (!TestNotNull(TEXT("Salvage source icon resolves from DAT"), OriginalForeground)) return false;
    TestTrue(TEXT("Steel can be queued"), Binder->AddItemToSalvageQueue(1000));
    TestEqual(TEXT("Salvage offer names use the same material prefix as inventory"),
        Binder->SalvageItemSlots[0]->GetToolTipText().ToString(), FString(TEXT("Steel Salvage item 1000")));
    Binder->RefreshInventoryOverlays();
    TestTrue(TEXT("Queueing adds the retail offered overlay to the source inventory icon"),
        Binder->InventorySlots[SourceSlot]->Background.GetResourceObject() != OriginalForeground);
    Binder->RemoveItemFromSalvageQueue(1000); Binder->RefreshInventoryOverlays();
    TestTrue(TEXT("Removing from salvage restores the source inventory foreground"),
        Binder->InventorySlots[SourceSlot]->Background.GetResourceObject() == OriginalForeground);
    Binder->AddItemToSalvageQueue(1000);
    TestFalse(TEXT("Duplicate is rejected"), Binder->AddItemToSalvageQueue(1000));
    TestFalse(TEXT("Single material option rejects iron after steel"), Binder->CanAddItemToSalvageQueue(1001));
    Session.CharacterOptions2 |= 0x80;
    TestTrue(TEXT("Retail SalvageMultiple flag allows iron after steel"), Binder->AddItemToSalvageQueue(1001));
    AddItem(1002, 64); Session.WorldObjects[1002].ObjectDescriptionFlags = ACEObjectDescFlag::Retained;
    AddItem(1003, 64); Session.WorldObjects[1003].WielderId = 1234;
    AddItem(1004, 64, 9999); AddItem(1005, 64); Session.WorldObjects[1005].Structure = 100;
    for (int32 Guid : {1002, 1003, 1004, 1005, 200})
        TestFalse(FString::Printf(TEXT("Retained, equipped, unowned, full, or tool item %d rejected"), Guid),
            Binder->CanAddItemToSalvageQueue(Guid));
    AddItem(1006, 0);
    for (int32 Material : {0, 3, 9, 56, 65, 72, 78})
    {
        Session.WorldObjects[1006].MaterialType = Material;
        TestFalse(FString::Printf(TEXT("Invalid material category %d rejected"), Material), Binder->CanAddItemToSalvageQueue(1006));
    }
    Session.WorldObjects[1006].MaterialType = 77;
    TestTrue(TEXT("Last retail material (teak) accepted"), Binder->CanAddItemToSalvageQueue(1006));
    Session.WorldObjects[1005].Structure = 99;
    TestTrue(TEXT("Partial salvage bag accepted"), Binder->CanAddItemToSalvageQueue(1005));

    Binder->ShowSalvagePanel(200);
    AddItem(2000, 52); Session.WorldObjects[2000].ItemsCapacity = 24;
    AddItem(2001, 0, 2000); Session.WorldObjects[2001].ItemsCapacity = 24;
    AddItem(2002, 64, 2000); AddItem(2003, 61, 2001);
    AddItem(2004, 64, 2001); Session.WorldObjects[2004].ObjectDescriptionFlags = ACEObjectDescFlag::Retained;
    TestTrue(TEXT("A pack queues eligible nested contents"), Binder->AddItemToSalvageQueue(2000));
    TestEqual(TEXT("Only the two eligible leaves were queued"), Binder->SalvageQueueGuids.Num(), 2);
    TestTrue(TEXT("Nested inventory item included"), Binder->SalvageQueueGuids.Contains(2003));
    TestFalse(TEXT("Nonempty parent container is never queued"), Binder->SalvageQueueGuids.Contains(2000));
    Binder->ShowSalvagePanel(200);
    Session.TradeSelfItems.Add(2001);
    TestFalse(TEXT("Items inside a traded pack are excluded"), Binder->CanAddItemToSalvageQueue(2003));
    Session.TradeSelfItems.Reset();
    AddItem(3000, 64, 3001); AddItem(3001, 64, 3000);
    TestFalse(TEXT("Cyclic ownership does not hang or authorize salvage"), Binder->CanAddItemToSalvageQueue(3000));
    Binder->AddItemToSalvageQueue(1000); Session.WorldObjects[1000].ContainerId = 9999;
    Binder->RefreshSalvageOverlays();
    TestTrue(TEXT("Inventory ownership loss prunes queued item"), Binder->SalvageQueueGuids.IsEmpty());
    Session.WorldObjects[1000].ContainerId = 1234;
    Binder->AddItemToSalvageQueue(1000); Session.WorldObjects[1000].ObjectDescriptionFlags = ACEObjectDescFlag::Retained;
    Binder->SubmitSalvageQueue();
    TestTrue(TEXT("Retaining after queueing prevents submission"), Session.CachedC2SPackets.IsEmpty());
    Session.WorldObjects[1000].ObjectDescriptionFlags = 0;
    AddItem(2005, 52); Session.WorldObjects[2005].ItemsCapacity = 24;
    Session.ContainerContents.FindOrAdd(2005).Reset();
    TestTrue(TEXT("An empty salvageable container can be queued"), Binder->AddItemToSalvageQueue(2005));
    AddItem(2006, 64, 2005); Binder->RefreshSalvageOverlays();
    TestFalse(TEXT("Late child after empty ViewContents removes destructive parent entry"), Binder->SalvageQueueGuids.Contains(2005));
    AddItem(2007, 52); Session.WorldObjects[2007].ItemsCapacity = 24;
    auto& UnresolvedChild = Session.ContainerContents.FindOrAdd(2007).AddDefaulted_GetRef(); UnresolvedChild.ItemGuid = 2008;
    TestFalse(TEXT("Unknown child records prevent destruction of apparently empty container"), Binder->CanAddItemToSalvageQueue(2007));

    Binder->ShowSalvagePanel(200);
    const int32 ItemCount = Binder->SalvageVisibleSlots + 5;
    for (int32 I = 0; I < ItemCount; ++I)
    {
        AddItem(4000 + I, 64); Binder->AddItemToSalvageQueue(4000 + I);
        if (I + 1 == Binder->SalvageVisibleSlots - 1)
            TestFalse(TEXT("Items plus one empty slot fit without scrollbar"), bool(Scrollbar->bVisible));
        if (I + 1 == Binder->SalvageVisibleSlots)
            TestTrue(TEXT("Exactly filling the row exposes scroll to the reserved empty slot"), bool(Scrollbar->bVisible));
    }
    for (float DPIScale : {1.f, 1.5f})
    {
        Binder->SetSalvageScrollOffset(0);
        Draw(DPIScale == 1.f ? TEXT("ThumbDrag100") : TEXT("ThumbDrag150"), DPIScale);
        const auto ThumbPart = Manager->FindElementUnder(TEXT("Salvage_ItemListScroll"), TEXT("widget_mid_field"));
        const auto Thumb = ThumbPart ? ThumbPart->Parent.Pin() : nullptr;
        if (!TestTrue(TEXT("Salvage scrollbar has its authored thumb"), Thumb.IsValid())) return false;
        const auto& Geometry = Canvas->GetCachedGeometry();
        TestTrue(TEXT("Thumb regression uses the requested actual Slate DPI transform"),
            (Geometry.LocalToAbsolute(FVector2D(100, 0)) - Geometry.LocalToAbsolute(FVector2D::ZeroVector))
                .Equals(FVector2D(100 * DPIScale, 0), .1));
        const FVector2D StartLocal = Canvas->LayoutToViewport(FVector2D(Thumb->GetScreenOrigin()) + FVector2D(Thumb->Width, Thumb->Height) * .5);
        const FVector2D Start = Geometry.LocalToAbsolute(StartLocal);
        const FPointerEvent Down(0, Start, Start, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0, FModifierKeysState());
        Canvas->NativeOnMouseButtonDown(Geometry, Down);
        TestTrue(FString::Printf(TEXT("Actual thumb press captures salvage at %.1fx DPI"), DPIScale),
            Binder->ScrollDragTarget == UACEUIGameplayBinder::EACEUIScrollTarget::Salvage);
        const int32 Maximum = Binder->SalvageQueueGuids.Num() + 1 - Binder->SalvageVisibleSlots;
        const int32 Travel = Scrollbar->Width - 32 - Thumb->Width;
        const FVector2D Middle = Geometry.LocalToAbsolute(StartLocal + Canvas->LayoutToViewport(FVector2D(Travel * .5f, 0)));
        const FPointerEvent MoveHalf(0, Middle, Start, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::Invalid, 0, FModifierKeysState());
        Canvas->NativeOnMouseMove(Geometry, MoveHalf);
        TestEqual(FString::Printf(TEXT("Thumb movement uses local coordinates at %.1fx DPI"), DPIScale),
            Binder->SalvageScrollOffset, FMath::RoundToInt(Maximum * .5f));
        const FVector2D End = Geometry.LocalToAbsolute(StartLocal + Canvas->LayoutToViewport(FVector2D(Travel, 0)));
        const FPointerEvent MoveEnd(0, End, Middle, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::Invalid, 0, FModifierKeysState());
        Canvas->NativeOnMouseMove(Geometry, MoveEnd);
        const FPointerEvent Up(0, End, End, TSet<FKey>{}, EKeys::LeftMouseButton, 0, FModifierKeysState());
        Canvas->NativeOnMouseButtonUp(Geometry, Up);
        TestEqual(TEXT("Dragging to the end reaches the trailing drop slot"), Binder->SalvageScrollOffset, Maximum);
        TestEqual(TEXT("The final visible slot remains empty"), Binder->SalvageItemSlotGuids.Last(), 0);
    }
    Binder->SetSalvageScrollOffset(MAX_int32);
    Draw(TEXT("QueuedAndScrolled"));
    TestEqual(TEXT("Horizontal scrolling retains the final queued item beside the empty slot"),
        Binder->SalvageItemSlotGuids[Binder->SalvageItemSlotGuids.Num() - 2], 4000 + ItemCount - 1);
    TestEqual(TEXT("End of list exposes one empty drop slot"), Binder->SalvageItemSlotGuids.Last(), 0);
    TestTrue(TEXT("Trailing empty drop slot remains visibly outlined after scrolling"),
        Binder->SalvageItemBackgrounds.Last()->GetVisibility() == ESlateVisibility::HitTestInvisible
        && Binder->SalvageItemBackgrounds.Last()->Background.GetResourceObject() == Resources->ResolveIconTexture(0x06004D20));
    if (const auto* Slot = Cast<UCanvasPanelSlot>(Binder->SalvageItemSlots[0]->Slot))
        TestTrue(TEXT("Salvage icon follows canvas coordinate scaling"), Slot->GetPosition().Equals(
            Canvas->LayoutToViewport(FVector2D(List->GetScreenOrigin())), .1));
    const int32 FirstVisible = Binder->SalvageItemSlotGuids[0];
    const FVector2D FirstCell = FVector2D(List->GetScreenOrigin()) + FVector2D(16, 16);
    ClickPoint(FirstCell);
    TestEqual(TEXT("Actual left click selects the scrolled salvage item"), Client->GetSelectedObject().Guid, FirstVisible);
    ClickPoint(FirstCell, EKeys::RightMouseButton);
    TestTrue(TEXT("Retail right click appraises without removing the offer"), Binder->SalvageQueueGuids.Contains(FirstVisible));
    const auto& QueueGeometry = Canvas->GetCachedGeometry();
    const FVector2D QueuePoint = QueueGeometry.LocalToAbsolute(Canvas->LayoutToViewport(FirstCell));
    const FPointerEvent QueueDoubleClick(0, QueuePoint, QueuePoint, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0, FModifierKeysState());
    const FPointerEvent QueueRelease(0, QueuePoint, QueuePoint, TSet<FKey>{}, EKeys::LeftMouseButton, 0, FModifierKeysState());
    Canvas->NativeOnMouseButtonDoubleClick(QueueGeometry, QueueDoubleClick);
    Canvas->NativeOnMouseButtonUp(QueueGeometry, QueueRelease);
    TestFalse(TEXT("Actual double click removes the visible queue item"), Binder->SalvageQueueGuids.Contains(FirstVisible));
    TestTrue(TEXT("Removing from queue does not destroy inventory object"), Session.WorldObjects.Contains(FirstVisible));
    Binder->HideSalvagePanel();
    TestEqual(TEXT("Close clears the Ust reference"), Binder->OpenSalvageToolGuid, 0);
    TestTrue(TEXT("Close clears queued items"), Binder->SalvageQueueGuids.IsEmpty());
    for (const auto& Icon : Binder->SalvageItemSlots)
        TestEqual(TEXT("Close hides queue overlays"), Icon->GetVisibility(), ESlateVisibility::Collapsed);

    // This independent session can send only to a receiver we bind on loopback.
    // No real character or server is used for the destructive action regression.
    ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto Address = Sockets->CreateInternetAddr(); bool Valid = false;
    Address->SetIp(TEXT("127.0.0.1"), Valid); Address->SetPort(0);
    auto* Receiver = Sockets->CreateSocket(NAME_DGram, TEXT("Salvage regression receiver"), false);
    if (!TestTrue(TEXT("Loopback receiver created"), Receiver != nullptr)) return false;
    ON_SCOPE_EXIT { Receiver->Close(); Sockets->DestroySocket(Receiver); };
    if (!TestTrue(TEXT("Loopback receiver bound"), Valid && Receiver->Bind(*Address))) return false;
    Receiver->GetAddress(*Address);
    Session.SocketC2S = Sockets->CreateSocket(NAME_DGram, TEXT("Salvage regression sender"), false);
    if (!TestTrue(TEXT("Loopback sender created"), Session.SocketC2S != nullptr)) return false;
    ON_SCOPE_EXIT { Session.SocketC2S->Close(); Sockets->DestroySocket(Session.SocketC2S); Session.SocketC2S = nullptr; };
    Session.ServerC2SAddr = Address; Session.IssacClient = MakeUnique<FACEIsaac>(123u);
    Binder->ShowSalvagePanel(200); Binder->AddItemToSalvageQueue(1000); Binder->AddItemToSalvageQueue(1001);
    Draw(TEXT("ReadyToSalvage"));
    Session.CachedC2SPackets.Reset();
    ClickPoint(FVector2D(Button->GetScreenOrigin()) + FVector2D(Button->Width, Button->Height) * .5);
    int32 SalvageActions = 0;
    for (const auto& Packet : Session.CachedC2SPackets)
    {
        FACEBinaryReader Reader(Packet.Value.Payload); Reader.Skip(24);
        if (!Reader.CanRead(4) || Reader.ReadUInt32() != ACEGameAction::CreateTinkeringTool) continue;
        ++SalvageActions;
        TestEqual(TEXT("Salvage action names the Ust"), Reader.ReadUInt32(), 200u);
        TestEqual(TEXT("Salvage action count"), Reader.ReadUInt32(), 2u);
        TestEqual(TEXT("Retail sends last queued item first"), Reader.ReadUInt32(), 1001u);
        TestEqual(TEXT("Retail sends first queued item last"), Reader.ReadUInt32(), 1000u);
    }
    TestEqual(TEXT("One actual button click sends exactly one salvage action"), SalvageActions, 1);
    TestTrue(TEXT("Retail clears queue immediately after submit"), Binder->SalvageQueueGuids.IsEmpty());
    TestFalse(TEXT("Submit disables visible Salvage button"), Button->bActivatable);
    TestFalse(TEXT("Submit hides the empty list's scrollbar"), bool(Scrollbar->bVisible));
    TestTrue(TEXT("Client waits for authoritative removal, without deleting local objects"), Session.WorldObjects.Contains(1000));

    TArray<FString> Chat;
    const auto ChatHandle = Session.OnChatMessage.AddLambda([&](const FString& Text, const FString&, int32) { Chat.Add(Text); });
    ON_SCOPE_EXIT { Session.OnChatMessage.Remove(ChatHandle); };
    FACEBinaryWriter Result;
    Result.WriteUInt32(40); Result.WriteUInt32(1); Result.WriteUInt32(1002); Result.WriteUInt32(1);
    Result.WriteUInt32(64); Result.WriteDouble(7.5); Result.WriteUInt32(25); Result.WriteUInt32(50);
    FACEBinaryReader ResultReader(Result.GetData()); Session.HandleSalvageOperationsResult(ResultReader);
    TestTrue(TEXT("Server result describes material and units"), Chat.ContainsByPredicate([](const FString& Line)
        { return Line.Contains(TEXT("25")) && Line.Contains(TEXT("Steel")); }));
    TestTrue(TEXT("Server rejection preserves item name"), Chat.ContainsByPredicate([](const FString& Line)
        { return Line.Contains(TEXT("Salvage item 1002")); }));
    TestTrue(TEXT("Server augmentation result is displayed"), Chat.ContainsByPredicate([](const FString& Line)
        { return Line.Contains(TEXT("50%")); }));
    Binder->AddItemToSalvageQueue(1000); Session.WorldObjects.Remove(200); Binder->RefreshSalvageOverlays();
    TestEqual(TEXT("Losing the tool closes salvage"), Binder->OpenSalvageToolGuid, 0);
    return true;
}
#endif
