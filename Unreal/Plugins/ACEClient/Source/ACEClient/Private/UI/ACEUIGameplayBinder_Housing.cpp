#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACERetailTextBlock.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "VR/ACEVRComponent.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Styling/CoreStyle.h"

void UACEUIGameplayBinder::CloseHouseOffer()
{
	OpenHouseLord=0; HousePaymentSplit={}; bHousePaymentConfirm=false; SelectedHouseItem=0;
	if(Client && Client->GetSession())
	{Client->GetSession()->CloseHouseProfile(); SeenHouseRevision=Client->GetSession()->GetHouseProfileRevision();}
	for(auto* Array:{&HouseOfferSlots,&HouseOfferBackgrounds,&HouseOfferSelections})
		for(auto& B:*Array) if(B) B->SetVisibility(ESlateVisibility::Collapsed);
	for(auto& L:HouseOfferLabels) if(L) L->SetVisibility(ESlateVisibility::Collapsed);
	SyncEnvPanelMode();
}

void UACEUIGameplayBinder::RefreshHouseOffer()
{
	const auto S=Client?Client->GetSession():nullptr;
	if(!S || !Canvas || !Canvas->WidgetTree || !Manager) return;
	const auto& P=S->GetHouseProfile();
	if(SeenHouseRevision!=S->GetHouseProfileRevision())
	{
		SeenHouseRevision=S->GetHouseProfileRevision();
		if(!P.LordGuid) { if(OpenHouseLord) CloseHouseOffer(); return; }
		if(OpenVendorGuid) HideVendorPanel();
		if(OpenLootContainerGuid) HideExternalContainer(true);
		if(bTradeOpen) HideTradePanel(true);
		if(OpenSalvageToolGuid) HideSalvagePanel();
		OpenHouseLord=P.LordGuid; bHouseRentTab=P.OwnerGuid!=0; HouseItemOffset=0;
		HousePaymentSplit={}; bHousePaymentConfirm=false; SelectedHouseItem=0;
		if(PlayerController) PlayerController->EndUseApproach();
		SyncEnvPanelMode();
		if (PlayerController && PlayerController->GetPawn())
			if (auto* VR=PlayerController->GetPawn()->FindComponentByClass<UACEVRComponent>(); VR && VR->IsActive())
				VR->OpenRetailPanel(NAME_None);
	}
	if(!OpenHouseLord) return;
	if(S->GetState()!=EACESessionState::InWorld || OpenVendorGuid || OpenLootContainerGuid || bTradeOpen || OpenSalvageToolGuid
		|| IsBeyondContainerUseRadius(OpenHouseLord)) { CloseHouseOffer(); return; }
	if(HousePaymentSplit.SourceGuid)
	{
		const int32 NewGuid=FindCompletedInventoryStackSplit(HousePaymentSplit);
		if(NewGuid || FPlatformTime::Seconds()>=HousePaymentSplit.Deadline)
		{
			HousePaymentSplit={};
			if(NewGuid!=0 && NewGuid!=INDEX_NONE) S->StageHousePayment(NewGuid,bHouseRentTab);
			else PostInventorySystemMessage(TEXT("Cannot split the stack for dwelling costs"));
		}
	}
	Manager->SetElementVisibleByName(TEXT("HouseBuyPage"),!bHouseRentTab);
	Manager->SetElementVisibleByName(TEXT("HouseMaintenancePage"),bHouseRentTab);
	ApplyPanelTabChrome(TEXT("HouseBuyTab"),!bHouseRentTab);
	ApplyPanelTabChrome(TEXT("HouseMaintenanceTab"),bHouseRentTab);
	while(HouseOfferLabels.Num()<5) HouseOfferLabels.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
	const FString Prefix=bHouseRentTab?TEXT("HouseMaintenance"):TEXT("HouseBuy");
	const FLinearColor Ink(.92f,.92f,.88f);
	PlaceTextOnElement(HouseOfferLabels[0],TEXT("HouseBuyTab"),TEXT("Buy"),10,Ink,120010,true);
	PlaceTextOnElement(HouseOfferLabels[1],TEXT("HouseMaintenanceTab"),TEXT("Maintenance"),10,Ink,120010,true);
	PlaceTextOnElement(HouseOfferLabels[2],Manager->FindElementByName(Prefix+TEXT("_HouseOwner")),
		TEXT("Owner: ")+(P.OwnerName.IsEmpty()?TEXT("None"):P.OwnerName),10,Ink,120010);
	FString Required;
	for(const auto& Pay:bHouseRentTab?P.Rent:P.Buy)
	{
		const FString Name=Pay.Required==1?Pay.Name:Pay.PluralName.IsEmpty()?Pay.Name+TEXT("s"):Pay.PluralName;
		if(bHouseRentTab) Required+=FString::Printf(TEXT("%lld/%d %s\n"),
			FMath::Min<int64>(Pay.Required,Pay.Paid+S->GetHousePaymentAmount(Pay.Wcid,true)),Pay.Required,*Name);
		else Required+=FString::Printf(TEXT("%d %s\n"),Pay.Required,*Name);
	}
	if(bHouseRentTab && P.bMaintenanceFree) Required+=TEXT("Maintenance is free this period.\n");
	PlaceTextOnElement(HouseOfferLabels[3],Manager->FindElementByName(Prefix+TEXT("RequirementsText")),Required,10,Ink,120010);
	const auto Button=Manager->FindElementByName(Prefix+TEXT("_Button"));
	const bool Enabled=S->CanPayHouse(bHouseRentTab) && !HousePaymentSplit.SourceGuid;
	if(Button) { Button->bActivatable=Enabled; Button->bGhosted=!Enabled; }
	PlaceTextOnElement(HouseOfferLabels[4],Button,bHouseRentTab?TEXT("Pay Maintenance"):TEXT("Buy House"),10,
		Enabled?Ink:FLinearColor(.45f,.45f,.45f),120010,true);
	const auto List=Manager->FindElementByName(Prefix+TEXT("ItemsList"));
	if(!List) return;
	const auto& Items=S->GetHousePaymentItems(bHouseRentTab);
	const int32 Columns=FMath::Max(1,List->Width/32);
	while(HouseOfferLabels.Num()<5+Columns) HouseOfferLabels.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
	HouseItemOffset=FMath::Clamp(HouseItemOffset,0,FMath::Max(0,Items.Num()-Columns));
	const auto Origin=List->GetScreenOrigin();
	for(int32 I=0;I<Columns;++I)
	{
		FACEWorldObject Item; const bool Has=Items.IsValidIndex(I+HouseItemOffset) && S->GetWorldObject(Items[I+HouseItemOffset],Item);
		auto* Bg=EnsureIconBorder(HouseOfferBackgrounds,I); auto* Icon=EnsureIconBorder(HouseOfferSlots,I);
		auto* Selection=EnsureIconBorder(HouseOfferSelections,I);
		SetItemSlotBackground(Bg,Has?&Item:nullptr); SetItemSlotForeground(Icon,Has?&Item:nullptr);
		SetIconDid(Selection,0x06004D09); // retail inventory selection frame
		int32 Layer=120002;
		for(auto* B:{Bg,Icon,Selection})
		{
			if(B->GetParent()!=Canvas->GetElementLayer()) Canvas->GetElementLayer()->AddChild(B);
			auto* Slot=Cast<UCanvasPanelSlot>(B->Slot);
			Slot->SetPosition(FVector2D(Origin.X+I*32,Origin.Y)*FVector2D(Canvas->GetLastScaleX(),Canvas->GetLastScaleY()));
			Slot->SetSize(FVector2D(32*Canvas->GetLastScaleX(),32*Canvas->GetLastScaleY()));
			Canvas->SetOverlayOrder(B,Manager->FindElementByName(TEXT("RootGameplay_FloatyEnvPanel_Field")),Layer++);
			B->SetVisibility((B==Bg || (Has && (B==Icon || Item.Guid==SelectedHouseItem)))?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
		}
		if(Has) SetRetailTooltip(Icon,FText::FromString(Item.Name));
		auto* Count=HouseOfferLabels[5+I].Get();
		Count->SetText(FText::AsNumber(Item.StackSize));
		Count->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),7));
		Count->SetColorAndOpacity(Ink); Count->SetJustification(ETextJustify::Right);
		if(Count->GetParent()!=Canvas->GetElementLayer()) Canvas->GetElementLayer()->AddChild(Count);
		if(auto* Slot=Cast<UCanvasPanelSlot>(Count->Slot))
		{
			Slot->SetPosition(FVector2D(Origin.X+I*32,Origin.Y+20)*Canvas->GetLastScale2D());
			Slot->SetSize(FVector2D(32,12)*Canvas->GetLastScale2D());
			Canvas->SetOverlayOrder(Count,Manager->FindElementByName(TEXT("RootGameplay_FloatyEnvPanel_Field")),120005);
		}
		Count->SetVisibility(Has && Item.StackSize>1?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
	}
	for(auto* Array:{&HouseOfferSlots,&HouseOfferBackgrounds,&HouseOfferSelections})
		for(int32 I=Columns;I<Array->Num();++I) (*Array)[I]->SetVisibility(ESlateVisibility::Collapsed);
	for(int32 I=5+Columns;I<HouseOfferLabels.Num();++I) HouseOfferLabels[I]->SetVisibility(ESlateVisibility::Collapsed);
	const auto Bar=Manager->FindElementByName(Prefix+TEXT("_ItemListScroll"));
	if (Bar) Bar->bVisible=Items.Num()>Columns;
	SyncDatScrollbar(Bar, float(HouseItemOffset)/FMath::Max(1,Items.Num()-Columns),
		float(Columns)/FMath::Max(Columns,Items.Num()));
}

