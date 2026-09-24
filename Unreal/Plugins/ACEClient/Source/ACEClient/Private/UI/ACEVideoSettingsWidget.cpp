#include "UI/ACEVideoSettingsWidget.h"
#include "ACECameraSettings.h"
#include "ACERuntimeOptions.h"
#include "ACEClientSubsystem.h"
#include "ACECharacterOptions.h"
#include "Components/ScrollBox.h"
#include "Engine/GameInstance.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIResourceResolver.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "UI/ACERetailComboBoxString.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Engine/Texture2D.h"
#include "GameFramework/GameUserSettings.h"
#include "RHI.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UACEVideoSettingsWidget::RebuildWidget()
{
 if (Resolution) return Super::RebuildWidget();
 auto* Canvas = GetTypedOuter<UACEUICanvasWidget>();
 auto* Resources = Canvas ? Canvas->GetResourceResolver() : nullptr;
 auto Brush = [Resources](uint32 Did, FVector2D Size, bool bBorder = false)
 {
  FSlateBrush B; B.SetResourceObject(Resources ? Resources->ResolveTexture(Did) : nullptr);
  B.ImageSize=Size; B.DrawAs=bBorder ? ESlateBrushDrawType::Box : ESlateBrushDrawType::Image;
  if (bBorder) B.Margin=FMargin(2.f/Size.X,2.f/Size.Y);
  return B;
 };
 auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
 Scroll=WidgetTree->ConstructWidget<UScrollBox>(); Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed); Scroll->SetAllowOverscroll(false); Scroll->SetAnimateWheelScrolling(true); Scroll->AddChild(Box); WidgetTree->RootWidget=Scroll;
 auto Label=[&](const TCHAR* Text, int32 Size=8)
 {
  auto* L=WidgetTree->ConstructWidget<UACERetailTextBlock>();
  L->SetText(FText::FromString(Text)); L->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size));
  L->SetColorAndOpacity(FLinearColor::White); return L;
 };
 auto Fixed=[&](UWidget* Widget,float W,float H)
 {
  auto* S=WidgetTree->ConstructWidget<USizeBox>(); S->SetWidthOverride(W); S->SetHeightOverride(H); S->SetContent(Widget); return S;
 };
 auto Section=[&](const TCHAR* Name)
 {
  auto* Divider=WidgetTree->ConstructWidget<UImage>(); Divider->SetBrush(Brush(0x060012C5,FVector2D(292,8)));
  Box->AddChild(Fixed(Divider,272,8));
  auto* Title=Label(Name); Title->SetColorAndOpacity(FLinearColor::Yellow); Box->AddChild(Fixed(Title,272,22));
 };
 auto Row=[&](const TCHAR* Name,UWidget* Widget,float Height=20.f)
 {
  auto* R=WidgetTree->ConstructWidget<UHorizontalBox>();
  auto* Caption=Label(Name); Caption->SetJustification(ETextJustify::Right);
  R->AddChildToHorizontalBox(Fixed(Caption,130,14))->SetVerticalAlignment(VAlign_Center);
  R->AddChildToHorizontalBox(Fixed(Widget,120,18))->SetVerticalAlignment(VAlign_Center);
  Box->AddChild(Fixed(R,272,Height));
 };
 auto Combo=[&](const TCHAR* Name)
 {
  auto* C=WidgetTree->ConstructWidget<UACERetailComboBoxString>();
  FButtonStyle Button;
  const auto Background=Brush(0x060012B3,FVector2D(103,18),true);
  Button.SetNormal(Background).SetHovered(Background).SetPressed(Background);
  Button.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
  FComboBoxStyle Style=C->GetWidgetStyle();
  Style.ComboButtonStyle.SetButtonStyle(Button).SetDownArrowImage(Brush(0x060012B1,FVector2D(17,19)))
   .SetDownArrowPadding(FMargin(0)).SetContentPadding(FMargin(2,0,0,0)).SetMenuBorderBrush(Background).SetMenuBorderPadding(FMargin(2));
  C->SetWidgetStyle(Style); C->SetContentPadding(FMargin(0));
  auto Items=C->GetItemStyle();
  Items.SetEvenRowBackgroundBrush(Background).SetOddRowBackgroundBrush(Background)
   .SetEvenRowBackgroundHoveredBrush(Background).SetOddRowBackgroundHoveredBrush(Background);
  C->SetItemStyle(Items); C->OnGenerateWidgetEvent.BindDynamic(this,&UACEVideoSettingsWidget::GenerateOption);
  Row(Name,C); return C;
 };
 FSliderStyle RuntimeSlider;
 RuntimeSlider.SetNormalBarImage(Brush(0x06001285,FVector2D(120,12))).SetHoveredBarImage(RuntimeSlider.NormalBarImage).SetDisabledBarImage(RuntimeSlider.NormalBarImage)
  .SetNormalThumbImage(Brush(0x06001286,FVector2D(7,12))).SetHoveredThumbImage(RuntimeSlider.NormalThumbImage).SetDisabledThumbImage(RuntimeSlider.NormalThumbImage).SetBarThickness(4);
 FCheckBoxStyle RoundToggle;
 RoundToggle.SetUncheckedImage(Brush(0x06004D15,FVector2D(13,13))).SetUncheckedHoveredImage(RoundToggle.UncheckedImage).SetUncheckedPressedImage(Brush(0x06004D16,FVector2D(13,13)))
  .SetCheckedImage(Brush(0x06004D17,FVector2D(13,13))).SetCheckedHoveredImage(RoundToggle.CheckedImage).SetCheckedPressedImage(Brush(0x06004D18,FVector2D(13,13)));
 auto AddValue = [&](const ACERuntimeOptions::FOption& O) {
  auto* V=WidgetTree->ConstructWidget<USlider>(); V->SetWidgetStyle(RuntimeSlider); V->SetMinValue(O.Min); V->SetMaxValue(O.Max); V->SetStepSize(.01f);
  V->SetValue(ACERuntimeOptions::Get(O.Key)); ValueSliders.Add(O.Key,V); Row(O.Label,V);
 };
 Section(TEXT("Sound Options"));
 for (int32 I=0;I<3;++I) AddValue(ACERuntimeOptions::Values[I]);
 ActiveSoundOnly=WidgetTree->ConstructWidget<UCheckBox>(); ActiveSoundOnly->SetWidgetStyle(RoundToggle); ActiveSoundOnly->SetContent(Label(TEXT("Play Sounds Only When Active"))); Box->AddChild(Fixed(ActiveSoundOnly,272,20));
 Section(TEXT("Camera Options"));
 for (int32 I=4;I<7;++I) AddValue(ACERuntimeOptions::Values[I]);
 Section(TEXT("Graphics Options"));
 DesktopScale=Combo(TEXT("Desktop UI scale"));
 DesktopScale->Rename(TEXT("DesktopUIScale"));
 for(int32 Percent=100;Percent<=300;Percent+=25) DesktopScale->AddOption(FString::Printf(TEXT("%d%%"),Percent));
 DesktopScale->SetToolTipText(FText::FromString(TEXT("Scales the desktop interface in 25% steps. Limited to fit the window; VR uses its own panel scale. 200% and 300% give whole-pixel enlargement of native artwork.")));
 ShowFrameRate=WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(),TEXT("ShowFrameRate"));
 ShowFrameRate->SetWidgetStyle(RoundToggle);ShowFrameRate->SetContent(Label(TEXT("Show FPS overlay")));Box->AddChild(Fixed(ShowFrameRate,272,20));
 Resolution=Combo(TEXT("Resolution"));
 FScreenResolutionArray Modes; RHIGetAvailableResolutions(Modes,true);
 for (const auto& M:Modes) if(M.Width>=800&&M.Height>=600)
 {
  FString Value=FString::Printf(TEXT("%u x %u"),M.Width,M.Height);
  if(Resolution->FindOptionIndex(Value)<0)Resolution->AddOption(Value);
 }
 WindowMode=Combo(TEXT("Display")); for(const TCHAR* S:{TEXT("Fullscreen"),TEXT("Borderless"),TEXT("Windowed")})WindowMode->AddOption(S);
 FrameLimit=Combo(TEXT("Frame limit")); for(const TCHAR* S:{TEXT("Unlimited"),TEXT("30"),TEXT("60"),TEXT("120"),TEXT("144"),TEXT("240")})FrameLimit->AddOption(S);
 VSync=WidgetTree->ConstructWidget<UCheckBox>();
 FCheckBoxStyle Toggle;
 Toggle.SetUncheckedImage(Brush(0x06004D15,FVector2D(13,13))).SetUncheckedHoveredImage(Brush(0x06004D15,FVector2D(13,13)))
  .SetUncheckedPressedImage(Brush(0x06004D16,FVector2D(13,13))).SetCheckedImage(Brush(0x06004D17,FVector2D(13,13)))
  .SetCheckedHoveredImage(Brush(0x06004D17,FVector2D(13,13))).SetCheckedPressedImage(Brush(0x06004D18,FVector2D(13,13)))
  .SetPadding(FMargin(3,0));
 VSync->SetWidgetStyle(Toggle); VSync->SetContent(Label(TEXT("Sync with Refresh Rate"))); Box->AddChild(Fixed(VSync,272,20));
 AddValue(ACERuntimeOptions::Values[7]);
 Section(TEXT("Rendering Quality Options"));
 Quality=Combo(TEXT("Graphics quality")); for(const TCHAR* S:{TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Epic"),TEXT("Cinematic"),TEXT("Custom")})Quality->AddOption(S);
 static const TCHAR* QualityNames[] = {TEXT("Texture Detail"),TEXT("Landscape Draw Distance"),TEXT("Shadows"),TEXT("Effects"),TEXT("Post Processing"),TEXT("Anti-Aliasing"),TEXT("Foliage"),TEXT("Shading")};
 for (const TCHAR* Name : QualityNames) {
  auto* C=Combo(Name); for(const TCHAR* V:{TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Epic"),TEXT("Cinematic")}) C->AddOption(V); QualityLevels.Add(C);
 }
 Filtering=Combo(TEXT("Texture Filtering")); for (const TCHAR* V:{TEXT("1x"),TEXT("2x"),TEXT("4x"),TEXT("8x"),TEXT("16x")}) Filtering->AddOption(V);
 Section(TEXT("Input Options"));
 InvertMouseX=WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(),TEXT("InvertMouseX"));
 InvertMouseY=WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(),TEXT("InvertMouseY"));
 InvertMouseX->SetWidgetStyle(RoundToggle);InvertMouseX->SetContent(Label(TEXT("Invert mouse X (horizontal look)")));Box->AddChild(Fixed(InvertMouseX,272,20));
 InvertMouseY->SetWidgetStyle(RoundToggle);InvertMouseY->SetContent(Label(TEXT("Invert mouse Y (vertical look)")));Box->AddChild(Fixed(InvertMouseY,272,20));
 MouseTurnSpeed=WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(),TEXT("MouseTurnSpeed"));
 FSliderStyle Slider;
 Slider.SetNormalBarImage(Brush(0x06001285,FVector2D(120,12))).SetHoveredBarImage(Slider.NormalBarImage).SetDisabledBarImage(Slider.NormalBarImage)
  .SetNormalThumbImage(Brush(0x06001286,FVector2D(7,12))).SetHoveredThumbImage(Slider.NormalThumbImage).SetDisabledThumbImage(Slider.NormalThumbImage).SetBarThickness(4);
 MouseTurnSpeed->SetWidgetStyle(Slider); MouseTurnSpeed->SetMinValue(ACECameraSettings::MinMouseTurnSpeed); MouseTurnSpeed->SetMaxValue(ACECameraSettings::MaxMouseTurnSpeed);
 MouseTurnSpeed->SetValue(ACECameraSettings::GetMouseTurnSpeed()); MouseTurnSpeed->SetStepSize(.05f);
 MouseTurnSpeed->OnValueChanged.AddDynamic(this,&UACEVideoSettingsWidget::ChangeMouseTurnSpeed);
 Row(TEXT("Mouse Turn Speed"),MouseTurnSpeed);
 auto* Ends=Label(TEXT("Slow                         Fast"),7); Ends->SetJustification(ETextJustify::Right); Box->AddChild(Fixed(Ends,250,14));
 TArray<const FACECharacterOptionDesc*> Options; ACECharacterOptions::GetPageOptions(ACECharacterOptions::PageConfig,Options);
 for (const auto* O:Options) {
  auto* C=WidgetTree->ConstructWidget<UCheckBox>(); C->SetWidgetStyle(RoundToggle); C->SetContent(Label(O->Label)); Box->AddChild(Fixed(C,272,20)); CharacterChecks.Add(O->Option,C);
 }
 Section(TEXT("User Interface Options"));
 ChatFontFace=Combo(TEXT("Chat Font")); ChatFontFace->Rename(TEXT("ChatFontFace"));
 for(const TCHAR* Name:{TEXT("Arial"),TEXT("Courier New"),TEXT("Palatino Linotype"),TEXT("Tahoma"),TEXT("Times New Roman")}) ChatFontFace->AddOption(Name);
 ChatFontSize=Combo(TEXT("Chat Font Size")); ChatFontSize->Rename(TEXT("ChatFontSize"));
 for(const TCHAR* Name:{TEXT("Tiny"),TEXT("Small"),TEXT("Medium"),TEXT("Large"),TEXT("Extra Large")}) ChatFontSize->AddOption(Name);
 ResetVideo();
 return Super::RebuildWidget();
}
void UACEVideoSettingsWidget::ResetVideo()
{
 auto* S=UGameUserSettings::GetGameUserSettings();if(!S||!Resolution)return;
 const auto R=S->GetScreenResolution();const FString Value=FString::Printf(TEXT("%d x %d"),R.X,R.Y);
 if(Resolution->FindOptionIndex(Value)<0)Resolution->AddOption(Value);Resolution->SetSelectedOption(Value);
 WindowMode->SetSelectedIndex(int32(S->GetFullscreenMode()));
 const int32 Q=S->GetOverallScalabilityLevel();Quality->SetSelectedIndex(Q>=0?Q:5);
 const int32 FPS=FMath::RoundToInt(S->GetFrameRateLimit());const FString Limit=FPS>0?FString::FromInt(FPS):TEXT("Unlimited");
 if(FrameLimit->FindOptionIndex(Limit)<0)FrameLimit->AddOption(Limit);FrameLimit->SetSelectedOption(Limit);
 VSync->SetIsChecked(S->IsVSyncEnabled());
 InitialQuality=Quality->GetSelectedIndex();
 const int32 Levels[]={S->GetTextureQuality(),S->GetViewDistanceQuality(),S->GetShadowQuality(),S->GetVisualEffectQuality(),S->GetPostProcessingQuality(),S->GetAntiAliasingQuality(),S->GetFoliageQuality(),S->GetShadingQuality()};
 for(int32 I=0;I<QualityLevels.Num();++I)QualityLevels[I]->SetSelectedIndex(Levels[I]);
 for(auto& P:ValueSliders)P.Value->SetValue(ACERuntimeOptions::Get(*P.Key.ToString()));
 ActiveSoundOnly->SetIsChecked(ACERuntimeOptions::Get(TEXT("ActiveSoundOnly"))>.5f);
 Filtering->SetSelectedIndex(FMath::Clamp(FMath::FloorLog2(FMath::RoundToInt(ACERuntimeOptions::Get(TEXT("Anisotropy")))),0,4));
 if(auto* GI=GetGameInstance()) if(auto* Client=GI->GetSubsystem<UACEClientSubsystem>()) for(auto& P:CharacterChecks)P.Value->SetIsChecked(Client->IsCharacterOptionSet(P.Key));
 if(MouseTurnSpeed)MouseTurnSpeed->SetValue(ACECameraSettings::GetMouseTurnSpeed());
 InvertMouseX->SetIsChecked(ACECameraSettings::GetInvertMouseX());InvertMouseY->SetIsChecked(ACECameraSettings::GetInvertMouseY());
 DesktopScale->SetSelectedIndex(FMath::RoundToInt((ACERuntimeOptions::Get(TEXT("DesktopUIScale"))-1.f)*4.f));
 ShowFrameRate->SetIsChecked(ACERuntimeOptions::Get(TEXT("ShowFrameRate"))>.5f);
 ChatFontFace->SetSelectedIndex(FMath::RoundToInt(ACERuntimeOptions::Get(TEXT("ChatFontFace"))));
 ChatFontSize->SetSelectedIndex(FMath::RoundToInt(ACERuntimeOptions::Get(TEXT("ChatFontSize"))));
}
UWidget* UACEVideoSettingsWidget::GenerateOption(FString Option)
{
 auto* Row=WidgetTree->ConstructWidget<UACERetailOptionWidget>();
 Row->Option=MoveTemp(Option);
 return Row;
}
TSharedRef<SWidget> UACERetailOptionWidget::RebuildWidget()
{
 if (!WidgetTree->RootWidget)
 {
  auto* Label=WidgetTree->ConstructWidget<UACERetailTextBlock>();
  Label->SetText(FText::FromString(Option));
  Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),8));
  Label->SetJustification(ETextJustify::Center);
  Label->SetColorAndOpacity(FSlateColor(FLinearColor(.95f,.91f,.78f,1)));
  WidgetTree->RootWidget=Label;
 }
 return Super::RebuildWidget();
}
void UACEVideoSettingsWidget::ApplyVideo()
{
 auto* S=UGameUserSettings::GetGameUserSettings();if(!S||!Resolution)return;
 FString X,Y;if(Resolution->GetSelectedOption().Split(TEXT(" x "),&X,&Y))S->SetScreenResolution(FIntPoint(FCString::Atoi(*X),FCString::Atoi(*Y)));
 S->SetFullscreenMode(EWindowMode::Type(FMath::Clamp(WindowMode->GetSelectedIndex(),0,2)));
 if(Quality->GetSelectedIndex()<5 && Quality->GetSelectedIndex()!=InitialQuality) S->SetOverallScalabilityLevel(Quality->GetSelectedIndex());
 else if(QualityLevels.Num()==8) {
  S->SetTextureQuality(QualityLevels[0]->GetSelectedIndex()); S->SetViewDistanceQuality(QualityLevels[1]->GetSelectedIndex()); S->SetShadowQuality(QualityLevels[2]->GetSelectedIndex());
  S->SetVisualEffectQuality(QualityLevels[3]->GetSelectedIndex()); S->SetPostProcessingQuality(QualityLevels[4]->GetSelectedIndex()); S->SetAntiAliasingQuality(QualityLevels[5]->GetSelectedIndex());
  S->SetFoliageQuality(QualityLevels[6]->GetSelectedIndex()); S->SetShadingQuality(QualityLevels[7]->GetSelectedIndex());
 }
 // Validation can reload/reset the config on first launch or after an engine
 // settings-version change. Save our preferences after that reload completes.
 S->SetFrameRateLimit(FCString::Atof(*FrameLimit->GetSelectedOption()));S->SetVSyncEnabled(VSync->IsChecked());
 S->ValidateSettings();S->ApplySettings(false);S->ConfirmVideoMode();S->SaveSettings();
 for(const auto& P:ValueSliders) ACERuntimeOptions::Set(*P.Key.ToString(),P.Value->GetValue());
 ApplyInterfaceOptions();
 ACERuntimeOptions::Set(TEXT("ActiveSoundOnly"),ActiveSoundOnly->IsChecked()?1:0);
 ACERuntimeOptions::Set(TEXT("Anisotropy"),float(1<<FMath::Clamp(Filtering->GetSelectedIndex(),0,4)));
 if(auto* GI=GetGameInstance()) if(auto* Client=GI->GetSubsystem<UACEClientSubsystem>()) for(auto& P:CharacterChecks)Client->SendSetSingleCharacterOption(P.Key,P.Value->IsChecked());
 ACERuntimeOptions::Apply(); ResetVideo();
}


