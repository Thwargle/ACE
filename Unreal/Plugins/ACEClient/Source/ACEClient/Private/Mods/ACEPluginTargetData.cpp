#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "UI/ACEAppraisalFormatting.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

FString ACEPluginWorldPlate::ObjectType(const FACEWorldObject& O)
{
    if(O.bIsPlayer)return TEXT("player");
    if(O.IsCorpse())return TEXT("corpse");
    if(O.ItemType&ACEItemType::Portal || O.ObjectDescriptionFlags&ACEObjectDescFlag::Portal)return TEXT("portal");
    if(O.IsLifeStone())return TEXT("lifestone");
    if(O.IsDoor())return TEXT("door");
    if(O.IsVendor())return TEXT("vendor");
    if(O.ItemType&ACEItemType::Creature)return TEXT("creature");
    if(O.ItemType&ACEItemType::Container)return TEXT("container");
    return TEXT("item");
}

void UACEPluginSubsystem::ClearPluginTarget(int32 Guid)
{
    TargetHealthSamples.Remove(Guid);TargetAppraisalTimes.Remove(Guid);Appraisals.Remove(Guid);
    for(const auto& P:Plugins)if(P->WorldPlates.RemoveAll([Guid](const auto& Plate){return Plate.Id==Guid;}))++P->WorldPlateRevision;
}

TArray<int32> UACEPluginSubsystem::ActivePlateTargets() const
{
    TArray<int32> Ids;auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    if(C->GetSessionState()!=EACESessionState::InWorld)return Ids;
    for(const auto& P:Plugins)if(P->Enabled&&P->Running&&P->HasPresentation()&&P->Player==C->GetPlayerGuid()
        && P->Server==C->GetServerName()&&P->VMSession.Pin()==C->GetSession()
        && P->Permissions.Contains(TEXT("ui"))&&P->Permissions.Contains(TEXT("markers")))
        for(const auto& Plate:P->WorldPlates)
        {
            if(Plate.SelectionOnly&&Plate.Id!=C->GetSelectedObject().Guid)continue;
            FACEWorldObject O;
            if(!C->GetWorldObject(Plate.Id,O)||!O.IsSelectableWorldObject()||O.bDying||O.IsCorpse()||DefeatedTargets.Contains(O.Guid))continue;
            const auto Player=C->GetPlayerPosition();
            if((uint32(Player.CellId)>>16)!=(uint32(O.Position.CellId)>>16)
                && ((uint32(Player.CellId)&65535)>=256||(uint32(O.Position.CellId)&65535)>=256))continue;
            if(FVector::DistSquared(Player.ToUnrealLocation(),O.Position.ToUnrealLocation())>FMath::Square(24000.))continue;
            Ids.AddUnique(Plate.Id);if(Ids.Num()==16)return Ids;
        }
    return Ids;
}

void UACEPluginSubsystem::RefreshPlateAppraisals()
{
    const double Now=FPlatformTime::Seconds();
    if(Now<NextPlateAppraisal||Now-LastAppraisalRequest<1)return;
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    if(auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());PC&&PC->IsWorldTransitionActive())return;
    const auto Ids=ActivePlateTargets();int32 Request=0;double Oldest=Now;
    for(int32 Id:Ids)
    {
        bool Refresh=false;
        for(const auto& P:Plugins)if(P->Running&&P->Enabled)
            for(const auto& Plate:P->WorldPlates)if(Plate.Id==Id&&Plate.Refresh)Refresh=true;
        FACEWorldObject O;if(!Refresh||!C->GetWorldObject(Id,O)||!(O.ItemType&ACEItemType::Creature)||Id==C->GetPlayerGuid())continue;
        const auto* A=Appraisals.Find(Id);const double Last=AppraisalRequests.FindRef(Id);
        const double Delay=A?(A->bSuccess?2.:30.):(Last>0?30.:0.);
        if(Now-Last<Delay)continue;
        if(Last<Oldest){Oldest=Last;Request=Id;}
    }
    if(!Request)return;
    // Share the existing global request budget. While automation runs, leave
    // alternate seconds available to its inventory/equipment appraisals.
    const auto Policy=PolicyPlugin();NextPlateAppraisal=Now+(Policy&&Policy->Running?2.:1.);
    LastAppraisalRequest=Now;AppraisalRequests.Add(Request,Now);C->RequestBackgroundAppraisal(Request);
}

