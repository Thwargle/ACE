#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginStyle.h"
#include "ACEClientSubsystem.h"
#include "UI/ACEUIElementManager.h"
#include "Engine/GameInstance.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Styling/CoreStyle.h"

namespace
{
    FString OpacityWindow(const FString& Id)
    {return Id==TEXT("waypoint.dungeon")?TEXT("waypoint.map"):Id;}
    FString WindowOwner(const FString& Id)
    {return Id==TEXT("ucm.log")?TEXT("ucm"):Id.StartsWith(TEXT("waypoint."))?TEXT("waypoint"):Id;}
    TSharedPtr<FJsonValue> WindowDefault(const UACEPluginSubsystem* Host,const FString& Id,const TCHAR* Key)
    {
        const FString Owner=WindowOwner(Id);
        for(const auto& P:Host->Plugins)if(P->Id==Owner)
        {
            const TSharedPtr<FJsonObject>* Window=nullptr,*Presentation=nullptr;
            if(P->Presentation&&P->Presentation->TryGetObjectField(TEXT("window"),Window))
                if(auto Value=(*Window)->TryGetField(Key))return Value;
            if(P->Manifest&&P->Manifest->TryGetObjectField(TEXT("presentation"),Presentation)
                &&(*Presentation)->TryGetObjectField(TEXT("window"),Window))return (*Window)->TryGetField(Key);
        }
        return nullptr;
    }
}
bool UACEPluginSubsystem::IsPluginOpacityGlobal() const
{bool Global=false;if(Settings)Settings->TryGetBoolField(TEXT("_plugin_opacity_global"),Global);return Global;}
void UACEPluginSubsystem::SetPluginOpacityGlobal(bool Global)
{
    if(!Settings)Settings=MakeShared<FJsonObject>();
    if(IsPluginOpacityGlobal()==Global)return;
    Settings->SetBoolField(TEXT("_plugin_opacity_global"),Global);SaveSettings();
}
float UACEPluginSubsystem::GetPluginWindowOpacity(const FString& Id) const
{
    const FString Window=OpacityWindow(Id);
    double Value=1.;if(Settings)Settings->TryGetNumberField(TEXT("_plugin_opacity"),Value);
    if(!Id.IsEmpty()&&!IsPluginOpacityGlobal())
    {
        if(!(Settings&&(Settings->TryGetNumberField(TEXT("_window_opacity.")+Window,Value)
            ||Settings->TryGetNumberField(TEXT("_window_opacity.")+WindowOwner(Window),Value))))
        {
            if(auto Default=WindowDefault(this,Window,TEXT("opacity")))Default->TryGetNumber(Value);
            // Preserve older Waypoint profiles until a shared window preference
            // is set. The normal and pinned maps now use the same opacity.
            if(Window==TEXT("waypoint.map"))if(auto P=Find(TEXT("waypoint")))
                P->Profile->TryGetNumberField(TEXT("map_opacity"),Value);
        }
    }
    return FMath::IsFinite(Value)?float(FMath::Clamp(Value,0.,1.)):1.f;
}
bool UACEPluginSubsystem::HasPluginWindowOpacity(const FString& Id) const
{return Settings&&Settings->HasTypedField<EJson::Number>(TEXT("_window_opacity.")+OpacityWindow(Id));}
void UACEPluginSubsystem::SetPluginWindowOpacity(const FString& Id,float Opacity,bool Persist)
{
    if(!FMath::IsFinite(Opacity))return;
    if(!Settings)Settings=MakeShared<FJsonObject>();
    const FString Key=Id.IsEmpty()?TEXT("_plugin_opacity"):TEXT("_window_opacity.")+OpacityWindow(Id);
    Settings->SetNumberField(Key,FMath::Clamp(Opacity,0.f,1.f));
    if(Persist)SaveSettings();
}
void UACEPluginSubsystem::ResetPluginWindowOpacity(const FString& Id)
{if(Settings&&HasPluginWindowOpacity(Id)){Settings->RemoveField(TEXT("_window_opacity.")+OpacityWindow(Id));SaveSettings();}}
bool UACEPluginSubsystem::IsPluginWindowPinned(const FString& Id) const
{
    bool Pinned=false;if(Settings&&Settings->TryGetBoolField(TEXT("_window_pin.")+Id,Pinned))return Pinned;
    if(auto Default=WindowDefault(this,Id,TEXT("pinned")))Default->TryGetBool(Pinned);return Pinned;
}
void UACEPluginSubsystem::SetPluginWindowPinned(const FString& Id,bool Pinned)
{
    if(Id.IsEmpty())return;if(!Settings)Settings=MakeShared<FJsonObject>();
    Settings->SetBoolField(TEXT("_window_pin.")+Id,Pinned);SaveSettings();
}
bool UACEPluginSubsystem::IsPluginQuickShow() const
{
    const auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();const auto* M=C?C->GetUIElementManager():nullptr;
    return M&&M->IsQuickShowUi();
}
bool UACEPluginSubsystem::IsPluginWindowChromeHidden(const FString& Id) const
{return !IsPluginQuickShow()&&(IsPluginWindowPinned(Id)||(IsLedger(Id)&&!IsPluginBarUnlocked()));}
bool UACEPluginSubsystem::CanManipulatePluginWindow(const FString& Id) const
{return IsPluginBarUnlocked()&&(!IsPluginWindowPinned(Id)||IsPluginQuickShow());}

