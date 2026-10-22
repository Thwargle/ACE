#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEDatTextureResolver.h"
#include "UI/ACEUIResourceResolver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEIconRenderingTest, "ACE.RetailParity.IconRendering",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEIconRenderingTest::RunTest(const FString&)
{
    auto* GI = NewObject<UGameInstance>();
    auto* Dat = NewObject<UACEDatSubsystem>(GI);
    if (!TestTrue(TEXT("Retail DAT opens"), Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    ON_SCOPE_EXIT { Dat->Deinitialize(); };
    auto* Resources = NewObject<UACEUIResourceResolver>(); Resources->Initialize(Dat);
    auto* Decoder = Dat->GetTextureResolver();
    auto Pixels = [](UTexture2D* Texture)
    {
        TArray<FColor> Result;
        if (Texture && Texture->GetPlatformData() && Texture->GetPlatformData()->Mips.Num())
        {
            auto& Mip = Texture->GetPlatformData()->Mips[0];
            const auto* Data = static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
            if (Data) Result.Append(Data, Mip.SizeX * Mip.SizeY);
            Mip.BulkData.Unlock();
        }
        return Result;
    };
    auto Decode = [&](uint32 Id, FACEDatDecodedSurface& Image)
    {
        FACEDatTexture Raw;
        return Decoder->LoadTextureForUi(Id, Raw) && Decoder->DecodeTextureForUi(Raw, Image)
            && Image.Width == 32 && Image.Height == 32 && Image.Pixels.Num() == 1024;
    };

    // Verify the native cutout against IconData::RenderIcons / ReplaceColor:
    // only exact opaque white is an effect mask. Transparent white is not art.
    constexpr uint32 TorchId = 0x06001072;
    FACEDatDecodedSurface Torch;
    if (!TestTrue(TEXT("Stock torch icon decodes"), Decode(TorchId, Torch))) return false;
    TArray<uint8> MapBlob;
    if (!TestTrue(TEXT("Retail effect map loads"), Dat->GetPortalDat()->ReadFile(0x25000009, MapBlob))) return false;
    FACEDatCursor Map(MapBlob); uint32 MapId = 0; uint8 Numbering = 0; bool Ok = Map.Read(MapId) && Map.Read(Numbering);
    const uint32 Count = Map.ReadCompressedUInt32(Ok);
    int32 EffectCases = 0;
    TArray<FColor> OriginalForeground;
    for (uint32 Entry = 0; Ok && Entry < Count; ++Entry)
    {
        uint32 Key = 0, Value = 0; Ok = Map.Read(Key) && Map.Read(Value);
        if (!Ok || Key < 1 || Key > 33) continue;
        FACEDatDecodedSurface Effect;
        if (!TestTrue(TEXT("Mapped effect image decodes"), Decode(Value, Effect))) return false;
        const uint32 Effects = Key == 33 ? 0 : 1u << (Key - 1);
        const auto Foreground = Pixels(Resources->ResolveItemForeground(TorchId, 0, Effects));
        if (!TestEqual(TEXT("Composed torch dimensions"), Foreground.Num(), 1024)) return false;
        bool Coverage = true, Artwork = true; int32 TransparentWhite = 0, Mask = 0;
        for (int32 P = 0; P < 1024; ++P)
        {
            const bool IsMask = Torch.Pixels[P] == FColor::White;
            const FColor Expected = IsMask ? Effect.Pixels[P] : Torch.Pixels[P];
            Mask += IsMask;
            Coverage &= Foreground[P].A == Expected.A;
            Artwork &= !Expected.A || Foreground[P] == Expected;
            TransparentWhite += Foreground[P] == FColor(255, 255, 255, 0);
            if (Key == 33) OriginalForeground.Add(Expected);
        }
        TestTrue(FString::Printf(TEXT("Effect %u preserves retail's complete alpha cutout and visible artwork"), Key), Coverage && Artwork && Mask > 0);
        // Default effect is black. There must be no hidden white border to sample.
        if (Key == 33) TestEqual(TEXT("Torch contains no invisible white fringe"), TransparentWhite, 0);
        ++EffectCases;
    }
    TestTrue(TEXT("Default and enchanted effects were checked"), Ok && EffectCases > 1 && OriginalForeground.Num() == 1024);

    // White visible art and every partial alpha survive final upload unchanged.
    TArray<FColor> AlphaArt;
    for (int32 A = 0; A < 256; ++A) AlphaArt.Add(FColor(255, 255, 255, A));
    const auto Uploaded = Pixels(FACEDatTextureResolver::CreateTransientRgbaUiIcon(16, 16, AlphaArt));
    if (!TestEqual(TEXT("All alpha levels upload"), Uploaded.Num(), 256)) return false;
    for (int32 A = 0; A < 256; ++A)
        TestTrue(TEXT("No alpha threshold, cropping, or removal of visible white"), Uploaded[A].A == A && (!A || Uploaded[A] == AlphaArt[A]));

    if (!FApp::CanEverRender() || !FSlateApplication::IsInitialized()) return !HasAnyErrors();
    FWidgetRenderer Renderer(false, true);
    auto Render = [&](TSharedRef<SWidget> Widget, FIntPoint Size)
    {
        auto* Target = FWidgetRenderer::CreateTargetFor(FVector2D(Size), TF_Bilinear, false);
        Target->ClearColor = FLinearColor::Black;
        ++GFrameCounter; Renderer.DrawWidget(Target, Widget, FVector2D(Size), .016f); FlushRenderingCommands();
        TArray<FColor> Result; FReadSurfaceDataFlags Flags; Flags.SetLinearToGamma(false);
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Result, Flags);
        Target->ReleaseResource(); return Result;
    };
    auto Brush = [](UTexture2D* Texture)
    {
        FSlateBrush Result; Result.SetResourceObject(Texture); Result.ImageSize = FVector2D(32, 32); return Result;
    };
    // A black cutout on black must remain black at any UI scale. The old upload
    // visibly leaks white at fractional samples, even though its alpha is correct.
    TArray<FColor> Edge; Edge.Init(FColor(255, 255, 255, 0), 1024);
    for (int32 Y = 8; Y < 24; ++Y) for (int32 X = 8; X < 24; ++X) Edge[Y * 32 + X] = FColor::Black;
    const auto Before = Brush(FACEDatTextureResolver::CreateTransientRgbaUi(32, 32, Edge));
    const auto After = Brush(FACEDatTextureResolver::CreateTransientRgbaUiIcon(32, 32, Edge));
    for (int32 Size : {32, 40, 48, 64})
    {
        const auto Fixed = Render(SNew(SImage).Image(&After), FIntPoint(Size, Size));
        TestEqual(TEXT("Scaled cutout rendered"), Fixed.Num(), Size * Size);
        TestTrue(TEXT("Bilinear scaling introduces no white halo"), !Fixed.ContainsByPredicate([](FColor C) { return C.R > 1 || C.G > 1 || C.B > 1; }));
        if (Size > 32)
        {
            const auto Old = Render(SNew(SImage).Image(&Before), FIntPoint(Size, Size));
            TestTrue(TEXT("Regression fixture detects the old white halo"), Old.ContainsByPredicate([](FColor C) { return C.R > 8; }));
        }
    }

    const auto OldTorch = Brush(FACEDatTextureResolver::CreateTransientRgbaUi(32, 32, OriginalForeground));
    const auto NewTorch = Brush(Resources->ResolveItemForeground(TorchId, 0, 0));
    const auto Background = Brush(Resources->ResolveItemBackground(128, 0));
    const auto Grid = SNew(SGridPanel);
    int32 Column = 0;
    for (int32 Size : {32, 40, 48, 64})
    {
        for (int32 Row = 0; Row < 2; ++Row)
            Grid->AddSlot(Column, Row).Padding(8).HAlign(HAlign_Center).VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(Size).HeightOverride(Size)
                [ SNew(SOverlay)
                    + SOverlay::Slot()[ SNew(SImage).Image(&Background) ]
                    + SOverlay::Slot()[ SNew(SImage).Image(Row ? &NewTorch : &OldTorch) ] ] ];
        ++Column;
    }
    const FIntPoint CaptureSize(288, 160);
    auto Capture = Render(Grid, CaptureSize);
    for (auto& C : Capture) C = C.ReinterpretAsLinear().ToFColor(true);
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("Automation/ItemIcons");
    IFileManager::Get().MakeDirectory(*Folder, true);
    TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(CaptureSize.X, CaptureSize.Y, Capture, Png);
    TestTrue(TEXT("Torch scale comparison saved (before above, fixed below)"), FFileHelper::SaveArrayToFile(Png, *(Folder / TEXT("TorchScaling.png"))));
    return !HasAnyErrors();
}
#endif
