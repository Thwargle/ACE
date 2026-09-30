#include "VR/ACEVRMenu.h"
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRUIStyle.h"
#include "ACEClientSubsystem.h"
#include "ACEInventoryRules.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUIResourceResolver.h"
#include "Components/WidgetInteractionComponent.h"
#include "Engine/Texture2D.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"

namespace
{
TSharedRef<SWidget> Text(const FString& Value,int32 Size=24)
{
    return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle("Regular",Size))
        .ColorAndOpacity(FLinearColor::White).AutoWrapText(true);
}
const FButtonStyle& SlotStyle()
{
    static const FButtonStyle Style=FButtonStyle()
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor(.014f,.019f,.027f),3,ACEVRUIStyle::Gold*.5f,1))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.12f,.10f,.04f),3,ACEVRUIStyle::Gold,2))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.035f,.13f,.09f),3,FLinearColor::White,2));
    return Style;
}
}
TSharedRef<SWidget> UACEVRMenu::Tab(const FString& Name,bool Active,TFunction<void()> Click)
{
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(Active?ACEVRUIStyle::Gold:FLinearColor::Transparent).Padding(2)
        [Button(Name,MoveTemp(Click))];
}
TSharedRef<SWidget> UACEVRMenu::Icon(uint32 Did,float Size)
{
    auto* Resources=Client->GetUIResourceResolver();
    auto* Texture=Resources?Resources->ResolveTexture(Did):nullptr;
    auto Brush=MakeShared<FSlateBrush>();Brush->ImageSize=FVector2D(Size,Size);
    Brush->DrawAs=Texture?ESlateBrushDrawType::Image:ESlateBrushDrawType::NoDrawType;
    Brush->SetResourceObject(Texture);IconBrushes.Add(Brush);if(Texture)IconTextures.Add(Texture);
    return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
        [SNew(SImage).Image_Lambda([Brush](){return &Brush.Get();}).Visibility(EVisibility::HitTestInvisible)];
}
TSharedRef<SWidget> UACEVRMenu::ItemIcon(const FACEWorldObject& Item,float Size)
{
    auto Layer=[this,Size](UTexture2D* Texture)
    {
        auto Brush=MakeShared<FSlateBrush>();Brush->ImageSize=FVector2D(Size,Size);
        Brush->DrawAs=Texture?ESlateBrushDrawType::Image:ESlateBrushDrawType::NoDrawType;
        Brush->SetResourceObject(Texture);IconBrushes.Add(Brush);if(Texture)IconTextures.Add(Texture);
        return SNew(SImage).Image_Lambda([Brush](){return &Brush.Get();}).Visibility(EVisibility::HitTestInvisible);
    };
    auto* Resources=Client->GetUIResourceResolver();
    const bool ForSale=Binder->VendorSellCart.ContainsByPredicate([&Item](const auto& O){return O.Value==Item.Guid;});
    return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
        [SNew(SOverlay)
            +SOverlay::Slot()[Layer(Resources?Resources->ResolveItemBackground(Item.ItemType,Item.IconUnderlayId):nullptr)]
            +SOverlay::Slot()[Layer(Resources?Resources->ResolveItemForeground(Item.IconId,Item.IconOverlayId,Item.UiEffects,ForSale):nullptr)]];
}
TSharedRef<SWidget> UACEVRMenu::ItemButton(const FACEWorldObject& Item,float Size)
{
    auto Content=SNew(SOverlay)
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[ItemIcon(Item)]
        +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
        [SNew(STextBlock).Text(FText::FromString(Item.StackSize>1?FString::FromInt(Item.StackSize):TEXT("")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold",18)).ColorAndOpacity(FLinearColor::White)
            .ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black).Visibility(EVisibility::HitTestInvisible)];
    return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
        [SNew(SButton).Tag(FName(*FString::Printf(TEXT("Item_%d"),Item.Guid))).ButtonStyle(&SlotStyle()).IsFocusable(false)
            .HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(2)
            .ButtonColorAndOpacity_Lambda([this,Id=Item.Guid](){return bItemDragging && DragItem==Id?FLinearColor(.25f,.25f,.25f):Id==Selected?FLinearColor(.2f,.65f,.4f):FLinearColor::White;})
            .OnHovered_Lambda([this,Name=Item.Name](){if(HoverLabel && !bItemDragging)HoverLabel->SetText(FText::FromString(Name));})
            .OnPressed_Lambda([this,Guid=Item.Guid](){BeginItemPointer(Guid);})
            .OnClicked_Lambda([this,Guid=Item.Guid](){if(!bSuppressItemClick)SelectItem(Guid);bSuppressItemClick=false;return FReply::Handled();})[Content]];
}
void UACEVRMenu::ClearItemSelection()
{
    CancelItemPointer();const int32 Previous=Selected;Selected=0;Confirmation.Reset();
    if(Binder)
    {
        // Opening a management page must not erase the world target.
        if(Client && Previous && Client->GetSelectedObject().Guid==Previous)Client->SelectObject(0);
        Binder->SelectedStackAmount=1;
    }
}
void UACEVRMenu::SelectPack(int32 Guid)
{
    if(Page=="Equipment")OpenPage("Inventory");
    ClearItemSelection();Pack=Guid;Binder->SelectedPackGuid=Guid;bDirty=true;
}
TSharedRef<SWidget> UACEVRMenu::PackGrid(int32 Container)
{
    const auto Items=Client->GetPackItems(Container);
    FACEWorldObject Bag;Client->GetWorldObject(Container,Bag);
    const int32 Capacity=FMath::Max(Items.Num(),Bag.ItemsCapacity>0?Bag.ItemsCapacity:Container==Client->GetPlayerGuid()?102:24);
    const int32 Columns=Capacity<=24?6:FMath::Max(12,FMath::CeilToInt(FMath::Sqrt(Capacity*1.3f)));
    auto Grid=SNew(SUniformGridPanel).SlotPadding(FMargin(2));
    for(int32 I=0;I<Capacity;++I)
    {
        const auto* Item=Items.IsValidIndex(I)?&Items[I]:nullptr;
        TSharedRef<SWidget> Cell=Item?ItemButton(*Item):StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(64).HeightOverride(64)
            [SNew(SButton).ButtonStyle(&SlotStyle()).IsFocusable(false).ContentPadding(2)]);
        Cell->SetTag(FName(*FString::Printf(TEXT("PackSlot_%d_%d"),Container,I)));
        Grid->AddSlot(I%Columns,I/Columns)[Cell];
        ItemDestinations.Add({Cell,Container,I,Item?Item->Guid:0});
    }
    // Both full and nearly empty bags retain the same panel footprint.
    return SNew(SBox).HeightOverride(540).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)[Grid]];
}
void UACEVRMenu::BeginItemPointer(int32 Guid)
{
    bSuppressItemClick=false;
    FACEWorldObject Item;
    if(UseSource || !Rig || !Client->GetWorldObject(Guid,Item) || !Client->IsOwnedInventoryItem(Item))return;
    auto* Pointer=Rig->FeedbackHand==0?Rig->LeftPointer.Get():Rig->RightPointer.Get();
    DragItem=Guid;DragAmount=Binder->GetSelectedItemAmount(Guid);DragStart=Pointer->Get2DHitLocation();bItemDragging=false;
}
void UACEVRMenu::UpdateItemPointer(bool OverMenu,FVector2D Pixel)
{
    if(!DragItem)return;
    if(!OverMenu || FVector2D::DistSquared(Pixel,DragStart)>100.f)bItemDragging=true;
    if(bItemDragging && HoverLabel)
    {
        FACEWorldObject Item;Client->GetWorldObject(DragItem,Item);
        HoverLabel->SetText(FText::FromString(FString::Printf(TEXT("Moving %d × %s — release on a slot, pack, or in the world"),DragAmount,*Item.Name)));
    }
}
void UACEVRMenu::CancelItemPointer(){DragItem=DragAmount=0;bItemDragging=false;}
void UACEVRMenu::DropItem(int32 Guid,int32 Amount)
{
    FACEWorldObject Item;
    if(!Client->GetWorldObject(Guid,Item) || !Client->IsOwnedInventoryItem(Item))return;
    if(!Item.CanDropToWorld()){Confirmation=TEXT("You cannot drop that item.");return;}
    if(!Rig || !Rig->TryDropInventoryItem(Guid,Amount))Binder->DropInventoryAmount(Guid,Amount);
}
void UACEVRMenu::FinishItemPointer(bool OverMenu,FVector2D Pixel,bool OverWorld)
{
    UpdateItemPointer(OverMenu,Pixel);
    const int32 Guid=DragItem,Amount=DragAmount;const bool Dragged=bItemDragging;
    CancelItemPointer();bSuppressItemClick=Dragged;
    if(!Guid || !Dragged)return;
    bDirty=true;
    if(OverMenu)
    {
        const FVector2D Absolute=GetCachedGeometry().LocalToAbsolute(Pixel);
        for(const auto& Destination:ItemDestinations)
        {
            const auto Widget=Destination.Widget.Pin();
            if(!Widget || !Widget->GetCachedGeometry().IsUnderLocation(Absolute))continue;
            if(Destination.Item==Guid)return;
            FACEWorldObject Item;if(!Client->GetWorldObject(Guid,Item))return;
            if(Destination.Sell){Binder->AddInventoryGuidToVendorSellCart(Guid,Amount);return;}
            if(Destination.EquipMask)
            {
                const int64 Mask=Item.ValidLocations&Destination.EquipMask;
                if(Mask)Client->SendGetAndWieldItem(Guid,Mask);
                else Confirmation=TEXT("That item cannot be equipped in this slot.");
                return;
            }
            if(!Destination.Container)return;
            if(!Binder->MergeInventoryAmount(Guid,Destination.Item,Amount))
            {
                const int32 Insert=Destination.Slot-((Amount>=FMath::Max(1,Item.StackSize) && Item.ContainerId==Destination.Container
                    && Item.PlacementPosition>=0 && Item.PlacementPosition<Destination.Slot && Destination.Item)?1:0);
                Binder->MoveInventoryAmount(Guid,Destination.Container,FMath::Max(0,Insert),Amount);
            }
            return;
        }
    }
    else if(OverWorld)DropItem(Guid,Amount);
}
void UACEVRMenu::BuildInventory()
{
    if(!Pack)Pack=Client->GetPlayerGuid();
    auto Layout=SNew(SHorizontalBox);
    if(Page=="Equipment")
    {
        struct FSlot {const TCHAR* Name; int64 Mask; int32 X,Y;};
        using namespace ACEEquipMask;
        const FSlot Slots[]={
            {TEXT("Head"),HeadWear,1,0},{TEXT("Neck"),NeckWear,2,0},
            {TEXT("Shoulders"),UpperArmArmor,0,1},{TEXT("Chest"),ChestArmor,1,1},{TEXT("Shirt"),ChestWear|UpperArmWear|LowerArmWear,2,1},
            {TEXT("Arms"),LowerArmArmor,0,2},{TEXT("Waist"),AbdomenArmor,1,2},{TEXT("Hands"),HandWear,2,2},
            {TEXT("Weapon"),MeleeWeapon|MissileWeapon|Held|TwoHanded,0,3},{TEXT("Legs"),UpperLegArmor,1,3},{TEXT("Shield"),Shield,2,3},
            {TEXT("Wrists"),WristWearLeft|WristWearRight,0,4},{TEXT("Lower legs"),LowerLegArmor,1,4},{TEXT("Rings"),FingerWearLeft|FingerWearRight,2,4},
            {TEXT("Ammo"),MissileAmmo,0,5},{TEXT("Feet"),FootWear,1,5},{TEXT("Trousers"),AbdomenWear|UpperLegWear|LowerLegWear,2,5},
            {TEXT("Cloak"),Cloak,0,6},{TEXT("Trinket"),TrinketOne,1,6},{TEXT("Sigils"),SigilOne|SigilTwo|SigilThree,2,6}};
        const auto Items=Client->GetEquippedItems();auto Grid=SNew(SUniformGridPanel).SlotPadding(FMargin(5));
        for(const auto& EquipmentSlot:Slots)
        {
            auto Row=SNew(SHorizontalBox);
            TArray<int64> Masks{EquipmentSlot.Mask};
            if(EquipmentSlot.Mask==(WristWearLeft|WristWearRight))Masks={WristWearLeft,WristWearRight};
            if(EquipmentSlot.Mask==(FingerWearLeft|FingerWearRight))Masks={FingerWearLeft,FingerWearRight};
            if(EquipmentSlot.Mask==(SigilOne|SigilTwo|SigilThree))Masks={SigilOne,SigilTwo,SigilThree};
            for(int64 Mask:Masks)
            {
                const auto* Item=Items.FindByPredicate([Mask](const auto& I){return (I.CurrentWieldedLocation&Mask)!=0;});
                TSharedRef<SWidget> Control=Item?ItemButton(*Item):StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(64).HeightOverride(64)[Button(TEXT("+"),[](){})]);
                Row->AddSlot().AutoWidth().Padding(2)[Control];ItemDestinations.Add({Control,0,0,Item?Item->Guid:0,Mask});
            }
            Grid->AddSlot(EquipmentSlot.X,EquipmentSlot.Y)[SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Row]
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Text(EquipmentSlot.Name)]];
        }
        Layout->AddSlot().FillWidth(1)[Grid];
    }
    else Layout->AddSlot().FillWidth(1)[PackGrid(Pack)];
    Layout->AddSlot().AutoWidth().Padding(8,0)[PackIcons()];
    Body->AddSlot().AutoHeight()[Layout];
    if(UseSource)Body->AddSlot().AutoHeight().Padding(4)[Button(TEXT("Choose an item or equipment slot — Cancel"),[this](){UseSource=0;Confirmation.Reset();bDirty=true;})];
    FACEWorldObject Item;if(!Selected || !Client->GetWorldObject(Selected,Item))return;
    Body->AddSlot().AutoHeight().Padding(4)[Text(Item.Name,28)];
    if(!Client->IsOwnedInventoryItem(Item))return;
    AddQuantity(Item.StackSize);
    auto Actions=SNew(SWrapBox).UseAllottedSize(true);
    for(const TCHAR* Name:{TEXT("Use / Equip"),TEXT("Inspect"),TEXT("Give"),TEXT("Drop")})
        Actions->AddSlot().Padding(2)[Button(Name,[this,Name](){Execute(Name);})];
    Body->AddSlot().AutoHeight().Padding(2)[Actions];
    Body->AddSlot().AutoHeight().Padding(2)[Button(UseSource?TEXT("Cancel use on item"):TEXT("Use selected on an item..."),[this](){UseSource=UseSource?0:Selected;Confirmation=UseSource?TEXT("Choose the receiving item or equipment slot."):TEXT("");bDirty=true;})];
    if(Client->GetOpenVendorGuid())Body->AddSlot().AutoHeight()[Button(TEXT("Add selected quantity to sell list"),[this](){Execute("Sell");})];
    if(Client->GetTradePartnerGuid())Body->AddSlot().AutoHeight()[Button(TEXT("Add selected quantity to trade"),[this](){Execute("Trade");})];
}
TSharedRef<SWidget> UACEVRMenu::PackIcons()
{
    if(!Pack)Pack=Client->GetPlayerGuid();
    auto Packs=SNew(SVerticalBox);
    auto AddPack=[&](int32 Guid,const FString& Name,const TSharedRef<SWidget>& Art)
    {
        auto Control=SNew(SBox).WidthOverride(68).HeightOverride(68)[SNew(SButton).ButtonStyle(&SlotStyle()).IsFocusable(false)
            .HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(2)
            .ButtonColorAndOpacity(Guid==Pack?FLinearColor(.25f,.7f,.4f):FLinearColor::White)
            .OnHovered_Lambda([this,Name](){if(HoverLabel)HoverLabel->SetText(FText::FromString(Name));})
            .OnClicked_Lambda([this,Guid](){SelectPack(Guid);return FReply::Handled();})[Art]];
        Packs->AddSlot().AutoHeight().Padding(2)[Control];ItemDestinations.Add({Control,Guid,0,0});
    };
    AddPack(Client->GetPlayerGuid(),TEXT("Main pack"),Icon(Pack==Client->GetPlayerGuid()?0x06004CF8:0x06004CF7));
    for(const auto& Bag:Client->GetPlayerPacks())AddPack(Bag.Guid,Bag.Name,ItemIcon(Bag));
    return Packs;
}

