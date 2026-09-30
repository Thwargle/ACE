#include "VR/ACEVRMenu.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRUIStyle.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "UI/ACEUIGameplayBinder.h"
#include "../UI/ACEAppraisalFormatting.h"
#include "Engine/GameInstance.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

void UACEVRMenu::QuickAction(bool Inspect,bool Pointed,FVector2D Pixel)
{
    if(!Client || !Binder || DragItem || DragSpell)return;
    if(Pointed)
    {
        const FVector2D Absolute=GetCachedGeometry().LocalToAbsolute(Pixel);
        if(!ContentScroll || !ContentScroll->GetCachedGeometry().IsUnderLocation(Absolute))return;
        const auto* Target=MenuTargets.FindByPredicate([&](const auto& T)
        {const auto Widget=T.Widget.Pin();return Widget && Widget->GetCachedGeometry().IsUnderLocation(Absolute);});
        if(!Target)return; // Chrome and empty slots never activate a stale item.
        if(Target->SpellId)SelectMenuSpell(Target->SpellId);
        else if(Target->Item)
        {
            const int32 PendingSource=UseSource;
            if(Inspect)UseSource=0; // Inspect must never complete targeted item use.
            SelectItem(Target->Item);
            if(Inspect)UseSource=PendingSource;
            else if(PendingSource)return;
        }
    }
    if(Inspect)InspectSelection();
    else if(Page=="Spellbook") {if(Spell && Rig)Rig->SelectSpell(Spell);}
    else Execute("Use / Equip");
}
void UACEVRMenu::SelectMenuSpell(int32 Id)
{
    Spell=Id;bDirty=true;
}
void UACEVRMenu::InspectSelection()
{
    if(!Client || !Binder)return;
    if(Page=="Spellbook")
    {
        if(!Spell)return;
        InspectSpell=Spell;InspectItem=0;
    }
    else
    {
        FACEWorldObject Item;if(!Selected || !Client->GetWorldObject(Selected,Item))return;
        InspectItem=Selected;InspectSpell=0;
        InspectionAppraisal=Binder->LastAppraisal.ObjectGuid==InspectItem?Binder->LastAppraisal:FACEAppraisalInfo{};
        Client->SendIdentifyObject(InspectItem);
    }
    bInspectionOpen=true;bDirty=true;
}
TSharedRef<SWidget> UACEVRMenu::GetInspectionWidget()
{
    if(!InspectionWidget)
    {
        InspectionWidget=ACEVRUIStyle::Frame(SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()[Button(TEXT("Close inspection"),[this](){bInspectionOpen=false;bDirty=true;})]
            +SVerticalBox::Slot().FillHeight(1).Padding(8)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(InspectionBody,SVerticalBox)]],12,true);
    }
    return InspectionWidget.ToSharedRef();
}
void UACEVRMenu::BuildInspection()
{
    GetInspectionWidget();InspectionBody->ClearChildren();
    auto AddText=[this](const FString& Text,int32 Size,FLinearColor Color=FLinearColor::White)
    {InspectionBody->AddSlot().AutoHeight().Padding(2,5)[SNew(STextBlock).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)).AutoWrapText(true).ColorAndOpacity(Color)];};
    auto* Dat=Client->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
    if(InspectSpell)
    {
        FString Name,Details;uint32 Did=0;
        if(!Dat || !Dat->TryGetSpellInfo(InspectSpell,Name,Did)){AddText(TEXT("Spell information is unavailable."),24);return;}
        InspectionBody->AddSlot().AutoHeight().HAlign(HAlign_Center)[Icon(Did,64)];
        AddText(Name,28,ACEVRUIStyle::TextColor);
        Dat->TryGetSpellExamination(InspectSpell,Details);AddText(Details,24);return;
    }
    FACEWorldObject Object;
    if(!Client->GetWorldObject(InspectItem,Object)){AddText(TEXT("This item is no longer available."),24);return;}
    InspectionBody->AddSlot().AutoHeight().HAlign(HAlign_Center)[ItemIcon(Object,64)];
    AddText(Object.Name,28,ACEVRUIStyle::TextColor);
    const auto& A=InspectionAppraisal;
    if(A.ObjectGuid!=InspectItem){AddText(TEXT("Waiting for the server's appraisal..."),24);return;}
    const FString Details=A.bIsCreature?A.Summary:ACEAppraisalFormatting::ItemExaminationText(A,Dat);
    TArray<FString> Lines;Details.ParseIntoArrayLines(Lines,false);
    const auto Colors=ACEAppraisalFormatting::ItemTextColors(A,Details);
    for(int32 I=0;I<Lines.Num();++I)AddText(Lines[I],24,Colors.IsValidIndex(I)?Colors[I]:FLinearColor::White);
    if(A.bIsCreature)AddText(FString::Printf(TEXT("Health %d / %d\nStamina %d / %d\nMana %d / %d"),A.Health,A.MaxHealth,A.Stamina,A.MaxStamina,A.Mana,A.MaxMana),24);
}
