#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEDatTextureResolver.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUICharSelectBinder.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "Engine/GameInstance.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECharacterButtonMaskTest, "ACE.RetailParity.CharacterButtonMasks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECharacterButtonMaskTest::RunTest(const FString&)
{
    auto* GI = NewObject<UGameInstance>();
    auto* Dat = NewObject<UACEDatSubsystem>(GI);
    FString DatDirectory = TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(), TEXT("RetailDatDir="), DatDirectory);
    if (!TestTrue(TEXT("Retail DAT opens"), Dat->LoadDatDirectory(DatDirectory))) return false;
    auto* Resources = NewObject<UACEUIResourceResolver>(); Resources->Initialize(Dat);
    auto* Manager = NewObject<UACEUIElementManager>(); Manager->Initialize();
    auto* Layout = NewObject<UACEUILayoutResolver>(); Layout->Initialize(Dat, Manager);
    if (!TestTrue(TEXT("Retail character layout loads"), Layout->LoadLayout(0x21000004))) return false;
    auto* Canvas = NewObject<UACEUICanvasWidget>(); Canvas->Initialize();
    // Also cover resources being assigned before the manager.
    Canvas->SetResourceResolver(Resources); Canvas->InitializeCanvas(Manager); Canvas->TakeWidget();
    FACECharacterInfo Character; Character.CharacterId = 0x50000011; Character.Name = TEXT("Mask test");
    auto* Binder = NewObject<UACEUICharSelectBinder>();
    Binder->Initialize(nullptr, Manager, Canvas, nullptr, {Character}, TEXT("Mask test"));
    Canvas->SetCharSelectBinder(Binder);
    // Exercise real Canvas routing without entering a world, exiting the process or opening a dialog.
    Manager->OnElementActivated.RemoveAll(Binder);
    TArray<TSharedPtr<FACEUIElement>> Activations;
    const auto ActivationHandle = Manager->OnElementActivated.AddLambda([&](auto Element) { Activations.Add(Element); });

    for (const FIntPoint Size : {FIntPoint(800,600), FIntPoint(1920,1080), FIntPoint(1100,900)})
    {
        Manager->ApplyEdgeAnchoredLayout(Size.X, Size.Y);
        // Pointer events arrive in desktop coordinates, including Slate DPI and window offset.
        const float Dpi = Size.X == 1100 ? 1.25f : 1.f;
        const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(Size), FSlateLayoutTransform(Dpi, FVector2D(37,53)));
        auto Pointer = [&](FIntPoint Point, bool bDown)
        {
            const FVector2D Position = Geometry.LocalToAbsolute(FVector2D(Point) + FVector2D(.5,.5));
            return FPointerEvent(0, Position, Position, bDown ? TSet<FKey>{EKeys::LeftMouseButton} : TSet<FKey>{},
                EKeys::LeftMouseButton, 0, FModifierKeysState());
        };
        auto Press = [&](FIntPoint Point) { Canvas->NativeOnMouseButtonDown(Geometry, Pointer(Point,true)); };
        auto Release = [&](FIntPoint Point) { Canvas->NativeOnMouseButtonUp(Geometry, Pointer(Point,false)); };
        auto Move = [&](FIntPoint Point, bool bDown = false) { Canvas->NativeOnMouseMove(Geometry, Pointer(Point,bDown)); };

        for (const TCHAR* Name : {TEXT("EnterGameButton"), TEXT("CreateCharacterButton"), TEXT("DeleteCharacterButton"),
            TEXT("RestoreCharacterButton"), TEXT("CreditsButton"), TEXT("ExitButton")})
        {
            Character.DeleteSeconds = FCString::Strcmp(Name,TEXT("RestoreCharacterButton")) == 0 ? 3600 : 0;
            Binder->Initialize(nullptr, Manager, Canvas, nullptr, {Character}, TEXT("Mask test"));
            Manager->OnElementActivated.RemoveAll(Binder);
            const auto Button = Manager->FindElementByName(Name);
            if (!TestTrue(FString::Printf(TEXT("%s exists"), Name), Button.IsValid())) continue;
            // Create is gated on a live session; this offline fixture tests its enabled artwork/input.
            Button->bActivatable = true; Button->bGhosted = false;
            FACEDatTexture Raw; FACEDatDecodedSurface Mask;
            if (!TestTrue(TEXT("Authored interaction mask decodes"),
                Dat->GetTextureResolver()->LoadTextureForUi(Button->AlphaFileId,Raw)
                && Dat->GetTextureResolver()->DecodeTextureForUi(Raw,Mask))) continue;
            TestEqual(TEXT("Retail mask is native button width"), Mask.Width, Button->Width);
            TestEqual(TEXT("Retail mask is native button height"), Mask.Height, Button->Height);
            TestTrue(TEXT("Button centre is inside mask"), Mask.Pixels[(Mask.Height/2)*Mask.Width+Mask.Width/2] == FColor(0,0,0,255));
            TestTrue(TEXT("Button corner is excluded"), Mask.Pixels[0] == FColor(255,255,255,0));
            const FIntPoint Origin = Button->GetScreenOrigin();
            const FIntPoint Inside = Origin + FIntPoint(Mask.Width/2,Mask.Height/2);
            const FIntPoint Outside = Origin;

            // Retail UIRegion negates SurfaceWindow::PointTestOverlay's nonzero RGB test.
            // Compare every native pixel in all three button shapes, including their shaded edges.
            if (Size.X == 800)
            {
                int32 Mismatches = 0;
                for (int32 Y = 0; Y < Mask.Height; ++Y)
                    for (int32 X = 0; X < Mask.Width; ++X)
                    {
                        const FColor Pixel = Mask.Pixels[Y*Mask.Width+X];
                        const bool Expected = (Pixel.ToPackedARGB() & 0x00ffffffu) == 0;
                        if ((Manager->HitTestCanvas(Origin.X+X,Origin.Y+Y) == Button) != Expected) ++Mismatches;
                    }
                TestEqual(FString::Printf(TEXT("%s matches retail mask at every pixel"), Name), Mismatches, 0);
            }
            // Image-only paint states must retain the base interaction mask.
            for (uint32 State : {1u,2u,3u,13u})
            {
                Button->PaintState = State;
                TestTrue(TEXT("State keeps opaque centre"), Manager->HitTestCanvas(Inside.X,Inside.Y) == Button);
                TestFalse(TEXT("State excludes transparent corner"), Manager->HitTestCanvas(Outside.X,Outside.Y) == Button);
            }

            Move(Outside);
            TestFalse(TEXT("Transparent corner does not hover"), Manager->GetHoverElement() == Button);
            Activations.Reset(); Press(Outside); Release(Inside);
            TestFalse(TEXT("Press outside then release inside cannot activate"), Activations.Contains(Button));
            Move(Inside);
            TestTrue(TEXT("Opaque centre hovers"), Manager->GetHoverElement() == Button);
            Activations.Reset(); Press(Inside);
            TestTrue(TEXT("Opaque centre captures"), Manager->GetCaptureElement() == Button);
            Move(Outside,true);
            TestFalse(TEXT("Moving into transparent corner clears hover even during capture"), Manager->GetHoverElement() == Button);
            Release(Outside);
            TestFalse(TEXT("Release in transparent corner cancels activation"), Activations.Contains(Button));
            TestFalse(TEXT("Release clears capture"), Manager->GetCaptureElement().IsValid());
            Activations.Reset(); Press(Inside); Release(Inside);
            TestEqual(TEXT("Opaque click activates once"), Activations.Num(), 1);
            TestTrue(TEXT("Opaque click activates the correct button"), Activations.Contains(Button));
            Activations.Reset(); Press(Inside); Move(Outside,true); Move(Inside,true); Release(Inside);
            TestTrue(TEXT("Returning to the mask before release activates"), Activations.Contains(Button));

            // A label/overlay must not restore rectangular input through its masked parent.
            auto Child = MakeShared<FACEUIElement>(); Child->Type = ACEUI::ElementType::Button;
            Child->Width = Button->Width; Child->Height = Button->Height; Child->Parent = Button;
            Button->Children.Add(Child);
            TestTrue(TEXT("Child receives input within mask"), Manager->HitTestCanvas(Inside.X,Inside.Y) == Child);
            TestFalse(TEXT("Child cannot receive input through excluded parent pixels"), Manager->HitTestCanvas(Outside.X,Outside.Y) == Child);
            Button->Children.Remove(Child);

            Button->bActivatable = false; Button->bGhosted = true;
            Activations.Reset(); Press(Inside); Release(Inside);
            TestFalse(TEXT("Disabled button still cannot activate"), Activations.Contains(Button));
        }
    }
    TestTrue(TEXT("Missing mask retains ordinary rectangular input"), Resources->IsPointInHitMask(0,FIntPoint(0,0)));
    Manager->OnElementActivated.Remove(ActivationHandle);
    Binder->Shutdown(); Canvas->SetCharSelectBinder(nullptr);
    Manager->Shutdown(); Resources->Shutdown();
    return true;
}
#endif
