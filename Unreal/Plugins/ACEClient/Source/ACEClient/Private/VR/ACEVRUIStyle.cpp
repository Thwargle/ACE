#include "ACEVRUIStyle.h"
#include "ACEClientSubsystem.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIResourceResolver.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/CoreStyle.h"

namespace
{
struct FNoticeTextStyle
{
 float Height, Width;
 FLinearColor Color;
 TArray<FLinearColor> GlyphColors;
 TSharedPtr<FACEUIElement> Source;
};
// Retain both the UMG label and its DAT element for the entire Slate lifetime.
// The same atlas renderer is used by the original chat and inventory windows.
class SNoticeText : public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SNoticeText) {} SLATE_END_ARGS()
 void Construct(const FArguments&, UACEClientSubsystem* Client, const FString& Value, const FNoticeTextStyle& Style)
 {
  const float Height=Style.Height,Width=Style.Width;
  const auto& Source=Style.Source;
  Label.Reset(NewObject<UACERetailTextBlock>());
  Element = Source ? MakeShared<FACEUIElement>(*Source) : MakeShared<FACEUIElement>();
  if (!Source) Element->FontId = 0x40000006; // retail basefont24
  Element->bTextOneLine = false;
  auto* Resources = Client ? Client->GetUIResourceResolver() : nullptr;
  Label->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", FMath::RoundToInt(Height * .75f)));
  // Resolve state/inherited font selection through the same renderer first,
  // then scale its actual glyph height for the headset's readable text size.
  Label->SetRetailElement(Resources, Element, FVector2D(1.f), 0.f, false);
  const auto* Font=Label->GetBitmapFont();
  const float Scale=Font?Height/FMath::Max(1,int32(Font->MaxCharHeight)):1.f;
  Label->SetRetailElement(Resources, Element, FVector2D(Scale), Width > 0 ? Width / Scale : 0.f, false);
  Label->SetText(FText::FromString(Value)); Label->SetColorAndOpacity(Style.Color);
  Label->SetAutoWrapText(true); Label->SetWrapTextAt(Width);
  Label->SetTextColors(Style.GlyphColors);
  ChildSlot[Label->TakeWidget()];
 }
private:
 TStrongObjectPtr<UACERetailTextBlock> Label;
 TSharedPtr<FACEUIElement> Element;
};
}

TSharedRef<SWidget> ACEVRUIStyle::Text(UACEClientSubsystem* Client, const FString& Value, float Height, float Width, FLinearColor Color,
    const TArray<FLinearColor>& GlyphColors, const TSharedPtr<FACEUIElement>& Source)
{
 const FNoticeTextStyle Style{Height,Width,Color,GlyphColors,Source};
 return SNew(SNoticeText, Client, Value, Style);
}

TSharedRef<SWidget> ACEVRUIStyle::Frame(TSharedRef<SWidget> Body, float Padding, bool bInteractive)
{
 const auto* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
 // Decorative notices exclude their children too; menus must keep buttons in
 // the widget-component hit grid for both controller pointers.
 return SNew(SBorder).Visibility(bInteractive ? EVisibility::SelfHitTestInvisible : EVisibility::HitTestInvisible).BorderImage(Brush).BorderBackgroundColor(Gold).Padding(2.f)
  [SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor::Black).Padding(2.f)
   [SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor(FColor(90,70,33))).Padding(1.f)
    [SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(Background).Padding(Padding)[Body]]]];
}
