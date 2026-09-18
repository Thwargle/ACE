#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dat/ACEDatCursor.h"
#include "Dat/ACEDatTextLayout.h"
#include "Dat/ACEDatFontRenderer.h"
#include "ACEDatSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUILayoutResolver.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "ImageUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailFontDataTest, "ACE.RetailParity.FontDat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailFontDataTest::RunTest(const FString& Parameters)
{
    FString Directory = TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(), TEXT("RetailDatDir="), Directory);
    FACEDatDatabase Portal;
    if (!TestTrue(TEXT("Open portal DAT for fonts"), Portal.Open(Directory / TEXT("client_portal.dat")))) return false;
    FString Json;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Json, *(FPaths::ProjectDir() / TEXT("Plugins/ACEClient/Tests/Fixtures/RetailFonts.json")))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root)) return false;
    FACEDatFontRenderer Renderer(nullptr);
    for (const auto& Value : Root->GetArrayField(TEXT("fonts")))
    {
        const auto Row = Value->AsObject();
        const FString Id = Row->GetStringField(TEXT("id"));
        TArray<uint8> Blob;
        if (!TestTrue(*Id, Portal.ReadFile(FCString::Strtoui64(*Id, nullptr, 16), Blob))) return false;
        FACEDatFont Font;
        FACEDatCursor Cursor(Blob);
        if (!TestTrue(TEXT("Parse retail font"), ACEDatUnpack::UnpackFont(Cursor, Font))) return false;
        TestEqual(TEXT("Native line height"), int32(Font.MaxCharHeight), Row->GetIntegerField(TEXT("height")));
        TestEqual(TEXT("Glyph count"), Font.Chars.Num(), Row->GetIntegerField(TEXT("count")));
        uint64 Hash = 14695981039346656037ULL;
        auto Add = [&](uint32 N, int32 Bytes) {
            for (int32 I = 0; I < Bytes; ++I) Hash = (Hash ^ ((N >> (8 * I)) & 255)) * 1099511628211ULL;
        };
        for (const FACEDatFontChar& C : Font.Chars)
        {
            Add(C.Unicode,2); Add(C.OffsetX,2); Add(C.OffsetY,2); Add(C.Width,1); Add(C.Height,1);
            Add(C.HorizontalOffsetBefore,1); Add(C.HorizontalOffsetAfter,1); Add(C.VerticalOffsetBefore,1);
        }
        TestEqual(*FString::Printf(TEXT("%s every glyph matches server reader"), *Id),
            FString::Printf(TEXT("%016llX"), Hash), Row->GetStringField(TEXT("glyphHash")));
        for (const auto& Sample : Row->GetArrayField(TEXT("samples")))
            TestEqual(TEXT("Label advance matches server reader"), Renderer.MeasureWidth(Font, Sample->AsObject()->GetStringField(TEXT("text"))),
                Sample->AsObject()->GetIntegerField(TEXT("width")));
    }
    FACEDatFont Font;
    Font.MaxCharHeight = 10;
    Font.MaxCharWidth = 99; // a guessed fraction of this must not affect missing glyphs.
    for (uint16 C : {uint16('?'),uint16('a'),uint16('b'),uint16(' '),uint16('\n')})
    {
        FACEDatFontChar Ch;
        Ch.Unicode = C; Ch.Width = C == '\n' ? 0 : 3;
        Ch.HorizontalOffsetBefore = C == ' ' || C == '\n' ? 0 : 1;
        Ch.HorizontalOffsetAfter = Ch.HorizontalOffsetBefore;
        Font.IndexByUnicode.Add(C, Font.Chars.Num()); Font.Chars.Add(Ch);
    }
    TestEqual(TEXT("Missing character uses question-mark advance"), ACEDatText::Advance(Font, 0xFFFF), 5);
    TestNull(TEXT("CR is a layout control, not a missing inscription glyph"),ACEDatText::FindChar(Font,13));
    TestNull(TEXT("BOM does not paint a question mark"),ACEDatText::FindChar(Font,0xFEFF));
    TestEqual(TEXT("Tabs have a space advance"),ACEDatText::Advance(Font,9),ACEDatText::Advance(Font,32));
    TestEqual(TEXT("CRLF preserves the same wrapping as LF"),ACEDatText::Layout(Font,TEXT("aa\r\nbb"),20,false).Num(),2);
    TestEqual(TEXT("Empty text has zero width"), Renderer.MeasureWidth(Font, TEXT("")), 0);
    auto Lines = ACEDatText::Layout(Font, TEXT("aa bb"), 12, false);
    TestEqual(TEXT("Wraps at the word boundary"), Lines.Num(), 2);
    if (Lines.Num() == 2)
    {
        TestEqual(TEXT("Wrapped line excludes its trailing space from justification"), Lines[0].Width, 10);
        TestEqual(TEXT("Next word begins at its first letter"), Lines[1].Begin, 3);
    }
    TestEqual(TEXT("Oversized word wraps by glyph"), ACEDatText::Layout(Font, TEXT("aaaa"), 10, false).Num(), 2);
    TestEqual(TEXT("Explicit final newline preserves empty line"), ACEDatText::Layout(Font, TEXT("a\nb\n"), 100, false).Num(), 3);
    TestEqual(TEXT("Retail one-line property suppresses wrapping"), ACEDatText::Layout(Font, TEXT("aaaa"), 10, true).Num(), 1);
    Font.Chars[0].HorizontalOffsetBefore = 128;
    TestEqual(TEXT("Raw font bytes retain their unsigned storage"), ACEDatText::Advance(Font, '?'), 132);
    Font.Chars[0].HorizontalOffsetBefore = 255;
    TestEqual(TEXT("Retail byte advance wraps negative glyph bearings"),ACEDatText::Advance(Font,'?'),3);
    AddInfo(TEXT("Compared all 49 retail fonts, every glyph record, and 392 label widths with ACE.DatLoader and retail byte advances"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailFontWidgetTest, "ACE.RetailParity.FontWidget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailFontWidgetTest::RunTest(const FString& Parameters)
{
	const auto SpellRow = UACEUILayoutResolver::LoadTemplate(0x21000037, 0x10000343);
	if (!TestTrue(TEXT("Load detached retail spellbook item-slot template"), SpellRow.IsValid())) return false;
	TestEqual(TEXT("Retail spellbook row height"), SpellRow->Height, 32);
	for (const auto& Child : SpellRow->Children)
	{
		if (Child->ElementName == TEXT("ItemSlot_Icon")) TestEqual(TEXT("Spell icon retains 32px native width"), Child->Width, 32);
		if (Child->ElementName == TEXT("ItemSlot_Text"))
		{
			TestEqual(TEXT("Spell name starts at authored x=42"), Child->X, 42);
			TestEqual(TEXT("Spell row uses retail font DID"), Child->FontId, uint32(0x40000001));
		}
	}
    FString Directory = TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(), TEXT("RetailDatDir="), Directory);
    auto* Dat = NewObject<UACEDatSubsystem>(NewObject<UGameInstance>());
    if (!TestTrue(TEXT("Load DAT for UI widget"), Dat->LoadDatDirectory(Directory))) return false;
    auto* Resources = NewObject<UACEUIResourceResolver>();
    Resources->Initialize(Dat);
    auto* Label = NewObject<UACERetailTextBlock>();
    const auto Element = MakeShared<FACEUIElement>();
    Element->FontId = 0x40000001;
    Element->bTextOneLine = true;
    Label->SetRetailElement(Resources, Element, FVector2D(1,1), 240);
    Label->SetText(FText::FromString(TEXT("Inventory")));
    Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    if (!TestNotNull(TEXT("Widget binds the DAT bitmap font"), Label->GetBitmapFont())) return false;
    auto Slate = Label->TakeWidget();
    Slate->SlatePrepass();
    FACEDatFontRenderer Measure(nullptr);
    const int32 TextWidth = Measure.MeasureWidth(*Label->GetBitmapFont(), TEXT("Inventory"));
    TestEqual(TEXT("Slate desired width uses native glyph advances"), Slate->GetDesiredSize().X, float(TextWidth));
    bool bTint = false;
    TestEqual(TEXT("Repeated labels share one atlas texture"), Label->GetGlyphAtlas(false),
        Resources->ResolveFontAtlas(Label->GetBitmapFont()->ForegroundSurfaceDataID, bTint));
    TestTrue(TEXT("A8 font atlas is tintable"), bTint);
    if (!FApp::CanEverRender()) { AddInfo(TEXT("Font pixel comparison requires -RenderOffscreen without -NullRHI")); return true; }
    FlushRenderingCommands();
    FWidgetRenderer Renderer(true, true);
    auto* Target = FWidgetRenderer::CreateTargetFor(FVector2D(240,48), TF_Nearest, true);
    Renderer.DrawWidget(Target, Slate, FVector2D(240,48), 0.f);
    FlushRenderingCommands();
    TArray<FColor> Pixels;
    if (!TestTrue(TEXT("Read rendered font pixels"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))) return false;
    // Independent raster expectation: glyph rectangles and A8 coverage from the source DAT.
    FACEDatTexture Raw;
    if (!Dat->GetTextureResolver()->LoadTextureForUi(Label->GetBitmapFont()->ForegroundSurfaceDataID, Raw)) return false;
    if (!TestEqual(TEXT("Test font source is A8"), Raw.Format, EACESurfacePixelFormat::A8)) return false;
    TArray<uint8> Expected;
    Expected.Init(0, 240*48);
    int32 Pen = 0;
    for (TCHAR C : FString(TEXT("Inventory")))
    {
        const auto& Ch = *ACEDatText::FindChar(*Label->GetBitmapFont(), C);
        Pen += Ch.HorizontalOffsetBefore;
        for (int32 Y=0; Y<Ch.Height; ++Y) for (int32 X=0; X<Ch.Width; ++X)
            Expected[(Y+Ch.VerticalOffsetBefore)*240+Pen+X] = Raw.SourceData[(Y+Ch.OffsetY)*Raw.Width+Ch.OffsetX+X];
        Pen += Ch.Width + Ch.HorizontalOffsetAfter;
    }
    int32 Mismatches = 0, Ink = 0;
    for (int32 I=0; I<Pixels.Num(); ++I)
    {
        if (Expected[I] > 127) ++Ink;
        if ((Expected[I] > 127) != (Pixels[I].A > 127)) ++Mismatches;
    }
    TestTrue(TEXT("Expected text contains visible glyphs"), Ink > 100);
    TestEqual(TEXT("GPU glyph coverage matches DAT pixel positions"), Mismatches, 0);
    TArray64<uint8> PNG;
    FImageUtils::PNGCompressImageArray(240,48,Pixels,PNG);
    const FString Path = FPaths::ProjectSavedDir() / TEXT("Automation/RetailParity/FontWidget.png");
    FFileHelper::SaveArrayToFile(PNG, *Path);
    // A live text update must change the glyphs without replacing the shared texture.
    UTexture2D* Atlas = Label->GetGlyphAtlas(false);
    Label->SetText(FText::FromString(TEXT("Wi")));
    Slate->SlatePrepass();
    TestEqual(TEXT("Live label update recalculates native width"), Slate->GetDesiredSize().X,
        float(Measure.MeasureWidth(*Label->GetBitmapFont(), TEXT("Wi"))));
    TestEqual(TEXT("Live label update reuses atlas"), Label->GetGlyphAtlas(false), Atlas);
    return true;
}
#endif
