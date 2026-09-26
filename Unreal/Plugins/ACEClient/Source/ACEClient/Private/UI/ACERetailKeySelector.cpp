#include "UI/ACERetailKeySelector.h"
#include "ACEInputBindings.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "UI/ACEUICanvasWidget.h"
#include "Components/TextBlock.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"

class FACERetailKeyCaptureFilter : public IInputProcessor
{
 TWeakObjectPtr<UACERetailKeySelector> Owner;
public:
 explicit FACERetailKeyCaptureFilter(UACERetailKeySelector* InOwner):Owner(InOwner) {}
 virtual void Tick(float,FSlateApplication&,TSharedRef<ICursor>) override {}
 virtual bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent& Event) override
 { return Owner.IsValid() && Owner->FilterCaptureKey(Event,true); }
 virtual bool HandleKeyUpEvent(FSlateApplication&,const FKeyEvent& Event) override
 { return Owner.IsValid() && Owner->FilterCaptureKey(Event,false); }
};

bool UACERetailKeySelector::FilterCaptureKey(const FKeyEvent& Event,bool Down)
{
 if(!GetIsSelectingKey()) { CapturePressedKeys.Reset(); return false; }
 if(Down)
 {
  if(!Event.IsRepeat()) CapturePressedKeys.Add(Event.GetKey());
  return false;
 }
 // Windows can synthesize modifier releases after a focus change. Only a key
 // actually pressed during this capture may complete the binding.
 return CapturePressedKeys.Remove(Event.GetKey())==0;
}
void UACERetailKeySelector::ReleaseSlateResources(bool ReleaseChildren)
{
 if(CaptureFilter && FSlateApplication::IsInitialized())
  FSlateApplication::Get().UnregisterInputPreProcessor(CaptureFilter);
 CaptureFilter.Reset();CapturePressedKeys.Reset();
 Super::ReleaseSlateResources(ReleaseChildren);
}
TSharedRef<SWidget> UACERetailKeySelector::RebuildWidget()
{
 auto Widget=Super::RebuildWidget();
 SetTextBlockVisibility(ESlateVisibility::Collapsed);
 return Widget;
}
void UACERetailKeySelector::CaptureStateChanged()
{
 CapturePressedKeys.Reset();
 if(!FSlateApplication::IsInitialized()) return;
 if(GetIsSelectingKey() && !CaptureFilter)
 {
  CaptureFilter=MakeShared<FACERetailKeyCaptureFilter>(this);
  FSlateApplication::Get().RegisterInputPreProcessor(CaptureFilter);
 }
 else if(!GetIsSelectingKey() && CaptureFilter)
 {
  FSlateApplication::Get().UnregisterInputPreProcessor(CaptureFilter);
  CaptureFilter.Reset();
 }
}
void UACERetailKeySelector::InitializeBinding(FKey Key,int32 InSlot)
{
 ActionKey=Key;BindingSlot=InSlot;SetAllowModifierKeys(true);SetAllowGamepadKeys(false);
 FTextBlockStyle Text=FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
 Text.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9));
 Text.SetColorAndOpacity(FSlateColor(FLinearColor(.95f,.91f,.78f,1)));
 SetTextStyle(Text);
 // The DAT template paints the actual retail button and bitmap-font label.
 // This widget supplies only input capture, including mouse/modifier chords.
 FButtonStyle Button=GetButtonStyle();
 Button.SetNormal(FSlateNoResource()).SetHovered(FSlateNoResource()).SetPressed(FSlateNoResource());
 SetButtonStyle(Button);
 SetTextBlockVisibility(ESlateVisibility::Collapsed);
 SetMargin(FMargin(0));
 SetNoKeySpecifiedText(FText::GetEmpty());
 SetKeySelectionText(FText::FromString(TEXT("Press a key")));
 SetToolTipText(FText::FromString(TEXT("Click, then press a key or mouse button. Backspace clears this binding; Escape cancels.")));
 SetSelectedKey(ACEInputBindings::Get(Key,InSlot));
 OnKeySelected.AddDynamic(this,&UACERetailKeySelector::AcceptBinding);
 OnIsSelectingKeyChanged.AddDynamic(this,&UACERetailKeySelector::CaptureStateChanged);
}
void UACERetailKeySelector::SetRetailButton(UACEUICanvasWidget* Canvas,const TSharedPtr<FACEUIElement>& Element,UTextBlock* Label)
{
 RetailElement=Element;RetailLabel=Label;
 Canvas->PlaceWidgetAtElement(this,Element,100220);
 Canvas->PlaceWidgetAtElement(Label,Element,100221);
 Label->SetVisibility(ESlateVisibility::HitTestInvisible);
 Label->SetJustification(ETextJustify::Center);
 RefreshBinding();
}
void UACERetailKeySelector::RefreshBinding()
{
 if(RetailElement){RetailElement->bUseExplicitState=true;RetailElement->DefaultState=GetIsSelectingKey()?3:IsHovered()?2:1;}
 if(RetailLabel)
 {
  const auto Chord=ACEInputBindings::Get(ActionKey,BindingSlot);
  FString Caption=Chord.Key.IsValid()?(Chord.Key.IsModifierKey()?Chord.Key.GetDisplayName().ToString():Chord.GetInputText().ToString()):FString();
  Caption.ReplaceInline(TEXT("Left Shift"),TEXT("L Shift"));Caption.ReplaceInline(TEXT("Right Shift"),TEXT("R Shift"));
  Caption.ReplaceInline(TEXT("Left Control"),TEXT("L Ctrl"));Caption.ReplaceInline(TEXT("Right Control"),TEXT("R Ctrl"));
  Caption.ReplaceInline(TEXT("Left Alt"),TEXT("L Alt"));Caption.ReplaceInline(TEXT("Right Alt"),TEXT("R Alt"));
  Caption.ReplaceInline(TEXT("Space Bar"),TEXT("Space"));
  RetailLabel->SetText(FText::FromString(GetIsSelectingKey()?TEXT("Press a key"):Caption));
 }
 if (GetIsSelectingKey()) return;
 bSyncing=true;
 SetSelectedKey(ACEInputBindings::Get(ActionKey,BindingSlot));
 bSyncing=false;
}
void UACERetailKeySelector::AcceptBinding(FInputChord Chord)
{
 if (bSyncing) return;
 if (Chord.Key==EKeys::BackSpace) Chord=FInputChord();
 ACEInputBindings::Set(ActionKey,BindingSlot,Chord);
}
