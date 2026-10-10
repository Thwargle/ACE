#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACEPlayerController.h"
#include "ACEInventoryRules.h"
#include "ACEEquipmentRules.h"
#include "ACEScrollLearning.h"
#include "ACEVendorPricing.h"
#include "ACEVTObjectClass.h"
#include "ACESession.h"
#include "ACEOpcodes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Mods/ACEPluginSight.h"
#include "Mods/ACEPluginRouteGround.h"
#include "Mods/ACEPluginCombat.h"
#include "UI/ACERetailObjectNames.h"
#include "UI/ACEUIResourceResolver.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
    double N(const TSharedPtr<FJsonObject>& O,const TCHAR* K,double Default=0){double V;return O&&O->TryGetNumberField(K,V)&&FMath::IsFinite(V)?V:Default;}
    FString S(const TSharedPtr<FJsonObject>& O,const TCHAR* K){FString V;if(O)O->TryGetStringField(K,V);return V;}
    FString CorpseOwnerName(FString Name)
    {
        // ACE strips administrator markers from Creature_Death's LongDesc to
        // match retail/VT corpse ownership. Character/object names retain them.
        Name.TrimStartAndEndInline();
        while(Name.RemoveFromStart(TEXT("+"))){}
        return Name;
    }
    FVector SightPoint(UACEDatSubsystem* Dat,const FACEWorldObject& Obj)
    {
        float Step=0,Height=Obj.IsCorpse()?.4f:1.8f,Radius=0;uint32 Anim=0;
        Dat->TryGetSetupPhysics(Obj.SetupId,Step,Height,Radius,Anim);
        return Obj.Position.ToUnrealLocation()+FVector(0,0,FMath::Max(15.f,Height*Obj.GetValidObjectScale()*60.f));
    }
    TSharedPtr<FJsonObject> Pos(const FACEPosition& P)
    {
        auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("cell"),uint32(P.CellId));
        O->SetNumberField(TEXT("x"),P.Location.X);O->SetNumberField(TEXT("y"),P.Location.Y);O->SetNumberField(TEXT("z"),P.Location.Z);
        return O;
    }
    bool LootItemAvailable(UACEClientSubsystem* Client,FACEWorldObject Item)
    {
        if(!Client->IsOwnedInventoryItem(Item)||(Item.ObjectDescriptionFlags&ACEObjectDescFlag::Retained))return false;
        for(int Depth=0;Depth<32;++Depth)
        {
            if(Item.WielderId||Item.ParentGuid||Item.CurrentWieldedLocation||Client->GetSession()->GetTradeSelfItems().Contains(Item.Guid))return false;
            if(Item.ContainerId==Client->GetPlayerGuid())return true;
            if(!Client->GetWorldObject(Item.ContainerId,Item))return false;
        }
        return false;
    }
    bool Wieldable(const FACEAppraisalInfo& A,const FACEPlayerVitals& V)
    {
        const int32 Keys[]={158,270,273,276};
        for(int32 Key:Keys)
        {
            const int32 Req=A.IntProperties.FindRef(Key),Id=A.IntProperties.FindRef(Key+1),Value=A.IntProperties.FindRef(Key+2);
            if(!Req)continue;
            if(Req==7){if(V.Level<Value)return false;continue;}
            if(Req==3||Req==4){if((Req==3?V.GetAttributeCurrent(Id):V.GetAttributeBase(Id))<Value)return false;continue;}
            if(Req==9){if(V.StatQualityInts.FindRef(Id)<Value)return false;continue;}
            if(Req==12){if(V.HeritageGroup!=Value)return false;continue;}
            if(Req==5||Req==6)
            {
                const int32 Actual=Id==1?(Req==5?V.MaxHealth:V.HealthStart+V.HealthRanks+V.Endurance/2)
                    :Id==3?(Req==5?V.MaxStamina:V.StaminaStart+V.StaminaRanks+V.Endurance)
                    :Id==5?(Req==5?V.MaxMana:V.ManaStart+V.ManaRanks+V.Self):0;
                if(Actual<Value)return false;continue;
            }
            if(Req==1||Req==2||Req==8)
            {
                bool Pass=false;for(const auto& Skill:V.Skills)if(Skill.SkillId==Id)
                    Pass=(Req==1?Skill.Current:Req==2?Skill.Base:Skill.AdvancementClass)>=Value;
                if(!Pass)return false;
            }
            else return false; // Do not guess about server-specific requirements.
        }
        return true;
    }
}
bool UACEPluginSubsystem::IsOwnedPluginItem(const FACEWorldObject& Item) const
{
    const auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    const int32 Player=C->GetPlayerGuid();
    // IsOwnedInventoryItem intentionally means unequipped pack contents (for
    // dragging/merging). Automation must also retain the item after wielding it.
    return Player && (C->IsOwnedInventoryItem(Item) || Item.WielderId==Player
        || (Item.CurrentWieldedLocation && Item.ParentGuid==Player));
}
void UACEPluginSubsystem::ObserveAppraisal(const FACEAppraisalInfo& Info)
{
    Appraisals.Add(Info.ObjectGuid,Info);++DataRevision;
    for(const auto& P:Plugins)if(P->WaitAction==TEXT("identify")&&P->WaitItem==Info.ObjectGuid)
    {
        P->NextAction=FMath::Min(P->NextAction,P->ActionSentAt+.25);
        P->NextDecision=0;P->WaitAction.Empty();
        if(P->Id==TEXT("ucm")&&P->Running&&Info.ObjectGuid==CombatTarget)
        {P->NextAction=0;ScheduleDecisionWake(FPlatformTime::Seconds());}
    }
}
void UACEPluginSubsystem::UnbindCombatEvents()
{
    if(auto S=ObservedSession.Pin())
    {
        S->OnObjectHealth.Remove(TargetHealthHandle);S->OnMotionUpdate.Remove(TargetMotionHandle);
        S->OnObjectDeleted.Remove(TargetDeletedHandle);S->OnObjectCreated.Remove(TargetCreatedHandle);
        S->OnVitalsUpdated.Remove(CombatVitalsHandle);
    }
}
void UACEPluginSubsystem::WakeCombatDecision()
{
    auto P=Find(TEXT("ucm"));if(!P||!P->Running)return;
    // Physical attack's retry throttle is not a server action lock. Release
    // only that delay; casts, inventory transfers and equipment still wait.
    if(PhysicalAttackOwner==P->Id&&P->WaitAction.IsEmpty()&&!PendingSpell
        &&P->NextAction==PhysicalAttackNextSendAt)P->NextAction=0;
    P->NextDecision=0;ScheduleDecisionWake(FPlatformTime::Seconds());
}
void UACEPluginSubsystem::ReleaseCombatTarget(int32 Guid)
{
    if(!Guid||CombatTarget!=Guid)return;
    WakeCombatDecision();CombatTarget=0;
    if(auto P=Find(TEXT("ucm"));P&&P->WaitAction==TEXT("identify")&&P->WaitItem==Guid)
    {P->WaitAction.Empty();P->NextAction=0;}
    if(PhysicalAttackTarget==Guid){PhysicalAttackTarget=0;PhysicalAttackOwner.Empty();PhysicalAttackNextSendAt=0;}
    // Do not clear PendingSpell: its untagged UseDone still belongs to that
    // cast. A death event can arrive while a projectile or recoil is active.
}
void UACEPluginSubsystem::ObserveTargetHealth(int32 Guid,float Fraction)
{
    if(!FMath::IsFinite(Fraction))return;
    FACEWorldObject Object;auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    if(Fraction>0){DefeatedTargets.Remove(Guid);return;}
    if(C->GetWorldObject(Guid,Object)&&Object.IsAttackable()&&!Object.bIsPlayer&&!Object.PetOwnerId)
    {DefeatedTargets.Add(Guid);ReleaseCombatTarget(Guid);}
}
void UACEPluginSubsystem::ObserveTargetMotion(int32 Guid,const FACEObjectMotionState& Motion)
{
    FACEWorldObject Object;
    if(Motion.IsDeathMotion()&&GetGameInstance()->GetSubsystem<UACEClientSubsystem>()->GetWorldObject(Guid,Object)&&Object.bDying)
        ReleaseCombatTarget(Guid);
}
void UACEPluginSubsystem::ObserveTargetDeleted(int32 Guid)
{DefeatedTargets.Remove(Guid);ReleaseCombatTarget(Guid);}
void UACEPluginSubsystem::ObserveTargetCreated(const FACEWorldObject& Object)
{DefeatedTargets.Remove(Object.Guid);}
void UACEPluginSubsystem::ObserveCombatVitals(const FACEPlayerVitals& Vitals)
{
    const FIntVector Current(Vitals.Health,Vitals.Stamina,Vitals.Mana);
    if(Current==LastCombatVitals)return;
    LastCombatVitals=Current;
    // VT's ChangeVital wakes the scheduler. Run recovery through the ordinary
    // policy before choosing another attack; never attack from this callback.
    if(CombatTarget||!PhysicalAttackOwner.IsEmpty())WakeCombatDecision();
}
void UACEPluginSubsystem::PrepareSpellConfirmation(const FString& SpellName)
{
    PendingSpellConfirmation.Empty();PendingSpellItemConfirmations.Empty();PendingSpellRecoveryConfirmations.Empty();
    auto* Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    const int32 Player=Client->GetPlayerGuid();
    const bool Self=PendingSpellTarget==Player;
    FACEWorldObject Recipient;
    const FString Prefix=TEXT("You cast ")+SpellName+TEXT(" on ");
    if(Self||Client->GetWorldObject(PendingSpellTarget,Recipient))
        PendingSpellConfirmation=Prefix+(Self?TEXT("yourself"):Recipient.Name);

    auto* Dat=GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
    uint32 School=0,Power=0,Category=0,Flags=0,TargetType=0;double Duration=0;bool Projectile=false;
    if(!Dat->TryGetPluginSpellInfo(PendingSpell,School,Power,Category,Flags,Duration))return;
    // VT d3 also recognizes boost results, which omit "on yourself".
    // Match the exact spell/recipient and a numeric restored amount, not merely
    // a general recovery phrase from unrelated server chat.
    if(School==2&&(Flags&4)&&Duration==0&&!PendingSpellConfirmation.IsEmpty())
        for(const FString Vital:{TEXT("health"),TEXT("stamina"),TEXT("mana")})
            PendingSpellRecoveryConfirmations.Emplace(
                Self?TEXT("You cast ")+SpellName+TEXT(" and restore "):TEXT("With ")+SpellName+TEXT(" you restore "),
                Self?TEXT(" points of your ")+Vital+TEXT("."):TEXT(" points of ")+Vital+TEXT(" to ")+Recipient.Name+TEXT("."));

    // ACE redirects a self-targeted bane/impenetrability to worn vestments.
    // Its result names each item, not "yourself". VT's d3/gj/dm likewise
    // accepts the item result and retains a timer for the requested buff.
    // Snapshot only compatible worn items: a matching spell on carried armor
    // or another player's equipment must not confirm this attempt.
    if(!Self||!Player||School!=3||!(Flags&4)||Duration<=0
        ||!Dat->TryGetRetailSpellTargeting(PendingSpell,Flags,TargetType,Projectile)
        ||!(TargetType&(ACEItemType::Armor|ACEItemType::Clothing)))return;
    if(auto Session=Client->GetSession())for(const auto& Pair:Session->GetWorldObjects())
    {
        const auto& Item=Pair.Value;
        const bool Worn=Item.WielderId==Player||(Item.CurrentWieldedLocation&&Item.ParentGuid==Player);
        if(Worn&&(uint32(Item.ItemType)&TargetType)&&!Item.Name.IsEmpty())
            PendingSpellItemConfirmations.AddUnique(Prefix+Item.Name);
    }
}
void UACEPluginSubsystem::ObserveUseDone(uint32 Error)
{
    // A confirmed buff may have already advanced the policy. Its late success
    // acknowledgment must never finish the next cast or an inventory action.
    if(ConfirmedBuffAckAt && !Error)
    {
        ConfirmedBuffAckAt=0;ConfirmedBuffOwner.Empty();
        if(BuffRetryAt)BuffRetryAt=FPlatformTime::Seconds();
        ScheduleDecisionWake(FPlatformTime::Seconds());return;
    }
    // A manually attempted use can fail while waiting for recoil. Its error
    // is not the successful buff's acknowledgment and must not release it.
    if(ConfirmedBuffAckAt&&!PendingSpell)return;
    // VT retries CastSpell while waiting for the spell words. Servers without
    // recoil queuing reject an early request as busy; retry that same request,
    // without consuming the buff family's failure limit or advancing Lua.
    if(Error==0x001D && PendingBuffResultDriven && !PendingSpellWords
        && FPlatformTime::Seconds()-PendingSpellAt<5)
    {
        BuffRetryAt=FPlatformTime::Seconds()+.2;
        ScheduleDecisionWake(BuffRetryAt);return;
    }
    CompletePendingAction(Error);
}
void UACEPluginSubsystem::CompleteConfirmedBuff()
{
    if(!PendingSpell||!PendingBuffResultDriven||!PendingSpellConfirmed||PendingSpellFizzled||ConfirmedBuffAckAt)return;
    // Words and result may arrive in one network batch. Preserve the single
    // movement edge that cancels recoil before letting the next buff start.
    if(!FastCastOwner.IsEmpty()&&FastCastStarted&&!FastCastMovementApplied)return;
    const FString Owner=PendingSpellOwner;
    CompletePendingAction(0);
    ConfirmedBuffAckAt=FPlatformTime::Seconds();ConfirmedBuffOwner=Owner;
}
void UACEPluginSubsystem::CompletePendingAction(uint32 Error)
{
    BuffRetryAt=0;PendingBuffResultDriven=false;PendingSpellWords=false;
    // UseDone releases the caster even while a launched projectile is still
    // travelling. Damage/enchantment confirmation is tracked separately.
    // Wake on the next frame like VT's SchedulePoke, rather than adding up to
    // half a second from the idle ticker. Fizzles/rejections retain backoff.
    if(PendingSpell)if(auto Owner=Find(PendingSpellOwner))
    {
        const bool Completed=!PendingSpellFizzled&&!Error;
        Owner->NextAction=FMath::Min(Owner->NextAction,Completed?FPlatformTime::Seconds():PendingSpellAt+.5);
        Owner->NextDecision=0;
        if(Owner->Running)ScheduleDecisionWake(Owner->NextAction);
    }
    PendingSpellOwner.Empty();
    FastCastOwner.Empty();
    FastCastStarted=false;
    if(Error&&PendingSpell)OffensiveCasts.RemoveAll([this](const auto& E){return E.Spell==PendingSpell&&E.Target==PendingSpellTarget;});
    if(Error&&PendingSpell)PendingDebuffCasts.RemoveAll([this](const auto& E){return N(E->AsObject(),TEXT("spell"))==PendingSpell&&N(E->AsObject(),TEXT("target"))==uint32(PendingSpellTarget);});
    LastActionError=Error;LastCompletedSpell=PendingSpell;LastSpellConfirmed=PendingSpellConfirmed&&!PendingSpellFizzled&&!Error;++ActionSerial;
    if(PendingManaRefresh&&!Error)
    {
        auto* Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
        if(auto Session=Client->GetSession())for(const auto& Pair:Session->GetWorldObjects())
            if(Pair.Value.WielderId==Client->GetPlayerGuid())
            {Appraisals.Remove(Pair.Key);AppraisalRequests.Remove(Pair.Key);}
        ++DataRevision;
    }
    PendingManaRefresh=false;
    // Stone contents and essence charges change after use as well as equipped mana.
    // Reassess them before another maintenance decision can reuse stale contents.
    for (int32 Id : PendingResourceRefresh)
    {
        Appraisals.Remove(Id);AppraisalRequests.Remove(Id);
        auto* Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();FACEWorldObject Item;
        if(Client->GetWorldObject(Id,Item)&&IsOwnedPluginItem(Item))
        {
            // At most the source and target: do not put confirmation behind a
            // full inventory's periodic appraisal scan.
            LastAppraisalRequest=FPlatformTime::Seconds();AppraisalRequests.Add(Id,LastAppraisalRequest);
            Client->RequestBackgroundAppraisal(Id);
        }
    }
    if (!PendingResourceRefresh.IsEmpty()) ++DataRevision;
    PendingResourceRefresh.Empty();
    if(PendingSpell && FPlatformTime::Seconds()-PendingSpellAt<30 && LastSpellConfirmed)
    {
        uint32 School,Power,Category,Flags;double Duration;
        auto* D=GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
        if(D->TryGetPluginSpellInfo(PendingSpell,School,Power,Category,Flags,Duration) && (Flags&4) && Duration>0)
        {
            // VT MySpell.EffectiveDurationS and ACE EnchantmentManager apply
            // +20% per IncreasedSpellDuration augmentation to cast buffs.
            // Otherwise item-only banes are refreshed before their real expiry.
            auto* Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
            Duration*=1.+.2*FMath::Max(0,Client->GetPlayerVitalsView().StatQualityInts.FindRef(238));
            // Item enchantments are not in the player's enchantment list. Only
            // record confirmed casts, never assume a request was successful.
            auto E=MakeShared<FJsonObject>();E->SetNumberField(TEXT("target"),uint32(PendingSpellTarget));
            E->SetNumberField(TEXT("category"),Category);E->SetNumberField(TEXT("power"),Power);
            E->SetNumberField(TEXT("expires"),FPlatformTime::Seconds()+Duration);
            E->SetNumberField(TEXT("duration"),Duration);
            ItemBuffs.RemoveAll([&](const auto& Old){return N(Old->AsObject(),TEXT("target"))==uint32(PendingSpellTarget)&&N(Old->AsObject(),TEXT("category"))==Category;});
            ItemBuffs.Add(MakeShared<FJsonValueObject>(E));
        }
    }
    PendingSpell=PendingSpellTarget=0;
    PendingSpellItemConfirmations.Empty();
    PendingSpellRecoveryConfirmations.Empty();
}
void UACEPluginSubsystem::RecordCombatOutcome(bool Hit)
{
    auto P=Find(PhysicalAttackOwner);auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    const double Now=FPlatformTime::Seconds();FACEWorldObject Target;
    if(!P||!P->Running||!PhysicalAttackTarget||Now>PhysicalAttackUntil
        ||C->GetSelectedObject().Guid!=PhysicalAttackTarget||!C->GetWorldObject(PhysicalAttackTarget,Target))return;
    AddCombatOutcome(PhysicalAttackTarget,Hit);
}
void UACEPluginSubsystem::AddCombatOutcome(int32 Target,bool Hit,bool Unhittable)
{
    auto E=MakeShared<FJsonObject>();E->SetNumberField(TEXT("serial"),++CombatOutcomeSerial);
    E->SetNumberField(TEXT("target"),uint32(Target));E->SetBoolField(TEXT("hit"),Hit);E->SetBoolField(TEXT("unhittable"),Unhittable);
    CombatOutcomes.Add(MakeShared<FJsonValueObject>(E));if(CombatOutcomes.Num()>128)CombatOutcomes.RemoveAt(0);
}
void UACEPluginSubsystem::ExpireOffensiveCasts(double Now)
{
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    for(int32 Index=OffensiveCasts.Num()-1;Index>=0;--Index)
    {
        const auto& E=OffensiveCasts[Index];auto P=Find(E.Owner);FACEWorldObject Target;
        if(!P||!P->Running||!C->GetWorldObject(E.Target,Target)||Target.Name!=E.TargetName)
        {OffensiveCasts.RemoveAt(Index);continue;}
        // VT gj uses four 907ms result ticks after spell words, not after the
        // cast request. Turning/equipping and a resisted spell are not misses.
        if(E.Started>0&&Now-E.Started>=3.628)
        {AddCombatOutcome(E.Target,false);OffensiveCasts.RemoveAt(Index);}
        else if(!E.Started&&Now-E.Sent>=5)OffensiveCasts.RemoveAt(Index);
    }
}
void UACEPluginSubsystem::ObserveOffensiveSpellChat(const FString& Text,const FString& Sender,int32 Type)
{
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();const double Now=FPlatformTime::Seconds();
    ExpireOffensiveCasts(Now);
    if(Type==ACEChatMessageType::Spellcasting)
    {
        FACEWorldObject Player;
        if(!C->GetWorldObject(C->GetPlayerGuid(),Player)||Player.Name.IsEmpty()||Sender!=Player.Name)return;
        for(int32 Index=OffensiveCasts.Num()-1;Index>=0;--Index)
            if(OffensiveCasts[Index].Spell==PendingSpell&&!OffensiveCasts[Index].Started)
            {OffensiveCasts[Index].Started=Now;break;}
        return;
    }
    if(!Sender.IsEmpty()||Type!=ACEChatMessageType::Magic)return;
    for(int32 Index=OffensiveCasts.Num()-1;Index>=0;--Index)
    {
        const auto E=OffensiveCasts[Index];if(!E.Started)continue;
        const FString Prefix=TEXT("You cast ")+E.SpellName+TEXT(" on ")+E.TargetName;
        const bool Damage=Text.Contains(TEXT("You "))&&Text.Contains(TEXT(" ")+E.TargetName+TEXT(" for "))
            &&Text.Contains(TEXT(" points with ")+E.SpellName+TEXT("."));
        const bool Hit=Damage||Text==Prefix||Text==Prefix+TEXT(".")||Text.StartsWith(Prefix+TEXT(", refreshing "))
            ||Text.StartsWith(Prefix+TEXT(", surpassing "))||Text.StartsWith(Prefix+TEXT(", but it is surpassed by "));
        const bool Resist=Text==E.TargetName+TEXT(" resists your spell")||Text==E.TargetName+TEXT(" resists your spell.");
        const bool Fizzle=Text==TEXT("Your spell fizzled.");
        const bool Invalid=Text==TEXT("Target is out of range")||Text==TEXT("Target is out of range.")
            ||Text==E.TargetName+TEXT(" is an invalid target.")
            ||(Text.StartsWith(TEXT("You fail to affect ")+E.TargetName+TEXT(" because "))
                &&(Text.EndsWith(TEXT(" is not a player killer!"))||Text.EndsWith(TEXT("you are not a player killer!"))
                    ||Text.Contains(TEXT(" because beneficial spells do not affect "))));
        if(Hit||Resist||Fizzle||Invalid)
        {
            if(Hit)AddCombatOutcome(E.Target,true);
            else if(Invalid&&C->GetSelectedObject().Guid==E.Target)AddCombatOutcome(E.Target,false,true);
            OffensiveCasts.RemoveAt(Index);break;
        }
    }
}
void UACEPluginSubsystem::ObserveCombatFeedback(const FString& Name,int32 Damage,bool Incoming,bool Critical)
{
    // The retail notification contains a name rather than a GUID. Associate it
    // only with our still-selected attack target; evades are not obstruction hits.
    FACEWorldObject Target;auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    if(!Incoming&&Damage>0&&C->GetWorldObject(PhysicalAttackTarget,Target)&&Name==Target.Name)RecordCombatOutcome(true);
    if(!Incoming&&Damage>0)for(int32 Index=OffensiveCasts.Num()-1;Index>=0;--Index)
    {
        const auto E=OffensiveCasts[Index];auto P=Find(E.Owner);
        if(P&&P->Running&&E.Started>0&&FPlatformTime::Seconds()-E.Started<3.628
            &&Name==E.TargetName&&C->GetSelectedObject().Guid==E.Target)
        {AddCombatOutcome(E.Target,true);OffensiveCasts.RemoveAt(Index);break;}
    }
}
void UACEPluginSubsystem::RefreshAutomationData()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ACEPluginAppraisalMaintenance);
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();auto Session=C->GetSession();
    if(!Session || C->GetSessionState()!=EACESessionState::InWorld)
    {
        CombatTarget=0;DefeatedTargets.Empty();LastCombatVitals=FIntVector(-1,-1,-1);
        ConfirmedBuffAckAt=BuffRetryAt=0;ConfirmedBuffOwner.Empty();DeferredBuffIntent.Reset();DeferredBuffOwner.Empty();PendingBuffResultDriven=false;
        DataPlayer=0;Appraisals.Empty();CorpseFirstSeen.Empty();AppraisalRequests.Empty();ItemBuffs.Empty();CachedInventory.Empty();ClearBuffRequests();
        PendingSpell=0;PendingSpellOwner.Empty();FastCastOwner.Empty();PendingManaRefresh=false;PendingResourceRefresh.Empty();Debuffs.Empty();PendingDebuffCasts.Empty();OffensiveCasts.Empty();InventoryRevision=MAX_uint64;C->BackgroundAppraisals.Empty();return;
    }
    if(DataPlayer!=C->GetPlayerGuid()||DataServer!=C->GetServerName()||ObservedSession.Pin()!=Session)
    {
        UnbindCombatEvents();CombatTarget=0;DefeatedTargets.Empty();LastCombatVitals=FIntVector(-1,-1,-1);
        ConfirmedBuffAckAt=BuffRetryAt=0;ConfirmedBuffOwner.Empty();DeferredBuffIntent.Reset();DeferredBuffOwner.Empty();PendingBuffResultDriven=false;
        if(auto Old=ObservedSession.Pin()){Old->OnUseDone.Remove(UseDoneHandle);Old->OnPlayerTell.Remove(TellHandle);Old->OnChatMessage.Remove(MetaChatHandle);Old->OnCombatFeedback.Remove(CombatFeedbackHandle);}
        ObservedSession=Session;UseDoneHandle=Session->OnUseDone.AddUObject(this,&UACEPluginSubsystem::ObserveUseDone);
        MetaChatHandle=Session->OnChatMessage.AddUObject(this,&UACEPluginSubsystem::ObserveMetaChat);MetaChatEvents.Empty();MetaPortalEvents.Empty();MetaPortalSpace=false;
        CombatFeedbackHandle=Session->OnCombatFeedback.AddUObject(this,&UACEPluginSubsystem::ObserveCombatFeedback);
        TargetHealthHandle=Session->OnObjectHealth.AddUObject(this,&UACEPluginSubsystem::ObserveTargetHealth);
        TargetMotionHandle=Session->OnMotionUpdate.AddUObject(this,&UACEPluginSubsystem::ObserveTargetMotion);
        TargetDeletedHandle=Session->OnObjectDeleted.AddUObject(this,&UACEPluginSubsystem::ObserveTargetDeleted);
        TargetCreatedHandle=Session->OnObjectCreated.AddUObject(this,&UACEPluginSubsystem::ObserveTargetCreated);
        CombatVitalsHandle=Session->OnVitalsUpdated.AddUObject(this,&UACEPluginSubsystem::ObserveCombatVitals);
        PhysicalAttackTarget=0;PhysicalAttackOwner.Empty();CombatOutcomes.Empty();OffensiveCasts.Empty();
        TellHandle=Session->OnPlayerTell.AddUObject(this,&UACEPluginSubsystem::ObservePlayerTell);ClearBuffRequests();
        DataPlayer=C->GetPlayerGuid();DataServer=C->GetServerName();NextAppraisalScan=0;Appraisals.Empty();CachedSpeciesNames.Reset();CorpseFirstSeen.Empty();AppraisalRequests.Empty();ItemBuffs.Empty();
        PendingSpell=0;PendingSpellOwner.Empty();FastCastOwner.Empty();PendingResourceRefresh.Empty();PendingManaRefresh=false;Debuffs.Empty();PendingDebuffCasts.Empty();InventoryRevision=MAX_uint64;++DataRevision;C->BackgroundAppraisals.Empty();
    }
    ExpireOffensiveCasts(FPlatformTime::Seconds());
    auto UCM=Find(TEXT("ucm"));if(!UCM||!UCM->Enabled)return;
    if(auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
    {
        const bool Portal=PC->IsWorldTransitionActive();
        if(Portal!=MetaPortalSpace){MetaPortalSpace=Portal;auto E=MakeShared<FJsonObject>();E->SetNumberField(TEXT("serial"),++MetaPortalSerial);E->SetBoolField(TEXT("entered"),Portal);MetaPortalEvents.Add(MakeShared<FJsonValueObject>(E));if(MetaPortalEvents.Num()>32)MetaPortalEvents.RemoveAt(0);}
    }
    const double Now=FPlatformTime::Seconds();if(Now<NextAppraisalScan||Now-LastAppraisalRequest<1)return;
    // The empty scan is work too. Previously a fully appraised inventory fell
    // through here every frame because only a sent request advanced the clock.
    NextAppraisalScan=Now+1;
    // Object IDs are session-local. Do not retain assessments for objects that
    // left awareness or inventory; refreshed mana/requirements must not stay stale.
    for(auto It=Appraisals.CreateIterator();It;++It)if(!Session->GetWorldObjects().Contains(It.Key()))It.RemoveCurrent();
    for(auto It=CorpseFirstSeen.CreateIterator();It;++It)if(!Session->GetWorldObjects().Contains(It.Key()))It.RemoveCurrent();
    for(auto It=AppraisalRequests.CreateIterator();It;++It)if(!Session->GetWorldObjects().Contains(It.Key()))
    {C->BackgroundAppraisals.Remove(It.Key());It.RemoveCurrent();}
    int32 Request=0;
    // Identify the current opponent first, then inventory, without opening an ID panel.
    auto Need=[&](int32 Id){const auto* A=Appraisals.Find(Id);return Now-AppraisalRequests.FindRef(Id)>(!A||!A->bSuccess?30:120);};
    const auto Sel=C->GetSelectedObject();
    if(UCM->Running && Sel.Guid && Need(Sel.Guid))Request=Sel.Guid;
    if(!Request)for(const auto& Pair:Session->GetWorldObjects())
    {
        const auto& O=Pair.Value;if(IsOwnedPluginItem(O)&&Need(O.Guid)
            && !(O.ItemType&(ACEItemType::SpellComponents|ACEItemType::Money|ACEItemType::Container))) {Request=O.Guid;break;}
    }
    if(Request){LastAppraisalRequest=Now;AppraisalRequests.Add(Request,Now);C->RequestBackgroundAppraisal(Request);}
}
void UACEPluginSubsystem::UpdateRouteVisibility(const FACEPosition& Position,
    const TArray<TSharedPtr<FJsonValue>>& Route, const FACEPluginSightQuery& Sight,
    const TSharedPtr<FJsonObject>& Out)
{
    auto Visible=MakeShared<FJsonObject>();
    struct FCandidate { double Distance; int32 Index; FVector Location; };
    TArray<FCandidate> Nearest;
    const FVector Origin=Position.ToUnrealLocation();
    const uint32 Cell=uint32(Position.CellId);
    uint32 Signature=GetTypeHash(Route.Num());
    for(int32 Index=0;Index<Route.Num();++Index)
    {
        const auto Point=Route[Index]->AsObject();FACEPosition Dest;
        bool Legacy=false,WalkFirst=false;Point->TryGetBoolField(TEXT("legacy"),Legacy);Point->TryGetBoolField(TEXT("walk_first"),WalkFirst);
        Dest.CellId=int32(uint32(N(Point,TEXT("cell"))));Dest.Location=FVector(N(Point,TEXT("x")),N(Point,TEXT("y")),N(Point,TEXT("z")));
        Signature=HashCombineFast(Signature,HashCombineFast(GetTypeHash(Dest.CellId),GetTypeHash(Dest.Location)));
        Signature=HashCombineFast(Signature,uint32(Legacy)|(uint32(WalkFirst)<<1));
        if(Legacy&&!WalkFirst)continue;
        Visible->SetBoolField(FString::FromInt(Index+1),false);
        const uint32 Target=uint32(Dest.CellId);
        const bool Connected=Legacy||(Cell>>16)==(Target>>16)||((Cell&0xffff)<0x100&&(Target&0xffff)<0x100);
        const double Distance=FVector::DistSquared(Origin,Dest.ToUnrealLocation());
        // Same movement validation as Execute: no cross-dungeon walk or
        // unbounded geometry scan. Combat acquisition is well inside 400m.
        if(Dest.IsValid()&&Connected&&Distance<=FMath::Square(40000.))
            Nearest.Add({Distance,Index,Dest.ToUnrealLocation()});
    }
    Nearest.Sort([](const FCandidate& A,const FCandidate& B){return A.Distance==B.Distance?A.Index<B.Index:A.Distance<B.Distance;});
    if(RouteVisibilityOffset>=Nearest.Num()||RouteVisibilitySignature!=Signature||RouteVisibilityCell!=Cell
        ||FVector::DistSquared(RouteVisibilityOrigin,Origin)>2500)
        RouteVisibilityOffset=0;
    if(RouteVisibilityOffset==0)RouteVisibilityOrigin=Origin;
    RouteVisibilitySignature=Signature;RouteVisibilityCell=Cell;
    FACEPluginRouteGround Ground(*GetWorld(),2048);
    const int32 ScanEnd=FMath::Min(RouteVisibilityOffset+16,Nearest.Num());
    bool Found=false;
    while(RouteVisibilityOffset<ScanEnd)
    {
        const auto& Candidate=Nearest[RouteVisibilityOffset];
        if(!Ground.CanQueryPath(Origin,Candidate.Location))break;
        ++RouteVisibilityOffset;
        if(Ground.Reachable(Origin,Candidate.Location,Sight))
        {
            Visible->SetBoolField(FString::FromInt(Candidate.Index+1),true);
            Found=true;break; // sorted: the closest reachable entry is sufficient
        }
    }
    const bool Pending=!Found&&RouteVisibilityOffset<Nearest.Num();
    Out->SetBoolField(TEXT("route_visible_pending"),Pending);
    Out->SetObjectField(TEXT("route_visible"),Visible);
    if(!Pending)RouteVisibilityOffset=0;
}