TSharedRef<SWidget> UACEPluginSubsystem::MakePluginWindowControls(const FString& Id)
{
    auto Theme=FACEPluginStyle::Current();TWeakObjectPtr<UACEPluginSubsystem> H=this;
    auto Text=[](const TCHAR* Caption){return SNew(SACEPluginText).Text(FText::FromString(Caption)).Font(FCoreStyle::GetDefaultFontStyle("Regular",14));};
    auto Body=SNew(SVerticalBox);auto Row=SNew(SHorizontalBox);
    Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)[Text(TEXT("Background opacity"))];
    Row->AddSlot().FillWidth(1).VAlign(VAlign_Center)[SNew(SSlider)
        .Tag(FName(*(TEXT("PluginOpacity.")+Id))).Style(&Theme->Slider).MinValue(0).MaxValue(100).StepSize(1).MouseUsesStep(true).PreventThrottling(true)
        .IsEnabled_Lambda([H,Id]{return H.IsValid()&&(Id.IsEmpty()||!H->IsPluginOpacityGlobal());})
        .Value_Lambda([H,Id]{return H.IsValid()?H->GetPluginWindowOpacity(Id)*100.f:100.f;})
        .OnValueChanged_Lambda([H,Id](float Value){if(H.IsValid())H->SetPluginWindowOpacity(Id,FMath::RoundToInt(Value)/100.f,false);})
        .OnMouseCaptureEnd_Lambda([H]{if(H.IsValid())H->SaveSettings();})
        .OnControllerCaptureEnd_Lambda([H]{if(H.IsValid())H->SaveSettings();})
        .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Fade backgrounds and borders only. Text, icons and controls remain readable."))))];
    Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(8,0,0,0)[SNew(SBox).WidthOverride(42).HAlign(HAlign_Right)
        [SNew(SACEPluginText).Font(FCoreStyle::GetDefaultFontStyle("Regular",14))
            .Text_Lambda([H,Id]{return FText::FromString(FString::Printf(TEXT("%d%%"),H.IsValid()?FMath::RoundToInt(H->GetPluginWindowOpacity(Id)*100):100));})]];
    Body->AddSlot().AutoHeight()[SNew(SBox).MinDesiredHeight(24).VAlign(VAlign_Center)[Row]];
    if(Id.IsEmpty())Body->AddSlot().AutoHeight().Padding(0,5)[SNew(SCheckBox).Tag(TEXT("PluginOpacityGlobal")).Style(&Theme->Check)
        .IsChecked_Lambda([H]{return H.IsValid()&&H->IsPluginOpacityGlobal()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
        .OnCheckStateChanged_Lambda([H](ECheckBoxState V){if(H.IsValid())H->SetPluginOpacityGlobal(V==ECheckBoxState::Checked);})
        [Text(TEXT("Use this opacity for all plugins"))]];
    else
    {
        auto Options=SNew(SHorizontalBox);
        Options->AddSlot().FillWidth(1).VAlign(VAlign_Center)[SNew(SCheckBox).Tag(FName(*(TEXT("PluginPin.")+Id))).Style(&Theme->Check)
            .IsChecked_Lambda([H,Id]{return H.IsValid()&&H->IsPluginWindowPinned(Id)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([H,Id](ECheckBoxState V){if(H.IsValid())H->SetPluginWindowPinned(Id,V==ECheckBoxState::Checked);})
            .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Hide the window frame and lock its position. Hold Quick-show pinned windows to move or unpin."))))
            [Text(TEXT("Pin / hide frame"))]];
        Options->AddSlot().AutoWidth()[SNew(SACEPluginButton).ContentPadding(FMargin(FACEPluginStyle::ButtonPadding)).ButtonStyle(&Theme->Button).IsFocusable(false)
            .IsEnabled_Lambda([H,Id]{return H.IsValid()&&H->HasPluginWindowOpacity(Id)&&!H->IsPluginOpacityGlobal();})
            .OnClicked_Lambda([H,Id]{if(H.IsValid())H->ResetPluginWindowOpacity(Id);return FReply::Handled();})
            [Text(TEXT("Use plugin/default"))]];
        Body->AddSlot().AutoHeight().Padding(0,5)[Options];
    }
    return Body;
}
