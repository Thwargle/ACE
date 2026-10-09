#include "VR/ACEVRMenu.h"
#include "UI/ACERetailObjectNames.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRUIStyle.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUIElementManager.h"
#include "../UI/ACEAppraisalFormatting.h"
#include "Engine/GameInstance.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
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
    if(bInspectionOpen) {InspectSpell=Id;InspectItem=0;InspectionAppraisal={};}
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
    // Keep retail glyphs, wrapping and character color offsets together. The
    // shared color array is per character, not per newline-separated row.
    auto Source=[this](const TCHAR* Name)
    {return Binder && Binder->Manager?Binder->Manager->FindElementUnder(TEXT("RootGameplay_FloatyExamination_Field"),Name):nullptr;};
    auto Label=[&](const FString& Text,float Size,float Width,FLinearColor Color=FLinearColor::White,const TCHAR* Element=TEXT("ItemDisplayText"),const TArray<FLinearColor>& Colors=TArray<FLinearColor>())
    {return ACEVRUIStyle::Text(Client,Text,Size,Width,Color,Colors,Source(Element));};
    auto AddText=[&](const FString& Text,int32 Size,FLinearColor Color=FLinearColor::White,const TCHAR* Element=TEXT("ItemDisplayText"))
    {InspectionBody->AddSlot().AutoHeight().Padding(2,4)[Label(Text,Size,400,Color,Element)];};
    auto AddRow=[&](const ACEAppraisalFormatting::FCreatureDetailLine& Row,bool ColorLabel)
    {
        if(Row.Label.IsEmpty() && Row.Value.IsEmpty())
        {InspectionBody->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(10)];return;}
        InspectionBody->AddSlot().AutoHeight().Padding(2,3)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1)[Label(Row.Label,24,200,ColorLabel?Row.Color:FLinearColor::White)]
            +SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Right)[Label(Row.Value,24,200,Row.Color)]];
    };
    auto* Dat=Client->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
    if(InspectSpell)
    {
        FString Name,Details;uint32 Did=0;
        if(!Dat || !Dat->TryGetSpellInfo(InspectSpell,Name,Did)){AddText(TEXT("Spell information is unavailable."),24);return;}
        AddText(Name,28,FLinearColor::White,TEXT("DisplayedNameText"));
        auto SpellIcon=Icon(Did,64);SpellIcon->SetTag(TEXT("SpellInspectionIcon"));
        InspectionBody->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(2,8)[SpellIcon];
        Dat->TryGetSpellExamination(InspectSpell,Details);AddText(Details,24,FLinearColor::White,TEXT("SpellDisplayText"));return;
    }
    FACEWorldObject Object;
    if(!Client->GetWorldObject(InspectItem,Object)){AddText(TEXT("This item is no longer available."),24);return;}
    const auto& A=InspectionAppraisal;
    const bool Appraised=A.ObjectGuid==InspectItem;
    const auto TitleSource=Source(TEXT("DisplayedNameText"));
    AddText(Appraised?ACEAppraisalFormatting::ExaminationName(A):ACERetailObjectNames::Name(Object),28,
        TitleSource?TitleSource->TextColor.Get(ACEVRUIStyle::TextColor):ACEVRUIStyle::TextColor,TEXT("DisplayedNameText"));
    if(A.ObjectGuid!=InspectItem){AddText(TEXT("Waiting for the server's appraisal..."),24);return;}
    if(A.bIsCreature)
    {
        const auto Headings=ACEAppraisalFormatting::CreatureHeadings(A,Client->GetUIResourceResolver(),&Object);
        if(ACEAppraisalFormatting::UsesCharacterExamination(A))
        {
            for(const FString& Heading:{Headings.Heritage,Headings.Profession,Headings.PlayerKiller,Headings.Allegiance})
                if(!Heading.IsEmpty())AddText(Heading,24);
        }
        else if(!Headings.Type.IsEmpty())AddText(Headings.Type,24);
        AddRow({TEXT("Level"),A.Level>0?FString::FromInt(A.Level):TEXT("???")},false);
        for(const auto& Row:ACEAppraisalFormatting::CreatureStatLines(A))AddRow(Row,false);
        for(const auto& Row:ACEAppraisalFormatting::CreatureDetailLines(A))AddRow(Row,true);
        return;
    }
    auto Header=SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().Padding(2,4,14,4)[ItemIcon(Object,64)];
    Header->AddSlot().FillWidth(1).VAlign(VAlign_Center)[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[Label(A.bHasValue?TEXT("Value: ")+FText::AsNumber(A.Value).ToString():TEXT("Value: ???"),24,320,FLinearColor::White,TEXT("ItemValueText"))]
        +SVerticalBox::Slot().AutoHeight()[Label(A.bHasBurden?TEXT("Burden: ")+FText::AsNumber(A.Burden).ToString():TEXT("Burden: Unknown"),24,320,FLinearColor::White,TEXT("ItemBurdenText"))]];
    InspectionBody->AddSlot().AutoHeight().Padding(0,4)[Header];
    const FACEPlayerVitals Viewer=Client->GetPlayerVitals();
    const FString Details=ACEAppraisalFormatting::ItemExaminationText(A,Dat,false,false,&Viewer);
    InspectionBody->AddSlot().AutoHeight().Padding(2,4)[Label(Details,24,400,FLinearColor::White,TEXT("ItemDisplayText"),ACEAppraisalFormatting::ItemTextColors(A,Details))];
    if(!A.Inscription.IsEmpty())
    {
        auto Inscription=SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Label(A.Inscription,24,376,FLinearColor::Black,TEXT("ItemInscriptionText"))];
        const FString Scribe=A.StringProperties.FindRef(8);
        if(!Scribe.IsEmpty())Inscription->AddSlot().AutoHeight().HAlign(HAlign_Right)[Label(TEXT("--")+Scribe,24,376,FLinearColor::Black,TEXT("ItemInscriptionSignatureText"))];
        InspectionBody->AddSlot().AutoHeight().Padding(2,8)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(FColor(225,207,156))).Padding(12)[Inscription]];
    }
}
