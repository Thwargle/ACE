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
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUIInteractionParityTest, "ACE.RetailParity.UIInteractions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEUIInteractionParityTest::RunTest(const FString&)
{
    const FString OriginalSettings=GGameUserSettingsIni;
    TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/InteractionPreferences.ini"));
    FConfigFile Preferences; Preferences.NoSave=true; GConfig->SetFile(GGameUserSettingsIni,&Preferences);
    auto* GI=NewObject<UGameInstance>();
    auto* Dat=NewObject<UACEDatSubsystem>(GI);
    if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    auto* Resources=NewObject<UACEUIResourceResolver>(); Resources->Initialize(Dat);
    auto* Manager=NewObject<UACEUIElementManager>(); Manager->Initialize();
    auto* Layout=NewObject<UACEUILayoutResolver>(); Layout->Initialize(Dat,Manager);
    if (!Layout->LoadLayout(0x21000005)) return false;
    auto* Canvas=NewObject<UACEUICanvasWidget>(); Canvas->Initialize();
    Canvas->InitializeCanvas(Manager); Canvas->SetResourceResolver(Resources);
    const auto Slate=Canvas->TakeWidget();
    auto* Client=NewObject<UACEClientSubsystem>(GI); Client->Session=MakeShared<FACESession>();
    auto& Session=*Client->Session; Session.State=EACESessionState::InWorld; Session.PlayerGuid=1234;
    FACEWorldObject Self; Self.Guid=1234; Self.Name=TEXT("UI regression"); Session.WorldObjects.Add(Self.Guid,Self);
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
    Binder->RequestGameplayScreenshot();
    const FString Saved=Binder->PendingScreenshotFilename;
    TestFalse(TEXT("Screenshot request has a destination"),Saved.IsEmpty());
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
    Binder->Shutdown();
    TestEqual(TEXT("A new session starts tab memory from scratch"),Binder->SelectionSpellTab,INDEX_NONE);
    for (const auto& Tab:Binder->SpellTabSelections) TestEqual(TEXT("New tab default is first spell"),Tab.Slot,0);
    return true;
}
#endif
