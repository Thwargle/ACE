#include "UI/ACERetailKeySelector.h"
#include "ACEHoverTooltipWidget.h"
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
 virtual bool HandleMouseButtonDownEvent(FSlateApplication&,const FPointerEvent& Event) override
 { return Owner.IsValid() && Owner->FilterCaptureMouseButton(Event,true); }
 virtual bool HandleMouseButtonUpEvent(FSlateApplication&,const FPointerEvent& Event) override
 { return Owner.IsValid() && Owner->FilterCaptureMouseButton(Event,false); }
 virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication&,const FPointerEvent& Event) override
 { return Owner.IsValid() && Owner->FilterCaptureMouseButton(Event,true); }
 virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication&,const FPointerEvent& Event,const FPointerEvent*) override
 { return Owner.IsValid() && Owner->FilterCaptureWheel(Event); }
 virtual bool HandleAnalogInputEvent(FSlateApplication&,const FAnalogInputEvent& Event) override
 { return Owner.IsValid() && Owner->FilterCaptureAnalog(Event); }
};

bool UACERetailKeySelector::FilterCaptureAnalog(const FAnalogInputEvent& Event)
{
 const auto Selector=GetCachedWidget();
 if(!GetIsSelectingKey() || !Selector)return false;
 if(FMath::Abs(Event.GetAnalogValue())<.6f)return false;
 const FKey Key=ACEInputBindings::StickDirectionKey(Event.GetKey(),Event.GetAnalogValue());
 if(!Key.IsValid())return false;
 CapturePulse(FKeyEvent(Key,Event.GetModifierKeys(),Event.GetUserIndex(),false,0,0));
 return true;
}

bool UACERetailKeySelector::FilterCaptureWheel(const FPointerEvent& Event)
{
 const auto Selector=GetCachedWidget();
 if(!GetIsSelectingKey() || !Selector || FMath::IsNearlyZero(Event.GetWheelDelta()))return false;
 // Wheel input is a pulse, with modifiers from the actual pointer event.
 const FKey Key=Event.GetWheelDelta()>0?EKeys::MouseScrollUp:EKeys::MouseScrollDown;
 CapturePulse(FKeyEvent(Key,Event.GetModifierKeys(),Event.GetUserIndex(),false,0,0));
 return true;
}

bool UACERetailKeySelector::FilterCaptureMouseButton(const FPointerEvent& Event,bool Down)
{
 // Stock SInputKeySelector strips all modifiers from mouse clicks. Route clicks
 // through the same chord capture as keyboard/controller buttons instead.
 return FilterCaptureKey(FKeyEvent(Event.GetEffectingButton(),Event.GetModifierKeys(),Event.GetUserIndex(),false,0,0),Down);
}