void UACEVideoSettingsWidget::ApplyInterfaceOptions()
{
 ACECameraSettings::SetMouseInversion(InvertMouseX->IsChecked(),InvertMouseY->IsChecked());
 if(DesktopScale->GetSelectedIndex()>=0) ACERuntimeOptions::Set(TEXT("DesktopUIScale"),1.f+DesktopScale->GetSelectedIndex()*.25f);
 ACERuntimeOptions::Set(TEXT("ShowFrameRate"),ShowFrameRate->IsChecked()?1.f:0.f);
 if(ChatFontFace->GetSelectedIndex()>=0) ACERuntimeOptions::Set(TEXT("ChatFontFace"),ChatFontFace->GetSelectedIndex());
 if(ChatFontSize->GetSelectedIndex()>=0) ACERuntimeOptions::Set(TEXT("ChatFontSize"),ChatFontSize->GetSelectedIndex());
 ACERuntimeOptions::Apply();
}

void UACEVideoSettingsWidget::ChangeMouseTurnSpeed(float Value)
{
 ACECameraSettings::SetMouseTurnSpeed(Value);
 MouseTurnSpeed->SetToolTipText(FText::FromString(FString::Printf(TEXT("Mouse turn speed: %d%%"),FMath::RoundToInt(Value*100.f))));
}
void UACEVideoSettingsWidget::ResetMouseTurnSpeed()
{
 ACECameraSettings::SetMouseTurnSpeed(ACECameraSettings::DefaultMouseTurnSpeed);
 if(MouseTurnSpeed)MouseTurnSpeed->SetValue(ACECameraSettings::DefaultMouseTurnSpeed);
}
void UACEVideoSettingsWidget::DefaultsVideo()
{
 if(!Resolution)return;
 WindowMode->SetSelectedIndex(0); Quality->SetSelectedIndex(2); FrameLimit->SetSelectedIndex(0); VSync->SetIsChecked(false);
 for(const auto& O:ACERuntimeOptions::Values) if(auto* V=ValueSliders.Find(O.Key))(*V)->SetValue(O.Default);
 ActiveSoundOnly->SetIsChecked(true); Filtering->SetSelectedIndex(4);
 for(UComboBoxString* C:QualityLevels)C->SetSelectedIndex(2);
 for(auto& P:CharacterChecks) if(const auto* O=ACECharacterOptions::Find(P.Key)) P.Value->SetIsChecked(((O->bInOptions2?ACECharacterOptions::Options2Default:ACECharacterOptions::Options1Default)&O->Flag)!=0);
 ResetMouseTurnSpeed();
 InvertMouseX->SetIsChecked(false);InvertMouseY->SetIsChecked(false);DesktopScale->SetSelectedIndex(0);
 ShowFrameRate->SetIsChecked(false);
 ChatFontFace->SetSelectedIndex(2); ChatFontSize->SetSelectedIndex(1);
}

float UACEVideoSettingsWidget::GetScrollOffset() const { return Scroll?Scroll->GetScrollOffset():0; }
float UACEVideoSettingsWidget::GetScrollEnd() const { return Scroll?Scroll->GetScrollOffsetOfEnd():0; }
void UACEVideoSettingsWidget::SetScrollOffset(float Offset) { if(Scroll)Scroll->SetScrollOffset(FMath::Clamp(Offset,0.f,GetScrollEnd())); }
