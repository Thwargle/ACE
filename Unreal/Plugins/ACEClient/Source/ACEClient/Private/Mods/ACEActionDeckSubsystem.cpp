#include "Mods/ACEActionDeckSubsystem.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACEInputBindings.h"
#include "ACEInventoryRules.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "UI/ACEUIGameplayBinder.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    const FKey DeckDefaultKeys[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,
        EKeys::Seven,EKeys::Eight,EKeys::Nine,EKeys::Zero,EKeys::Hyphen,EKeys::Equals};
    uint8 ModifierBits(const FInputChord& K){return (K.bShift?1:0)|(K.bCtrl?2:0)|(K.bAlt?4:0)|(K.bCmd?8:0);}
    FInputChord Chord(FKey Key,uint8 Bits){return FInputChord(Key,(Bits&1)!=0,(Bits&2)!=0,(Bits&4)!=0,(Bits&8)!=0);}
    double Number(const TSharedPtr<FJsonObject>& J,const TCHAR* K,double Default=0)
    {double V=Default;if(J)J->TryGetNumberField(K,V);return FMath::IsFinite(V)?V:Default;}
    int32 Integer(const TSharedPtr<FJsonObject>& J,const TCHAR* K,int32 Default=0)
    {const double V=Number(J,K,Default);return V>=MIN_int32&&V<=MAX_int32&&V==FMath::TruncToDouble(V)?int32(V):Default;}
    FString Text(const TSharedPtr<FJsonObject>& J,const TCHAR* K){FString V;if(J)J->TryGetStringField(K,V);return V;}
}

class FACEActionDeckInput final : public IInputProcessor
{
public:
    explicit FACEActionDeckInput(UACEActionDeckSubsystem* InHost):Host(InHost){}
    virtual void Tick(float,FSlateApplication&,TSharedRef<ICursor>) override {}
    virtual bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent& E) override
    {
        return Host.IsValid()&&Host->HandleKey(FInputChord(E.GetKey(),E.IsShiftDown(),E.IsControlDown(),
            E.IsAltDown(),E.IsCommandDown()),E.IsRepeat());
    }
private:
    TWeakObjectPtr<UACEActionDeckSubsystem> Host;
};

void UACEActionDeckSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UACEClientSubsystem>();
    Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    Reset();
    if(FSlateApplication::IsInitialized())
    {
        Input=MakeShared<FACEActionDeckInput>(this);
        // Runs before Slate widgets and UE's polled hotbars. The Windows keypad
        // translator may precede us; its translated event re-enters this path.
        FSlateApplication::Get().RegisterInputPreProcessor(Input,0);
    }
}
void UACEActionDeckSubsystem::Deinitialize()
{
    if(Input&&FSlateApplication::IsInitialized())FSlateApplication::Get().UnregisterInputPreProcessor(Input);
    Input.Reset();Panel.Reset();Client=nullptr;
    Super::Deinitialize();
}
void UACEActionDeckSubsystem::Reset()
{
    Slots.SetNum(SlotCount);Available.SetNum(SlotCount);
    for(auto& S:Slots)S=FACEDeckSlot();
    RowModifiers[0]=1;RowModifiers[1]=2;RowModifiers[2]=0;
    Override=false;ConfigOpen=false;CapturingKey=false;NextRefresh=0;Notice.Empty();
}
void UACEActionDeckSubsystem::EnsureCharacter()
{
    if(!Client||Client->GetSessionState()!=EACESessionState::InWorld)return;
    if(Player==Client->GetPlayerGuid()&&Server==Client->GetServerName())return;
    Player=Client->GetPlayerGuid();Server=Client->GetServerName();Reset();
    auto* Host=GetGameInstance()->GetSubsystem<UACEPluginSubsystem>();
    StatePath=Host->UserDirectory()/TEXT("ActionDeck")/(FMD5::HashAnsiString(*Server)
        +FString::Printf(TEXT(".%08X.json"),uint32(Player)));
    FString Source;TSharedPtr<FJsonObject> Json;
    if(IFileManager::Get().FileSize(*StatePath)<=128*1024&&FFileHelper::LoadFileToString(Source,*StatePath)
        &&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source),Json))Deserialize(Json);
}
bool UACEActionDeckSubsystem::Save()
{
    if(StatePath.IsEmpty())return false;
    FString Json;FJsonSerializer::Serialize(ExportState(),TJsonWriterFactory<>::Create(&Json));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(StatePath),true);
    const FString Temp=StatePath+TEXT(".tmp");
    if(!FFileHelper::SaveStringToFile(Json,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        ||!IFileManager::Get().Move(*StatePath,*Temp,true,true))
    {Notice=TEXT("Could not save Action Deck settings.");return false;}
    return true;
}
TSharedRef<FJsonObject> UACEActionDeckSubsystem::ExportState() const
{
    auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("version"),1);J->SetBoolField(TEXT("override"),Override);
    TArray<TSharedPtr<FJsonValue>> Mods,Entries;
    for(auto M:RowModifiers)Mods.Add(MakeShared<FJsonValueNumber>(M));
    J->SetArrayField(TEXT("rows"),Mods);
    for(const auto& S:Slots)
    {
        auto E=MakeShared<FJsonObject>();
        E->SetNumberField(TEXT("action"),uint8(S.Action));E->SetNumberField(TEXT("guid"),S.Guid);
        E->SetNumberField(TEXT("spell"),S.Spell);E->SetNumberField(TEXT("height"),S.Height);
        E->SetNumberField(TEXT("class"),S.Item.ClassId);E->SetStringField(TEXT("name"),S.Item.Name);
        E->SetNumberField(TEXT("icon"),S.Item.Icon);E->SetNumberField(TEXT("overlay"),S.Item.Overlay);
        E->SetNumberField(TEXT("underlay"),S.Item.Underlay);E->SetNumberField(TEXT("effects"),S.Item.Effects);
        E->SetNumberField(TEXT("item_type"),S.Item.ItemType);
        E->SetBoolField(TEXT("custom_key"),S.CustomKey);E->SetStringField(TEXT("key"),S.Key.Key.ToString());
        E->SetNumberField(TEXT("modifiers"),ModifierBits(S.Key));
        Entries.Add(MakeShared<FJsonValueObject>(E));
    }
    J->SetArrayField(TEXT("slots"),Entries);return J;
}
void UACEActionDeckSubsystem::Deserialize(const TSharedPtr<FJsonObject>& J)
{
    if(!J||Integer(J,TEXT("version"))!=1)return;
    J->TryGetBoolField(TEXT("override"),Override);
    const TArray<TSharedPtr<FJsonValue>>* Array=nullptr;
    if(J->TryGetArrayField(TEXT("rows"),Array)&&Array->Num()==Rows)
        for(int32 R=0;R<Rows;++R){double V=0;if((*Array)[R]->TryGetNumber(V)&&V>=0&&V<16&&V==FMath::TruncToDouble(V))RowModifiers[R]=uint8(V);}
    if(J->TryGetArrayField(TEXT("slots"),Array))
    {
        for(int32 I=0;I<FMath::Min(SlotCount,Array->Num());++I)
        {
            if((*Array)[I]->Type!=EJson::Object)continue;
            const auto E=(*Array)[I]->AsObject();auto& S=Slots[I];
            S.Action=EACEDeckAction(FMath::Clamp(Integer(E,TEXT("action")),0,4));
            S.Guid=Integer(E,TEXT("guid"));S.Spell=Integer(E,TEXT("spell"));S.Height=FMath::Clamp(Integer(E,TEXT("height"),2),1,3);
            S.Item.ClassId=Integer(E,TEXT("class"));S.Item.Name=Text(E,TEXT("name")).Left(256);
            S.Item.Icon=Integer(E,TEXT("icon"));S.Item.Overlay=Integer(E,TEXT("overlay"));S.Item.Underlay=Integer(E,TEXT("underlay"));
            S.Item.Effects=Integer(E,TEXT("effects"));S.Item.ItemType=Integer(E,TEXT("item_type"));
            E->TryGetBoolField(TEXT("custom_key"),S.CustomKey);
            S.Key=Chord(FKey(*Text(E,TEXT("key"))),uint8(FMath::Clamp(Integer(E,TEXT("modifiers")),0,15)));
            if(S.CustomKey&&!ValidKey(S.Key.Key))S.CustomKey=false;
        }
    }
    // A hand-edited file must not dispatch two slots from one key.
    TArray<FInputChord> Seen;
    for(int32 I=0;I<SlotCount;++I)
    {
        const auto K=EffectiveKey(I);
        if(Seen.Contains(K)){Slots[I].CustomKey=true;Slots[I].Key=FInputChord();}
        else Seen.Add(K);
    }
}
FInputChord UACEActionDeckSubsystem::EffectiveKey(int32 Index) const
{
    if(!Slots.IsValidIndex(Index))return FInputChord();
    return Slots[Index].CustomKey?Slots[Index].Key:Chord(DeckDefaultKeys[Index%Columns],RowModifiers[Index/Columns]);
}
bool UACEActionDeckSubsystem::ValidKey(const FKey& Key)
{
    return Key.IsValid()&&!Key.IsMouseButton()&&!Key.IsGamepadKey()&&!Key.IsModifierKey()
        &&!Key.IsAxis1D()&&!Key.IsAxis2D()&&!Key.IsAxis3D()
        &&Key!=EKeys::Escape
        &&Key!=EKeys::MouseScrollUp&&Key!=EKeys::MouseScrollDown;
}
int32 UACEActionDeckSubsystem::MatchKey(const FInputChord& Key) const
{
    if(!ValidKey(Key.Key))return INDEX_NONE;
    for(int32 I=0;I<Slots.Num();++I)if(EffectiveKey(I)==Key)return I;
    return INDEX_NONE;
}
bool UACEActionDeckSubsystem::SetKey(int32 Index,const FInputChord& Key)
{
    EnsureCharacter();
    if(!Slots.IsValidIndex(Index)||!ValidKey(Key.Key)){Notice=TEXT("Choose a keyboard key other than Escape.");return false;}
    const int32 Other=MatchKey(Key);
    if(Other!=INDEX_NONE&&Other!=Index){Notice=FString::Printf(TEXT("That key belongs to row %d, slot %d."),Other/Columns+1,Other%Columns+1);return false;}
    Slots[Index].Key=Key;Slots[Index].CustomKey=true;Notice.Empty();Save();return true;
}
bool UACEActionDeckSubsystem::SetRowModifier(int32 Row,uint8 Modifiers)
{
    EnsureCharacter();if(Row<0||Row>=Rows||Modifiers>15)return false;
    const uint8 Previous=RowModifiers[Row];RowModifiers[Row]=Modifiers;
    TArray<FInputChord> Seen;
    for(int32 I=0;I<SlotCount;++I)
    {
        const auto K=EffectiveKey(I);if(!K.Key.IsValid())continue;
        if(Seen.Contains(K)){RowModifiers[Row]=Previous;Notice=TEXT("That row modifier would duplicate another slot's key.");return false;}
        Seen.Add(K);
    }
    Notice.Empty();Save();return true;
}
FString UACEActionDeckSubsystem::KeyLabel(int32 Index) const
{
    const auto K=EffectiveKey(Index);if(!K.Key.IsValid())return TEXT("-");
    FString Name=K.Key.GetDisplayName().ToString();
    if(K.Key==EKeys::Hyphen)Name=TEXT("-");else if(K.Key==EKeys::Equals)Name=TEXT("=");
    return FString(K.bCtrl?TEXT("C+"):TEXT(""))+(K.bShift?TEXT("S+"):TEXT(""))
        +(K.bAlt?TEXT("A+"):TEXT(""))+(K.bCmd?TEXT("M+"):TEXT(""))+Name;
}
FString UACEActionDeckSubsystem::SlotName(int32 Index) const
{
    if(!Slots.IsValidIndex(Index))return FString();
    const auto& S=Slots[Index];
    if(S.Action==EACEDeckAction::Empty)return TEXT("Empty slot");
    if(S.Action==EACEDeckAction::Melee||S.Action==EACEDeckAction::Missile)
        return FString(S.Action==EACEDeckAction::Melee?TEXT("Melee"):TEXT("Missile"))
            +TEXT(" · ")+(S.Height==1?TEXT("Low"):S.Height==3?TEXT("High"):TEXT("Medium"));
    return S.Item.Name;
}
bool UACEActionDeckSubsystem::IsEnabled() const
{
    const auto* H=GetGameInstance()->GetSubsystem<UACEPluginSubsystem>();
    return H&&H->Plugins.ContainsByPredicate([](const auto& P){return P->Id==TEXT("actiondeck")&&P->Enabled;});
}
bool UACEActionDeckSubsystem::IsOverrideActive() const
{
    const auto* H=GetGameInstance()->GetSubsystem<UACEPluginSubsystem>();
    return Override&&IsEnabled()&&H->IsPluginWindowOpen(TEXT("actiondeck"));
}
UACEUIGameplayBinder* UACEActionDeckSubsystem::Binder() const
{
    auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    return PC?PC->GetActionDeckBinder():nullptr;
}
bool UACEActionDeckSubsystem::ReservesKey(const FInputChord& Key) const
{
    if(!IsOverrideActive()||MatchKey(Key)==INDEX_NONE)return false;
    if(!Client||Client->GetSessionState()!=EACESessionState::InWorld||ACEInputBindings::IsEditing()
        ||ACEInputBindings::IsTextEntryFocused())return false;
    auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    if(!PC||PC->IsVRActive()||PC->IsWorldTransitionActive()||PC->IsDesktopInterfaceHidden())return false;
    if(auto* B=Binder();B&&B->IsChatEntryFocused())return false;
    if(FSlateApplication::IsInitialized())
    {
        const auto& Slate=FSlateApplication::Get();
        if(Slate.GetActiveModalWindow()||Slate.AnyMenusVisible())return false;
    }
    return true;
}
bool UACEActionDeckSubsystem::HandleKey(const FInputChord& Key,bool Repeat)
{
    EnsureCharacter();
    if(!ReservesKey(Key))return false;
    // Let Slate's key selector see the event, but ReservesKey still prevents
    // a bubbled key-down from reaching the controller or native hotbar poll.
    if(CapturingKey)return false;
    if(FSlateApplication::IsInitialized())
    {
        const auto Focus=FSlateApplication::Get().GetKeyboardFocusedWidget();
        if(Focus&&Focus->GetTypeAsString().Contains(TEXT("InputKeySelector")))return false;
    }
    // Editing pauses deck actions without silently handing keys to native bars.
    if(!Repeat&&!ConfigOpen)Activate(MatchKey(Key));
    // Even empty/depleted slots reserve their binding: never fall through and
    // accidentally cast a different native hotbar spell.
    return true;
}
bool UACEActionDeckSubsystem::MatchesItem(const FACEDeckSlot& S,const FACEWorldObject& I)
{
    return S.Action==EACEDeckAction::Item
        &&(S.Item.ClassId?S.Item.ClassId==I.WeenieClassId:S.Item.Icon==I.IconId)
        &&S.Item.ItemType==I.ItemType&&S.Item.Name.Equals(I.Name,ESearchCase::IgnoreCase);
}
void UACEActionDeckSubsystem::Refresh(bool Force)
{
    EnsureCharacter();
    const double Now=FPlatformTime::Seconds();if(!Force&&Now<NextRefresh)return;NextRefresh=Now+.15;
    for(auto& A:Available)A=FACEDeckAvailability();
    const auto Session=Client?Client->GetSession():nullptr;
    if(!Session||Client->GetSessionState()!=EACESessionState::InWorld)return;
    const auto& Objects=Session->GetWorldObjects();const auto Known=Client->GetKnownSpells();
    const auto Offered=Client->GetTradeSelfItems();
    TArray<const FACEWorldObject*> Inventory;
    for(const auto& Pair:Objects)
        if(ACELedger::IsOwned(Pair.Value,Player,Objects)&&!Offered.Contains(Pair.Key))
            Inventory.Add(&Pair.Value);
    for(int32 I=0;I<SlotCount;++I)
    {
        auto& S=Slots[I];auto& A=Available[I];
        if(S.Action==EACEDeckAction::Empty){A.Reason=TEXT("Drag an inventory item or spell here.");continue;}
        if(S.Action==EACEDeckAction::Item)
        {
            const FACEWorldObject* Best=nullptr;
            for(const auto* Item:Inventory)if(MatchesItem(S,*Item))
            {
                const bool Kit=(Item->ObjectDescriptionFlags&0x10000)!=0;
                if((Kit||Item->MaxStructure>0)&&Item->Structure<=0)continue;
                A.Count+=FMath::Max(1,Item->StackSize);
                if(!Best||Item->Guid==S.Guid||(Best->Guid!=S.Guid&&uint32(Item->Guid)<uint32(Best->Guid)))Best=Item;
            }
            if(!Best){A.Reason=TEXT("None available in your inventory.");continue;}
            A.Guid=Best->Guid;S.Item.CopyAppearance(*Best);
            if(ACEInventoryRules::DetermineOwnedUse(*Best,Player)==EACEOwnedItemUse::None)
            {A.Reason=TEXT("This item cannot be used.");continue;}
        }
        else
        {
            const int32 Mode=S.Action==EACEDeckAction::Spell?int32(ACECombatMode::Magic)
                :S.Action==EACEDeckAction::Melee?int32(ACECombatMode::Melee):int32(ACECombatMode::Missile);
            if(S.Action==EACEDeckAction::Spell&&!Known.Contains(S.Spell)){A.Reason=TEXT("This spell is not known.");continue;}
            if(!Session->IsCombatModeConfirmed(Mode))
            {A.Reason=Mode==int32(ACECombatMode::Magic)?TEXT("Requires magic stance."):Mode==int32(ACECombatMode::Melee)?TEXT("Requires melee stance."):TEXT("Requires missile stance.");continue;}
        }
        if(Client->GetPlayerVitalsView().Health<=0){A.Reason=TEXT("Cannot act while dead.");continue;}
        if(Client->IsUseBusy()||Session->IsEquipmentSwapPending()){A.Reason=TEXT("Waiting for the current action.");continue;}
        A.Ready=true;
    }
}
bool UACEActionDeckSubsystem::BindItem(int32 Index,int32 Guid)
{
    EnsureCharacter();FACEWorldObject Item;
    if(!Slots.IsValidIndex(Index)||!Client||!Client->GetWorldObject(Guid,Item)||!Client->IsOwnedInventoryItem(Item))
    {Notice=TEXT("Drag an item from your own inventory.");return false;}
    auto& S=Slots[Index];S.Action=EACEDeckAction::Item;S.Guid=Guid;S.Spell=0;
    S.Item.ClassId=Item.WeenieClassId;S.Item.Name=Item.Name;S.Item.CopyAppearance(Item);
    Notice.Empty();Save();Refresh(true);return true;
}
bool UACEActionDeckSubsystem::BindSpell(int32 Index,int32 Spell)
{
    EnsureCharacter();
    if(!Slots.IsValidIndex(Index)||!Client||!Client->GetKnownSpells().Contains(Spell))return false;
    FString Name;uint32 Icon=0;
    auto* Dat=GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
    if(!Dat||!Dat->TryGetSpellInfo(uint32(Spell),Name,Icon))return false;
    auto& S=Slots[Index];S.Action=EACEDeckAction::Spell;S.Spell=Spell;S.Guid=0;
    S.Item=FACESupplyEntry();S.Item.Name=Name;S.Item.Icon=int32(Icon);
    Notice.Empty();Save();Refresh(true);return true;
}
void UACEActionDeckSubsystem::BindAttack(int32 Index,bool Missile,int32 Height)
{
    EnsureCharacter();if(!Slots.IsValidIndex(Index))return;
    auto& S=Slots[Index];S.Action=Missile?EACEDeckAction::Missile:EACEDeckAction::Melee;S.Height=FMath::Clamp(Height,1,3);
    S.Guid=S.Spell=0;S.Item=FACESupplyEntry();Notice.Empty();Save();Refresh(true);
}
void UACEActionDeckSubsystem::ClearSlot(int32 Index)
{
    EnsureCharacter();if(!Slots.IsValidIndex(Index))return;
    auto& S=Slots[Index];const auto K=S.Key;const bool Custom=S.CustomKey;S=FACEDeckSlot();S.Key=K;S.CustomKey=Custom;
    Save();Refresh(true);
}
void UACEActionDeckSubsystem::SwapSlots(int32 From,int32 To)
{
    if(!Slots.IsValidIndex(From)||!Slots.IsValidIndex(To)||From==To)return;
    const auto A=Slots[From],B=Slots[To];Swap(Slots[From],Slots[To]);
    Slots[From].Key=A.Key;Slots[From].CustomKey=A.CustomKey;Slots[To].Key=B.Key;Slots[To].CustomKey=B.CustomKey;
    Save();Refresh(true);
}
int32 UACEActionDeckSubsystem::HealingTarget(const FACEWorldObject& Kit,int32 Selected,int32 Self,const TMap<int32,FACEWorldObject>& Objects)
{
    auto Valid=[&](int32 Guid)
    {
        const auto* Target=Objects.Find(Guid);
        return Target&&(Guid==Self||Target->bIsPlayer)&&!Target->bDying
            &&ACEInventoryRules::IsTargetCompatible(Kit,*Target,Self,true,false,false);
    };
    if(Selected&&Valid(Selected))return Selected;
    return Valid(Self)?Self:0;
}
bool UACEActionDeckSubsystem::Activate(int32 Index)
{
    Refresh(true);
    if(!Slots.IsValidIndex(Index)||!Client||Client->GetSessionState()!=EACESessionState::InWorld||!IsEnabled())return false;
    auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    if(PC&&(PC->IsWorldTransitionActive()||PC->IsVRActive()))return false;
    if(!Available[Index].Ready){Notice=Available[Index].Reason;return false;}
    const auto& S=Slots[Index];auto* B=Binder();
    if(S.Action==EACEDeckAction::Spell)
    {
        if(!Client->SendCastSpell(S.Spell)){Notice=TEXT("Select a valid target for this spell.");return false;}
    }
    else if(S.Action==EACEDeckAction::Item)
    {
        FACEWorldObject Item;if(!Client->GetWorldObject(Available[Index].Guid,Item))return false;
        if(Item.ObjectDescriptionFlags&0x10000)
        {
            const int32 Target=HealingTarget(Item,Client->GetSelectedObject().Guid,Player,Client->GetSession()->GetWorldObjects());
            if(!Target){Notice=TEXT("No valid healing target.");return false;}
            if(B)B->CancelPendingUseWith();
            if(PC)PC->PrepareManualCombat();
            Client->SendUseWithTarget(Item.Guid,Target);
        }
        else if(B)B->UseActionDeckItem(Item.Guid);
        else {Notice=TEXT("The gameplay interface is not ready.");return false;}
    }
    else if(B)B->RequestActionDeckAttack(S.Height);
    else return false;
    Notice.Empty();return true;
}
