#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACETestDatDirectory.h"
#include "ACEDatSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/SVirtualWindow.h"
#include "RenderingThread.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACERetailTextEntry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUISharedControlsTest,"ACE.RetailParity.SharedControls",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUISharedControlsTest::RunTest(const FString&)
{
    auto* Manager=NewObject<UACEUIElementManager>(); Manager->Initialize();
    ON_SCOPE_EXIT { Manager->Shutdown(); };
    auto Root=MakeShared<FACEUIElement>(); Root->ElementName=TEXT("RootGameplay_Field"); Root->Width=800; Root->Height=600;
    auto Window=MakeShared<FACEUIElement>(); Window->ElementName=TEXT("RootGameplay_Test_Field"); Window->Width=300; Window->Height=200;
    auto Other=MakeShared<FACEUIElement>(); Other->ElementName=TEXT("RootGameplay_Other_Field"); Other->Width=300; Other->Height=200; Other->X=400;
    auto A=MakeShared<FACEUIElement>(); A->Type=ACEUI::ElementType::Button; A->X=10; A->Y=10; A->Width=40; A->Height=20;
    auto B=MakeShared<FACEUIElement>(*A); B->X=80;
    Root->AddChild(Window); Root->AddChild(Other); Window->AddChild(A); Window->AddChild(B); Manager->AddRoot(Root);
    const FVector2D View(800,600), PA(25,15), PB(95,15), Outside(350,250);
    int Clicks=0; TSharedPtr<FACEUIElement> Last;
    Manager->OnElementActivated.AddLambda([&](auto Button){ ++Clicks; Last=Button; });
    auto Down=[&](FVector2D P){Manager->NotifyMouseDown(P,View,EKeys::LeftMouseButton);};
    auto Up=[&](FVector2D P){Manager->NotifyMouseUp(P,View,EKeys::LeftMouseButton);};
    Down(PA); Up(PB); Down(PA); Up(Outside);
    TestEqual(TEXT("Releasing over a different button or empty space cancels the original click"),Clicks,0);
    Down(PA); A->bGhosted=true; Up(PA); A->bGhosted=false;
    Down(PA); Window->bVisible=false; Up(PA); Window->bVisible=true;
    TestEqual(TEXT("Disabled and hidden controls cannot activate after the press"),Clicks,0);
    Down(PA); Manager->NotifyMouseDown(PB,View,EKeys::RightMouseButton);
    Manager->NotifyMouseUp(PB,View,EKeys::RightMouseButton); Up(PA);
    TestEqual(TEXT("Secondary click preserves the left-button capture"),Clicks,1);
    TestTrue(TEXT("The originally pressed button receives activation"),Last==A);

    // Retail UIElement_Button::MouseDown/ListenToGlobalMessage/MouseUp.
    A->bHotButton=true;
    Down(PA); const double Start=FPlatformTime::Seconds();
    TestEqual(TEXT("Hot arrow scrolls immediately on press"),Clicks,2);
    Manager->TickPointerInput(Start+.25); TestEqual(TEXT("Initial repeat delay is honored"),Clicks,2);
    Manager->TickPointerInput(Start+.51); TestEqual(TEXT("Held arrow repeats after its first interval"),Clicks,3);
    Manager->TickPointerInput(Start+.55); TestEqual(TEXT("Repeat rate limits scrolling"),Clicks,3);
    Manager->TickPointerInput(Start+.62); TestEqual(TEXT("Held arrow continues repeating"),Clicks,4);
    Manager->NotifyMouseMove(PB,View); Manager->TickPointerInput(Start+2);
    TestEqual(TEXT("Leaving the button pauses repeat"),Clicks,4);
    Manager->NotifyMouseMove(PA,View); Manager->TickPointerInput(Start+2.01);
    TestEqual(TEXT("Returning to a held button resumes repeat"),Clicks,5);
    Up(PA); Manager->TickPointerInput(Start+3);
    TestEqual(TEXT("Release does not add an extra hot-button activation"),Clicks,5);
    Down(PA); Manager->CancelPointerCapture(); Manager->TickPointerInput(Start+4); Up(PA);
    TestEqual(TEXT("Capture loss cancels the repeat and release click"),Clicks,6);
    Down(PA); A->bActivatable=false; Manager->TickPointerInput(Start+5); A->bActivatable=true; Up(PA);
    TestEqual(TEXT("Disabling a held hot control cancels it"),Clicks,7);

    for (int I=0;I<8100;++I) Manager->BringFloatyToFront(I%2 ? Window : Other);
    TestTrue(TEXT("Rebased z-order still brings the clicked window to the front"),Root->Children.Last()==Window);
    Down(PA); Manager->ClearRoots(); Manager->TickPointerInput(Start+6); Up(PA);
    TestEqual(TEXT("Changing screens cancels a held control"),Clicks,8);
    TestFalse(TEXT("Changing screens clears pointer capture"),Manager->GetCaptureElement().IsValid());

    // The repeat intervals come from the retail layout, not an arrow-name heuristic.
    auto* Dat=NewObject<UACEDatSubsystem>(NewObject<UGameInstance>());
    if (!TestTrue(TEXT("Load retail DAT"),Dat->LoadDatDirectory(ACETestDatDirectory()))) return false;
    auto* Layout=NewObject<UACEUILayoutResolver>(); Layout->Initialize(Dat,Manager);
    TestTrue(TEXT("Load gameplay layout"),Layout->LoadLayout(0x21000005));
    int HotCount=0;
    TFunction<void(TSharedPtr<FACEUIElement>)> Visit=[&](auto E)
    {
        if (!E) return;
        if (E->bHotButton) {++HotCount; TestTrue(TEXT("DAT hot button has a positive repeat interval"),E->HotClickInterval>0);}
        for (auto Child:E->Children) Visit(Child);
    };
    Visit(Manager->GetSyntheticRoot());
    TestTrue(TEXT("Gameplay imports authored hot buttons"),HotCount>10);
    Dat->Deinitialize();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUITextEntryScrollTest,"ACE.RetailParity.TextEntryScroll",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUITextEntryScrollTest::RunTest(const FString&)
{
    auto* Dat=NewObject<UACEDatSubsystem>(NewObject<UGameInstance>());
    if (!Dat->LoadDatDirectory(ACETestDatDirectory())) return false;
    auto* Resources=NewObject<UACEUIResourceResolver>(); Resources->Initialize(Dat);
    auto* Entry=NewObject<UACERetailTextEntry>();
    auto Element=MakeShared<FACEUIElement>(); Element->FontId=0x40000001;
    Entry->SetRetailElement(Resources,Element); Entry->ContentMargins=FMargin(4);
    const FString Value=TEXT("A long retail search or journal title extending beyond the input field");
    Entry->SetText(FText::FromString(Value));
    const auto Widget=Entry->TakeWidget();
    auto Window=SNew(SVirtualWindow).Size(FVector2D(110,24)); Window->SetIsFocusable(true); Window->SetContent(Widget);
    auto& App=FSlateApplication::Get(); const auto PreviousFocus=App.GetKeyboardFocusedWidget(); App.RegisterVirtualWindow(Window);
    FWidgetRenderer Renderer(true,true); auto* Target=FWidgetRenderer::CreateTargetFor({110,24},TF_Nearest,true);
    ON_SCOPE_EXIT {App.ClearKeyboardFocus(); if(PreviousFocus)App.SetKeyboardFocus(PreviousFocus); App.UnregisterVirtualWindow(Window); Target->ReleaseResource(); Dat->Deinitialize();};
    auto Draw=[&](){Renderer.DrawWidget(Target,Window,{110,24},0.f); FlushRenderingCommands();};
    auto Key=[&](FKey K){Widget->OnKeyDown(Widget->GetCachedGeometry(),FKeyEvent(K,FModifierKeysState(),0,false,0,0));Draw();};
    Draw(); App.SetKeyboardFocus(Widget); Key(EKeys::End);
    TestTrue(TEXT("Long single-line input scrolls to keep its end caret visible"),Entry->GetHorizontalScrollOffset()>100);
    const float EndOffset=Entry->GetHorizontalScrollOffset();
    Key(EKeys::Home); TestEqual(TEXT("Home reveals the start of the input"),Entry->GetHorizontalScrollOffset(),0.f);
    Entry->SetText(FText::FromString(Value));
    Widget->OnKeyChar(Widget->GetCachedGeometry(),FCharacterEvent('Z',FModifierKeysState(),0,false));
    TestEqual(TEXT("Identical binder refresh preserves the user's caret"),Entry->GetText().ToString(),TEXT("Z")+Value);
    Key(EKeys::End); Draw();
    const auto& G=Widget->GetCachedGeometry(); const FVector2D AtEnd=G.LocalToAbsolute(FVector2D(105,10));
    Widget->OnMouseButtonDown(G,FPointerEvent(0,AtEnd,AtEnd,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
    Key(EKeys::BackSpace);
    TestEqual(TEXT("Pointer hit-testing follows horizontally scrolled glyphs"),Entry->GetText().ToString(),(TEXT("Z")+Value).Left(Value.Len()));
    TestTrue(TEXT("Scrolling remains at the edited text end"),Entry->GetHorizontalScrollOffset()>=EndOffset-20);
    Entry->SetText(FText::FromString(TEXT("short"))); Draw();
    TestEqual(TEXT("Short replacement resets horizontal scrolling"),Entry->GetHorizontalScrollOffset(),0.f);
    Entry->bMultiline=true; Entry->SetText(FText::FromString(Value)); Key(EKeys::End);
    TestEqual(TEXT("Multiline fields retain wrapping rather than horizontal scrolling"),Entry->GetHorizontalScrollOffset(),0.f);
    return true;
}
#endif
