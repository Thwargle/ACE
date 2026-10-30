#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginVM.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEOpcodes.h"
#include "Engine/GameInstance.h"

namespace
{
bool NotifyMatches(const TSharedPtr<FACEClientPlugin>& P)
{
    bool Enabled=true;
    if(!P||!P->Enabled||!P->Profile||!P->Permissions.Contains(TEXT("loot")))return false;
    P->Profile->TryGetBoolField(TEXT("loot_notify_match"),Enabled);
    return Enabled;
}
bool LootItem(const FACEWorldObject& Item)
{
    return !Item.bIsPlayer&&!Item.IsCorpse()&&!Item.IsDoor()
        &&!(Item.ItemType&(ACEItemType::Creature|ACEItemType::Portal|ACEItemType::LifeStone));
}
// Use precisely the same first-match evaluator as looting (including imported
// VT conditions). Unknown properties wait for an appraisal; this service never
// sends ID requests. Cooperative scans retain only their cursor between calls.
const TCHAR* MatchSource=TEXT(R"lua(
local job,index,scan
return function(s,p)
 if job~=s.job then job=s.job;index=1;scan={} end
 local rules=p.loot_rules or {}
 local examined=0
 while index<=#rules do
  if examined>=256 or not workavailable() then return {pending=true} end
  local rule=rules[index]
  local match=lootmatch(rule,s.item,s,scan)
  if match=='pending' then return {pending=true} end
  if match==nil then return {} end
  if match then return {rule=index,label=rule.label or ('Rule '..index),action=rule.action or 'keep',limit=rule.keep_up_to} end
  index=index+1;scan={};examined=examined+1
 end
 return {}
end
)lua");
}

void UACEPluginSubsystem::ShowLootNotification(const FString& Text)
{
    // Local output only: never send these messages to the server or fellowship.
    FString Line=Text.Left(900);Line.ReplaceInline(TEXT("\r"),TEXT(" "));Line.ReplaceInline(TEXT("\n"),TEXT(" "));
    if(auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>())
        C->ReceiveChatMessage(TEXT("[UCM] ")+Line,FString(),ACEChatMessageType::System,ACEChatMessageType::Plugin);
}

void UACEPluginSubsystem::QueueLootMatch(int32 Guid)
{
    if(!Guid||!NotifyMatches(PolicyPlugin()))return;
    // Assess bursts cannot grow a persistent unbounded work queue.
    if(!LootMatchQueue.Contains(Guid))
    {
        if(LootMatchQueue.Num()>=32)LootMatchQueue.RemoveAt(0);
        LootMatchQueue.Add(Guid);
    }
}

