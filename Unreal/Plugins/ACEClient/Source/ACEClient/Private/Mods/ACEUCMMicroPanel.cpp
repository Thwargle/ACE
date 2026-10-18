#include "Mods/ACEPluginStyle.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UACEPluginSubsystem::MakeUCMMicroPanel()
{
    FACEPluginStyle::Use(this,TEXT("ucmmicro"));
    const TWeakObjectPtr<UACEPluginSubsystem> Host(this);
    const auto UCM=[Host]() -> TSharedPtr<FACEClientPlugin>
    {if(Host.IsValid())for(auto P:Host->Plugins)if(P->Id==TEXT("ucm"))return P;return nullptr;};
    auto Rows=SNew(SVerticalBox);
    const auto Row=[Rows](const TCHAR* Label,TFunction<bool()> Read,TFunction<void(bool)> Write)
    {
        Rows->AddSlot().AutoHeight().Padding(2,4)
        [SNew(SCheckBox).Style(&FACEPluginStyle::Current()->Check).IsFocusable(false).ClickMethod(EButtonClickMethod::MouseDown)
            .IsChecked_Lambda([Read](){return Read()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([Write](ECheckBoxState State){Write(State==ECheckBoxState::Checked);})
            [SNew(SACEPluginText).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Regular",15))]];
    };
    Rows->AddSlot().AutoHeight().Padding(2,4)[SNew(SButton).ButtonStyle(&FACEPluginStyle::Current()->Button)
        .IsFocusable(false).HAlign(HAlign_Center).ContentPadding(FMargin(8,8))
        .OnClicked_Lambda([Host,UCM]()
        {
            if(Host.IsValid())
            {
                if(auto P=UCM())
                {
                    if(P->Running)Host->Stop(P->Id,TEXT("Stopped"),true);
                    else Host->Start(P->Id);
                }
            }
            return FReply::Handled();
        })
        [SNew(SACEPluginText).Font(FCoreStyle::GetDefaultFontStyle("Regular",18))
            .Text_Lambda([UCM](){auto P=UCM();return FText::FromString(P&&P->Running?TEXT("Stop UCM"):TEXT("Start UCM"));})]];
    for(const auto& Entry:TArray<TPair<FString,FString>>{
        {TEXT("buffing"),TEXT("Buffing")},{TEXT("combat"),TEXT("Combat")},
        {TEXT("looting"),TEXT("Looting")},{TEXT("navigation"),TEXT("Navigation")},
        {TEXT("recovery"),TEXT("Vital recovery")},{TEXT("buff_others"),TEXT("Buff others")},
        {TEXT("summon_pets"),TEXT("Summoning")},
        {TEXT("vendor_restock"),TEXT("Vendor restocking")},{TEXT("meta_enabled"),TEXT("Metas")},{TEXT("idle_peace"),TEXT("Peace when idle")}})
    {
        const FString Key=Entry.Key;
        Row(*Entry.Value,[UCM,Key]()
        {
            auto P=UCM();if(!P)return false;
            if(Key==TEXT("combat")){FString Mode;P->Profile->TryGetStringField(Key,Mode);return !Mode.IsEmpty()&&Mode!=TEXT("off");}
            bool Value=Key==TEXT("recovery")||Key==TEXT("meta_enabled");P->Profile->TryGetBoolField(Key,Value);return Value;
        },[Host,UCM,Key](bool On)
        {
            if(!Host.IsValid())return;auto P=UCM();if(!P)return;
            if(Key==TEXT("combat")){P->Profile->SetStringField(Key,On?TEXT("auto"):TEXT("off"));P->Profile->SetBoolField(TEXT("manual_combat"),false);}
            else P->Profile->SetBoolField(Key,On);
            Host->SaveCurrentProfile(P->Id,false);
        });
    }
    Rows->AddSlot().AutoHeight().Padding(2,4)[SNew(SButton).ButtonStyle(&FACEPluginStyle::Current()->Button).IsFocusable(false)
        .HAlign(HAlign_Center).ContentPadding(FMargin(8,8))
        .OnClicked_Lambda([Host](){if(Host.IsValid())Host->TogglePluginWindow(TEXT("ucm.log"));return FReply::Handled();})
        [SNew(SACEPluginText).Font(FCoreStyle::GetDefaultFontStyle("Regular",15)).Text(FText::FromString(TEXT("Open UCM Log")))]];
    return SNew(SBorder).BorderImage(&FACEPluginStyle::Current()->Background).Padding(10)
        [SNew(SScrollBox)+SScrollBox::Slot()[Rows]];
}
