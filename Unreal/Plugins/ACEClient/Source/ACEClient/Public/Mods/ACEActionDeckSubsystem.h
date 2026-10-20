#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Framework/Commands/InputChord.h"
#include "Mods/ACELedger.h"
#include "ACEActionDeckSubsystem.generated.h"

class SWidget;
class UACEClientSubsystem;
class UACEUIGameplayBinder;
class FACEActionDeckInput;
class SACEActionDeck;
class FJsonObject;

enum class EACEDeckAction : uint8 { Empty, Item, Spell, Melee, Missile };

struct FACEDeckSlot
{
    EACEDeckAction Action=EACEDeckAction::Empty;
    FInputChord Key;
    bool CustomKey=false;
    int32 Guid=0, Spell=0, Height=2;
    FACESupplyEntry Item;
};

struct FACEDeckAvailability
{
    int32 Guid=0;
    int64 Count=0;
    bool Ready=false;
    FString Reason;
};

/** Manual shortcuts only. No automation or inventory relocation is performed by the deck. */
UCLASS()
class ACECLIENT_API UACEActionDeckSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
    friend class FACEActionDeckTest;
    friend class SACEActionDeck;
public:
    static constexpr int32 Rows=3;
    static constexpr int32 Columns=12;
    static constexpr int32 SlotCount=Rows*Columns;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    TSharedRef<SWidget> MakePanel();
    void Refresh(bool Force=false);
    bool IsEnabled() const;
    bool IsOverrideActive() const;
    /** Shared by event routing and native polling; never dispatches an action. */
    bool ReservesKey(const FInputChord& Key) const;
    bool HandleKey(const FInputChord& Key,bool Repeat);
    bool Activate(int32 Index);
    bool BindItem(int32 Index,int32 Guid);
    bool BindSpell(int32 Index,int32 Spell);
    void BindAttack(int32 Index,bool Missile,int32 Height);
    void ClearSlot(int32 Index);
    void SwapSlots(int32 From,int32 To);
    bool DropAt(FVector2D Absolute,int32 ItemGuid,int32 SpellId=0);
    bool SetKey(int32 Index,const FInputChord& Key);
    bool SetRowModifier(int32 Row,uint8 Modifiers);
    FInputChord EffectiveKey(int32 Index) const;
    FString KeyLabel(int32 Index) const;
    FString SlotName(int32 Index) const;
    int32 MatchKey(const FInputChord& Key) const;
    static bool ValidKey(const FKey& Key);
    static int32 HealingTarget(const FACEWorldObject& Kit,int32 Selected,int32 Player,
        const TMap<int32,FACEWorldObject>& Objects);
    static bool MatchesItem(const FACEDeckSlot& Slot,const FACEWorldObject& Item);
    const FACEDeckSlot& Slot(int32 Index) const {return Slots[Index];}
    const FACEDeckAvailability& Availability(int32 Index) const {return Available[Index];}
    bool IsConfigOpen() const {return ConfigOpen;}
    bool Override=false;
    FString Notice;
private:
    UPROPERTY() TObjectPtr<UACEClientSubsystem> Client;
    TSharedPtr<FACEActionDeckInput> Input;
    TWeakPtr<SACEActionDeck> Panel;
    TArray<FACEDeckSlot> Slots;
    TArray<FACEDeckAvailability> Available;
    uint8 RowModifiers[Rows]={1,2,0}; // Shift, Ctrl, Alt, Command bits.
    FString StatePath;
    int32 Player=0;
    FString Server;
    bool ConfigOpen=false, CapturingKey=false;
    double NextRefresh=0;
    void Reset();
    void EnsureCharacter();
    bool Save();
    TSharedRef<FJsonObject> ExportState() const;
    void Deserialize(const TSharedPtr<FJsonObject>& Json);
    UACEUIGameplayBinder* Binder() const;
};
