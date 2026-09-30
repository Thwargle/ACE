#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ACETypes.h"
#include "ACEVRMenu.generated.h"

class UACEVRComponent;
class UACEClientSubsystem;
class UACEUIGameplayBinder;
class SVerticalBox;
class STextBlock;
class SScrollBox;
class UEditableTextBox;

/** Controller-sized gameplay views sharing the desktop interaction/session model. */
UCLASS()
class ACECLIENT_API UACEVRMenu : public UUserWidget
{
    GENERATED_BODY()
public:
    void InitializeMenu(UACEVRComponent* InRig, UACEClientSubsystem* InClient, UACEUIGameplayBinder* InBinder);
    void OpenPage(FName InPage);
    void RefreshIfDirty();
    void SelectItem(int32 Guid);
    void Execute(FName Action);
    void UpdateItemPointer(bool OverMenu, FVector2D Pixel);
    void FinishItemPointer(bool OverMenu, FVector2D Pixel, bool OverWorld);
    void CancelItemPointer();
    FName GetPage() const { return Page; }
    int32 GetRefreshCount() const { return RefreshCount; }
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
    virtual void NativeDestruct() override;
private:
    friend class FACEVRRigTest;
    friend class UACEVRComponent;
    UPROPERTY(Transient) TObjectPtr<UACEVRComponent> Rig;
    UPROPERTY(Transient) TObjectPtr<UACEClientSubsystem> Client;
    UPROPERTY(Transient) TObjectPtr<UACEUIGameplayBinder> Binder;
    UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> IconTextures;
    TArray<TSharedPtr<FSlateBrush>> IconBrushes;
    TSharedPtr<SVerticalBox> Body;
    TSharedPtr<STextBlock> HoverLabel;
    TSharedPtr<SScrollBox> ContentScroll;
    FName Page="Inventory";
    int32 Pack=0, Selected=0, Spell=0, PageIndex=0, RefreshCount=0;
    int32 MenuOwnerGuid=0;
    bool bDirty=true;
	uint64 InventoryRevision=0;
	int32 UseSource=0;
    int32 SpellSchool=0, SpellLevel=0;
    bool bShareFellowXP=true;
    bool bAssignShortcut=false;
    bool bSkillsPage=false, bVendorSelling=false;
    int32 FellowSelection=0;
    int32 DragItem=0, DragAmount=0;
    bool bItemDragging=false, bSuppressItemClick=false;
    FVector2D DragStart=FVector2D::ZeroVector;
    struct FItemDestination { TWeakPtr<SWidget> Widget; int32 Container=0, Slot=0, Item=0; int64 EquipMask=0; bool Sell=false; };
    TArray<FItemDestination> ItemDestinations;
    FString Search, FellowName;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> TextEntry;
    UFUNCTION() void EntryChanged(const FText& Text);
    TSharedRef<SWidget> Entry(const FString& Hint, const FString& Value);
    void AddQuantity(int32 Maximum);
    FString Confirmation;
    UFUNCTION() void ObjectChanged(const FACEWorldObject& Object);
    UFUNCTION() void ObjectDeleted(int32 Guid);
    UFUNCTION() void SelectionChanged(const FACESelectedObject& Selection);
    UFUNCTION() void VitalsChanged(const FACEPlayerVitals& Vitals);
    UFUNCTION() void Appraised(const FACEAppraisalInfo& Appraisal);
    UFUNCTION() void Changed();
    UFUNCTION() void ContextChanged(int32 EventOrGuid);
    TSharedRef<SWidget> Button(const FString& Text, TFunction<void()> Click, bool Enabled=true);
    TSharedRef<SWidget> ItemButton(const FACEWorldObject& Item, float Size=64.f);
    TSharedRef<SWidget> Icon(uint32 Did, float Size=48.f);
    TSharedRef<SWidget> ItemIcon(const FACEWorldObject& Item, float Size=48.f);
    TSharedRef<SWidget> PackGrid(int32 Container);
    TSharedRef<SWidget> PackIcons();
    TSharedRef<SWidget> Tab(const FString& Text, bool Active, TFunction<void()> Click);
    void ClearItemSelection();
    void SelectPack(int32 Guid);
    void BeginItemPointer(int32 Guid);
    void DropItem(int32 Guid,int32 Amount);
    void BuildInventory();
    void BuildSpells();
    void BuildFellowship();
    void BuildCharacter();
    void BuildInspection();
    void BuildVendor();
    void BuildLoot();
    void BuildTrade();
    void BuildMore();
    void BuildAllegiance();
    void BuildOptions();
    void BuildShortcuts();
};