void UACEPluginSubsystem::ExtendSnapshot(const TSharedPtr<FJsonObject>& Out)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ACEPluginSnapshot);
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();auto Session=C->GetSession();if(!Session)return;
    const auto& V=C->GetPlayerVitalsView();const double Now=FPlatformTime::Seconds();
    const bool* ComponentsRequired=V.QualityBools.Find(68); // SpellComponentsRequired, absent means true.
    Out->SetBoolField(TEXT("components_required"),!ComponentsRequired||*ComponentsRequired);
    if(auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))Out->SetBoolField(TEXT("jumping"),PC->IsPluginJumpPending());
    Out->SetNumberField(TEXT("player"),uint32(C->GetPlayerGuid()));Out->SetNumberField(TEXT("action_serial"),ActionSerial);
    Out->SetNumberField(TEXT("action_error"),LastActionError);Out->SetNumberField(TEXT("container"),uint32(C->GetOpenExternalContainerGuid()));
    Out->SetNumberField(TEXT("teleport_sequence"),C->GetTeleportSeq());
    Out->SetNumberField(TEXT("last_spell"),LastCompletedSpell);
    Out->SetBoolField(TEXT("last_spell_confirmed"),LastSpellConfirmed);
    TArray<TSharedPtr<FJsonValue>> Confirmations;
    for(const auto& Dialog:Session->GetConfirmations()){auto D=MakeShared<FJsonObject>();D->SetNumberField(TEXT("type"),Dialog.Type);D->SetNumberField(TEXT("context"),Dialog.Context);Confirmations.Add(MakeShared<FJsonValueObject>(D));}
    Out->SetArrayField(TEXT("confirmations"),Confirmations);
    Out->SetNumberField(TEXT("chat_serial"),MetaChatSerial);Out->SetArrayField(TEXT("chat_events"),MetaChatEvents);
    Out->SetArrayField(TEXT("combat_events"),CombatOutcomes);
    Out->SetNumberField(TEXT("portal_serial"),MetaPortalSerial);Out->SetArrayField(TEXT("portal_events"),MetaPortalEvents);Out->SetBoolField(TEXT("portal_space"),MetaPortalSpace);
    Out->SetNumberField(TEXT("selected"),uint32(C->GetSelectedObject().Guid));Out->SetNumberField(TEXT("vendor"),uint32(C->GetOpenVendorGuid()));
    FACEWorldObject Vendor;
    TArray<TSharedPtr<FJsonValue>> Stock;
    TArray<TSharedPtr<FJsonValue>> Notes;
    int64 Pyreals=0;
    if(C->GetOpenVendorGuid()&&C->GetWorldObject(C->GetOpenVendorGuid(),Vendor))
    {
        Out->SetStringField(TEXT("vendor_name"),Vendor.Name);Out->SetNumberField(TEXT("vendor_wcid"),Vendor.WeenieClassId);
        for(const auto& Item:C->GetVendorMerchandise())
        {
            auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("id"),uint32(Item.Guid));J->SetNumberField(TEXT("wcid"),Item.WeenieClassId);
            J->SetStringField(TEXT("name"),ACERetailObjectNames::Name(Item));J->SetNumberField(TEXT("limit"),ACEInventoryRules::VendorPurchaseLimit(Item));
            J->SetNumberField(TEXT("unit_value"),FMath::Max(0,Item.Value)/FMath::Max(1,Item.StackSize));
            J->SetNumberField(TEXT("sell_rate"),Item.ItemType==ACEItemType::PromissoryNote?1.15:double(C->GetVendorSellRate()));
            Stock.Add(MakeShared<FJsonValueObject>(J));
        }
        // Use live ownership/retained/trade flags here, not the appraisal cache.
        for(const auto& Pair:Session->GetWorldObjects())
        {
            const auto& Item=Pair.Value;
            if(!C->IsOwnedInventoryItem(Item))continue;
            if(Item.WeenieClassId==273)Pyreals+=FMath::Max(1,Item.StackSize);
            if(Item.ItemType!=ACEItemType::PromissoryNote || Item.Value<=0
                || !LootItemAvailable(C,Item) || !Session->CanVendorBuyItem(Item))continue;
            const int32 UnitValue=Item.Value/FMath::Max(1,Item.StackSize);
            if(UnitValue<=0)continue;
            auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("id"),uint32(Item.Guid));
            J->SetNumberField(TEXT("wcid"),Item.WeenieClassId);J->SetStringField(TEXT("name"),ACERetailObjectNames::Name(Item));
            J->SetNumberField(TEXT("count"),FMath::Max(1,Item.StackSize));J->SetNumberField(TEXT("unit_value"),ACEVendorPricing::VendorBuyPayout(Item,1,C->GetVendorBuyRate()));
            Notes.Add(MakeShared<FJsonValueObject>(J));
        }
    }
    Out->SetArrayField(TEXT("vendor_stock"),Stock);
    Out->SetArrayField(TEXT("vendor_trade_notes"),Notes);
    Out->SetBoolField(TEXT("vendor_uses_pyreals"),Session->VendorUsesPyreals());
    Out->SetNumberField(TEXT("pyreals"),double(Pyreals));
    int32 Encumbrance=0;Session->TryGetPlayerEncumbrance(Encumbrance);
    Out->SetNumberField(TEXT("burden_percent"),100.*FMath::Max(0,Encumbrance)/(FMath::Max(1,V.GetBuffedStrength())*(150.+30.*FMath::Clamp(V.CarryingCapacityAugs,0,5))));Out->SetNumberField(TEXT("level"),V.Level);
    auto CharStrings=MakeShared<FJsonObject>();for(const auto& Pair:V.QualityStrings)CharStrings->SetStringField(FString::FromInt(Pair.Key),Pair.Value);FACEWorldObject PlayerObject;
    if(C->GetWorldObject(C->GetPlayerGuid(),PlayerObject) && !CharStrings->HasField(TEXT("1")))CharStrings->SetStringField(TEXT("1"),PlayerObject.Name);
    Out->SetObjectField(TEXT("char_strings"),CharStrings);
    auto CharInts=MakeShared<FJsonObject>();for(const auto& Pair:V.StatQualityInts)CharInts->SetNumberField(FString::FromInt(Pair.Key),Pair.Value);Out->SetObjectField(TEXT("char_ints"),CharInts);
    auto CharQuads=MakeShared<FJsonObject>();for(const auto& Pair:V.QualityInt64s)CharQuads->SetNumberField(FString::FromInt(Pair.Key),double(Pair.Value));Out->SetObjectField(TEXT("char_quads"),CharQuads);
    auto CharDoubles=MakeShared<FJsonObject>();for(const auto& Pair:V.QualityDoubles)CharDoubles->SetNumberField(FString::FromInt(Pair.Key),Pair.Value);Out->SetObjectField(TEXT("char_doubles"),CharDoubles);
    auto CharBools=MakeShared<FJsonObject>();for(const auto& Pair:V.QualityBools)CharBools->SetNumberField(FString::FromInt(Pair.Key),Pair.Value?1:0);Out->SetObjectField(TEXT("char_bools"),CharBools);
    Out->SetNumberField(TEXT("base_health"),V.HealthStart+V.HealthRanks+FMath::RoundToInt(V.Endurance/2.f));
    Out->SetNumberField(TEXT("base_stamina"),V.StaminaStart+V.StaminaRanks+V.Endurance);
    Out->SetNumberField(TEXT("base_mana"),V.ManaStart+V.ManaRanks+V.Self);
    int FreeSlots=FMath::Max(0,(PlayerObject.ItemsCapacity>0?PlayerObject.ItemsCapacity:102)-C->GetPackItems(C->GetPlayerGuid()).Num());
    Out->SetNumberField(TEXT("main_pack_slots"),FreeSlots);
    for(const auto& Pack:C->GetPlayerPacks())FreeSlots+=FMath::Max(0,Pack.ItemsCapacity-C->GetPackItems(Pack.Guid).Num());
    Out->SetNumberField(TEXT("pack_slots"),FreeSlots);Out->SetNumberField(TEXT("combat_mode"),V.CombatMode);
    Out->SetBoolField(TEXT("in_fellowship"),C->GetFellowship().bValid);
    Out->SetStringField(TEXT("world_name"),C->GetServerName());
    const auto Fellowship=C->GetFellowship();auto Fellow=MakeShared<FJsonObject>();
    Fellow->SetBoolField(TEXT("valid"),Fellowship.bValid);Fellow->SetStringField(TEXT("name"),Fellowship.Name);
    Fellow->SetNumberField(TEXT("leader"),uint32(Fellowship.LeaderGuid));Fellow->SetBoolField(TEXT("open"),Fellowship.bOpen);Fellow->SetBoolField(TEXT("locked"),Fellowship.bLocked);
    TArray<TSharedPtr<FJsonValue>> Members;
    for(const auto& M:Fellowship.Members)
    {
        auto Member=MakeShared<FJsonObject>();Member->SetNumberField(TEXT("id"),uint32(M.Guid));Member->SetStringField(TEXT("name"),M.Name);Member->SetBoolField(TEXT("share_loot"),M.bShareLoot);
        Member->SetNumberField(TEXT("health"),M.HealthCur);Member->SetNumberField(TEXT("max_health"),M.HealthMax);
        Member->SetNumberField(TEXT("stamina"),M.StaminaCur);Member->SetNumberField(TEXT("max_stamina"),M.StaminaMax);
        Member->SetNumberField(TEXT("mana"),M.ManaCur);Member->SetNumberField(TEXT("max_mana"),M.ManaMax);
        Member->SetNumberField(TEXT("vitals_age"),M.VitalsReceivedAt>0?FMath::Max(0.,Now-M.VitalsReceivedAt):1.e9);
        Members.Add(MakeShared<FJsonValueObject>(Member));
    }
    Fellow->SetArrayField(TEXT("members"),Members);Out->SetObjectField(TEXT("fellowship"),Fellow);
    TArray<TSharedPtr<FJsonValue>> Packs;for(const auto& Pack:C->GetPlayerPacks()){auto Bag=MakeShared<FJsonObject>();Bag->SetNumberField(TEXT("id"),uint32(Pack.Guid));Bag->SetNumberField(TEXT("free"),FMath::Max(0,Pack.ItemsCapacity-C->GetPackItems(Pack.Guid).Num()));Packs.Add(MakeShared<FJsonValueObject>(Bag));}Out->SetArrayField(TEXT("packs"),Packs);
    TArray<TSharedPtr<FJsonValue>> Skills,UsableSkills;
    auto* Dat=GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
    for(const auto& Skill:V.Skills)
    {
        if(Skill.AdvancementClass>=2)Skills.Add(MakeShared<FJsonValueNumber>(Skill.SkillId));
        uint32 MinLevel=2;Dat->TryGetSkillMinLevel(Skill.SkillId,MinLevel);
        if(Skill.AdvancementClass>=2 || (Skill.AdvancementClass>=1 && MinLevel==1))UsableSkills.Add(MakeShared<FJsonValueNumber>(Skill.SkillId));
    }
    auto SkillStats=MakeShared<FJsonObject>();
    for(const auto& Skill:V.Skills){auto Record=MakeShared<FJsonObject>();Record->SetNumberField(TEXT("base"),Skill.Base);Record->SetNumberField(TEXT("current"),Skill.Current);Record->SetNumberField(TEXT("training"),Skill.AdvancementClass);SkillStats->SetObjectField(FString::FromInt(Skill.SkillId),Record);}
    Out->SetObjectField(TEXT("skills"),SkillStats);
    Out->SetArrayField(TEXT("trained_skills"),Skills);
    Out->SetArrayField(TEXT("usable_skills"),UsableSkills);
    auto ItemJson=[&](const FACEWorldObject& Item)
    {
        auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("id"),uint32(Item.Guid));J->SetNumberField(TEXT("wcid"),Item.WeenieClassId);
        J->SetStringField(TEXT("name"),ACERetailObjectNames::Name(Item));J->SetNumberField(TEXT("type"),uint32(Item.ItemType));J->SetNumberField(TEXT("icon"),uint32(Item.IconId));
        J->SetNumberField(TEXT("object_class"),ACEVTObjectClass::Classify(Item));
        J->SetNumberField(TEXT("material"),Item.MaterialType);J->SetNumberField(TEXT("value"),Item.Value);J->SetNumberField(TEXT("count"),FMath::Max(1,Item.StackSize));
        J->SetNumberField(TEXT("max_stack"),Item.MaxStackSize);J->SetNumberField(TEXT("container"),uint32(Item.ContainerId));
        J->SetNumberField(TEXT("spell"),Item.SpellDID);
        if(ACEVTObjectClass::Classify(Item)==42&&Item.SpellDID)
        {
            uint32 School=0,Power=0,Category=0,Flags=0;double Duration=0;
            if(Dat->TryGetPluginSpellInfo(Item.SpellDID,School,Power,Category,Flags,Duration))
            {
                J->SetBoolField(TEXT("scroll_can_learn"),ACEScrollLearning::CanLearn(Power,School,V));
            }
        }
        J->SetNumberField(TEXT("slots"),Item.ValidLocations);
        J->SetBoolField(TEXT("auto_wield_left"),(Item.ObjectDescriptionFlags&ACEObjectDescFlag::WieldLeft)!=0);
        TArray<TSharedPtr<FJsonValue>> Palettes;for(const auto& Palette:Item.Appearance.SubPalettes)Palettes.Add(MakeShared<FJsonValueNumber>(uint32(Palette.SubPaletteId)));J->SetArrayField(TEXT("palettes"),Palettes);
        J->SetNumberField(TEXT("burden"),Item.Burden);
        J->SetNumberField(TEXT("structure"),Item.Structure);J->SetNumberField(TEXT("max_structure"),Item.MaxStructure);J->SetBoolField(TEXT("retained"),(Item.ObjectDescriptionFlags&ACEObjectDescFlag::Retained)!=0);
        J->SetNumberField(TEXT("icon_effects"),Item.UiEffects);
        if(Item.SalvageWorkmanship>=0)J->SetNumberField(TEXT("workmanship"),Item.SalvageWorkmanship);
        const bool Equipped=Item.WielderId==C->GetPlayerGuid() || (Item.CurrentWieldedLocation && Item.ParentGuid==C->GetPlayerGuid());
        J->SetBoolField(TEXT("equipped"),Equipped);J->SetNumberField(TEXT("ammo_type"),Item.AmmoType);
        J->SetBoolField(TEXT("resource_available"),LootItemAvailable(C,Item));
        J->SetNumberField(TEXT("equipped_slot"),uint32(Item.CurrentWieldedLocation));
        J->SetBoolField(TEXT("targeted"),ACEItemUseable::IsTargeted(Item.ItemUseable));J->SetBoolField(TEXT("usable"),ACEInventoryRules::IsUsable(Item.ItemUseable));
        J->SetBoolField(TEXT("healing_kit"),(Item.ObjectDescriptionFlags&0x10000)!=0); // Retail Healer descriptor.
        const auto* A=Appraisals.Find(Item.Guid);J->SetBoolField(TEXT("identified"),A&&A->bSuccess);
        auto Ints=MakeShared<FJsonObject>(),Floats=MakeShared<FJsonObject>(),Strings=MakeShared<FJsonObject>();
        Ints->SetNumberField(TEXT("19"),Item.Value);Ints->SetNumberField(TEXT("5"),Item.Burden);Ints->SetNumberField(TEXT("131"),Item.MaterialType);
        Strings->SetStringField(TEXT("1"),ACERetailObjectNames::Name(Item));
        // Public description aliases used by Decal; these are not server property IDs.
        const TPair<int64,int64> PublicInts[]={{218103808,Item.WeenieClassId},{218103809,Item.IconId},{218103810,uint32(Item.ContainerId)},{218103811,uint32(Item.Position.CellId)>>16},{218103812,Item.ItemsCapacity},{218103813,Item.ContainersCapacity},{218103814,FMath::Max(1,Item.StackSize)},{218103815,Item.MaxStackSize},{218103816,Item.SpellDID},{218103818,uint32(Item.WielderId)},{218103819,Item.CurrentWieldedLocation},{218103821,Item.ClothingPriority},{218103822,Item.ValidLocations},{218103823,Item.CombatUse},{218103824,Item.UiEffects},{218103825,Item.AmmoType},{218103826,Item.ItemUseable},{218103834,uint32(Item.ItemType)},{218103835,uint32(Item.ObjectDescriptionFlags)},{218103849,Item.IconOverlayId},{218103850,Item.IconUnderlayId}};
        for(const auto& Pair:PublicInts)Ints->SetNumberField(FString::Printf(TEXT("%lld"),Pair.Key),Pair.Value);
        if(Item.SalvageWorkmanship>=0)Floats->SetNumberField(TEXT("167772169"),Item.SalvageWorkmanship);
        if(A&&A->bSuccess)
        {
            for(const auto& Prop:A->IntProperties)Ints->SetNumberField(FString::FromInt(Prop.Key),Prop.Value);
            for(const auto& Prop:A->FloatProperties)Floats->SetNumberField(FString::FromInt(Prop.Key),Prop.Value);
            for(const auto& Prop:A->StringProperties)Strings->SetStringField(FString::FromInt(Prop.Key),Prop.Value.Left(1024));
            if(A->bHasWeaponProfile)
            {
                Ints->SetNumberField(TEXT("218103839"),A->WeaponTime);Ints->SetNumberField(TEXT("218103840"),A->WeaponSkill);
                Ints->SetNumberField(TEXT("218103841"),A->DamageType);Ints->SetNumberField(TEXT("218103842"),A->Damage);
                Floats->SetNumberField(TEXT("167772171"),A->DamageVariance);Floats->SetNumberField(TEXT("167772172"),A->WeaponOffense);
                Floats->SetNumberField(TEXT("167772173"),A->WeaponMaxVelocity);Floats->SetNumberField(TEXT("167772174"),A->WeaponDamageMod);
            }
            Ints->SetNumberField(TEXT("218103838"),A->SpellIds.Num());
            for(int Index=0;Index<A->ArmorResistances.Num()&&Index<7;++Index)Floats->SetNumberField(FString::FromInt(167772160+Index),A->ArmorResistances[Index]);
            TArray<TSharedPtr<FJsonValue>> SpellNames,SpellIds;
            for(int32 Id:A->SpellIds){SpellIds.Add(MakeShared<FJsonValueNumber>(Id));FString Name;uint32 Icon=0;if(Dat->TryGetSpellInfo(uint32(Id),Name,Icon))SpellNames.Add(MakeShared<FJsonValueString>(Name));}
            J->SetArrayField(TEXT("spell_names"),SpellNames);
            J->SetArrayField(TEXT("spell_ids"),SpellIds);
        }
        J->SetObjectField(TEXT("int_properties"),Ints);J->SetObjectField(TEXT("float_properties"),Floats);J->SetObjectField(TEXT("string_properties"),Strings);

        if(A&&A->bSuccess)
        {
            J->SetBoolField(TEXT("can_wield"),Equipped||Wieldable(*A,V));
            if(Item.SalvageWorkmanship<0)if(const auto* W=A->IntProperties.Find(105))
                J->SetNumberField(TEXT("workmanship"),double(*W)/FMath::Max(1,A->IntProperties.FindRef(170)));
            J->SetNumberField(TEXT("damage_type"),A->DamageType);J->SetNumberField(TEXT("damage"),A->Damage);
            J->SetNumberField(TEXT("damage_mod"),A->WeaponDamageMod);J->SetNumberField(TEXT("weapon_time"),A->WeaponTime);
            J->SetNumberField(TEXT("weapon_skill"),A->WeaponSkill);J->SetNumberField(TEXT("variance"),A->DamageVariance);
            J->SetNumberField(TEXT("weapon_type"),A->IntProperties.FindRef(353));J->SetNumberField(TEXT("attack_type"),A->IntProperties.FindRef(47));
            J->SetNumberField(TEXT("slayer"),A->IntProperties.FindRef(166));J->SetNumberField(TEXT("slayer_mod"),A->FloatProperties.FindRef(138));
            J->SetNumberField(TEXT("element_mod"),A->FloatProperties.FindRef(152));
            J->SetNumberField(TEXT("boost_vital"),A->IntProperties.FindRef(89));J->SetNumberField(TEXT("boost"),A->IntProperties.FindRef(90));
            if(const double* HealMod=A->FloatProperties.Find(100))J->SetNumberField(TEXT("heal_mod"),*HealMod);
            J->SetNumberField(TEXT("mana"),A->IntProperties.FindRef(107));J->SetNumberField(TEXT("max_mana"),A->IntProperties.FindRef(108));
            J->SetBoolField(TEXT("mana_empty"),!A->IntProperties.Contains(107));
            if(const auto* Rating=A->IntProperties.Find(307))J->SetNumberField(TEXT("damage_rating"),*Rating);
            J->SetNumberField(TEXT("armor"),A->IntProperties.FindRef(28));
            J->SetNumberField(TEXT("spell"),Item.SpellDID);
        }
        return MakeShared<FJsonValueObject>(J);
    };
    // Inventory records are immutable until an inventory/appraisal revision changes.
    uint32 Eligibility=GetTypeHash(V.Level);
    for(int32 Id=1;Id<=6;++Id){Eligibility=HashCombine(Eligibility,GetTypeHash(V.GetAttributeBase(Id)));Eligibility=HashCombine(Eligibility,GetTypeHash(V.GetAttributeCurrent(Id)));}
    for(const auto& Skill:V.Skills){Eligibility=HashCombine(Eligibility,GetTypeHash(Skill.Base));Eligibility=HashCombine(Eligibility,GetTypeHash(Skill.Current));Eligibility=HashCombine(Eligibility,GetTypeHash(Skill.AdvancementClass));}
    Eligibility=HashCombine(Eligibility,GetTypeHash(V.MaxHealth));Eligibility=HashCombine(Eligibility,GetTypeHash(V.MaxStamina));Eligibility=HashCombine(Eligibility,GetTypeHash(V.MaxMana));
    for(const auto& Pair:V.StatQualityInts)Eligibility=HashCombine(Eligibility,HashCombine(GetTypeHash(Pair.Key),GetTypeHash(Pair.Value)));
    if(InventoryRevision!=Session->GetInventoryDataRevision() || InventoryAppraisalRevision!=DataRevision || InventoryEligibility!=Eligibility)
    {
        CachedInventory.Empty();for(const auto& Pair:Session->GetWorldObjects())
            if(IsOwnedPluginItem(Pair.Value) && CachedInventory.Num()<1024)CachedInventory.Add(ItemJson(Pair.Value));
        InventoryRevision=Session->GetInventoryDataRevision();InventoryAppraisalRevision=DataRevision;InventoryEligibility=Eligibility;
    }
    Out->SetArrayField(TEXT("inventory"),CachedInventory);
    Out->SetNumberField(TEXT("inventory_revision"),double(InventoryRevision));
    Out->SetNumberField(TEXT("appraisal_revision"),double(InventoryAppraisalRevision));
    bool AcceptOthers=false;if(auto Plugin=Find(TEXT("ucm")))Plugin->Profile->TryGetBoolField(TEXT("buff_others"),AcceptOthers);
    if(!AcceptOthers)ClearBuffRequests();
    if(BuffRequests.Num())
    {
        auto Request=MakeShared<FJsonObject>(*BuffRequests[0]->AsObject());
        bool Started=false;Request->TryGetBoolField(TEXT("started"),Started);Request->SetBoolField(TEXT("started"),Started);
        const int32 Recipient=int32(uint32(N(Request,TEXT("player"))));FACEWorldObject Person;
        const bool Present=C->GetWorldObject(Recipient,Person)&&Person.bIsPlayer;
        Request->SetBoolField(TEXT("present"),Present);
        Request->SetNumberField(TEXT("distance"),Present?FVector::Distance(Person.Position.ToUnrealLocation(),C->GetPlayerPosition().ToUnrealLocation())/100:1.e9);
        TArray<TSharedPtr<FJsonValue>> Gear;
        if(Present)for(const auto& Pair:Session->GetWorldObjects())
            if(Pair.Value.WielderId==Recipient || (Pair.Value.CurrentWieldedLocation && Pair.Value.ParentGuid==Recipient))
                Gear.Add(ItemJson(Pair.Value));
        Request->SetArrayField(TEXT("equipment"),Gear);Out->SetObjectField(TEXT("buff_request"),Request);
    }
    else Out->RemoveField(TEXT("buff_request"));
    FACEPosition P=C->GetPlayerPosition();if(auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))PC->TryGetLocallyPredictedPosition(P);
    auto Position=Out->GetObjectField(TEXT("position"));const FVector Global=P.ToUnrealLocation()/100;
    Position->SetNumberField(TEXT("gx"),Global.X);Position->SetNumberField(TEXT("gy"),Global.Y);Position->SetNumberField(TEXT("gz"),Global.Z);
    FACEWorldObject Container;
    if(!CachedSpeciesNames||CachedSpeciesNames->Values.IsEmpty())
    {
        CachedSpeciesNames=MakeShared<FJsonObject>();
        auto* Resolver=NewObject<UACEUIResourceResolver>(this);Resolver->Initialize(Dat);
        for(const auto& Pair:Resolver->ResolveEnumStrings(0x10000005))CachedSpeciesNames->SetStringField(FString::FromInt(Pair.Key),Pair.Value);
    }
    Out->SetObjectField(TEXT("species_names"),CachedSpeciesNames);
    Out->SetBoolField(TEXT("container_is_corpse"),C->GetWorldObject(C->GetOpenExternalContainerGuid(),Container)&&Container.IsCorpse());
    TArray<TSharedPtr<FJsonValue>> Targets,Corpses,Contents,RouteObjects;bool OwnedPet=false;TSet<int32> ArmoredParents;
    for(const auto& Pair:Session->GetWorldObjects())
    {
        const auto& Obj=Pair.Value;
        // VT's hasshield predicate checks any attached Armor-class object,
        // rather than requiring appraisal or a known shield WCID.
        if(ACEVTObjectClass::Classify(Obj)==2)
        {if(Obj.WielderId)ArmoredParents.Add(Obj.WielderId);if(Obj.ParentGuid)ArmoredParents.Add(Obj.ParentGuid);}
        if(Obj.PetOwnerId==C->GetPlayerGuid()&&!Obj.IsCorpse())OwnedPet=true;
        if(Obj.IsCorpse()&&!CorpseFirstSeen.Contains(Obj.Guid))CorpseFirstSeen.Add(Obj.Guid,Now);
        if(C->GetOpenExternalContainerGuid() && Obj.ContainerId==C->GetOpenExternalContainerGuid()){Contents.Add(ItemJson(Obj));continue;}
        if(!Obj.IsSelectableWorldObject() || FVector::DistSquared(Obj.Position.ToUnrealLocation(),P.ToUnrealLocation())>FMath::Square(24000.))continue;
        const bool Monster=Obj.IsAttackable()&&!Obj.bIsPlayer&&!Obj.PetOwnerId&&(Obj.ItemType&ACEItemType::Creature);
        if(!Monster&&!Obj.IsCorpse())
        {
            if(Obj.bIsPlayer || ACEInventoryRules::IsUsable(Obj.ItemUseable))
            {auto R=Pos(Obj.Position);R->SetNumberField(TEXT("id"),uint32(Obj.Guid));R->SetNumberField(TEXT("wcid"),Obj.WeenieClassId);R->SetStringField(TEXT("name"),Obj.Name);R->SetNumberField(TEXT("object_class"),ACEVTObjectClass::Classify(Obj));R->SetBoolField(TEXT("door_open"),Obj.IsDoor()&&(Obj.InitialMotionCommand==int32(ACEMotion::On)||Obj.InitialMotionCommand==int32(ACEMotion::OnCommandU16)));R->SetNumberField(TEXT("distance"),FVector::Distance(Obj.Position.ToUnrealLocation(),P.ToUnrealLocation())/100);RouteObjects.Add(MakeShared<FJsonValueObject>(R));}
            continue;
        }
        auto J=Pos(Obj.Position);J->SetNumberField(TEXT("id"),uint32(Obj.Guid));J->SetStringField(TEXT("name"),Obj.Name);
        J->SetNumberField(TEXT("distance"),FVector::Distance(Obj.Position.ToUnrealLocation(),P.ToUnrealLocation())/100);
        J->SetNumberField(TEXT("object_class"),ACEVTObjectClass::Classify(Obj));J->SetNumberField(TEXT("wcid"),Obj.WeenieClassId);
        if(Monster)
        {
            if(DefeatedTargets.Contains(Obj.Guid))continue;
            const FVector Delta=Obj.Position.ToUnrealLocation()-P.ToUnrealLocation();
            J->SetNumberField(TEXT("attack_height"),ACEPluginCombat::AttackHeight(*Dat,Obj,P));
            J->SetNumberField(TEXT("angle"),FMath::Abs(FMath::FindDeltaAngleDegrees(N(Out,TEXT("heading")),double(Delta.Rotation().Yaw))));
            if(C->GetSelectedObject().Guid==Obj.Guid&&C->GetSelectedObject().bShowHealth&&C->GetSelectedObject().HealthFraction<=0)continue;
            if(C->GetSelectedObject().Guid==Obj.Guid&&C->GetSelectedObject().bShowHealth)J->SetNumberField(TEXT("health_fraction"),C->GetSelectedObject().HealthFraction);
            auto Resist=MakeShared<FJsonObject>();const auto* A=Appraisals.Find(Obj.Guid);
            J->SetBoolField(TEXT("identified"),A&&A->bSuccess);
            if(A&&A->bSuccess)
            {
                J->SetNumberField(TEXT("creature_type"),A->CreatureType);
                J->SetNumberField(TEXT("max_health"),A->MaxHealth);
                const int Props[]={64,65,66,67,68,69,70,166};
                for(int I=0;I<8;++I)if(const double* R=A->FloatProperties.Find(Props[I]))Resist->SetNumberField(FString::FromInt(I==7?1024:1<<I),*R);
            }
            J->SetObjectField(TEXT("resists"),Resist);Targets.Add(MakeShared<FJsonValueObject>(J));
        }
        else if(Obj.IsCorpse())
        {
            const auto* Info=Appraisals.Find(Obj.Guid);J->SetBoolField(TEXT("identified"),Info&&Info->bSuccess);
            J->SetBoolField(TEXT("ownership_available"),true);J->SetNumberField(TEXT("observed_age"),FMath::Max(0.,Now-CorpseFirstSeen.FindRef(Obj.Guid)));
            if(Info&&Info->bSuccess)
            {
                const FString Description=Info->StringProperties.FindRef(16);int32 End=INDEX_NONE;
                if(Description.StartsWith(TEXT("Killed by "))&&Description.FindChar('.',End)&&End>10)
                {
                    FString Killer=Description.Mid(10,End-10);
                    const FString NormalizedKiller=CorpseOwnerName(Killer);
                    auto Matches=[&](const FString& Name)
                    {
                        const FString Owner=CorpseOwnerName(Name);
                        return !Owner.IsEmpty()&&(NormalizedKiller.Equals(Owner,ESearchCase::IgnoreCase)||NormalizedKiller.StartsWith(Owner+TEXT("'s "),ESearchCase::IgnoreCase));
                    };
                    const FString PlayerName=S(CharStrings,TEXT("1"));
                    if(Matches(PlayerName)||Matches(PlayerObject.Name))Killer=PlayerName.IsEmpty()?PlayerObject.Name:PlayerName;
                    else for(const auto& M:Fellowship.Members)if(Matches(M.Name)){Killer=M.Name;break;}
                    J->SetStringField(TEXT("killer"),Killer);J->SetBoolField(TEXT("rare"),Description.Contains(TEXT("generated"),ESearchCase::IgnoreCase));
                }
                else {J->SetStringField(TEXT("killer"),TEXT(""));J->SetBoolField(TEXT("rare"),true);}
            }
            Corpses.Add(MakeShared<FJsonValueObject>(J));
        }
    }
    Out->SetBoolField(TEXT("owned_pet"),OwnedPet);
    auto Sort=[](const auto& A,const auto& B){return N(A->AsObject(),TEXT("distance"))<N(B->AsObject(),TEXT("distance"));};
    Targets.Sort(Sort);Corpses.Sort(Sort);RouteObjects.Sort(Sort);
    Contents.Sort([](const auto& A,const auto& B){return N(A->AsObject(),TEXT("id"))<N(B->AsObject(),TEXT("id"));});
    bool Ready=true;
    if(C->GetOpenExternalContainerGuid())
    {
        const auto* Expected=Session->GetContainerContents(C->GetOpenExternalContainerGuid());
        Ready=Expected!=nullptr;
        if(Expected)for(const auto& Ref:*Expected)
        {
            FACEWorldObject Item;
            if(!C->GetWorldObject(Ref.ItemGuid,Item)||Item.Name.IsEmpty()||!Item.WeenieClassId){Ready=false;break;}
        }
    }
    Out->SetBoolField(TEXT("contents_ready"),Ready);
    if(Targets.Num()>128)Targets.SetNum(128);if(Corpses.Num()>64)Corpses.SetNum(64);if(RouteObjects.Num()>256)RouteObjects.SetNum(256);
    for(const auto& Value:Targets)Value->AsObject()->SetBoolField(TEXT("has_shield"),ArmoredParents.Contains(int32(uint32(N(Value->AsObject(),TEXT("id"))))));
    if(UWorld* World=GetWorld())
    {
        auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
        FACEPluginSightQuery Sight(*World,PC?PC->GetPawn():nullptr);
        const FVector Eye=P.ToUnrealLocation()+FVector(0,0,120);
        Out->SetNumberField(TEXT("movement_blocked_serial"),MovementBlockedSerial);
        if(auto UCM=Find(TEXT("ucm"));UCM&&UCM->Running&&bRouteJoinRequested)
        {
            const TArray<TSharedPtr<FJsonValue>>* Route=nullptr;
            if((RuntimeRoute?RuntimeRoute:UCM->Profile)->TryGetArrayField(TEXT("route"),Route))
                UpdateRouteVisibility(P,*Route,Sight,Out);
        }
        for(const auto& Value:Members)
        {
            const auto J=Value->AsObject();FACEWorldObject Obj;
            if(C->bPluginFellowshipUpdates && N(J,TEXT("vitals_age"))<10 && N(J,TEXT("id"))!=uint32(C->GetPlayerGuid()) && C->GetWorldObject(int32(uint32(N(J,TEXT("id")))),Obj) && Obj.IsSelectableWorldObject())
            {
                J->SetNumberField(TEXT("distance"),FVector::Distance(P.ToUnrealLocation(),Obj.Position.ToUnrealLocation())/100.);
                J->SetBoolField(TEXT("line_of_sight"),Sight.Clear(Eye,SightPoint(Dat,Obj)));
            }
        }
        for(auto* List:{&Targets,&Corpses})for(const auto& Value:*List)
        {
            auto J=Value->AsObject();FACEWorldObject Obj;
            const bool Clear=C->GetWorldObject(int32(uint32(N(J,TEXT("id")))),Obj)&&Sight.Clear(Eye,SightPoint(Dat,Obj));
            J->SetBoolField(TEXT("line_of_sight"),Clear);
        }
    }
    Out->SetArrayField(TEXT("targets"),Targets);Out->SetArrayField(TEXT("corpses"),Corpses);Out->SetArrayField(TEXT("contents"),Contents);Out->SetArrayField(TEXT("route_objects"),RouteObjects);
    ItemBuffs.RemoveAll([Now](const auto& E){return N(E->AsObject(),TEXT("expires"))<=Now;});Out->SetArrayField(TEXT("item_buffs"),ItemBuffs);
    Debuffs.RemoveAll([&](const auto& E){return N(E->AsObject(),TEXT("expires"))<=Now||!Session->GetWorldObjects().Contains(int32(uint32(N(E->AsObject(),TEXT("target")))));});Out->SetArrayField(TEXT("debuffs"),Debuffs);
    PendingDebuffCasts.RemoveAll([Now](const auto& E){return Now-N(E->AsObject(),TEXT("sent"))>30;});
    Out->SetNumberField(TEXT("debuff_revision"),DebuffRevision);Out->SetNumberField(TEXT("debuff_target"),uint32(LastDebuffTarget));Out->SetNumberField(TEXT("debuff_spell"),LastDebuffSpell);
}
bool UACEPluginSubsystem::HasClearCombatSight(const FACEWorldObject& Target) const
{
    auto* World=GetWorld();if(!World)return false;
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    FACEPosition P=C->GetPlayerPosition();if(PC)PC->TryGetLocallyPredictedPosition(P);
    FACEPluginSightQuery Sight(*World,PC?PC->GetPawn():nullptr);
    return Sight.Clear(P.ToUnrealLocation()+FVector(0,0,120),SightPoint(GetGameInstance()->GetSubsystem<UACEDatSubsystem>(),Target));
}
void UACEPluginSubsystem::TrackActionWait(FACEClientPlugin& P,const FString& Action,int32 Item,int32 Value)
{
    P.WaitAction=Action;P.WaitItem=Item;P.WaitValue=Value;
    P.WaitSerial=ActionSerial;P.ActionSentAt=FPlatformTime::Seconds();
}
void UACEPluginSubsystem::RefreshActionWait(FACEClientPlugin& P)
{
    if(P.WaitAction.IsEmpty())return;
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();auto Session=C->GetSession();
    const auto* Item=Session?Session->GetWorldObjects().Find(P.WaitItem):nullptr;
    bool Complete=false;
    if(P.WaitAction==TEXT("loot"))
        Complete=Item&&(C->IsOwnedInventoryItem(*Item)||Item->StackSize<P.WaitValue);
    else if(P.WaitAction==TEXT("equip"))
        Complete=Item&&(P.WaitValue?Item->WielderId==C->GetPlayerGuid()&&(Item->CurrentWieldedLocation&P.WaitValue)!=0:
            C->IsOwnedInventoryItem(*Item)&&!Item->CurrentWieldedLocation);
    else if(P.WaitAction==TEXT("open_corpse"))Complete=C->GetOpenExternalContainerGuid()==P.WaitItem;
    else if(P.WaitAction==TEXT("use"))Complete=ActionSerial!=P.WaitSerial;
    if(Complete)
    {
        // Resume on the relevant server update, not the worst-case timeout.
        // Lua still confirms quantities/ownership, and the host retains busy gates.
        P.NextAction=FMath::Min(P.NextAction,P.ActionSentAt+.25);
        P.NextDecision=0;P.WaitAction.Empty();
    }
    else if(FPlatformTime::Seconds()>=P.NextAction)
    {
        // Keep ownership of an ongoing corpse approach beyond the initial
        // throttle so combat can interrupt it even on a long walk.
        auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
        if(P.WaitAction!=TEXT("open_corpse")||UseApproachOwner!=P.Id||!PC||!PC->IsUseApproachActive())P.WaitAction.Empty();
    }
}
bool UACEPluginSubsystem::ExecuteInventory(FACEClientPlugin& P,const TSharedPtr<FJsonObject>& I,const FString& Action)
{
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();const double Now=FPlatformTime::Seconds();
    const double Raw=N(I,TEXT("item"));const int32 Id=Raw>0&&Raw<=MAX_uint32&&FMath::FloorToDouble(Raw)==Raw?int32(uint32(Raw)):0;
    FACEWorldObject Item;const bool Found=C->GetWorldObject(Id,Item),Owned=Found&&C->IsOwnedInventoryItem(Item);
    if(Action==TEXT("split_note")||Action==TEXT("sell_note"))
    {
        auto Session=C->GetSession();const int32 Vendor=C->GetOpenVendorGuid();const double Amount=N(I,TEXT("count"));
        if(!Session||!Vendor||N(I,TEXT("vendor"))!=uint32(Vendor)||!Session->VendorUsesPyreals()
            || !Owned||!LootItemAvailable(C,Item)||Item.ItemType!=ACEItemType::PromissoryNote||Item.Value<=0
            || !Session->CanVendorBuyItem(Item)||Amount<1||Amount>FMath::Max(1,Item.StackSize)||FMath::FloorToDouble(Amount)!=Amount)
        {ReportActivityFailure(P,I,TEXT("Trade note or vendor changed; restocking stopped"));return true;}
        if(Action==TEXT("sell_note"))
        {
            if(Amount!=FMath::Max(1,Item.StackSize)){ReportActivityFailure(P,I,TEXT("Trade note split is not confirmed; nothing sold"));return true;}
            C->SendSellItems(Vendor,{{int32(Amount),Id}});return true;
        }
        if(Amount>=FMath::Max(1,Item.StackSize)){ReportActivityFailure(P,I,TEXT("Trade note stack changed; nothing split"));return true;}
        int32 Destination=0;
        auto TryPack=[&](int32 PackId)
        {
            FACEWorldObject Pack;if(Destination||!C->GetWorldObject(PackId,Pack))return;
            const int32 Capacity=Pack.ItemsCapacity>0?Pack.ItemsCapacity:(PackId==C->GetPlayerGuid()?102:0);
            if(Capacity>C->GetPackItems(PackId).Num())Destination=PackId;
        };
        TryPack(Item.ContainerId);TryPack(C->GetPlayerGuid());for(const auto& Pack:C->GetPlayerPacks())TryPack(Pack.Guid);
        if(!Destination){ReportActivityFailure(P,I,TEXT("Free an inventory slot to split trade notes for restocking"));return true;}
        C->SendStackableSplitToContainer(Id,Destination,0,int32(Amount));return true;
    }
    if(Action==TEXT("buy"))
    {
        const int32 Vendor=C->GetOpenVendorGuid();const double Amount=N(I,TEXT("count"));
        if(!Vendor||N(I,TEXT("vendor"))!=uint32(Vendor)||Amount<1||Amount>100000||FMath::FloorToDouble(Amount)!=Amount)
        {ReportActivityFailure(P,I,TEXT("Vendor changed or purchase quantity is invalid"));return true;}
        for(const auto& Stock:C->GetVendorMerchandise())if(Stock.Guid==Id)
        {
            if(Amount>ACEInventoryRules::VendorPurchaseLimit(Stock)){ReportActivityFailure(P,I,TEXT("Requested vendor stock is no longer available"));return true;}
            C->SendBuyItems(Vendor,{{int32(Amount),Id}});return true;
        }
        ReportActivityFailure(P,I,TEXT("Purchase item is not in the open vendor's stock"));return true;
    }
    if(Action==TEXT("combine_salvage"))
    {
        const double ToolId=N(I,TEXT("tool"));FACEWorldObject Tool;
        if(ToolId<=0||ToolId>MAX_uint32||FMath::FloorToDouble(ToolId)!=ToolId||!C->GetWorldObject(int32(uint32(ToolId)),Tool)||!LootItemAvailable(C,Tool)||ACEVTObjectClass::Classify(Tool)!=40){ReportActivityFailure(P,I,TEXT("Salvage combining requires an available Ust"));return true;}
        TArray<FString> Parts;S(I,TEXT("items")).ParseIntoArray(Parts,TEXT(","));TArray<int32> Items;int32 Material=0;
        if(Parts.Num()<2||Parts.Num()>64){ReportActivityFailure(P,I,TEXT("Invalid salvage combine list"));return true;}
        for(const FString& Part:Parts)
        {
            uint32 BagId=0;FACEWorldObject Bag;
            if(!LexTryParseString(BagId,*Part)||!BagId||Items.Contains(int32(BagId))||!C->GetWorldObject(int32(BagId),Bag)||!LootItemAvailable(C,Bag)||ACEVTObjectClass::Classify(Bag)!=39||Bag.Structure<=0||Bag.Structure>=100||Bag.MaterialType<=0||(Material&&Material!=Bag.MaterialType)){ReportActivityFailure(P,I,TEXT("Salvage bags changed or are unavailable; no bags combined"));return true;}
            Material=Bag.MaterialType;Items.Add(int32(BagId));
        }
        if(C->GetPlayerVitalsView().CombatMode!=1)C->SendChangeCombatMode(1);C->SendCreateTinkeringTool(Tool.Guid,Items);P.NextAction=Now+3;return true;
    }
    if(Action==TEXT("identify"))
    {
        if(Found)
        {
            TrackActionWait(P,Action,Id);
            // Share the request ledger with the background inventory scanner so
            // it cannot immediately duplicate a policy-requested appraisal.
            LastAppraisalRequest=Now;AppraisalRequests.Add(Id,Now);C->RequestBackgroundAppraisal(Id);
        }
        return true;
    }
    if(Action==TEXT("equip"))
    {
        bool Unequip=false,Offhand=false;I->TryGetBoolField(TEXT("unequip"),Unequip);I->TryGetBoolField(TEXT("offhand"),Offhand);
        if(Unequip)
        {
            if(Owned&&Item.CurrentWieldedLocation)
            {if(C->GetPlayerVitalsView().CombatMode!=1)C->SendChangeCombatMode(1);TrackActionWait(P,Action,Id,0);C->SendPutItemInContainer(Id,C->GetPlayerGuid(),0);P.NextAction=Now+2;}
            return true;
        }
        const auto* A=Appraisals.Find(Id);
        if(!Owned||!A||!A->bSuccess||!Wieldable(*A,C->GetPlayerVitalsView())){P.Status=TEXT("Equipment requirements unavailable or not met");return true;}
        // Ordinary equip honors the same automatic hand preference as retail
        // ItemHolder::DetermineUse. It is live object data, not an appraisal hint.
        if((Item.ObjectDescriptionFlags&ACEObjectDescFlag::WieldLeft)!=0)Offhand=true;
        int64 Mask=Item.ItemType&ACEItemType::Caster?0x1000000:Item.ItemType&ACEItemType::MissileWeapon?0x400000:Item.ItemType&ACEItemType::MeleeWeapon?0x100000:Item.ValidLocations;
        if(Item.ValidLocations&0x2000000)Mask=0x2000000;
        if(Item.ValidLocations&0x800000)Mask=0x800000;
        if(Offhand)
        {
            // A melee weapon's normal location is the primary hand; dual wield
            // explicitly uses ShieldLoc. Never put a two-handed weapon there.
            if(!ACEEquipmentRules::CanWieldInSlot(Item,ACEEquipMask::Shield))return true;
            Mask=0x200000;
        }
        Mask &= Item.ValidLocations;
        if(Offhand&&(Item.ItemType&ACEItemType::MeleeWeapon)&&!(Item.ValidLocations&0x2000000))Mask=0x200000;
        if(Mask){if(C->GetPlayerVitalsView().CombatMode!=1)C->SendChangeCombatMode(1);TrackActionWait(P,Action,Id,int32(Mask));C->SendGetAndWieldItem(Id,Mask);P.NextAction=Now+2;}
        return true;
    }
    if(Action==TEXT("salvage")||Action==TEXT("sell")||Action==TEXT("read"))
    {
        if(!Owned||!LootItemAvailable(C,Item))
        {ReportActivityFailure(P,I,TEXT("Loot action requires an unequipped, unretained owned item outside trade"));return true;}
        if(Action==TEXT("read"))
        {if(ACEVTObjectClass::Classify(Item)!=42){ReportActivityFailure(P,I,TEXT("Read rule requires a spell scroll"));return true;}C->SendUseItem(Id);P.NextAction=Now+3;return true;}
        if(Action==TEXT("sell"))
        {
            const int32 Vendor=C->GetOpenVendorGuid();if(!Vendor){ReportActivityFailure(P,I,TEXT("A vendor must be open to sell loot"));return true;}
            C->SendSellItems(Vendor,{TPair<int32,int32>(FMath::Max(1,Item.StackSize),Id)});P.NextAction=Now+2;return true;
        }
        const double ToolId=N(I,TEXT("tool"));FACEWorldObject Tool;
        const auto* SalvageAppraisal=Appraisals.Find(Id);
        if(!SalvageAppraisal||!SalvageAppraisal->bSuccess||SalvageAppraisal->IntProperties.FindRef(171)>0||!SalvageAppraisal->StringProperties.FindRef(8).IsEmpty())
        {ReportActivityFailure(P,I,TEXT("Automatic salvage requires appraisal and skips tinkered or inscribed items"));return true;}
        if(ToolId<=0||ToolId>MAX_uint32||FMath::FloorToDouble(ToolId)!=ToolId||!C->GetWorldObject(int32(uint32(ToolId)),Tool)||!LootItemAvailable(C,Tool)||ACEVTObjectClass::Classify(Tool)!=40||Item.Guid==Tool.Guid||Item.ItemsCapacity>0||(ACEVTObjectClass::Classify(Item)==39&&Item.Structure>=100)||Item.MaterialType<1||Item.MaterialType>77||TArray<int>{3,9,56,65,72}.Contains(Item.MaterialType))
        {ReportActivityFailure(P,I,TEXT("Item is not salvageable or no owned Ust is available"));return true;}
        if(C->GetPlayerVitalsView().CombatMode!=1)C->SendChangeCombatMode(1);
        C->SendCreateTinkeringTool(Tool.Guid,{Id});P.NextAction=Now+3;return true;
    }
    if(Action==TEXT("store_item"))
    {
        const double TargetId=N(I,TEXT("target"));FACEWorldObject Bag;
        if(Owned&&TargetId>0&&TargetId<=MAX_uint32&&FMath::FloorToDouble(TargetId)==TargetId&&C->GetWorldObject(int32(uint32(TargetId)),Bag)&&C->IsOwnedInventoryItem(Bag)&&Bag.ItemsCapacity>C->GetPackItems(Bag.Guid).Num()&&!(Item.ItemType&ACEItemType::Container)&&!(Item.ObjectDescriptionFlags&ACEObjectDescFlag::RequiresPackSlot))
        {C->SendPutItemInContainer(Id,Bag.Guid,0);P.NextAction=Now+1;}else ReportActivityFailure(P,I,TEXT("AutoCram destination is unavailable or full"));return true;
    }
    if(Action==TEXT("give"))
    {
        const double TargetId=N(I,TEXT("target"));FACEWorldObject Target;
        if(!Owned||TargetId<=0||TargetId>MAX_uint32||FMath::FloorToDouble(TargetId)!=TargetId||!C->GetWorldObject(int32(uint32(TargetId)),Target)||!Target.IsSelectableWorldObject()||!Target.IsGiveOrCreatureTarget())
        {ReportActivityFailure(P,I,TEXT("Meta give requires an owned item and a valid recipient"));return true;}
        const int Amount=int(FMath::Clamp(N(I,TEXT("amount"),1),1.,double(FMath::Max(1,Item.StackSize))));
        if(C->GetPlayerVitalsView().CombatMode!=1)C->SendChangeCombatMode(1);
        C->SendGiveObjectRequest(Target.Guid,Id,Amount);P.NextAction=Now+3;return true;
    }
    if(Action==TEXT("apply_item"))
    {
        const double TargetValue=N(I,TEXT("target"));FACEWorldObject Target;
        if(Owned&&TargetValue>0&&TargetValue<=MAX_uint32&&FMath::FloorToDouble(TargetValue)==TargetValue&&C->GetWorldObject(int32(uint32(TargetValue)),Target)
            &&(IsOwnedPluginItem(Target)||Target.IsSelectableWorldObject())&&ACEInventoryRules::IsUsable(Item.ItemUseable))
        {
            if (Item.ItemType & ACEItemType::ManaStone)
            {
                const auto* Stone=Appraisals.Find(Id); const auto* Source=Appraisals.Find(Target.Guid);
                if (!Stone || !Stone->bSuccess) { ReportActivityFailure(P,I,TEXT("Inspect the mana stone before using it")); return true; }
                if (!Stone->IntProperties.Contains(107) && (!LootItemAvailable(C,Target) || !Source || !Source->bSuccess
                    || Source->IntProperties.FindRef(107)<=0 || Source->IntProperties.FindRef(108)<=0
                    || Target.Guid==Id || (Target.ItemType&(ACEItemType::ManaStone|ACEItemType::Container))))
                { ReportActivityFailure(P,I,TEXT("Mana source must be an unequipped, unretained owned item with mana, outside trade")); return true; }
                PendingManaRefresh=true;
            }
            PendingResourceRefresh.Add(Id);PendingResourceRefresh.Add(Target.Guid);
            if(C->GetPlayerVitalsView().CombatMode!=1)C->SendChangeCombatMode(1);TrackActionWait(P,TEXT("use"));C->SendUseWithTarget(Id,Target.Guid);P.NextAction=Now+3;
        }
        else ReportActivityFailure(P,I,TEXT("Item or target is unavailable for use"));
        return true;
    }
    if(Action==TEXT("use_item"))
    {
        if(Owned&&ACEInventoryRules::IsUsable(Item.ItemUseable))
        {
            PendingManaRefresh=(Item.ItemType&ACEItemType::ManaStone)!=0;
            if(PendingManaRefresh)PendingResourceRefresh.Add(Id);
            // The server's summoning cooldown quality identifies the same
            // devices accepted by UCM's pet policy, including custom essences.
            // PetDevice.ActOnUse has no peace-mode requirement.
            const auto* Appraisal=Appraisals.Find(Id);
            const bool SummoningDevice=Appraisal&&Appraisal->bSuccess&&Appraisal->IntProperties.FindRef(280)==213;
            if(!SummoningDevice&&C->GetPlayerVitalsView().CombatMode!=1)C->SendChangeCombatMode(1);
            TrackActionWait(P,TEXT("use"));
            if(ACEItemUseable::IsTargeted(Item.ItemUseable))C->SendUseWithTarget(Id,C->GetPlayerGuid());else C->SendUseItem(Id);
            P.NextAction=Now+3;
        }
        return true;
    }
    if(Action==TEXT("use_world")||Action==TEXT("open_corpse"))
    {
        if(Found&&Item.IsSelectableWorldObject() && (Action!=TEXT("open_corpse")||Item.IsCorpse()))
        {
            if(auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
            {
                // Corpse Use/loot is valid while armed. Preserve the player's
                // stance for both the loot action and a recorded corpse Use.
                if(!Item.IsCorpse()&&C->GetPlayerVitalsView().CombatMode!=1)C->SendChangeCombatMode(1);
                PC->BeginUseApproach(Id,Item.UseRadius);UseApproachOwner=P.Id;
                TrackActionWait(P,Item.IsCorpse()?TEXT("open_corpse"):TEXT("use"),Id);
                P.NextAction=Now+2;
            }
        }
        return true;
    }
    if(Action==TEXT("close_corpse")){if(C->GetOpenExternalContainerGuid())C->SendNoLongerViewingContents(C->GetOpenExternalContainerGuid());return true;}
    if(Action==TEXT("loot"))
    {
        FACEWorldObject Container;
        if(Found && Item.ContainerId && Item.ContainerId==C->GetOpenExternalContainerGuid() && C->GetWorldObject(Item.ContainerId,Container) && Container.IsCorpse())
        {
            const int32 Amount=int32(FMath::Clamp(N(I,TEXT("amount"),FMath::Max(1,Item.StackSize)),1.,double(FMath::Max(1,Item.StackSize))));
            TrackActionWait(P,Action,Id,Item.StackSize);
            if(Amount<Item.StackSize)C->SendStackableSplitToContainer(Id,C->GetPlayerGuid(),0,Amount);
            else C->SendPutItemInContainer(Id,C->GetPlayerGuid(),0);
        }
        P.NextAction=Now+1;return true;
    }
    if(Action==TEXT("merge"))
    {
        FACEWorldObject Target;const double RawTarget=N(I,TEXT("target"));
        if(RawTarget<=0||RawTarget>MAX_uint32||FMath::FloorToDouble(RawTarget)!=RawTarget)return true;
        const int32 To=int32(uint32(RawTarget));
        if(Owned&&C->GetWorldObject(To,Target)&&C->IsOwnedInventoryItem(Target))
            if(int32 Amount=ACEInventoryRules::MergeAmount(Item,Target))C->SendStackableMerge(Id,To,Amount);
        return true;
    }
    if(Action==TEXT("jump"))
    {
        if(auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
            PC->QueuePluginJump(FMath::Clamp(float(N(I,TEXT("charge"),.5)),0.f,1.f),(I->HasTypedField<EJson::Boolean>(TEXT("current_heading"))&&I->GetBoolField(TEXT("current_heading"))&&PC->GetPawn())?PC->GetPawn()->GetActorRotation().Yaw:float(N(I,TEXT("heading"))),N(I,TEXT("forward"),1),N(I,TEXT("strafe")),I->HasTypedField<EJson::Boolean>(TEXT("walk_jump"))&&I->GetBoolField(TEXT("walk_jump")));
        P.NextAction=Now+2;return true;
    }
    return false;
}
void UACEPluginSubsystem::RecordRouteAction(const FString& Type,float Charge)
{
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();FACEWorldObject Obj;
    if((Type==TEXT("portal")||Type==TEXT("use")) && (!C->GetWorldObject(C->GetSelectedObject().Guid,Obj)||!Obj.IsSelectableWorldObject()))
    {Notice=TEXT("Select a portal, door or device before recording Use");return;}
    auto Existing=Find(TEXT("ucm"));const TArray<TSharedPtr<FJsonValue>>* Before=nullptr;
    const int32 Count=Existing&&Existing->Profile->TryGetArrayField(TEXT("route"),Before)?Before->Num():0;
    RecordWaypoint(TEXT("ucm"));auto P=Find(TEXT("ucm"));if(!P)return;
    const TArray<TSharedPtr<FJsonValue>>* Points=nullptr;if(!P->Profile->TryGetArrayField(TEXT("route"),Points)||Points->Num()!=Count+1)return;
    auto Point=Points->Last()->AsObject();Point->SetStringField(TEXT("kind"),Type);
    if(Type==TEXT("portal")||Type==TEXT("use"))
    {
        if(!C->GetWorldObject(C->GetSelectedObject().Guid,Obj)||!Obj.IsSelectableWorldObject()){Notice=TEXT("Select a portal, door or device before recording Use");return;}
        Point->SetNumberField(TEXT("wcid"),Obj.WeenieClassId);Point->SetStringField(TEXT("object_name"),Obj.Name);
    }
    Point->SetNumberField(TEXT("charge"),Charge);
    if(auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))Point->SetNumberField(TEXT("heading"),PC->GetPawn()?PC->GetPawn()->GetActorRotation().Yaw:0);
    SaveProfile(P->Id,P->ProfileName,ProfileJson(P->Id),false);
}
void UACEPluginSubsystem::DrawRoute()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ACEPluginRoute);
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();auto P=Find(TEXT("ucm"));bool Show=true;
    if(P)P->Profile->TryGetBoolField(TEXT("route_preview"),Show);
    if(!P||!P->Enabled||!Show||C->GetSessionState()!=EACESessionState::InWorld)
    {if(RouteMesh&&RouteMesh->GetNumSections())RouteMesh->ClearAllMeshSections();RouteSegments.Reset();RouteRebuiltAt=0;return;}
    const TArray<TSharedPtr<FJsonValue>>* Points=nullptr;
    if(!(RuntimeRoute?RuntimeRoute:P->Profile)->TryGetArrayField(TEXT("route"),Points)||Points->IsEmpty())
    {if(RouteMesh&&RouteMesh->GetNumSections())RouteMesh->ClearAllMeshSections();RouteSegments.Reset();RouteRebuiltAt=0;return;}
    // Edits explicitly invalidate RouteRebuiltAt. Player movement and waypoint
    // changes need at most a 10 Hz overlay check, not a full JSON hash at 144 Hz.
    const double Now=FPlatformTime::Seconds();
    if(RouteRebuiltAt>0&&Now<NextRouteCheck)return;
    NextRouteCheck=Now+.1;
    UWorld* W=GetWorld();if(!W)return;
    if(!RouteMesh||RouteMesh->GetWorld()!=W)
    {
        if(RouteMesh)RouteMesh->DestroyComponent();if(RouteActor)RouteActor->Destroy();
        RouteActor=W->SpawnActor<AActor>();if(!RouteActor)return;
        RouteMesh=NewObject<UProceduralMeshComponent>(RouteActor);RouteActor->SetRootComponent(RouteMesh);
        RouteMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);RouteMesh->SetCastShadow(false);
        RouteMesh->SetMaterial(0,GetGameInstance()->GetSubsystem<UACEDatSubsystem>()->GetVertexColorMaterial());
        RouteMesh->SetCanEverAffectNavigation(false);
        RouteMesh->RegisterComponentWithWorld(W);RouteRebuiltAt=0;
    }
    const auto Player=C->GetPlayerPosition();const FVector PlayerWorld=Player.ToUnrealLocation();
    bool Loop=false,Reverse=false;const auto Route=RuntimeRoute?RuntimeRoute:P->Profile;
    Route->TryGetBoolField(TEXT("loop_route"),Loop);Route->TryGetBoolField(TEXT("reverse_route"),Reverse);
    // Outdoor cell indices and same-landblock room changes do not move route
    // geometry. Only the landblock/rebasing mode affects the coordinate frame.
    const uint32 Area=(uint32(Player.CellId)&0xffff0000u)|((uint32(Player.CellId)&0xffffu)>=0x100?0x100u:0u);
    uint32 Signature=HashCombine(GetTypeHash(RoutePoint),GetTypeHash(Area));
    Signature=HashCombine(Signature,GetTypeHash(Loop&&!Reverse));
    Signature=HashCombine(Signature,GetTypeHash(FIntVector(FMath::FloorToInt(PlayerWorld.X/5000),FMath::FloorToInt(PlayerWorld.Y/5000),0)));
    for(const auto& Value:*Points)if(auto O=Value->AsObject())
    {for(const TCHAR* Key:{TEXT("cell"),TEXT("x"),TEXT("y"),TEXT("z")})Signature=HashCombine(Signature,GetTypeHash(N(O,Key)));Signature=HashCombine(Signature,GetTypeHash(S(O,TEXT("kind"))));}
    // Persistent geometry; edits/waypoint changes redraw immediately. Periodic
    // reprojection accommodates freshly streamed terrain without per-frame traces.
    if(RouteRebuiltAt>0&&Signature==RouteSignature&&Now-RouteRebuiltAt<3)return;
    RouteRebuiltAt=Now;RouteSignature=Signature;RouteSegments.Reset();
    FACEPluginRouteGround Floor(*W);
    auto Ground=[&](const FVector& V)
    {
        FVector Hit;
        if(Floor.Sample(V,Hit))return Hit+FVector(0,0,10);
        return V+FVector(0,0,10);
    };
    auto& Lines=RouteSegments;
    auto Line=[&](const FVector& A,const FVector& B,FLinearColor Color,float Width=10)
    {Lines.Add({A,B,Color,Width});};
    auto Segment=[&](const FACEPosition& From,const FACEPosition& To,FLinearColor Color)
    {
        const bool Connected=(uint32(From.CellId)>>16)==(uint32(To.CellId)>>16)||((uint32(From.CellId)&0xffff)<0x100&&(uint32(To.CellId)&0xffff)<0x100);
        const FVector A=From.ToUnrealLocation(),B=To.ToUnrealLocation();const double Length=FVector::Distance(A,B);
        if(!Connected||Length>=40000)return;
        TArray<FVector> Path;
        if(!Floor.Path(A,B,Path))return; // no false line through voids or another floor
        for(int32 I=1;I<Path.Num()&&Lines.Num()<8192;++I)
            Line(Path[I-1]+FVector(0,0,10),Path[I]+FVector(0,0,10),Color);
    };
    FACEPosition Prev,First;bool Have=false,FirstVisible=false;int32 Index=0;
    for(const auto& Value:*Points)
    {
        ++Index;const auto O=Value->AsObject();if(!O){Have=false;continue;}
        FACEPosition Next;Next.CellId=int32(uint32(N(O,TEXT("cell"))));Next.Location=FVector(N(O,TEXT("x")),N(O,TEXT("y")),N(O,TEXT("z")));
        // NAV has global coordinates, not env-cell IDs. Apply the same dungeon
        // rebasing as Execute(move), including points beyond a nominal LB edge.
        bool Legacy=false;O->TryGetBoolField(TEXT("legacy"),Legacy);
        if(Legacy && (uint32(Player.CellId)&0xffff)>=0x100)
        {
            const uint32 From=uint32(Next.CellId)>>16,To=uint32(Player.CellId)>>16;
            Next.Location.X+=(int32(From>>8)-int32(To>>8))*192;
            Next.Location.Y+=(int32(From&255)-int32(To&255))*192;
            Next.CellId=Player.CellId;
        }
        const bool SameArea=(uint32(Next.CellId)>>16)==(uint32(Player.CellId)>>16)||((uint32(Next.CellId)&0xffff)<0x100&&(uint32(Player.CellId)&0xffff)<0x100);
        const FVector Position=Next.ToUnrealLocation();
        const bool Visible=SameArea&&FVector::DistSquared(Position,PlayerWorld)<FMath::Square(40000.);
        if(Index==1){First=Next;FirstVisible=Visible;}
        if(Visible)
        {
            const FLinearColor Color=S(O,TEXT("kind"))==TEXT("jump")?FLinearColor(1,.7f,.15f):FLinearColor(.1f,.75f,1);
            const FVector Center=Ground(Position);
            if(Index==RoutePoint)
            {
                constexpr int32 Sides=32;FVector Last;
                for(int32 I=0;I<=Sides;++I)
                {
                    const float Angle=2*PI*I/Sides;
                    const FVector V=Ground(Position+FVector(FMath::Cos(Angle)*80,FMath::Sin(Angle)*80,0));
                    if(I)Line(Last,V,FLinearColor(.2f,1,.35f),8);Last=V;
                }
            }
            else if(Lines.Num()+24<8192)
            {
                // Three great circles read as a small sphere without the full
                // latitude/longitude debug-sphere cost at every waypoint.
                for(int32 Axis=0;Axis<3;++Axis)for(int32 I=0;I<8;++I)
                {
                    auto Vertex=[&](int32 Side){const float Angle=2*PI*Side/8;FVector V=FVector::ZeroVector;V[(Axis+1)%3]=12*FMath::Cos(Angle);V[(Axis+2)%3]=12*FMath::Sin(Angle);return Center+FVector(0,0,12)+V;};
                    Line(Vertex(I),Vertex(I+1),Color,2);
                }
            }
            if(Have)Segment(Prev,Next,Color);
        }
        Prev=Next;Have=Visible&&S(O,TEXT("kind"))!=TEXT("portal")&&S(O,TEXT("kind"))!=TEXT("recall");
    }
    if(Loop&&!Reverse&&Points->Num()>1&&Have&&FirstVisible)Segment(Prev,First,FLinearColor(.1f,.75f,1));
    // Game geometry, not the engine's editor/debug PDI. One unlit mesh is
    // visible in packaged builds, stereo eyes and scene captures alike.
    // Local vertices avoid precision loss at distant world coordinates.
    TArray<FVector> Vertices,Normals;TArray<int32> Triangles;TArray<FLinearColor> Colors;
    Vertices.Reserve(Lines.Num()*8);Colors.Reserve(Lines.Num()*8);Triangles.Reserve(Lines.Num()*24);
    for(const auto& L:Lines)
    {
        const FVector Direction=(L.End-L.Start).GetSafeNormal();if(Direction.IsNearlyZero())continue;
        const FVector Side=FVector::CrossProduct(Direction,FMath::Abs(Direction.Z)>.9?FVector::RightVector:FVector::UpVector).GetSafeNormal();
        const FVector Up=FVector::CrossProduct(Direction,Side);const int32 Base=Vertices.Num();
        for(const FVector End:{L.Start,L.End})for(int32 Corner=0;Corner<4;++Corner)
        {
            const FVector Normal=Side*FMath::Cos(PI*.5f*Corner)+Up*FMath::Sin(PI*.5f*Corner);
            Vertices.Add(End-PlayerWorld+Normal*L.Thickness*.5f);Normals.Add(Normal);Colors.Add(L.Color);
        }
        for(int32 Corner=0;Corner<4;++Corner)
        {const int32 Next=(Corner+1)%4;Triangles.Append({Base+Corner,Base+Next,Base+4+Corner,Base+Next,Base+4+Next,Base+4+Corner});}
    }
    RouteMesh->SetWorldLocation(PlayerWorld);
    RouteMesh->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,{},Colors,{},false);
}

