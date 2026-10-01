#include "VR/ACEVRMenu.h"
#include "ACEClientSubsystem.h"
#include "ACEInventoryRules.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACERetailObjectNames.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
TSharedRef<SWidget> SalvageText(const FString& Value,int32 Size=24)
{return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size)).ColorAndOpacity(FLinearColor::White).AutoWrapText(true);}
}
void UACEVRMenu::BuildSalvage()
{
    if(!Binder->OpenSalvageToolGuid)
    {
        Body->AddSlot().AutoHeight().Padding(8)[SalvageText(TEXT("Use an Ust to open the salvage tool."),28)];
        for(const auto& Item:Client->GetWorldObjects())
            if(Client->IsOwnedInventoryItem(Item) && ACEInventoryRules::DetermineOwnedUse(Item,Client->GetPlayerGuid())==EACEOwnedItemUse::Salvage)
                Body->AddSlot().AutoHeight().Padding(6)[Button(TEXT("Use ")+ACERetailObjectNames::Name(Item),[this,Id=Item.Guid](){Binder->UseInventoryItem(Id);bDirty=true;})];
        return;
    }
    Body->AddSlot().AutoHeight().Padding(4)[SalvageText(TEXT("WARNING: Items in this panel will be destroyed!\nAdd items below, then press Salvage. Drag an offer out to remove it."),22)];
    auto Grid=PackGrid(Pack,360);
    const int32 FirstPackDestination=ItemDestinations.Num();
    auto Bags=SNew(SBox).WidthOverride(80).HeightOverride(360)[SNew(SScrollBox)+SScrollBox::Slot()[PackIcons()]];
    for(int32 I=FirstPackDestination;I<ItemDestinations.Num();++I)ItemDestinations[I].ClipWidget=Bags;
    auto Inventory=SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1)[Grid]
        +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[Bags];
    Body->AddSlot().AutoHeight()[Inventory];
    Body->AddSlot().AutoHeight().Padding(4)[SalvageText(FString::Printf(TEXT("Salvage list — %d item(s)"),Binder->SalvageQueueGuids.Num()))];
    auto Offers=SNew(SHorizontalBox);
    for(int32 Guid:Binder->SalvageQueueGuids)
    {
        FACEWorldObject Item;if(!Client->GetWorldObject(Guid,Item))continue;
        auto Offer=ItemButton(Item,64,true);Offer->SetTag(FName(*FString::Printf(TEXT("SalvageItem_%d"),Guid)));
        Offers->AddSlot().AutoWidth().Padding(3)[Offer];
    }
    // Match the retail horizontal item list: show its default slot squares
    // even before the first offer, and always leave one empty drop slot.
    constexpr int32 VisibleOfferSlots=12;
    for(int32 I=Binder->SalvageQueueGuids.Num();I<FMath::Max(VisibleOfferSlots,Binder->SalvageQueueGuids.Num()+1);++I)
    {
        auto Empty=Icon(0x06004D20,64);
        Empty->SetTag(FName(*FString::Printf(TEXT("SalvageEmpty_%d"),I)));
        Offers->AddSlot().AutoWidth().Padding(3)[Empty];
    }
    auto Queue=SNew(SBox).HeightOverride(84)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.035f,.055f,.06f)).Padding(4)
        [SNew(SScrollBox).Orientation(Orient_Horizontal)+SScrollBox::Slot()[Offers]]];
    Queue->SetTag("SalvageQueue");Body->AddSlot().AutoHeight().Padding(4)[Queue];
    FItemDestination Destination;Destination.Widget=Queue;Destination.Salvage=true;ItemDestinations.Add(Destination);
    FACEWorldObject SelectedItem;const bool Valid=Client->GetWorldObject(Selected,SelectedItem);
    Body->AddSlot().AutoHeight().Padding(4)[SNew(SBox).HeightOverride(48).Clipping(EWidgetClipping::ClipToBoundsAlways)
        [SalvageText(Valid?ACERetailObjectNames::Name(SelectedItem):TEXT("Select an item to add, remove, or inspect."))]];
    auto Actions=SNew(SHorizontalBox);
    Actions->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("Add item"),[this](){Binder->AddItemToSalvageQueue(Selected);bDirty=true;},Binder->CanAddItemToSalvageQueue(Selected))];
    Actions->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("Add pack"),[this](){Binder->AddItemToSalvageQueue(Pack);bDirty=true;},Binder->CanAddItemToSalvageQueue(Pack))];
    Actions->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("Remove"),[this](){Binder->RemoveItemFromSalvageQueue(Selected);bDirty=true;},Binder->SalvageQueueGuids.Contains(Selected))];
    Actions->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("Inspect"),[this](){InspectSelection();},Valid)];
    Actions->AddSlot().FillWidth(1).Padding(2)[Button(TEXT("Clear"),[this](){Binder->SalvageQueueGuids.Reset();Binder->SalvageMaterialType=0;Binder->SetSalvageScrollOffset(0);bDirty=true;},!Binder->SalvageQueueGuids.IsEmpty())];
    Body->AddSlot().AutoHeight()[Actions];
    Body->AddSlot().AutoHeight().Padding(4)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("Salvage items"),[this](){Binder->SubmitSalvageQueue();bDirty=true;},!Binder->SalvageQueueGuids.IsEmpty())]
        +SHorizontalBox::Slot().FillWidth(1).Padding(2)[Button(TEXT("Close salvage"),[this](){Binder->HideSalvagePanel();OpenPage("Inventory");})]];
}