bool UACERetailKeySelector::FilterCaptureKey(const FKeyEvent& Event,bool Down)
{
 if(!GetIsSelectingKey()) { CapturePressedKeys.Reset(); CaptureChord.Reset(); CaptureControllerModifiers.Reset(); return false; }
 if(Down)
 {
  if(!Event.IsRepeat()&&!CapturePressedKeys.Contains(Event.GetKey()))
  {
   CapturePressedKeys.Add(Event.GetKey());
   if(Event.GetKey()==EKeys::Escape){CaptureChord=Event;CaptureControllerModifiers.Reset();}
   else if(!Event.GetKey().IsModifierKey() && (!CaptureChord.IsSet()||CaptureChord->GetKey().IsGamepadKey()))
   {
    // The last button is the action; earlier held controller buttons become
    // modifiers. A trigger tapped on its own remains a normal trigger binding.
    CaptureChord=Event;CaptureControllerModifiers.Reset();
    for(FKey Held:CapturePressedKeys)if(Held.IsGamepadKey()&&Held!=Event.GetKey())CaptureControllerModifiers.Add(Held);
   }
  }
  return true;
 }
 // Windows can synthesize modifier releases after a focus change. Only a key
 // actually pressed during this capture may complete the binding.
 if(CapturePressedKeys.Remove(Event.GetKey())!=0)
 {
  // A held non-modifier owns the capture. For Shift+R, either release order
  // must bind Shift+R rather than Shift alone or an unmodified R.
  if(CaptureChord.IsSet() && CaptureChord->GetKey()!=Event.GetKey())return true;
  const FKeyEvent ChordEvent=CaptureChord.IsSet()?CaptureChord.GetValue():Event;
  // Deliver to the selector that owns capture. Platform focus/navigation must
  // not consume Tab/Enter or route the release to a different Slate widget.
  CompleteCapture(ChordEvent);
 }
 return true;
}
void UACERetailKeySelector::CapturePulse(const FKeyEvent& Event)
{
 CaptureControllerModifiers.Reset();
 for(FKey Held:CapturePressedKeys)if(Held.IsGamepadKey()&&Held!=Event.GetKey())CaptureControllerModifiers.Add(Held);
 CompleteCapture(Event);
}
void UACERetailKeySelector::CompleteCapture(const FKeyEvent& Event)
{
 if(CaptureControllerModifiers.IsEmpty()||Event.GetKey()==EKeys::Escape)
 {
  if(const auto Selector=GetCachedWidget())Selector->OnKeyUp(Selector->GetCachedGeometry(),Event);
  return;
 }
 // SInputKeySelector can only emit keyboard modifier bits. Finish its capture
 // without emitting a plain binding (which would unbind the plain-button action).
 const auto Modifiers=CaptureControllerModifiers;
 CancelCapture();
 ACEInputBindings::Set(ActionKey,BindingSlot,FInputChord(Event.GetKey(),Event.IsShiftDown(),Event.IsControlDown(),Event.IsAltDown(),Event.IsCommandDown()),Modifiers);
 RefreshBinding();
}
void UACERetailKeySelector::ReleaseSlateResources(bool ReleaseChildren)
{
 if(CaptureFilter && FSlateApplication::IsInitialized())
  FSlateApplication::Get().UnregisterInputPreProcessor(CaptureFilter);
 CaptureFilter.Reset();CapturePressedKeys.Reset();CaptureChord.Reset();CaptureControllerModifiers.Reset();
 Super::ReleaseSlateResources(ReleaseChildren);
}
void UACERetailKeySelector::CancelCapture()
{
 if (!GetIsSelectingKey() && !CaptureFilter) return;
 if (GetIsSelectingKey())
  if (const auto Selector=GetCachedWidget()) Selector->OnFocusLost(FFocusEvent(EFocusCause::Cleared, 0));
 CaptureStateChanged();
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
 CaptureChord.Reset();
 CaptureControllerModifiers.Reset();

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
 ActionKey=Key;BindingSlot=InSlot;SetAllowModifierKeys(true);SetAllowGamepadKeys(true);
 // The engine reserves controller Start as another Escape by default. All
 // controller buttons are bindable here; keyboard Escape still cancels.
 SetEscapeKeys({});
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
 UACEHoverTooltipWidget::SetWidgetTooltip(this, FText::FromString(TEXT("Click, then press a key, mouse button, controller button, move a stick, or scroll the wheel. For combinations, hold Shift/Ctrl/Alt or a controller button, then press the action button (for example, hold RT and press A). Release the action button to finish. Backspace alone clears; Escape cancels. Controller mappings are saved locally; retail keymap files contain the three keyboard/mouse columns.")));
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
  Caption.ReplaceInline(TEXT("Gamepad "),TEXT(""));
  Caption.ReplaceInline(TEXT("Left Thumbstick"),TEXT("L Stick"));Caption.ReplaceInline(TEXT("Right Thumbstick"),TEXT("R Stick"));
  Caption.ReplaceInline(TEXT("Left Trigger"),TEXT("LT"));Caption.ReplaceInline(TEXT("Right Trigger"),TEXT("RT"));
  Caption.ReplaceInline(TEXT("Left Shoulder"),TEXT("LB"));Caption.ReplaceInline(TEXT("Right Shoulder"),TEXT("RB"));
  FString ButtonName;
  if(Chord.Key==EKeys::Gamepad_FaceButton_Bottom)ButtonName=TEXT("A / Cross");
  else if(Chord.Key==EKeys::Gamepad_FaceButton_Right)ButtonName=TEXT("B / Circle");
  else if(Chord.Key==EKeys::Gamepad_FaceButton_Left)ButtonName=TEXT("X / Square");
  else if(Chord.Key==EKeys::Gamepad_FaceButton_Top)ButtonName=TEXT("Y / Triangle");
  else if(Chord.Key==EKeys::Gamepad_Special_Left)ButtonName=TEXT("Back / View");
  else if(Chord.Key==EKeys::Gamepad_Special_Right)ButtonName=TEXT("Start / Menu");
  if(!ButtonName.IsEmpty())
  {
   const FString Modifiers=(Chord.bCtrl?TEXT("Ctrl+"):FString())+(Chord.bAlt?TEXT("Alt+"):FString())
    +(Chord.bShift?TEXT("Shift+"):FString())+(Chord.bCmd?TEXT("Cmd+"):FString());
   Caption=Modifiers+ButtonName;
  }
  FString ControllerPrefix;
  for(FKey Modifier:ACEInputBindings::GetControllerModifiers(ActionKey,BindingSlot))
  {
   FString Name=Modifier.GetDisplayName().ToString();Name.ReplaceInline(TEXT("Gamepad "),TEXT(""));
   if(Modifier==EKeys::Gamepad_RightTrigger)Name=TEXT("RT");
   else if(Modifier==EKeys::Gamepad_LeftTrigger)Name=TEXT("LT");
   else if(Modifier==EKeys::Gamepad_LeftShoulder)Name=TEXT("LB");
   else if(Modifier==EKeys::Gamepad_RightShoulder)Name=TEXT("RB");
   else if(Modifier==EKeys::Gamepad_FaceButton_Bottom)Name=TEXT("A");
   else if(Modifier==EKeys::Gamepad_FaceButton_Right)Name=TEXT("B");
   else if(Modifier==EKeys::Gamepad_FaceButton_Left)Name=TEXT("X");
   else if(Modifier==EKeys::Gamepad_FaceButton_Top)Name=TEXT("Y");
   ControllerPrefix+=Name+TEXT("+");
  }
  Caption=ControllerPrefix+Caption;
  Caption.ReplaceInline(TEXT("L Stick "),TEXT("L "));Caption.ReplaceInline(TEXT("R Stick "),TEXT("R "));
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
 if (Chord.Key==EKeys::BackSpace && !Chord.bShift && !Chord.bCtrl && !Chord.bAlt && !Chord.bCmd) Chord=FInputChord();
 ACEInputBindings::Set(ActionKey,BindingSlot,Chord);
}
