#include "UI/ACERetailKeySelector.h"
#include "ACEInputBindings.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
void UACERetailKeySelector::InitializeBinding(FKey Key,int32 InSlot)
{
 ActionKey=Key;BindingSlot=InSlot;SetAllowModifierKeys(true);SetAllowGamepadKeys(false);
 FTextBlockStyle Text=FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
 Text.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9));
 Text.SetColorAndOpacity(FSlateColor(FLinearColor(.95f,.91f,.78f,1)));
 SetTextStyle(Text);
 FButtonStyle Button=GetButtonStyle();
 Button.SetNormal(FSlateColorBrush(FLinearColor(.06f,.045f,.02f,1)));
 Button.SetHovered(FSlateColorBrush(FLinearColor(.20f,.15f,.06f,1)));
 Button.SetPressed(FSlateColorBrush(FLinearColor(.3f,.22f,.08f,1)));
 SetButtonStyle(Button);
 SetNoKeySpecifiedText(FText::FromString(TEXT("Unbound")));
 SetKeySelectionText(FText::FromString(TEXT("Press a key")));
 SetSelectedKey(ACEInputBindings::Get(Key,InSlot));
 OnKeySelected.AddDynamic(this,&UACERetailKeySelector::AcceptBinding);
}
void UACERetailKeySelector::RefreshBinding()
{
 if (GetIsSelectingKey()) return;
 bSyncing=true;
 SetSelectedKey(ACEInputBindings::Get(ActionKey,BindingSlot));
 bSyncing=false;
}
void UACERetailKeySelector::AcceptBinding(FInputChord Chord)
{
 if (bSyncing) return;
 if (Chord.Key==EKeys::Delete || Chord.Key==EKeys::BackSpace) Chord=FInputChord();
 ACEInputBindings::Set(ActionKey,BindingSlot,Chord);
}
