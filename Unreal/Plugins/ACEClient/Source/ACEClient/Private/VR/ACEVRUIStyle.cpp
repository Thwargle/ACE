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
// Retain both the UMG label and its DAT element for the entire Slate lifetime.
// The same atlas renderer is used by the original chat and inventory windows.
class SNoticeText : public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SNoticeText) {} SLATE_END_ARGS()
 void Construct(const FArguments&, UACEClientSubsystem* Client, const FString& Value, float Height, float Width, FLinearColor Color)
 {
  Label.Reset(NewObject<UACERetailTextBlock>());
  Element = MakeShared<FACEUIElement>();
  Element->FontId = 0x40000006; // retail basefont24
  FACEDatFont Font;
  auto* Resources = Client ? Client->GetUIResourceResolver() : nullptr;
  const float Scale = Resources && Resources->ResolveFont(Element->FontId, Font) ? Height / FMath::Max(1, int32(Font.MaxCharHeight)) : 1.f;
  Label->SetRetailElement(Resources, Element, FVector2D(Scale), Width > 0 ? Width / Scale : 0.f, false);
  Label->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", FMath::RoundToInt(Height * .75f)));
  Label->SetText(FText::FromString(Value)); Label->SetColorAndOpacity(Color);
  ChildSlot[Label->TakeWidget()];
 }
private:
 TStrongObjectPtr<UACERetailTextBlock> Label;
 TSharedPtr<FACEUIElement> Element;
};
}

TSharedRef<SWidget> ACEVRUIStyle::Text(UACEClientSubsystem* Client, const FString& Value, float Height, float Width, FLinearColor Color)
{
 return SNew(SNoticeText, Client, Value, Height, Width, Color);
}

TSharedRef<SWidget> ACEVRUIStyle::Frame(TSharedRef<SWidget> Body, float Padding)
{
 const auto* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
 return SNew(SBorder).Visibility(EVisibility::HitTestInvisible).BorderImage(Brush).BorderBackgroundColor(Gold).Padding(2.f)
  [SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor::Black).Padding(2.f)
   [SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor(FColor(90,70,33))).Padding(1.f)
    [SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(Background).Padding(Padding)[Body]]]];
}
