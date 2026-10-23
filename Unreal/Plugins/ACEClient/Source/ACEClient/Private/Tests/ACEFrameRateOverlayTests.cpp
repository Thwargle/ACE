#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "UI/ACEFrameRateWidget.h"
#include "UI/ACEUIElementManager.h"
#include "ACERuntimeOptions.h"
#include "Widgets/SWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SVirtualWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEFrameRateOverlayTest,"ACE.UI.FrameRateOverlay",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEFrameRateOverlayTest::RunTest(const FString&)
{
    const FString PreviousIni=GGameUserSettingsIni;
    GGameUserSettingsIni=FPaths::ProjectSavedDir()/TEXT("Automation/FPSLayout.ini");
    FConfigFile Settings;Settings.NoSave=false;Settings.bCanSaveAllSections=true;
    GConfig->SetFile(GGameUserSettingsIni,&Settings);
    ON_SCOPE_EXIT {GGameUserSettingsIni=PreviousIni;};
    ACERuntimeOptions::Set(TEXT("ConstrainUIWindows"),1.f);
    auto* Manager=NewObject<UACEUIElementManager>();Manager->Initialize();
    auto* Counter=NewObject<UACEFrameRateWidget>();Counter->Initialize();
    auto Slate=Counter->TakeWidget();
    const FVector2D View(1600,900),Start(20,70),End(220,170);
    const FGeometry Geometry=FGeometry::MakeRoot(FVector2D(320,48),FSlateLayoutTransform(2.f));
    auto Press=[&](FVector2D At){return Slate->OnMouseButtonDown(Geometry,FPointerEvent(0,At,At,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));};
    auto Move=[&](FVector2D At){return Slate->OnMouseMove(Geometry,FPointerEvent(0,At,Start,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));};
    auto Release=[&](FVector2D At){return Slate->OnMouseButtonUp(Geometry,FPointerEvent(0,At,At,{},EKeys::LeftMouseButton,0,FModifierKeysState()));};

    Manager->SetUiLocked(true);Counter->UpdatePresentation(Manager,true,View);
    TestTrue(TEXT("Locked FPS remains click-through"),Counter->GetVisibility()==ESlateVisibility::HitTestInvisible);
    TestFalse(TEXT("Locked FPS ignores a drag press"),Press(Start).IsEventHandled());
    TestTrue(TEXT("Existing default position retained"),Counter->PlacedPosition.Equals(FVector2D(16,64)));
    Manager->SetUiLocked(false);Counter->UpdatePresentation(Manager,true,View);
    TestTrue(TEXT("Shared unlock makes FPS interactive"),Counter->GetVisibility()==ESlateVisibility::Visible);
    TestTrue(TEXT("Slate routes press to FPS drag handler"),Press(Start).IsEventHandled());
    TestTrue(TEXT("FPS requests mouse capture"),Press(Start).GetMouseCaptor()==Slate);
    Move(End);
    TestTrue(TEXT("Dragging accounts for DPI/application scale"),Counter->PlacedPosition.Equals(FVector2D(116,114)));
    Move(Start);TestTrue(TEXT("Dragging back to the start restores the original position"),Counter->PlacedPosition.Equals(FVector2D(16,64)));
    Move(End);Release(End);TestFalse(TEXT("Release ends drag"),Counter->bDragging);

    FConfigFile Disk;Disk.Read(GGameUserSettingsIni);GConfig->SetFile(GGameUserSettingsIni,&Disk);
    auto* Restored=NewObject<UACEFrameRateWidget>();Restored->Initialize();Restored->TakeWidget();
    Restored->UpdatePresentation(Manager,true,View);
    TestTrue(TEXT("New widget restores saved location from disk"),Restored->PlacedPosition.Equals(FVector2D(116,114)));
    Counter->ResetSample();Counter->UpdatePresentation(Manager,true,View,true);
    TestTrue(TEXT("Hide/show preserves position"),Counter->PlacedPosition.Equals(Restored->PlacedPosition));

    Manager->SetUiLocked(true);Manager->SetQuickShowUi(true);Counter->UpdatePresentation(Manager,true,View);
    TestTrue(TEXT("Temporary UI unlock permits movement"),Counter->CanMove());
    Press(Start);Move(End);
    Manager->SetQuickShowUi(false);Counter->UpdatePresentation(Manager,true,View);
    const FVector2D LockedAt=Counter->PlacedPosition;
    TestFalse(TEXT("Releasing modifier ends active drag"),Counter->bDragging);
    Move(End+FVector2D(200,200));TestTrue(TEXT("Lock prevents continued movement"),Counter->PlacedPosition.Equals(LockedAt));
    double SavedX=0;GConfig->GetDouble(TEXT("ACEClient.DatHUD"),TEXT("FPS_X"),SavedX,GGameUserSettingsIni);
    TestEqual(TEXT("Locking during drag commits position"),SavedX,LockedAt.X);

    Manager->SetUiLocked(false);Counter->UpdatePresentation(Manager,true,View);
    Press(Start);Move(FVector2D(20000,20000));Release(End);
    TestTrue(TEXT("Drag honors window bounds"),Counter->PlacedPosition.Equals(FVector2D(1280,852)));
    Counter->UpdatePresentation(Manager,true,FVector2D(640,480));
    TestTrue(TEXT("Smaller viewport keeps counter accessible"),Counter->PlacedPosition.Equals(FVector2D(320,432)));
    Counter->UpdatePresentation(Manager,true,View);
    TestTrue(TEXT("Temporary resize retains preferred position"),Counter->PlacedPosition.Equals(FVector2D(1280,852)));

    ACERuntimeOptions::Set(TEXT("ConstrainUIWindows"),0.f);Counter->UpdatePresentation(Manager,true,View);
    Press(Start);Move(Start-FVector2D(4000,4000));
    Counter->NativeOnMouseCaptureLost(FCaptureLostEvent(0,0));
    TestTrue(TEXT("Unconstrained option permits offscreen placement"),Counter->Position.X<0 && Counter->Position.Y<0);
    TestFalse(TEXT("Capture loss finishes drag"),Counter->bDragging);
    GConfig->GetDouble(TEXT("ACEClient.DatHUD"),TEXT("FPS_X"),SavedX,GGameUserSettingsIni);
    TestEqual(TEXT("Capture loss saves placement"),SavedX,Counter->Position.X);
    ACERuntimeOptions::Set(TEXT("ConstrainUIWindows"),1.f);Counter->UpdatePresentation(Manager,true,View);
    TestTrue(TEXT("Re-enabling constraints recovers offscreen counter"),Counter->PlacedPosition.Equals(FVector2D::ZeroVector));

    Counter->UpdatePresentation(Manager,false,View);
    TestFalse(TEXT("VR counter never enables desktop dragging"),Counter->CanMove());
    TestTrue(TEXT("VR counter stays noninteractive"),Counter->GetVisibility()==ESlateVisibility::HitTestInvisible);
    TestFalse(TEXT("VR ignores pointer drags"),Press(Start).IsEventHandled());
    Counter->Sample(0.);for(int32 I=1;I<=45;++I)Counter->Sample(I/90.);
    TestEqual(TEXT("Placement does not change frame measurement"),Counter->GetCounterText(),FString(TEXT("90 FPS  |  11.1 ms")));

    // Route actual Slate events too: a noninteractive child border can make
    // direct handler tests pass while the player cannot grab the visible panel.
    auto& App=FSlateApplication::Get();
    auto Window=SNew(SVirtualWindow).Size(FVector2D(400,100));
    Window->SetContent(SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Top)
        [SNew(SBox).WidthOverride(320).HeightOverride(48)[Slate]]);
    App.RegisterVirtualWindow(Window);
    FWidgetRenderer Renderer(false,true);
    auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(400,100),TF_Bilinear,true);
    ON_SCOPE_EXIT {App.ReleaseAllPointerCapture();App.UnregisterVirtualWindow(Window);Target->ReleaseResource();};
    auto Paint=[&]{++GFrameCounter;Renderer.DrawWindow(Target,Window->GetHittestGrid(),Window,1.f,FVector2D(400,100),.016f);FlushRenderingCommands();};
    auto Path=[&](FVector2D At){return App.LocateWindowUnderMouse(At,{Window},false,0);};
    const FVector2D At(40,20);
    Manager->SetUiLocked(true);Counter->UpdatePresentation(Manager,true,View);Paint();
    TestFalse(TEXT("Locked FPS is absent from the real pointer hit path"),Path(At).ContainsWidget(&Slate.Get()));
    Manager->SetUiLocked(false);Counter->UpdatePresentation(Manager,true,View);Paint();
    TestTrue(TEXT("Unlocked FPS participates in the real pointer hit path"),Path(At).ContainsWidget(&Slate.Get()));
    App.RoutePointerDownEvent(Path(At),FPointerEvent(0,At,At,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
    TestTrue(TEXT("Real pointer press captures the FPS widget"),Counter->HasMouseCapture() && Counter->bDragging);
    App.RoutePointerMoveEvent(Path(At),FPointerEvent(0,At+FVector2D(40,20),At,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()),false);
    TestTrue(TEXT("Captured pointer movement reaches FPS"),Counter->bMoved);
    Manager->SetUiLocked(true);Counter->UpdatePresentation(Manager,true,View);
    TestFalse(TEXT("Relocking releases real mouse capture immediately"),Counter->HasMouseCapture());
    return true;
}
#endif