void UACEPluginSubsystem::AddTargetDetails(const TSharedPtr<FJsonObject>& Out)
{
    if(CompanionReadBatch && CompanionReadBatch->TargetDetails)
    {
        for(const auto& V:CompanionReadBatch->TargetDetails->Values)Out->SetField(V.Key,V.Value);
        return;
    }
    auto Details=MakeShared<FJsonObject>(),Targets=MakeShared<FJsonObject>(),Effects=MakeShared<FJsonObject>();
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    auto* Dat=GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
    const auto Session=C->GetSession();
    const double Now=FPlatformTime::Seconds();
    const bool SameSession=Session && ObservedSession.Pin()==Session && DataPlayer==C->GetPlayerGuid() && DataServer==C->GetServerName();
    const int32 Selection=C->GetSelectedObject().Guid;
    TSet<int32> Ids;if(Selection)Ids.Add(Selection);
    for(int32 Id:ActivePlateTargets())Ids.Add(Id);
    if(SameSession)for(const auto& Value:Debuffs)
    {
        const auto E=Value->AsObject();
        const int32 Target=int32(uint32(E->GetNumberField(TEXT("target"))));
        const auto* Object=Session->GetWorldObjects().Find(Target);
        const double Remaining=E->GetNumberField(TEXT("expires"))-Now;
        if(Remaining<=0 || !Object || Object->bDying || DefeatedTargets.Contains(Target))continue;
        uint32 Icon=0;FString Name;const int32 Spell=int32(E->GetNumberField(TEXT("spell")));
        if(!Dat->TryGetSpellInfo(Spell,Name,Icon))continue;
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("spell"),Spell);Row->SetNumberField(TEXT("icon"),Icon);
        Row->SetNumberField(TEXT("remaining"),Remaining);Row->SetNumberField(TEXT("duration"),E->GetNumberField(TEXT("duration")));
        Row->SetStringField(TEXT("source"),TEXT("own_confirmed_cast"));
        const FString Key=FString::Printf(TEXT("%u"),uint32(Target));
        const TArray<TSharedPtr<FJsonValue>>* Existing=nullptr;Effects->TryGetArrayField(Key,Existing);
        TArray<TSharedPtr<FJsonValue>> Rows=Existing?*Existing:TArray<TSharedPtr<FJsonValue>>{};
        Rows.Add(MakeShared<FJsonValueObject>(Row));Effects->SetArrayField(Key,Rows);Ids.Add(Target);
    }
    if(Session && C->GetSessionState()==EACESessionState::InWorld)for(int32 Id:Ids)
    {
        const auto* Object=Session->GetWorldObjects().Find(Id);if(!Object)continue;
        const auto& O=*Object;auto Row=MakeShared<FJsonObject>(),Vitals=MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("id"),uint32(Id));Row->SetStringField(TEXT("name"),O.Name.Left(256));
        Row->SetNumberField(TEXT("wcid"),O.WeenieClassId);Row->SetStringField(TEXT("object_type"),ACEPluginWorldPlate::ObjectType(O));
        Row->SetNumberField(TEXT("item_type"),uint32(O.ItemType));Row->SetBoolField(TEXT("dead"),O.bDying||DefeatedTargets.Contains(Id));
        Row->SetBoolField(TEXT("monster"),O.IsAttackable()&&!O.bIsPlayer&&!O.PetOwnerId&&(O.ItemType&ACEItemType::Creature));
        const auto* A=SameSession?Appraisals.Find(Id):nullptr;
        auto Vital=[&](const TCHAR* Key,int32 Current,int32 Max)
        {
            if(Current<0 || Max<=0 || Current>Max)return;
            auto V=MakeShared<FJsonObject>();V->SetNumberField(TEXT("current"),Current);V->SetNumberField(TEXT("max"),Max);
            Vitals->SetObjectField(Key,V);
        };
        if(A)
        {
            Row->SetNumberField(TEXT("assessment_age"),FMath::Max(0.,Now-TargetAppraisalTimes.FindRef(Id)));
            if(A->Level>0)Row->SetNumberField(TEXT("level"),A->Level);
            if(const auto* Type=A->IntProperties.Find(9007))Row->SetNumberField(TEXT("weenie_type"),*Type);
            if(A->CreatureType)Row->SetNumberField(TEXT("creature_type"),A->CreatureType);
            const auto Headings=ACEAppraisalFormatting::CreatureHeadings(*A,C->GetUIResourceResolver(),&O);
            if(!Headings.Type.IsEmpty())Row->SetStringField(TEXT("creature_class"),Headings.Type);
            const FString Title=Headings.Profession.IsEmpty()?A->StringProperties.FindRef(5):Headings.Profession;
            if(!Title.IsEmpty())Row->SetStringField(TEXT("title"),Title);
            if(A->bHasCreatureHealth)Vital(TEXT("health"),A->Health,A->MaxHealth);
            if(A->bHasCreatureAttributes){Vital(TEXT("stamina"),A->Stamina,A->MaxStamina);Vital(TEXT("mana"),A->Mana,A->MaxMana);}
        }
        if(SameSession)if(const auto* Health=TargetHealthSamples.Find(Id);Health && (!A||Health->ReceivedAt>TargetAppraisalTimes.FindRef(Id)))
        {
            // Health replies are percentages, not integer vitals. Do not display
            // stale appraisal health or invent current hit points from rounding.
            auto V=MakeShared<FJsonObject>();V->SetNumberField(TEXT("fraction"),Health->Fraction);
            V->SetNumberField(TEXT("age"),FMath::Max(0.,Now-Health->ReceivedAt));
            if(A&&A->bHasCreatureHealth&&A->MaxHealth>0)V->SetNumberField(TEXT("max"),A->MaxHealth);
            Vitals->SetObjectField(TEXT("health"),V);
        }
        if(Id==C->GetPlayerGuid())
        {
            const auto& V=C->GetPlayerVitalsView();
            if(V.bValid){Vital(TEXT("health"),V.Health,V.MaxHealth);Vital(TEXT("stamina"),V.Stamina,V.MaxStamina);Vital(TEXT("mana"),V.Mana,V.MaxMana);Row->SetNumberField(TEXT("level"),V.Level);}
        }
        if(O.bIsPlayer || O.ObjectDescriptionFlags&(ACEObjectDescFlag::PlayerKiller|ACEObjectDescFlag::PkLiteStatus))
            Row->SetStringField(TEXT("pk_status"),O.ObjectDescriptionFlags&ACEObjectDescFlag::PlayerKiller?TEXT("PK"):O.ObjectDescriptionFlags&ACEObjectDescFlag::PkLiteStatus?TEXT("PK Lite"):TEXT("NPK"));
        Row->SetObjectField(TEXT("vitals"),Vitals);
        Targets->SetObjectField(FString::Printf(TEXT("%u"),uint32(Id)),Row);
        if(Id==Selection)Details->SetObjectField(TEXT("selection"),Row);
    }
    if(!Details->HasField(TEXT("selection")))Details->SetField(TEXT("selection"),MakeShared<FJsonValueNull>());
    Details->SetObjectField(TEXT("tracked_targets"),Targets);Details->SetObjectField(TEXT("target_effects"),Effects);
    for(const auto& V:Details->Values)Out->SetField(V.Key,V.Value);
    if(CompanionReadBatch)CompanionReadBatch->TargetDetails=Details;
}