void UACEPluginSubsystem::ObserveMetaChat(const FString& Text,const FString& Sender,int32 Type)
{
    ObserveOffensiveSpellChat(Text,Sender,Type);
    if(Sender.IsEmpty()&&Type==0&&Text.TrimStartAndEnd()==TEXT("Your missile attack hit the environment."))RecordCombatOutcome(false);
    // VT starts its backward input when the local spell words arrive, not when
    // requesting the spell (which can still be turning or entering magic mode).
    if(PendingSpell&&Type==ACEChatMessageType::Spellcasting)
    {
        auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();FACEWorldObject Player;
        if(!FastCastStarted&&C->GetWorldObject(C->GetPlayerGuid(),Player)&&!Player.Name.IsEmpty()&&Sender==Player.Name)
        {PendingSpellWords=true;BuffRetryAt=0;if(!FastCastOwner.IsEmpty()){FastCastStarted=true;FastCastStartedAt=FPlatformTime::Seconds();}}
    }
    if(Sender.IsEmpty()&&Type==ACEChatMessageType::Magic)
    {
        if(PendingSpell)
        {
            // UseDone acknowledges the action, including a fizzle. Only the
            // matching server spell result confirms an enchantment was applied.
            const FString& Prefix=PendingSpellConfirmation;
            const auto Matches=[&Text](const FString& Expected)
            {
                return !Expected.IsEmpty()&&(Text==Expected||Text==Expected+TEXT(".")
                    ||Text.StartsWith(Expected+TEXT(", refreshing "))||Text.StartsWith(Expected+TEXT(", surpassing "))
                    ||Text.StartsWith(Expected+TEXT(", but it is surpassed by "))||Text.StartsWith(Expected+TEXT(" and ")));
            };
            const bool Direct=!Prefix.IsEmpty()&&(Text==Prefix||Text==Prefix+TEXT(".")
                ||Text.StartsWith(Prefix+TEXT(", "))||Text.StartsWith(Prefix+TEXT(" and ")));
            const bool Recovery=PendingSpellRecoveryConfirmations.ContainsByPredicate([&Text](const auto& Pattern)
            {
                if(!Text.StartsWith(Pattern.Key)||!Text.EndsWith(Pattern.Value))return false;
                const int32 Digits=Text.Len()-Pattern.Key.Len()-Pattern.Value.Len();
                if(Digits<=0)return false;
                for(int32 I=0;I<Digits;++I)if(!FChar::IsDigit(Text[Pattern.Key.Len()+I]))return false;
                return true;
            });
            if(Direct||Recovery||PendingSpellItemConfirmations.ContainsByPredicate(Matches))
            {
                PendingSpellConfirmed=true;
                // Success is authoritative. Keep at most one completed cast's
                // acknowledgment outstanding; a second overlapping completion
                // falls back to the ordinary UseDone path.
                CompleteConfirmedBuff();
            }
            if(Text==TEXT("Your spell fizzled.")){PendingSpellFizzled=true;FastCastOwner.Empty();FastCastStarted=false;}
        }
        const double Now=FPlatformTime::Seconds();
        for(int32 Index=PendingDebuffCasts.Num()-1;Index>=0;--Index)
        {
            const auto E=PendingDebuffCasts[Index]->AsObject();
            if(Now-N(E,TEXT("sent"))>30){PendingDebuffCasts.RemoveAt(Index);continue;}
            const FString Prefix=E->GetStringField(TEXT("confirmation"));
            const bool Success=Text==Prefix||Text==Prefix+TEXT(".")||Text.StartsWith(Prefix+TEXT(", refreshing "))||Text.StartsWith(Prefix+TEXT(", surpassing "))||Text.StartsWith(Prefix+TEXT(", but it is surpassed by "));
            const FString Resist=E->GetStringField(TEXT("resist"));
            if(Success)
            {
                // Use the known spell's base duration conservatively. A stronger
                // pre-existing enchantment still confirms this weaker layer exists.
                E->SetNumberField(TEXT("expires"),Now+N(E,TEXT("duration")));
                Debuffs.RemoveAll([&](const auto& Old){return N(Old->AsObject(),TEXT("target"))==N(E,TEXT("target"))&&N(Old->AsObject(),TEXT("category"))==N(E,TEXT("category"));});
                Debuffs.Add(MakeShared<FJsonValueObject>(E));
            }
            if(Success||Text==Resist||Text==Resist+TEXT("."))
            {++DebuffRevision;LastDebuffTarget=int32(uint32(N(E,TEXT("target"))));LastDebuffSpell=int32(N(E,TEXT("spell")));PendingDebuffCasts.RemoveAt(Index);break;}
        }
    }
    auto P=Find(TEXT("ucm"));if(!P||!P->Running||!P->Profile->HasField(TEXT("vt_meta")))return;
    FString Line=Text;
    // Decal chat conditions see the rendered line, including the speaker.
    // Channel messages are already formatted by the session decoder.
    if(Type==ACEChatMessageType::Tell)Line=FString::Printf(TEXT("%s tells you, \"%s\""),Sender.IsEmpty()?TEXT("Someone"):*Sender,*Text);
    else if(Type==ACEChatMessageType::Speech&&!Sender.IsEmpty())Line=Sender.Equals(TEXT("You"),ESearchCase::IgnoreCase)?FString::Printf(TEXT("You say, \"%s\""),*Text):FString::Printf(TEXT("%s says, \"%s\""),*Sender,*Text);
    else if(Type==ACEChatMessageType::Emote&&!Sender.IsEmpty())Line=Sender+TEXT(" ")+Text;
    auto E=MakeShared<FJsonObject>();E->SetNumberField(TEXT("serial"),++MetaChatSerial);E->SetStringField(TEXT("text"),Line.Left(4096));E->SetNumberField(TEXT("color"),Type);E->SetStringField(TEXT("sender"),Sender);
    MetaChatEvents.Add(MakeShared<FJsonValueObject>(E));if(MetaChatEvents.Num()>128)MetaChatEvents.RemoveAt(0);
}