void UACEPluginSubsystem::TickLootNotifications()
{
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    auto P=PolicyPlugin();
    if(!C||C->GetSessionState()!=EACESessionState::InWorld||!NotifyMatches(P))
    {
        LootMatchVM.Reset();LootMatchSnapshot.Reset();LootMatchQueue.Reset();LootMatchReported.Reset();
        LootMatchProfile.Reset();LootMatchSession.Reset();LootMatchSelection=LootMatchPlayer=0;
        return;
    }
    double Revision=0;P->Profile->TryGetNumberField(TEXT("loot_revision"),Revision);
    if(LootMatchProfile.Pin()!=P->Profile||LootMatchSession.Pin()!=C->GetSession()
        ||LootMatchPlayer!=C->GetPlayerGuid()||LootMatchRevision!=Revision)
    {
        LootMatchVM.Reset();LootMatchSnapshot.Reset();LootMatchReported.Reset();
        if(LootMatchPlayer&&(LootMatchSession.Pin()!=C->GetSession()||LootMatchPlayer!=C->GetPlayerGuid()))LootMatchQueue.Reset();
        LootMatchProfile=P->Profile;LootMatchSession=C->GetSession();LootMatchPlayer=C->GetPlayerGuid();
        LootMatchRevision=Revision;LootMatchSelection=0;
    }
    const int32 Selection=C->GetSelectedObject().Guid;
    if(Selection!=LootMatchSelection)
    {
        LootMatchSelection=Selection;
        // Explicit re-selection is another TestItem request; repeated appraisals
        // or idle frames for the same item/rule must not flood the chat window.
        LootMatchReported.Remove(Selection);QueueLootMatch(Selection);
    }
    const TArray<TSharedPtr<FJsonValue>>* Rules=nullptr;
    if(!P->Profile->TryGetArrayField(TEXT("loot_rules"),Rules)||Rules->IsEmpty())
    {LootMatchQueue.Reset();LootMatchSnapshot.Reset();return;}
    if(!LootMatchSnapshot)
    {
        FACEWorldObject Item;bool Found=false;
        while(!LootMatchQueue.IsEmpty()&&!Found)
        {
            const int32 Guid=LootMatchQueue[0];LootMatchQueue.RemoveAt(0);
            Found=C->GetWorldObject(Guid,Item)&&LootItem(Item);
        }
        if(!Found)return;
        LootMatchSnapshot=MakeShared<FJsonObject>();
        LootMatchSnapshot->SetNumberField(TEXT("job"),++LootMatchSerial);
        LootMatchSnapshot->SetField(TEXT("item"),PluginItemSnapshot(Item));
        const auto& V=C->GetPlayerVitalsView();
        LootMatchSnapshot->SetNumberField(TEXT("level"),V.Level);
        auto Skills=MakeShared<FJsonObject>();
        for(const auto& Skill:V.Skills)
        {
            auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("base"),Skill.Base);
            J->SetNumberField(TEXT("current"),Skill.Current);J->SetNumberField(TEXT("training"),Skill.AdvancementClass);
            Skills->SetObjectField(FString::FromInt(Skill.SkillId),J);
        }
        LootMatchSnapshot->SetObjectField(TEXT("skills"),Skills);
        FACEWorldObject Player;C->GetWorldObject(C->GetPlayerGuid(),Player);
        TMap<int32,int32> Counts;Counts.Add(C->GetPlayerGuid(),0);C->GetSession()->GetPackItemCounts(Counts);
        const int32 Capacity=Player.ItemsCapacity?Player.ItemsCapacity:102;
        LootMatchSnapshot->SetNumberField(TEXT("main_pack_slots"),Capacity==-1?MAX_int32:FMath::Max(0,Capacity-Counts.FindRef(C->GetPlayerGuid())));
    }
    FString Error;
    if(!LootMatchVM)
    {
        LootMatchVM=MakeShared<FACEPluginVM>();
        if(!LootMatchVM->Load(MatchSource,Error))
        {LogUCMEvent(*P,TEXT("Loot notification check: ")+Error,EACEUCMLogLevel::Warning);LootMatchSnapshot.Reset();LootMatchVM.Reset();return;}
    }
    // Marshal only the selected loot rules, not routes, catalogs, inventory or
    // meta libraries. No snapshot work is done on unchanged idle frames.
    auto Profile=MakeShared<FJsonObject>();Profile->SetArrayField(TEXT("loot_rules"),*Rules);
    TSharedPtr<FJsonObject> Result;
    if(!LootMatchVM->Step(LootMatchSnapshot,Profile,Result,Error))
    {LogUCMEvent(*P,TEXT("Loot notification check: ")+Error,EACEUCMLogLevel::Warning);LootMatchSnapshot.Reset();LootMatchVM.Reset();return;}
    bool Pending=false;if(Result&&Result->TryGetBoolField(TEXT("pending"),Pending)&&Pending)return;
    double Rule=0;
    if(Result&&Result->TryGetNumberField(TEXT("rule"),Rule))
    {
        const auto Item=LootMatchSnapshot->GetObjectField(TEXT("item"));
        const int32 Guid=int32(uint32(Item->GetNumberField(TEXT("id"))));
        FString Action=Result->GetStringField(TEXT("action"));
        double Limit=0;
        if(Result->TryGetNumberField(TEXT("limit"),Limit)&&Limit>0&&Action==TEXT("keep"))
            Action=FString::Printf(TEXT("keep up to %.0f"),Limit);
        const FString Label=Result->GetStringField(TEXT("label"));
        const FString Identity=FString::Printf(TEXT("%.0f|%s|%s"),Rule,*Label,*Action);
        if(LootMatchReported.FindRef(Guid)!=Identity)
        {
            if(LootMatchReported.Num()>=128)LootMatchReported.Remove(LootMatchReported.CreateConstIterator().Key());
            LootMatchReported.Add(Guid,Identity);
            ShowLootNotification(FString::Printf(TEXT("%s matches rule \"%s\" (%s)."),*Item->GetStringField(TEXT("name")),*Label,*Action));
        }
    }
    LootMatchSnapshot.Reset();
}
