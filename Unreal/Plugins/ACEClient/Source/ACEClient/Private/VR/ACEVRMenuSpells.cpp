#include "VR/ACEVRMenu.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRUIStyle.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "UI/ACEUIGameplayBinder.h"
#include "Components/WidgetInteractionComponent.h"
#include "Engine/GameInstance.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"

namespace
{
TSharedRef<SWidget> Text(const FString& Value,int32 Size=24)
{return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)).ColorAndOpacity(FLinearColor::White).AutoWrapText(true);}
const FButtonStyle& SpellSlotStyle()
{
    static const FButtonStyle Style=FButtonStyle()
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor(.014f,.019f,.027f),3,ACEVRUIStyle::Gold*.5f,1))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.12f,.10f,.04f),3,ACEVRUIStyle::Gold,2))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.035f,.13f,.09f),3,FLinearColor::White,2));
    return Style;
}
}
TSharedRef<SWidget> UACEVRMenu::SpellButton(int32 Id,int32 Bar,int32 Index,bool WithName)
{
    FString Name;uint32 Did=0;
    auto* Dat=Client->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
    if(Id && Dat)Dat->TryGetSpellInfo(Id,Name,Did);
    auto Content=SNew(SHorizontalBox);
    Content->AddSlot().AutoWidth().VAlign(VAlign_Center)[Icon(Did)];
    if(WithName)Content->AddSlot().FillWidth(1).Padding(10,0).VAlign(VAlign_Center)[Text(Name,22)];
    auto Cell=SNew(SBox).HeightOverride(WithName?76:64).WidthOverride(WithName?430:64)
        [SNew(SButton).Tag(FName(*FString::Printf(TEXT("Spell_%d_%d_%d"),Id,Bar,Index))).ButtonStyle(&SpellSlotStyle()).IsFocusable(false).ContentPadding(4)
            .HAlign(WithName?HAlign_Fill:HAlign_Center).VAlign(VAlign_Center)
            .ButtonColorAndOpacity_Lambda([this,Id,Bar](){return bItemDragging && DragSpell==Id && DragSpellBar==Bar?FLinearColor(.25f,.25f,.25f):Id && Id==Spell?FLinearColor(.25f,.7f,.4f):FLinearColor::White;})
            .OnHovered_Lambda([this,Name](){if(HoverLabel && !bItemDragging)HoverLabel->SetText(FText::FromString(Name));})
            .OnPressed_Lambda([this,Id,Bar](){BeginSpellPointer(Id,Bar);})
            .OnClicked_Lambda([this,Id](){if(Id && !bSuppressItemClick)SelectMenuSpell(Id);bSuppressItemClick=false;return FReply::Handled();})[Content]];
    if(Id)MenuTargets.Add({Cell,0,Id,Bar,Index});
    if(Bar!=INDEX_NONE)SpellDestinations.Add({Cell,Bar,Index});
    return Cell;
}
void UACEVRMenu::BeginSpellPointer(int32 Id,int32 Bar)
{
    bSuppressItemClick=false;
    if(!Id || !Rig)return;
    auto* Pointer=Rig->FeedbackHand==0?Rig->LeftPointer.Get():Rig->RightPointer.Get();
    if(!Pointer)return;
    DragSpell=Id;DragSpellBar=Bar;DragStart=Pointer->Get2DHitLocation();bItemDragging=false;
}
void UACEVRMenu::PlaceSpell(int32 Id,int32 SourceBar,int32 TargetBar,int32 Index)
{
    if(!Id || !Client || TargetBar<0 || TargetBar>=8)return;
    const auto Target=Client->GetSpellBar(TargetBar).FilterByPredicate([](int32 S){return S!=0;});
    const int32 Existing=Target.Find(Id);
    Index=FMath::Clamp(Index,0,Target.Num());
    if(Existing!=INDEX_NONE)
    {
        Client->SendRemoveSpellFromBar(Id,TargetBar);
        if(Existing<Index)--Index;
    }
    if(SourceBar!=INDEX_NONE && SourceBar!=TargetBar)Client->SendRemoveSpellFromBar(Id,SourceBar);
    Client->SendAddSpellToBar(Id,Index,TargetBar);Spell=Id;bDirty=true;
}
void UACEVRMenu::FinishSpellPointer(bool OverMenu,FVector2D Pixel,bool OverWorld)
{
    UpdateItemPointer(OverMenu,Pixel);
    const int32 Id=DragSpell,Source=DragSpellBar;const bool Dragged=bItemDragging;
    CancelItemPointer();bSuppressItemClick=Dragged;
    if(!Dragged || !Id)return;
    bDirty=true;
    if(OverMenu)
    {
        const FVector2D Absolute=GetCachedGeometry().LocalToAbsolute(Pixel);
        for(const auto& Destination:SpellDestinations)
        {
            const auto Widget=Destination.Widget.Pin();
            if(!Widget || !Widget->GetCachedGeometry().IsUnderLocation(Absolute))continue;
            if(Destination.Remove){if(Source!=INDEX_NONE)Client->SendRemoveSpellFromBar(Id,Source);}
            else PlaceSpell(Id,Source,Destination.Bar,Destination.Index);
            return;
        }
    }
    else if(OverWorld && Source!=INDEX_NONE)Client->SendRemoveSpellFromBar(Id,Source);
}
void UACEVRMenu::BuildSpells()
{
    auto* Dat=Client->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();if(!Dat)return;
    const int32 BarIndex=Client->GetActiveSpellBar();
    const auto Bar=Client->GetSpellBar(BarIndex).FilterByPredicate([](int32 Id){return Id!=0;});
    Body->AddSlot().AutoHeight().Padding(4)[Text(TEXT("Drag spells onto a hotbar slot. Drag back to the book to remove."),22)];
    auto Tabs=SNew(SHorizontalBox);
    for(int32 I=0;I<8;++I)
    {
        auto Control=Tab(FString::FromInt(I+1),I==BarIndex,[this,I](){Binder->SetCombatSpellBar(I);bDirty=true;});
        Tabs->AddSlot().FillWidth(1).Padding(2)[Control];SpellDestinations.Add({Control,I,MAX_int32});
    }
    Body->AddSlot().AutoHeight()[Tabs];
    auto BarIcons=SNew(SHorizontalBox);
    for(int32 I=0;I<FMath::Max(12,Bar.Num()+1);++I)BarIcons->AddSlot().AutoWidth().Padding(2)[SpellButton(Bar.IsValidIndex(I)?Bar[I]:0,BarIndex,I)];
    Body->AddSlot().AutoHeight().Padding(4)[SNew(SBox).HeightOverride(88)
        [SNew(SScrollBox).Orientation(Orient_Horizontal)+SScrollBox::Slot()[BarIcons]]];
    FString SelectedName;uint32 SelectedIcon=0;const bool Valid=Spell && Dat->TryGetSpellInfo(Spell,SelectedName,SelectedIcon);
    Body->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(60).Clipping(EWidgetClipping::ClipToBoundsAlways)
        [Text(Valid?SelectedName:TEXT("Select a spell to inspect it or edit the hotbar."),26)]];
    auto Actions=SNew(SHorizontalBox);
    Actions->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("Inspect"),[this](){InspectSelection();},Valid)];
    Actions->AddSlot().FillWidth(1).Padding(2)[Button(FString::Printf(TEXT("Add to bar %d"),BarIndex+1),[this,BarIndex](){PlaceSpell(Spell,INDEX_NONE,BarIndex,MAX_int32);},Valid && !Bar.Contains(Spell))];
    Actions->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("Remove"),[this,BarIndex](){Client->SendRemoveSpellFromBar(Spell,BarIndex);bDirty=true;},Valid && Bar.Contains(Spell))];
    Body->AddSlot().AutoHeight().Padding(2)[SNew(SBox).HeightOverride(64)[Actions]];
    Body->AddSlot().AutoHeight().Padding(4)[Entry(TEXT("Search known spells"),Search)];
    auto Filters=SNew(SHorizontalBox);
    Filters->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("Search"),[this](){if(Rig)Rig->DismissTextEntry();PageIndex=0;bDirty=true;})];
    const TCHAR* Schools[]={TEXT("All schools"),TEXT("War"),TEXT("Life"),TEXT("Item"),TEXT("Creature"),TEXT("Void")};
    SpellSchool=FMath::Clamp(SpellSchool,0,5);SpellLevel=FMath::Clamp(SpellLevel,0,8);
    auto SchoolFilter=Button(Schools[SpellSchool],[this](){SpellSchool=(SpellSchool+1)%6;PageIndex=0;bDirty=true;});
    SchoolFilter->SetTag("SpellSchoolFilter");Filters->AddSlot().FillWidth(1).Padding(2)[SchoolFilter];
    auto LevelFilter=Button(SpellLevel?FString::Printf(TEXT("Level %d"),SpellLevel):TEXT("All levels"),[this](){SpellLevel=(SpellLevel+1)%9;PageIndex=0;bDirty=true;});
    LevelFilter->SetTag("SpellLevelFilter");Filters->AddSlot().FillWidth(1).Padding(2)[LevelFilter];
    Filters->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("Clear filters"),[this](){Search.Reset();SpellSchool=SpellLevel=PageIndex=0;if(Rig)Rig->DismissTextEntry();bDirty=true;},!Search.IsEmpty() || SpellSchool || SpellLevel)];
    Body->AddSlot().AutoHeight()[Filters];
    TArray<int32> Spells;
    const FString Query=Search.TrimStartAndEnd();
    for(int32 Id:Client->GetKnownSpells())
    {
        FString Name;uint32 Did=0,School=0,Level=0;
        if(!Id || !Dat->TryGetSpellInfo(Id,Name,Did) || !Dat->TryGetSpellSchoolAndLevel(Id,School,Level))continue;
        // Retail gmSpellbookUI::IsFilteredOut excludes unclassified spells even
        // with every filter enabled; a known server ID need not be book-visible.
        if(School<1 || School>5 || Level<1 || Level>8)continue;
        if((!Query.IsEmpty() && !Name.Contains(Query,ESearchCase::IgnoreCase)) || (SpellSchool && School!=SpellSchool) || (SpellLevel && Level!=SpellLevel))continue;
        Spells.Add(Id);
    }
    Spells.Sort([Dat](int32 A,int32 B)
    {
        uint32 OrderA=0,OrderB=0;Dat->TryGetSpellDisplayOrder(A,OrderA);Dat->TryGetSpellDisplayOrder(B,OrderB);
        return OrderA!=OrderB?OrderA<OrderB:A<B;
    });
    // Three rows leave the hotbar and the entire current book page visible
    // together, so a controller drag never has to cross a scrolled-away bar.
    constexpr int32 PerPage=6;
    const int32 Pages=FMath::Max(1,FMath::DivideAndRoundUp(Spells.Num(),PerPage));PageIndex=FMath::Clamp(PageIndex,0,Pages-1);
    Body->AddSlot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Previous"),[this](){--PageIndex;bDirty=true;},PageIndex>0)]
        +SHorizontalBox::Slot().FillWidth(1).Padding(12)[Text(FString::Printf(TEXT("%d spells — page %d / %d"),Spells.Num(),PageIndex+1,Pages))]
        +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Next"),[this](){++PageIndex;bDirty=true;},PageIndex+1<Pages)]];
    auto Grid=SNew(SUniformGridPanel).SlotPadding(FMargin(3));
    for(int32 I=PageIndex*PerPage;I<FMath::Min(Spells.Num(),(PageIndex+1)*PerPage);++I)
        Grid->AddSlot(I%2,(I%PerPage)/2).VAlign(VAlign_Top)[SpellButton(Spells[I],INDEX_NONE,I,true)];
    if(Spells.IsEmpty())
    {
        auto Empty=Text(Client->GetKnownSpells().IsEmpty()?TEXT("You have not learned any spells yet."):TEXT("No known spells match these filters. Use Clear filters to show all schools and levels."),22);
        Empty->SetTag("SpellBookEmpty");Grid->AddSlot(0,0)[Empty];
    }
    auto Library=SNew(SBox).HeightOverride(280).VAlign(VAlign_Top)[Grid];Library->SetTag("SpellBookDrop");
    Body->AddSlot().AutoHeight()[Library];SpellDestinations.Add({Library,INDEX_NONE,0,true});
}