bool UACEUIGameplayBinder::HitHouseItemList(FVector2D Local) const
{
	return OpenHouseLord && Manager && Canvas && Canvas->IsElementExposedAt(Manager->FindElementByName(
		bHouseRentTab?TEXT("HouseMaintenanceItemsList"):TEXT("HouseBuyItemsList")),Local);
}

bool UACEUIGameplayBinder::TryHouseItemClick(FVector2D Local)
{
	if(!HitHouseItemList(Local) || !Client || !Client->GetSession()) return false;
	const auto List=Manager->FindElementByName(bHouseRentTab?TEXT("HouseMaintenanceItemsList"):TEXT("HouseBuyItemsList"));
	const auto& Items=Client->GetSession()->GetHousePaymentItems(bHouseRentTab);
	const int32 Index=HouseItemOffset+int32((Canvas->ViewportToLayout(Local).X-List->GetScreenOrigin().X)/32);
	if(!Items.IsValidIndex(Index)) return true;
	FACEWorldObject Item; if(!Client->GetWorldObject(Items[Index],Item)) return true;
	SelectedHouseItem=Item.Guid; SelectInventoryGuid(Item.Guid);
	InvDragGuid=Item.Guid; InvDragIconDid=Item.IconId; InvDragStartLocal=Local;
	InvDragSourcePack=0; InvDragSourceSlot=InvDragPackSlotIndex=INDEX_NONE;
	bInvDragPending=true; bInvDragActive=false; bInvDragFromHouse=true;
	bInvDoubleClickPending=LastInvClickGuid==Item.Guid && FPlatformTime::Seconds()-LastInvClickTime<InventoryDoubleClickSeconds();
	return true;
}

