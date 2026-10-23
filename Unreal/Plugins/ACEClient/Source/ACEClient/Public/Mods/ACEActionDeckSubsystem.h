#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Framework/Commands/InputChord.h"
#include "Mods/ACELedger.h"
#include "Containers/Ticker.h"
#include "ACEActionDeckSubsystem.generated.h"

class SWidget;
class UACEClientSubsystem;
class UACEUIGameplayBinder;
class FACEActionDeckInput;
class SACEActionDeck;
class FJsonObject;
class SACEDeckMacroEditor;
class FACESession;

enum class EACEDeckAction : uint8 { Empty, Item, Spell, Melee, Missile, Self, Macro };

struct FACEDeckMacroStep
{
    FString Command;
    int32 WaitMilliseconds=0;
};

struct FACEDeckSlot
{
    EACEDeckAction Action=EACEDeckAction::Empty;
    FInputChord Key;
    bool CustomKey=false;
    int32 Guid=0, Spell=0, Height=2;
    FACESupplyEntry Item;
    FString MacroText;
    int32 MacroDelayMs=0;
};

struct FACEDeckAvailability
{
    int32 Guid=0;
    int64 Count=0;
    bool Ready=false;
    FString Reason;
};

struct FACEDeckEffectTimer
{
    double Remaining=0, Duration=0;
    float Fraction() const {return Duration>0?float(FMath::Clamp(Remaining/Duration,0.,1.)):0.f;}
    FString Label() const;
};

/** User-triggered shortcuts and timed chat macros; never relocates inventory. */
UCLASS()
class ACECLIENT_API UACEActionDeckSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
    friend class FACEActionDeckTest;
    friend class SACEActionDeck;
    friend class SACEDeckMacroEditor;
public:
    static constexpr int32 DefaultRows=3;
    static constexpr int32 Rows=4;
    static constexpr int32 Columns=12;
    static constexpr int32 SlotCount=Rows*Columns;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    TSharedRef<SWidget> MakePanel();
    void Refresh(bool Force=false);
    bool IsEnabled() const;
    bool IsOverrideActive() const;
    bool HidesNativeBar(int32 CombatMode);
    FACEDeckEffectTimer EffectTimer(int32 Index) const;
    /** Shared by event routing and native polling; never dispatches an action. */
    bool ReservesKey(const FInputChord& Key) const;
    bool HandleKey(const FInputChord& Key,bool Repeat);
    bool Activate(int32 Index);
    bool BindItem(int32 Index,int32 Guid);
    bool BindSpell(int32 Index,int32 Spell);
    void BindAttack(int32 Index,bool Missile,int32 Height);
    bool BindMacro(int32 Index,const FString& Name,uint32 Icon,const FString& Commands,int32 DelayMs,const FInputChord& Key);
    static bool ParseMacro(const FString& Text,TArray<FACEDeckMacroStep>& Steps,FString& Error);
    static constexpr int32 MaxMacroLength=8192;
    void OpenMacroEditor(int32 Index);
    void CloseMacroEditor();
    bool IsMacroEditorOpen() const {return MacroEditor.IsValid();}
    bool IsMacroRunning() const {return !MacroSteps.IsEmpty();}
    void StopMacro();
    FString MacroStatus() const;
    void ClearSlot(int32 Index);
    void SwapSlots(int32 From,int32 To);
    bool DropAt(FVector2D Absolute,int32 ItemGuid,int32 SpellId=0);
    bool SetKey(int32 Index,const FInputChord& Key);
    bool SetRowModifier(int32 Row,uint8 Modifiers);
    bool SetFourthRowEnabled(bool Enabled);
    bool IsFourthRowEnabled() const {return FourthRowEnabled;}
    int32 VisibleRows() const {return FourthRowEnabled?Rows:DefaultRows;}
    int32 VisibleSlots() const {return VisibleRows()*Columns;}
    /** Stable saved slot IDs: the optional row is stored last and drawn first. */
    int32 RowAtDisplay(int32 Row) const;
    int32 DisplayRow(int32 Index) const;
    FInputChord EffectiveKey(int32 Index) const;
    FString KeyLabel(int32 Index) const;
    FString SlotHint(int32 Index) const;
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
    bool HideSpellBar=false, HideCombatBar=false, ShowTargetTimers=false;
    FString Notice;
private:
    UPROPERTY() TObjectPtr<UACEClientSubsystem> Client;
    TSharedPtr<FACEActionDeckInput> Input;
    TWeakPtr<SACEActionDeck> Panel;
    TSharedPtr<SWidget> MacroEditor;
    TWeakObjectPtr<class UGameViewportClient> MacroEditorViewport;
    FTSTicker::FDelegateHandle MacroTicker;
    TArray<FACEDeckMacroStep> MacroSteps;
    TWeakPtr<FACESession> MacroSession;
    int32 MacroStep=0, MacroPlayer=0, MacroSpacingMs=0;
    FString MacroServer, RunningMacroName;
    double MacroDue=0;
    bool TickMacros(float Delta);
    void AdvanceMacro(double Now);
    TArray<FACEDeckSlot> Slots;
    TArray<FACEDeckAvailability> Available;
    uint8 RowModifiers[Rows]={1,2,0,4}; // Shift, Ctrl, Alt, Command bits.
    bool FourthRowEnabled=false;
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
