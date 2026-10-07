#include "Mods/ACEPluginSubsystem.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UACEPluginSubsystem::MakeUCMMicroPanel()
{
    const TWeakObjectPtr<UACEPluginSubsystem> Host(this);
    const auto UCM=[Host]() -> TSharedPtr<FACEClientPlugin>
    {if(Host.IsValid())for(auto P:Host->Plugins)if(P->Id==TEXT("ucm"))return P;return nullptr;};
    auto Rows=SNew(SVerticalBox);
    const auto Row=[Rows](const TCHAR* Label,TFunction<bool()> Read,TFunction<void(bool)> Write)
    {
        Rows->AddSlot().AutoHeight().Padding(2,4)
        [SNew(SCheckBox).IsFocusable(false).ClickMethod(EButtonClickMethod::MouseDown)
            .IsChecked_Lambda([Read](){return Read()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([Write](ECheckBoxState State){Write(State==ECheckBoxState::Checked);})
            [SNew(STextBlock).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Regular",15))]];
    };
    Row(TEXT("UCM running"),[UCM](){auto P=UCM();return P&&P->Running;},[Host,UCM](bool On)
    {if(Host.IsValid())if(auto P=UCM()){if(On)Host->Start(P->Id);else Host->Stop(P->Id,TEXT("Stopped"),true);}});
    for(const auto& Entry:TArray<TPair<FString,FString>>{
        {TEXT("buffing"),TEXT("Buffing")},{TEXT("combat"),TEXT("Combat")},
        {TEXT("looting"),TEXT("Looting")},{TEXT("navigation"),TEXT("Navigation")},
        {TEXT("recovery"),TEXT("Vital recovery")},{TEXT("buff_others"),TEXT("Buff others")},
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
            Host->SaveProfile(P->Id,P->ProfileName,Host->ProfileJson(P->Id),false);
        });
    }
    Rows->AddSlot().AutoHeight().Padding(2,6)[SNew(STextBlock).AutoWrapText(true)
        .Font(FCoreStyle::GetDefaultFontStyle("Regular",12))
        .Text_Lambda([UCM](){auto P=UCM();return FText::FromString(P?P->Status:TEXT("Enable UCM in Plugins."));})];
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.012f,.018f,.03f)).Padding(10)
        [SNew(SScrollBox)+SScrollBox::Slot()[Rows]];
}