void UACEUIGameplayBinder::StageHouseItem(int32 Guid,int32 Amount)
{
	if(!Client || !Client->GetSession() || HousePaymentSplit.SourceGuid) return;
	FACEWorldObject Item;
	if(Client->GetWorldObject(Guid,Item) && Amount>0 && Amount<Item.StackSize)
	{
		// Validate first, then wait for a server-confirmed split before staging its new ID.
		if(!Client->GetSession()->StageHousePayment(Guid,bHouseRentTab)) return;
		Client->GetSession()->RemoveHousePayment(Guid,bHouseRentTab);
		TrackInventoryStackSplit(HousePaymentSplit,Item,Amount);
		Client->SendStackableSplitToContainer(Guid,Item.ContainerId,0,Amount);
	}
	else Client->GetSession()->StageHousePayment(Guid,bHouseRentTab);
}

bool UACEUIGameplayBinder::HandleHouseControl(const TSharedPtr<FACEUIElement>& E)
{
	if(!E || !OpenHouseLord || !Client || !Client->GetSession()) return false;
	const auto S=Client->GetSession(); const FString& Name=E->ElementName;
	if(Name==TEXT("CloseSlumlordButton")) {CloseHouseOffer();return true;}
	if(Name==TEXT("HouseBuyTab") || Name==TEXT("HouseMaintenanceTab"))
	{if(!HousePaymentSplit.SourceGuid && !bHousePaymentConfirm) {bHouseRentTab=Name==TEXT("HouseMaintenanceTab");HouseItemOffset=0;SelectedHouseItem=0;}return true;}
	if(Name==TEXT("ScrollBar_Left") || Name==TEXT("ScrollBar_Right"))
		for(auto P=E->Parent.Pin();P;P=P->Parent.Pin()) if(P->ElementName.StartsWith(TEXT("House")))
		{HouseItemOffset+=Name==TEXT("ScrollBar_Left")?-1:1;return true;}
	if(Name!=TEXT("HouseBuy_Button") && Name!=TEXT("HouseMaintenance_Button")) return false;
	if(!S->CanPayHouse(bHouseRentTab) || HousePaymentSplit.SourceGuid) return true;
	const auto& P=S->GetHouseProfile();
	if((!bHouseRentTab && P.Type!=4) || (bHouseRentTab && P.OwnerGuid!=Client->GetPlayerGuid()))
	{
		bHousePaymentConfirm=true;
		HouseConfirmPrompt=bHouseRentTab?TEXT("You are paying maintenance on someone else's house. Are you sure you wish to continue?"):
			TEXT("When you buy a landscape house like this one, you are restricted from buying another for 30 days. Are you sure you want to buy this house?");
		RefreshServerConfirmation();
	}
	else S->SendHousePayment(bHouseRentTab);
	return true;
}
