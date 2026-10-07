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
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
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
    TestTrue(TEXT("UI regression has no network connection"), Session.CachedC2SPackets.IsEmpty());
    return !HasAnyErrors();
}
#endif