void UACEPluginSubsystem::UpdatePluginWorldPlates()
{
    auto* W=GetWorld();auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    TSet<UACEPluginWorldPlateComponent*> Keep;
    for(const auto& P:Plugins)if(W && P->HasPresentation() && P->Enabled && P->Running && P->Player==C->GetPlayerGuid()
        && P->VMSession.Pin()==C->GetSession() && P->Server==C->GetServerName() && C->GetSessionState()==EACESessionState::InWorld
        && P->Permissions.Contains(TEXT("ui")) && P->Permissions.Contains(TEXT("markers")))
    {
        for(const auto& Plate:P->WorldPlates)
        {
            if(Keep.Num()>=16)break;
            UACEPluginWorldPlateComponent* Component=nullptr;
            for(auto Existing:WorldPlateComponents)if(IsValid(Existing) && Existing->GetWorld()==W && Existing->PluginId==P->Id && Existing->GetPlate().Id==Plate.Id){Component=Existing;break;}
            if(!Component)
            {
                if(!WorldPlateActor || WorldPlateActor->GetWorld()!=W)
                {
                    if(WorldPlateActor)WorldPlateActor->Destroy();WorldPlateComponents.Reset();
                    WorldPlateActor=W->SpawnActor<AActor>();if(!WorldPlateActor)return;
                }
                Component=NewObject<UACEPluginWorldPlateComponent>(WorldPlateActor);
                WorldPlateActor->AddInstanceComponent(Component);Component->PluginId=P->Id;
                Component->SetPlate(Plate,this);Component->Revision=P->WorldPlateRevision;
                Component->RegisterComponentWithWorld(W);WorldPlateComponents.Add(Component);
            }
            else if(Component->Revision!=P->WorldPlateRevision){Component->SetPlate(Plate,this);Component->Revision=P->WorldPlateRevision;}
            Keep.Add(Component);
        }
    }
    WorldPlateComponents.RemoveAll([&](auto Component){if(Keep.Contains(Component))return false;if(IsValid(Component))Component->DestroyComponent();return true;});
}
