#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginCastMotion.h"
#include "ACEPluginVM.h"
#include "ACEVTProfile.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ProceduralMeshComponent.h"

namespace
{
    bool SafeName(const FString& S)
    {
        if (S.IsEmpty() || S.Len() > 64) return false;
        for (TCHAR C : S) if (!(FChar::IsAlnum(C) || C == '_' || C == '-')) return false;
        return true;
    }
    TSharedPtr<FJsonObject> Read(const FString& Path)
    {
        FString Text; TSharedPtr<FJsonObject> O;
        if (IFileManager::Get().FileSize(*Path) > 16 * 1024 * 1024) return nullptr;
        if (FFileHelper::LoadFileToString(Text, *Path)) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O);
        return O;
    }
    FString Encode(const TSharedPtr<FJsonObject>& O)
    { FString Text; if (O) FJsonSerializer::Serialize(O.ToSharedRef(), TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text)); return Text; }
    bool Write(const FString& Path, const TSharedPtr<FJsonObject>& O)
    {
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
        const FString Temp = Path + TEXT(".tmp");
        return FFileHelper::SaveStringToFile(Encode(O), *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && IFileManager::Get().Move(*Path, *Temp, true, true);
    }
    FString WriteNewCopy(const FString& Folder,const FString& Name,const TSharedPtr<FJsonObject>& Object)
    {
        FString Stem;
        for(TCHAR Ch:Name.Left(48))Stem.AppendChar(FChar::IsAlnum(Ch)||Ch=='_'||Ch=='-'?Ch:'_');
        if(Stem.IsEmpty())Stem=TEXT("Imported");
        IFileManager::Get().MakeDirectory(*Folder,true);
        const FString Json=Encode(Object);
        for(int32 Copy=1;Copy<=10000;++Copy)
        {
            const FString Candidate=Stem+(Copy==1?FString():FString::Printf(TEXT("_%d"),Copy));
            const FString Path=Folder/(Candidate+TEXT(".json"));
            if(IFileManager::Get().FileExists(*Path))continue;
            // Exclusive creation also protects a copy created by another process.
            if(FFileHelper::SaveStringToFile(Json,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_NoReplaceExisting))return Candidate;
            if(!IFileManager::Get().FileExists(*Path))return FString();
        }
        return FString();
    }
    FString String(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FString& Default = FString())
    { FString V; return O && O->TryGetStringField(Key, V) ? V : Default; }
    double Number(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default = 0)
    { double V; return O && O->TryGetNumberField(Key, V) && FMath::IsFinite(V) ? V : Default; }
    int32 Guid(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, uint32 Default = 0)
    {
        const double V=Number(O,Key,Default);
        return V>=0 && V<=MAX_uint32 && FMath::FloorToDouble(V)==V ? int32(uint32(V)) : 0;
    }
    TSharedPtr<FJsonObject> Position(const FACEPosition& P)
    {
        auto O = MakeShared<FJsonObject>(); O->SetNumberField(TEXT("cell"), uint32(P.CellId));
        O->SetNumberField(TEXT("x"), P.Location.X); O->SetNumberField(TEXT("y"), P.Location.Y); O->SetNumberField(TEXT("z"), P.Location.Z); return O;
    }
    bool ValidUCMProfile(const TSharedPtr<FJsonObject>& O)
    {
        for(const TCHAR* Key:{TEXT("melee_weapon"),TEXT("melee_secondary_equip")})if(O->HasField(Key))
        {double Value=0;if(!O->TryGetNumberField(Key,Value)||!FMath::IsFinite(Value)||Value<-1||Value>MAX_uint32||FMath::FloorToDouble(Value)!=Value)return false;}
        if(O->HasField(TEXT("monster_catalog")))
        {
            const TSharedPtr<FJsonObject>* Catalog=nullptr;
            if(!O->TryGetObjectField(TEXT("monster_catalog"),Catalog)||(*Catalog)->Values.Num()>10000)return false;
            for(const auto& Pair:(*Catalog)->Values)
            {
                if(Pair.Key.IsEmpty()||Pair.Key.Len()>256||Pair.Value->Type!=EJson::Object)return false;
                for(const TCHAR* Key:{TEXT("species"),TEXT("max_health")})
                {double V=0;if(!Pair.Value->AsObject()->TryGetNumberField(Key,V)||!FMath::IsFinite(V)||V<0||V>MAX_int32||FMath::FloorToDouble(V)!=V)return false;}
            }
        }
        // Validate structures consumed by native widgets, not only the script.
        for(const TCHAR* Key:{TEXT("route"),TEXT("states"),TEXT("loot_rules"),TEXT("monsters"),TEXT("buff_commands"),TEXT("assist_items"),TEXT("buff_items"),TEXT("item_buff_targets"),TEXT("recharge_handlers"),TEXT("pea_recipes"),TEXT("vendor_rules")})
        {
            if(!O->HasField(Key))continue;
            const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
            if(!O->TryGetArrayField(Key,Values)||Values->Num()>((FString(Key)==TEXT("loot_rules")||FString(Key)==TEXT("route"))?2048:512))return false;
            for(const auto& Value:*Values)
            {
                if(Value->Type!=EJson::Object)return false;
                auto Entry=Value->AsObject();
                if(FString(Key)==TEXT("vendor_rules"))
                {
                    for(const TCHAR* K:{TEXT("vendor_name"),TEXT("item_name"),TEXT("server")})
                    {FString V;if(!Entry->TryGetStringField(K,V)||V.IsEmpty()||V.Len()>256)return false;}
                    for(const TCHAR* K:{TEXT("vendor_wcid"),TEXT("item_wcid"),TEXT("quantity")})
                    {double V;if(!Entry->TryGetNumberField(K,V)||!FMath::IsFinite(V)||V<1||V>(FString(K)==TEXT("quantity")?100000:MAX_int32)||FMath::FloorToDouble(V)!=V)return false;}
                }
                if(FString(Key)==TEXT("pea_recipes"))
                {
                    for(const TCHAR* TextKey:{TEXT("tool"),TEXT("input"),TEXT("output")})
                    {FString V;if(!Entry->TryGetStringField(TextKey,V)||V.IsEmpty()||V.Len()>256)return false;}
                    double Count=0;if(!Entry->TryGetNumberField(TEXT("count"),Count)||!FMath::IsFinite(Count)||Count<1||Count>10000||FMath::FloorToDouble(Count)!=Count)return false;
                }
                for(const TCHAR* TextKey:{TEXT("name"),TEXT("kind"),TEXT("label"),TEXT("combat"),TEXT("object_name"),TEXT("keyword"),TEXT("role")})
                    if(Entry->HasField(TextKey)&&Entry->Values[TextKey]->Type!=EJson::String)return false;
                if(Entry->HasField(TEXT("transitions")))
                {
                    const TArray<TSharedPtr<FJsonValue>>* Rules=nullptr;
                    if(!Entry->TryGetArrayField(TEXT("transitions"),Rules)||Rules->Num()>256)return false;
                    for(const auto& Rule:*Rules)if(Rule->Type!=EJson::Object)return false;
                }
            }
        }
        for(const TCHAR* Key:{TEXT("weapons"),TEXT("weapon_items"),TEXT("consumables"),TEXT("buffs"),TEXT("excluded_buffs"),TEXT("excluded_buff_spells")})
        {
            if(!O->HasField(Key))continue;
            const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(!O->TryGetArrayField(Key,Values)||Values->Num()>2048)return false;
            for(const auto& Value:*Values)if(Value->Type!=EJson::Number||Value->AsNumber()<0||Value->AsNumber()>MAX_uint32)return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* Commands=nullptr;
        if(O->TryGetArrayField(TEXT("buff_commands"),Commands))
        {
            if(Commands->Num()>32)return false;TSet<FString> Keywords;
            for(const auto& V:*Commands)
            {
                const FString Key=String(V->AsObject(),TEXT("keyword")).TrimStartAndEnd().ToLower(),Role=String(V->AsObject(),TEXT("role"));
                if(Key.IsEmpty()||Key.Len()>64||Keywords.Contains(Key)||!TArray<FString>{TEXT("mage"),TEXT("heavy"),TEXT("missile"),TEXT("light"),TEXT("finesse"),TEXT("twohanded"),TEXT("unarmed"),TEXT("melee")}.Contains(Role))return false;
                Keywords.Add(Key);
            }
        }
        if(O->HasField(TEXT("salvage_policy")))
        {
            const TSharedPtr<FJsonObject>* Policy=nullptr;
            if(!O->TryGetObjectField(TEXT("salvage_policy"),Policy))return false;
            auto ValidRanges=[](const TSharedPtr<FJsonValue>& V)
            {
                const TArray<TSharedPtr<FJsonValue>>* Ranges=nullptr;if(!V->TryGetArray(Ranges)||Ranges->Num()>64)return false;
                double Previous=-1;
                for(const auto& R:*Ranges)
                {
                    if(R->Type!=EJson::Object)return false;double Min,Max;
                    if(!R->AsObject()->TryGetNumberField(TEXT("min"),Min)||!R->AsObject()->TryGetNumberField(TEXT("max"),Max)||!FMath::IsFinite(Min)||!FMath::IsFinite(Max)||Min<0||Min<Previous||Max<Min||Max>1000)return false;
                    Previous=Max;
                }
                return true;
            };
            if(!(*Policy)->HasField(TEXT("default"))||!ValidRanges((*Policy)->Values[TEXT("default")]))return false;
            for(const TCHAR* Key:{TEXT("materials"),TEXT("values")})if((*Policy)->HasField(Key))
            {
                const TSharedPtr<FJsonObject>* Map=nullptr;if(!(*Policy)->TryGetObjectField(Key,Map)||(*Map)->Values.Num()>1024)return false;
                for(const auto& Pair:(*Map)->Values)
                {
                    uint32 Id=0;if(!LexTryParseString(Id,*Pair.Key)||!Id||FString::Printf(TEXT("%u"),Id)!=FString(Pair.Key))return false;
                    if(FString(Key)==TEXT("materials")){if(!ValidRanges(Pair.Value))return false;}
                    else if(Pair.Value->Type!=EJson::Number||!FMath::IsFinite(Pair.Value->AsNumber())||Pair.Value->AsNumber()<0||Pair.Value->AsNumber()>MAX_int32)return false;
                }
            }
        }
        const TArray<TSharedPtr<FJsonValue>>* Loot=nullptr;
        if(O->TryGetArrayField(TEXT("loot_rules"),Loot))for(const auto& V:*Loot)
        {
            const auto R=V->AsObject();
            for(const TCHAR* K:{TEXT("name"),TEXT("label")})if(FTCHARToUTF8(*String(R,K)).Length()>256)return false;
            for(const TCHAR* K:{TEXT("action"),TEXT("name_mode")})if(R->HasField(K)&&R->Values[K]->Type!=EJson::String)return false;
            if(!TArray<FString>{TEXT("keep"),TEXT("skip"),TEXT("salvage"),TEXT("sell"),TEXT("read")}.Contains(String(R,TEXT("action"),TEXT("keep"))))return false;
            if(!TArray<FString>{TEXT("prefix"),TEXT("exact"),TEXT("contains")}.Contains(String(R,TEXT("name_mode"),TEXT("prefix"))))return false;
            if(R->HasField(TEXT("enabled"))&&R->Values[TEXT("enabled")]->Type!=EJson::Boolean)return false;
            if(R->HasField(TEXT("count_by_name"))&&R->Values[TEXT("count_by_name")]->Type!=EJson::Boolean)return false;
            if(R->HasField(TEXT("compatibility_issues")))
            {
                const TArray<TSharedPtr<FJsonValue>>* Problems=nullptr;bool Enabled=true;R->TryGetBoolField(TEXT("enabled"),Enabled);
                if(!R->TryGetArrayField(TEXT("compatibility_issues"),Problems)||Problems->Num()>128||(Enabled&&Problems->Num()))return false;
                for(const auto& Problem:*Problems)if(Problem->Type!=EJson::String||Problem->AsString().Len()>2048)return false;
            }
            for(const TCHAR* K:{TEXT("material"),TEXT("type"),TEXT("keep_up_to"),TEXT("min_value"),TEXT("max_value"),TEXT("min_workmanship"),TEXT("max_workmanship"),TEXT("min_burden"),TEXT("max_burden"),TEXT("min_rating"),TEXT("max_rating")})
                if(R->HasField(K)&&(R->Values[K]->Type!=EJson::Number||!FMath::IsFinite(R->Values[K]->AsNumber())||R->Values[K]->AsNumber()<0||R->Values[K]->AsNumber()>MAX_uint32))return false;
            for(const TCHAR* K:{TEXT("material"),TEXT("type"),TEXT("keep_up_to")})if(FMath::FloorToDouble(Number(R,K))!=Number(R,K))return false;
            if(R->HasField(TEXT("conditions")))
            {
                const TArray<TSharedPtr<FJsonValue>>* Conditions=nullptr;
                if(!R->TryGetArrayField(TEXT("conditions"),Conditions)||Conditions->Num()>ACEVTProfile::MaxLootConditions)return false;
                for(const auto& CV:*Conditions)
                {
                    if(CV->Type!=EJson::Object)return false;const auto C=CV->AsObject();const FString Field=String(C,TEXT("field"));
                    if(!TArray<FString>{TEXT("int"),TEXT("float"),TEXT("string"),TEXT("spells"),TEXT("legacy")}.Contains(Field))return false;
                    for(const TCHAR* K:{TEXT("value"),TEXT("key")})if(C->HasField(K)&&(C->Values[K]->Type!=EJson::Number||!FMath::IsFinite(C->Values[K]->AsNumber())))return false;
                    if(Field!=TEXT("spells")&&Field!=TEXT("legacy")&&(Number(C,TEXT("key"))<1||Number(C,TEXT("key"))>MAX_int32||FMath::FloorToDouble(Number(C,TEXT("key")))!=Number(C,TEXT("key"))))return false;
                    if(Field==TEXT("legacy"))
                    {
                        const int Key=int(Number(C,TEXT("key")));const TArray<TSharedPtr<FJsonValue>>* Args=nullptr;
                        if(Number(C,TEXT("key"))!=Key||!TArray<int>{6,7,10,17,1000,1001,1002,1003,1004,2000,2001,2003,2005,2006,2007,2008}.Contains(Key)||!C->TryGetArrayField(TEXT("args"),Args)||Args->Num()!=((Key==1000||Key==17||Key==2003||Key==2005)?2:(Key==1004||Key==2008)?3:1))return false;
                        for(const auto& Arg:*Args)if(Arg->Type!=EJson::Number||!FMath::IsFinite(Arg->AsNumber()))return false;
                    }
                    if(Field==TEXT("int")||Field==TEXT("float"))
                    {
                        if(!TArray<FString>{TEXT("le"),TEXT("ge"),TEXT("eq"),TEXT("ne"),TEXT("bits")}.Contains(String(C,TEXT("op"))))return false;
                        if(String(C,TEXT("op"))==TEXT("bits")&&(Number(C,TEXT("value"))<0||Number(C,TEXT("value"))>MAX_uint32||FMath::FloorToDouble(Number(C,TEXT("value")))!=Number(C,TEXT("value"))))return false;
                    }
                    if(C->HasField(TEXT("missing_zero"))&&C->Values[TEXT("missing_zero")]->Type!=EJson::Boolean)return false;
                    for(const TCHAR* K:{TEXT("pattern"),TEXT("exclude")})if(C->HasField(K)&&(C->Values[K]->Type!=EJson::String||!ACEVTProfile::IsSupportedPattern(String(C,K))))return false;
                    if(Field==TEXT("spells")&&(Number(C,TEXT("value"))<0||Number(C,TEXT("value"))>1024||FMath::FloorToDouble(Number(C,TEXT("value")))!=Number(C,TEXT("value"))))return false;
                }
            }
            for(const TCHAR* Field:{TEXT("value"),TEXT("workmanship"),TEXT("burden"),TEXT("rating")})
            {
                const double Lo=Number(R,*(FString(TEXT("min_"))+Field)),Hi=Number(R,*(FString(TEXT("max_"))+Field));
                if(Hi>0&&Hi<Lo)return false;
            }
        }
        return true;
    }
}

void UACEPluginSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UACEClientSubsystem>();
    GetGameInstance()->GetSubsystem<UACEClientSubsystem>()->OnAppraisalObserved.AddDynamic(this,&UACEPluginSubsystem::ObserveAppraisal);
    Settings = Read(UserDirectory() / TEXT("settings.json"));
    if (!Settings) Settings = MakeShared<FJsonObject>();
    Discover();
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UACEPluginSubsystem::Tick), .5f);
}
void UACEPluginSubsystem::Deinitialize()
{
    if(RouteMesh){RouteMesh->DestroyComponent();RouteMesh=nullptr;}
    if(RouteActor){RouteActor->Destroy();RouteActor=nullptr;}
    GetGameInstance()->GetSubsystem<UACEClientSubsystem>()->OnAppraisalObserved.RemoveDynamic(this,&UACEPluginSubsystem::ObserveAppraisal);
    if(auto S=ObservedSession.Pin()){S->OnUseDone.Remove(UseDoneHandle);S->OnPlayerTell.Remove(TellHandle);S->OnChatMessage.Remove(MetaChatHandle);S->OnCombatFeedback.Remove(CombatFeedbackHandle);}
    StopAll(TEXT("Client closing")); FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    if (DesktopPanel) if (auto* V = GetGameInstance()->GetGameViewportClient()) V->RemoveViewportWidgetContent(DesktopPanel.ToSharedRef());
    RemoveDesktopDock();
    DesktopPanel.Reset(); Plugins.Reset(); Super::Deinitialize();
}
FString UACEPluginSubsystem::UserDirectory() const { return StorageRoot.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("ClientPlugins") : StorageRoot; }
TSharedPtr<FACEClientPlugin> UACEPluginSubsystem::Find(const FString& Id) const
{ for (const auto& P : Plugins) if (P->Id == Id) return P; return nullptr; }
void UACEPluginSubsystem::Discover()
{
    RemoveDesktopDock();
    StopAll(TEXT("Reloading plugins")); Plugins.Reset(); Notice.Empty();
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("ACEClient"));
    TArray<FString> Roots; if (Plugin) Roots.Add(Plugin->GetBaseDir() / TEXT("ClientMods"));
    Roots.Add(UserDirectory() / TEXT("Installed"));
    for (const FString& Root : Roots)
    {
        TArray<FString> Dirs; IFileManager::Get().FindFiles(Dirs, *(Root / TEXT("*")), false, true); Dirs.Sort();
        for (const FString& Dir : Dirs)
        {
            if (Plugins.Num() >= 16) break;
            auto M = Read(Root / Dir / TEXT("plugin.json"));
            const FString Id = String(M, TEXT("id"));
            if (!M || !SafeName(Id) || Find(Id) || Number(M, TEXT("api")) != 1)
            { Notice += TEXT("Skipped invalid, duplicate, or incompatible plugin: ") + Dir + TEXT("\n"); continue; }
            auto P = MakeShared<FACEClientPlugin>(); P->Id = Id; P->Manifest = M;
            P->Name = String(M, TEXT("name"), Id); P->Version = String(M, TEXT("version"));
            P->Description = String(M, TEXT("description")); P->Directory = Root / Dir;
            const TArray<TSharedPtr<FJsonValue>>* Permissions = nullptr;
            bool Valid = M->TryGetArrayField(TEXT("permissions"), Permissions);
            if (Permissions) for (const auto& V : *Permissions)
            {
                const FString Permission = V->AsString();
                if (Permission != TEXT("cast") && Permission != TEXT("combat") && Permission != TEXT("navigation") && Permission != TEXT("inventory") && Permission != TEXT("loot") && Permission != TEXT("say") && Permission != TEXT("fellowship") && Permission != TEXT("confirm")) Valid = false;
                else P->Permissions.AddUnique(Permission);
            }
            if (!Valid) { Notice += TEXT("Unsupported permissions: ") + Id + TEXT("\n"); continue; }
            // Bind consent to the actual capability set, including after plugin upgrades.
            P->Permissions.Sort();
            const FString Grant = FString::Join(P->Permissions, TEXT(","));
            P->Enabled = String(Settings, *Id) == TEXT("enabled:") + Grant;
            if((Id==TEXT("looteditor")||Id==TEXT("waypoint")||Id==TEXT("ucmmicro"))&&!Settings->HasField(Id))P->Enabled=true;
            P->Profile = Read(P->Directory / TEXT("default.json"));
            if (!P->Profile) P->Profile = MakeShared<FJsonObject>();
            Plugins.Add(P);
            const FString Profile = String(Settings, *(Id + TEXT(".profile")), TEXT("Default"));
            if(IFileManager::Get().FileExists(*(UserDirectory()/TEXT("Profiles")/Id/(Profile+TEXT(".json")))))LoadProfile(Id, Profile);
        }
    }
}
void UACEPluginSubsystem::SaveSettings() { if (!Write(UserDirectory() / TEXT("settings.json"), Settings)) Notice = TEXT("Could not save plugin preferences"); }
void UACEPluginSubsystem::SetEnabled(const FString& Id, bool Enabled)
{
    if (auto P = Find(Id))
    {
        Stop(Id); P->Enabled = Enabled;
        Settings->SetStringField(Id, Enabled ? TEXT("enabled:") + FString::Join(P->Permissions, TEXT(",")) : TEXT("disabled")); SaveSettings();
    }
}
void UACEPluginSubsystem::ToggleUCM()
{
    if(auto P=Find(TEXT("ucm")))
    {if(P->Running)Stop(P->Id,TEXT("Stopped"),true);else Start(P->Id);}
}
bool UACEPluginSubsystem::Start(const FString& Id)
{
    auto P = Find(Id); auto* C = GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    if (!P || !P->Enabled || C->GetSessionState() != EACESessionState::InWorld || !C->GetPlayerVitalsView().bValid || (C->GetPlayerVitalsView().Health <= 0 && !P->Profile->HasField(TEXT("vt_meta"))))
    { Notice = TEXT("Enable the plugin and log into a living character before starting."); return false; }
    if(Id==TEXT("looteditor")||Id==TEXT("waypoint")||Id==TEXT("ucmmicro")){Notice=TEXT("This plugin has no automation to start. Open its window from the plugin bar.");return false;}
    StopAll(TEXT("Another plugin started"));
    FString Source, Error;
    const FString Path = P->Directory / TEXT("main.lua");
    if (IFileManager::Get().FileSize(*Path) > 256 * 1024 || !FFileHelper::LoadFileToString(Source, *Path))
    { P->Status = TEXT("Cannot read main.lua (maximum 256 KiB)"); return false; }
    const uint32 SourceHash=GetTypeHash(Source),ProfileHash=GetTypeHash(Encode(P->Profile));
    const bool Resume=P->CanResumeMeta&&P->VM.IsValid()&&P->VMSession.Pin()==C->GetSession()
        &&P->Player==C->GetPlayerGuid()&&P->Server==C->GetServerName()
        &&P->VMSourceHash==SourceHash&&P->VMProfileHash==ProfileHash;
    if(!Resume)
    {
        P->VM = MakeShared<FACEPluginVM>();
        if (!P->VM->Load(Source, Error)) { P->Status = Error; P->VM.Reset();P->CanResumeMeta=false;return false; }
    }
    P->CanResumeMeta=false;P->ResumeMetaPending=Resume;P->VMSourceHash=SourceHash;P->VMProfileHash=ProfileHash;P->VMSession=C->GetSession();
    P->Running = true; P->ActivityFailure.Reset(); P->Player = C->GetPlayerGuid(); P->Server = C->GetServerName();
    if(Id==TEXT("ucm")){IdleManaVM.Reset();IdleManaFailed=false;bRouteJoinRequested=true;RouteVisibilityOffset=0;}
    if(Id==TEXT("ucm")){PhysicalAttackTarget=0;PhysicalAttackOwner.Empty();CombatOutcomes.Empty();ClearBuffRequests();if(!Resume){RuntimeRoute.Reset();RoutePoint=1;}RouteRebuiltAt=0;}
    CachedSpells.Reset();
    P->NextAction = 0; P->NextDecision=0;P->WaitAction.Empty();P->Status = TEXT("Running"); Notice.Empty(); return true;
}
bool UACEPluginSubsystem::RequestForceBuff()
{
    auto P=Find(TEXT("ucm"));if(!P)return false;
    if(ForceBuffRequest)return true; // Repeated clicks must not restart completed families.
    const bool WasRunning=P->Running&&!bUCMCommandOnly;
    if(!WasRunning&&!Start(P->Id))return false;
    bForceBuffOnly=!WasRunning;ForceBuffRequest=++ForceBuffSerial;
    if(!ForceBuffRequest)ForceBuffRequest=++ForceBuffSerial;
    MovementOwner.Empty();MoveExpires=0;P->NextDecision=0;
    P->Status=TEXT("Force Buff queued; refreshing enabled buffs");return true;
}
bool UACEPluginSubsystem::RunUCMCommand(const FString& Arguments)
{
    const FString Args=Arguments.TrimStartAndEnd();
    if(Args.IsEmpty()){TogglePluginWindow(TEXT("ucm"));return true;}
    if(Args.Equals(TEXT("stop"),ESearchCase::IgnoreCase)){Stop(TEXT("ucm"),TEXT("Stopped"),true);Notice=TEXT("UCM stopped.");return true;}
    if(Args.Equals(TEXT("start"),ESearchCase::IgnoreCase))return Start(TEXT("ucm"));
    if(Args.Equals(TEXT("help"),ESearchCase::IgnoreCase))
    {Notice=TEXT("/ucm start | stop | forcebuff | cancelforcebuff | jump <heading> <true/false> <milliseconds> [forward/strafeleft/straferight] | tapjump | opt set <option> <value> | enablecombat/enablebuffing/enablenav/enablelooting <true/false> | meta/nav/loot/looting/settings load <file> | setmetastate <state> | mexec <expression> | echo <text>");return true;}
    TArray<FString> Issues;const auto Command=ACEVTProfile::CompileCommand(TEXT("/ucm ")+Args,Issues);
    if(!Command||Issues.Num()){Notice=FString::Join(Issues,TEXT("\n"));return false;}
    auto P=Find(TEXT("ucm"));if(!P||!P->Enabled){Notice=TEXT("Enable UCM before using its commands.");return false;}
    const FString Op=String(Command,TEXT("op"));
    if(Op==TEXT("load"))
    {
        const FString Requested=String(Command,TEXT("profile"));
        for(const auto& File:GetProfileFiles(String(Command,TEXT("kind"))))if(File.Equals(Requested,ESearchCase::IgnoreCase))return SelectProfileFile(File);
        Notice=TEXT("Profile not found in the configured folder: ")+Requested;return false;
    }
    if(Op==TEXT("echo")){Notice=String(Command,TEXT("text"));return true;}
    if(Op==TEXT("forcebuff"))
    {
        if(Command->GetBoolField(TEXT("value")))return RequestForceBuff();
        CancelForceBuff();if(!P->Running)return true;
        // Also cancel a force-buff initiated by an imported meta command.
    }
    if(Op==TEXT("option")&&!P->Running)
    {
        auto Updated=MakeShared<FJsonObject>(*P->Profile);
        Updated->SetField(String(Command,TEXT("key")),Command->TryGetField(TEXT("value")));
        return SaveProfile(TEXT("ucm"),P->ProfileName,Encode(Updated));
    }
    if(Op==TEXT("state")&&!P->Running){Notice=TEXT("Start UCM before changing its active meta state.");return false;}
    if(UCMCommands.Num()>=64){Notice=TEXT("UCM command queue is full.");return false;}
    if(!P->Running)
    {
        if(!Start(TEXT("ucm")))return false;
        bUCMCommandOnly=true; // A manual jump must not also start hunting or looting.
    }
    UCMCommands.Add(MakeShared<FJsonValueObject>(Command));P->NextDecision=0;
    Notice=TEXT("Queued ")+ACEVTProfile::NativeCommand(TEXT("/ucm ")+Args);return true;
}
void UACEPluginSubsystem::CancelForceBuff()
{
    ForceBuffRequest=0;
    if(bForceBuffOnly)Stop(TEXT("ucm"),TEXT("Force Buff cancelled"),true);
    bForceBuffOnly=false;
}
void UACEPluginSubsystem::Stop(const FString& Id, const FString& Reason, bool PreserveMeta)
{
    if (auto P = Find(Id))
    {
        if (P->Running)
        {
            auto* C = GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
            if (C && C->GetSession()) C->GetSession()->SendCancelAttack();
        }
        P->CanResumeMeta=PreserveMeta&&Id==TEXT("ucm")&&P->VM.IsValid()&&P->Profile->HasField(TEXT("vt_meta"));
        P->Running = false;P->WaitAction.Empty();P->ResumeMetaPending=false;if(!P->CanResumeMeta)P->VM.Reset();P->Status=Reason;
        if(Id==TEXT("ucm"))if(auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>())C->SetPluginFellowshipUpdates(false);
        if(Id==TEXT("ucm")){ClearBuffRequests();ForceBuffRequest=0;bForceBuffOnly=false;bUCMCommandOnly=false;UCMCommands.Reset();if(!P->CanResumeMeta){RuntimeRoute.Reset();RoutePoint=1;}RouteRebuiltAt=0;}
        if(auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
        {PC->CancelPluginJump();if(UseApproachOwner==Id){PC->EndUseApproach();UseApproachOwner.Empty();}}
        if (MovementOwner == Id) { MovementOwner.Empty(); MoveExpires = 0; }
        if (FastCastOwner == Id) FastCastOwner.Empty();
        if (PendingSpellOwner == Id) { PendingSpell=0; PendingSpellOwner.Empty(); PendingSpellActivity.Empty(); }
        OffensiveCasts.RemoveAll([&](const auto& E){return E.Owner==Id;});
    }
}
void UACEPluginSubsystem::StopAll(const FString& Reason, bool PreserveMeta) { for (const auto& P : Plugins) if (P->Running) Stop(P->Id, Reason, PreserveMeta); }
FString UACEPluginSubsystem::ProfileJson(const FString& Id) const { auto P = Find(Id); return P ? Encode(P->Profile) : FString(); }
bool UACEPluginSubsystem::SaveProfile(const FString& Id, const FString& Name, const FString& Json, bool bStopRunning)
{
    auto P = Find(Id); TSharedPtr<FJsonObject> O;
    if (!P || !SafeName(Name) || Json.Len() > 8 * 1024 * 1024 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), O) || !O)
    { Notice = TEXT("Use a profile name of letters, numbers, _ or -, and valid JSON (maximum 1 MiB)."); return false; }
    if(Id==TEXT("ucm")&&!ValidUCMProfile(O)){Notice=TEXT("Invalid UCM profile: check rule types, name lengths (256 bytes), nonnegative values and minimum/maximum ranges. Routes/states must contain objects and item/spell lists numeric IDs.");return false;}
    // Invalidate a cooperative loot cursor even when edited rules have the same count.
    if(Id==TEXT("ucm"))O->SetNumberField(TEXT("loot_revision"),Number(P->Profile,TEXT("loot_revision"))+1);
    if (!Write(UserDirectory() / TEXT("Profiles") / Id / (Name + TEXT(".json")), O)) { Notice = TEXT("Profile could not be saved"); return false; }
    if(bStopRunning) Stop(Id, TEXT("Profile changed; press Start to resume")); P->Profile = O; P->ProfileName = Name;
    Settings->SetStringField(Id + TEXT(".profile"), Name); SaveSettings(); Notice = TEXT("Profile saved"); return true;
}
bool UACEPluginSubsystem::LoadProfile(const FString& Id, const FString& Name)
{
    auto P = Find(Id); if (!P || !SafeName(Name)) return false;
    auto O = Read(UserDirectory() / TEXT("Profiles") / Id / (Name + TEXT(".json")));
    if (!O || (Id==TEXT("ucm")&&!ValidUCMProfile(O))) {Notice=TEXT("Profile is missing or has invalid UCM data");return false;}
    Stop(Id); P->Profile = O; P->ProfileName = Name; Settings->SetStringField(Id + TEXT(".profile"), Name); SaveSettings(); return true;
}
TSharedPtr<FJsonObject> UACEPluginSubsystem::InspectLegacyProfile(const FString& Path)
{
    const int64 Bytes=IFileManager::Get().FileSize(*Path);FString Source,Error;
    if(Bytes<0||Bytes>4*1024*1024||!FFileHelper::LoadFileToString(Source,*Path)){Notice=TEXT("Cannot read profile (maximum 4 MiB).");return nullptr;}
    auto D=ACEVTProfile::Read(Source,FPaths::GetExtension(Path),Error);
    if(!D)Notice=Error;
    return D;
}
bool UACEPluginSubsystem::SaveLegacyLoot(const FString& Name,const TSharedPtr<FJsonObject>& Document)
{
    if(!SafeName(Name)){Notice=TEXT("Choose a filename using letters, numbers, _ or - (maximum 64).");return false;}
    FString Error;const FString Source=ACEVTProfile::WriteLoot(Document,Error);if(!Error.IsEmpty()){Notice=Error;return false;}
    const FString Path=UserDirectory()/TEXT("LegacyLootProfiles")/(Name+TEXT(".utl"));IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    if(!FFileHelper::SaveStringToFile(Source,*(Path+TEXT(".tmp")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)||!IFileManager::Get().Move(*Path,*(Path+TEXT(".tmp")),true,true)){Notice=TEXT("Could not save UTL copy.");return false;}
    Notice=TEXT("Saved UTL copy: ")+Path+TEXT(". Automation has not changed.");return true;
}
FString UACEPluginSubsystem::GetProfileFolder() const
{
    FString Folder;if(Settings)Settings->TryGetStringField(TEXT("ucm.folder"),Folder);
    return Folder.IsEmpty()?UserDirectory()/TEXT("ImportInbox"):Folder;
}
bool UACEPluginSubsystem::SetProfileFolder(const FString& Folder)
{
    const FString Full=FPaths::ConvertRelativePathToFull(Folder.TrimStartAndEnd());
    if(Folder.TrimStartAndEnd().IsEmpty()||!IFileManager::Get().DirectoryExists(*Full)){Notice=TEXT("Profile folder does not exist.");return false;}
    Settings->SetStringField(TEXT("ucm.folder"),Full);SaveSettings();Notice=TEXT("Profile folder refreshed: ")+Full;return true;
}
TArray<FString> UACEPluginSubsystem::GetProfileFiles(const FString& Extension) const
{
    TArray<FString> Files;if(Extension!=TEXT("nav")&&Extension!=TEXT("met")&&Extension!=TEXT("utl")&&Extension!=TEXT("usd"))return Files;
    // Filter ourselves so a folder copied from Windows works on case-sensitive hosts.
    TArray<FString> All;IFileManager::Get().FindFiles(All,*(GetProfileFolder()/TEXT("*")),true,false);
    for(const auto& File:All)if(FPaths::GetExtension(File).Equals(Extension,ESearchCase::IgnoreCase))Files.Add(File);
    Files.Sort();return Files;
}
bool UACEPluginSubsystem::SelectProfileFile(const FString& Filename)
{
    const auto P=Find(TEXT("ucm"));const FString Ext=FPaths::GetExtension(Filename).ToLower();
    if(!P||Filename!=FPaths::GetCleanFilename(Filename)||!GetProfileFiles(Ext).Contains(Filename))return false;
    TArray<FString> Issues;const auto Converted=ACEVTProfile::ConvertFile(GetProfileFolder()/Filename,Issues);
    if(!Converted||Issues.Num()){Notice=TEXT("Cannot load ")+Filename+TEXT(": ")+(Issues.Num()?FString::Join(Issues,TEXT("\n")):TEXT("Invalid profile"));return false;}
    auto Updated=MakeShared<FJsonObject>(*P->Profile);
    if(Ext==TEXT("usd")){const TArray<TSharedPtr<FJsonValue>>* Keys=nullptr;if(Updated->TryGetArrayField(TEXT("usd_keys"),Keys)){const auto Copy=*Keys;for(const auto& Key:Copy)Updated->RemoveField(Key->AsString());}}
    if(Ext==TEXT("nav"))for(const TCHAR* Key:{TEXT("route"),TEXT("loop_route"),TEXT("reverse_route"),TEXT("follow_target"),TEXT("follow_distance")})Updated->RemoveField(Key);
    if(Ext==TEXT("met")){Updated->RemoveField(TEXT("vt_meta"));Updated->RemoveField(TEXT("vt_library"));}
    if(Ext==TEXT("utl")){Updated->RemoveField(TEXT("salvage_policy"));Updated->RemoveField(TEXT("loot_profile"));Updated->RemoveField(TEXT("loot_modified"));}
    for(const auto& Pair:Converted->Values)
    {
        if(Pair.Key==TEXT("vt_library"))
        {auto Library=MakeShared<FJsonObject>();const TSharedPtr<FJsonObject>* Old=nullptr;if(Updated->TryGetObjectField(TEXT("vt_library"),Old))Library->Values=(*Old)->Values;
         for(const auto& Entry:Pair.Value->AsObject()->Values)Library->Values.Add(Entry.Key,Entry.Value);Updated->SetObjectField(Pair.Key,Library);}
        else Updated->Values.Add(Pair.Key,Pair.Value);
    }
    Updated->SetStringField(Ext+TEXT("_source"),GetProfileFolder()/Filename);
    if(Ext==TEXT("nav"))
    {
        Updated->SetBoolField(TEXT("route_preview"),true);
        // Conversion is also used for inactive meta dependencies. A route the
        // user explicitly selects is ready to follow when they press Start.
        Updated->SetBoolField(TEXT("navigation"),true);
    }
    return SaveImportedProfile(FPaths::GetBaseFilename(Filename)+TEXT("_ucm"),Updated,GetProfileFolder()/Filename);
}
bool UACEPluginSubsystem::ClearProfileFile(const FString& Ext)
{
    const auto P=Find(TEXT("ucm"));if(!P||(Ext!=TEXT("nav")&&Ext!=TEXT("met")&&Ext!=TEXT("utl")&&Ext!=TEXT("usd")))return false;
    auto Updated=MakeShared<FJsonObject>(*P->Profile);Updated->RemoveField(Ext+TEXT("_source"));
    if(Ext==TEXT("usd")){const TArray<TSharedPtr<FJsonValue>>* Keys=nullptr;if(Updated->TryGetArrayField(TEXT("usd_keys"),Keys)){const auto Copy=*Keys;for(const auto& Key:Copy)Updated->RemoveField(Key->AsString());}Updated->RemoveField(TEXT("usd_keys"));}
    if(Ext==TEXT("nav")){Updated->SetArrayField(TEXT("route"),{});Updated->SetBoolField(TEXT("navigation"),false);Updated->SetStringField(TEXT("follow_name"),TEXT(""));Updated->SetNumberField(TEXT("follow_id"),0);}
    if(Ext==TEXT("met")){Updated->RemoveField(TEXT("vt_meta"));Updated->RemoveField(TEXT("vt_library"));Updated->SetBoolField(TEXT("meta_enabled"),false);}
    if(Ext==TEXT("utl")){Updated->SetArrayField(TEXT("loot_rules"),{});Updated->RemoveField(TEXT("salvage_policy"));Updated->RemoveField(TEXT("loot_profile"));Updated->RemoveField(TEXT("loot_modified"));Updated->SetBoolField(TEXT("looting"),false);}
    return SaveProfile(TEXT("ucm"),P->ProfileName,Encode(Updated),true);
}
bool UACEPluginSubsystem::ImportLegacyProfile(const FString& Path,const FString& Name)
{
    auto P=Find(TEXT("ucm"));if(!P||!SafeName(Name)){Notice=TEXT("Choose an import name using letters, numbers, _ or - (maximum 64).");return false;}
    auto D=InspectLegacyProfile(Path);if(!D)return false;
    TArray<FString> Issues;auto Converted=ACEVTProfile::ConvertFile(Path,Issues);
    auto Archive=MakeShared<FJsonObject>();Archive->SetObjectField(TEXT("document"),D);Archive->SetObjectField(TEXT("converted"),Converted);
    TArray<TSharedPtr<FJsonValue>> Warnings;for(const auto& Issue:Issues)Warnings.Add(MakeShared<FJsonValueString>(Issue));Archive->SetArrayField(TEXT("issues"),Warnings);
    const FString Report=WriteNewCopy(UserDirectory()/TEXT("Imports"),Name,Archive);
    if(Report.IsEmpty()){Notice=TEXT("Could not save import report.");return false;}
    if(Issues.Num()){Notice=FString::Printf(TEXT("%d compatibility issues. Original data and report saved in Imports/%s.json; active settings were not changed."),Issues.Num(),*Report);return false;}
    auto Updated=MakeShared<FJsonObject>();Updated->Values=P->Profile->Values;
    for(const auto& Pair:Converted->Values)Updated->Values.Add(Pair.Key,Pair.Value);
    return SaveImportedProfile(Name,Updated,Path);
}
bool UACEPluginSubsystem::SaveImportedProfile(const FString& Name,const TSharedPtr<FJsonObject>& Profile,const FString& Source)
{
    Profile->SetStringField(TEXT("import_source"),FPaths::ConvertRelativePathToFull(Source));
    Profile->SetNumberField(TEXT("ucm_format_version"),1);
    if(!ValidUCMProfile(Profile)||Encode(Profile).Len()>8*1024*1024){Notice=TEXT("Converted profile exceeds UCM limits.");return false;}
    const FString Copy=WriteNewCopy(UserDirectory()/TEXT("Profiles/ucm"),Name,Profile);
    if(Copy.IsEmpty()){Notice=TEXT("Could not save a new UCM profile copy.");return false;}
    if(!LoadProfile(TEXT("ucm"),Copy))return false;
    Notice=TEXT("Converted to Profiles/ucm/")+Copy+TEXT(".json. Original files and earlier copies are unchanged. Press Start to run.");return true;
}
bool UACEPluginSubsystem::SaveLootProfile(const FString& Name)
{
    const auto P=Find(TEXT("ucm"));
    if(!P||!SafeName(Name)){Notice=TEXT("Use letters, numbers, _ or - for the loot profile name (maximum 64).");return false;}
    auto O=MakeShared<FJsonObject>();const TArray<TSharedPtr<FJsonValue>>* Rules=nullptr;
    if(P->Profile->TryGetArrayField(TEXT("loot_rules"),Rules))O->SetArrayField(TEXT("loot_rules"),*Rules);
    else O->SetArrayField(TEXT("loot_rules"),{});
    if(P->Profile->HasTypedField<EJson::Object>(TEXT("salvage_policy")))O->SetObjectField(TEXT("salvage_policy"),P->Profile->GetObjectField(TEXT("salvage_policy")));
    if(P->Profile->TryGetArrayField(TEXT("vendor_rules"),Rules))O->SetArrayField(TEXT("vendor_rules"),*Rules);
    O->SetNumberField(TEXT("version"),1);
    if(!ValidUCMProfile(O)||Encode(O).Len()>8*1024*1024||!Write(UserDirectory()/TEXT("LootProfiles")/(Name+TEXT(".json")),O))
    {Notice=TEXT("Loot profile could not be saved; check rule ranges and file access.");return false;}
    Notice=TEXT("Saved loot rules: ")+Name;return true;
}
bool UACEPluginSubsystem::LoadLootProfile(const FString& Name)
{
    const auto P=Find(TEXT("ucm"));if(!P||!SafeName(Name))return false;
    const auto O=Read(UserDirectory()/TEXT("LootProfiles")/(Name+TEXT(".json")));
    const TArray<TSharedPtr<FJsonValue>>* Rules=nullptr;
    if(!O||Encode(O).Len()>8*1024*1024||Number(O,TEXT("version"))!=1||!ValidUCMProfile(O)||!O->TryGetArrayField(TEXT("loot_rules"),Rules))
    {Notice=TEXT("Loot profile is missing, incompatible, or contains invalid rules.");return false;}
    // Copy before saving: a failed load must leave the active profile untouched.
    auto Updated=MakeShared<FJsonObject>();Updated->Values=P->Profile->Values;
    Updated->SetArrayField(TEXT("loot_rules"),*Rules);Updated->SetStringField(TEXT("loot_profile"),Name);
    const TArray<TSharedPtr<FJsonValue>>* Vendors=nullptr;
    Updated->SetArrayField(TEXT("vendor_rules"),O->TryGetArrayField(TEXT("vendor_rules"),Vendors)?*Vendors:TArray<TSharedPtr<FJsonValue>>{});
    Updated->RemoveField(TEXT("utl_source"));Updated->RemoveField(TEXT("loot_modified"));
    Updated->RemoveField(TEXT("salvage_policy"));if(O->HasTypedField<EJson::Object>(TEXT("salvage_policy")))Updated->SetObjectField(TEXT("salvage_policy"),O->GetObjectField(TEXT("salvage_policy")));
    return SaveProfile(TEXT("ucm"),P->ProfileName,Encode(Updated));
}
void UACEPluginSubsystem::RecordWaypoint(const FString& Id)
{
    auto P = Find(Id); auto* C = GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    if (!P || C->GetSessionState() != EACESessionState::InWorld) { Notice = TEXT("Enter the world before recording a waypoint"); return; }
    auto Pos = C->GetPlayerPosition();
    if (auto* PC = Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController())) PC->TryGetLocallyPredictedPosition(Pos);
    const TArray<TSharedPtr<FJsonValue>>* Old = nullptr; TArray<TSharedPtr<FJsonValue>> Points;
    if (P->Profile->TryGetArrayField(TEXT("route"), Old)) Points = *Old;
    if (Points.Num() >= 2048) { Notice = TEXT("Route limit: 2048 waypoints"); return; }
    Points.Add(MakeShared<FJsonValueObject>(Position(Pos))); P->Profile->SetArrayField(TEXT("route"), Points);
    SaveProfile(Id, P->ProfileName, Encode(P->Profile));
}
void UACEPluginSubsystem::AddBuff(const FString& Id, int32 SpellId)
{
    auto P = Find(Id); if (!P) return;
    const TArray<TSharedPtr<FJsonValue>>* Old = nullptr; TArray<TSharedPtr<FJsonValue>> Buffs;
    if (P->Profile->TryGetArrayField(TEXT("buffs"), Old)) Buffs = *Old;
    bool Found = false;
    for (int I = Buffs.Num() - 1; I >= 0; --I) if (Buffs[I]->AsNumber() == SpellId) { Buffs.RemoveAt(I); Found = true; }
    if (!Found) Buffs.Add(MakeShared<FJsonValueNumber>(SpellId));
    P->Profile->SetArrayField(TEXT("buffs"), Buffs); SaveProfile(Id, P->ProfileName, Encode(P->Profile));
}
TSharedPtr<FJsonObject> UACEPluginSubsystem::Snapshot()
{
    auto O = MakeShared<FJsonObject>(); auto* C = GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    auto* D = GetGameInstance()->GetSubsystem<UACEDatSubsystem>(); const auto& V = C->GetPlayerVitalsView();
    O->SetNumberField(TEXT("time"), FPlatformTime::Seconds());
    O->SetNumberField(TEXT("force_buff_request"),ForceBuffRequest);
    O->SetBoolField(TEXT("force_buff_only"),bForceBuffOnly);
    O->SetNumberField(TEXT("health"), V.Health); O->SetNumberField(TEXT("max_health"), V.MaxHealth);
    O->SetNumberField(TEXT("mana"), V.Mana); O->SetNumberField(TEXT("max_mana"), V.MaxMana);
    O->SetNumberField(TEXT("stamina"), V.Stamina); O->SetNumberField(TEXT("max_stamina"), V.MaxStamina);
    O->SetBoolField(TEXT("busy"), C->IsUseBusy() || PendingSpell!=0);
    bool Ready = true;
    for(const auto& P:Plugins)if(P->Running && FPlatformTime::Seconds()<P->NextAction)Ready=false;
    O->SetBoolField(TEXT("ready"), Ready);
    FACEPosition Pos = C->GetPlayerPosition();
    auto* PC = Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    if (PC) {PC->TryGetLocallyPredictedPosition(Pos);if(PC->IsWorldTransitionActive())O->SetBoolField(TEXT("ready"),false);
        if(PC->IsUseApproachActive())O->SetBoolField(TEXT("busy"),true);}
    O->SetObjectField(TEXT("position"), Position(Pos));
    O->SetObjectField(TEXT("server_position"),Position(C->GetPlayerPosition()));
    O->SetBoolField(TEXT("moving"), !MovementOwner.IsEmpty() || (!FastCastOwner.IsEmpty() && FastCastStarted));
    O->SetNumberField(TEXT("heading"),PC&&PC->GetPawn()?PC->GetPawn()->GetActorRotation().Yaw:0);
    const auto Known = C->GetKnownSpells(); TSet<int32> KnownIds(Known);
    auto SpellIds=Known;
    // Profile references may be a higher tier learned by a different character.
    // Include their DAT family metadata, but never treat them as castable.
    for(const auto& P:Plugins)if(P->Running)
    {
        const TArray<TSharedPtr<FJsonValue>>* Buffs=nullptr;
        for(const TCHAR* Key:{TEXT("buffs"),TEXT("excluded_buff_spells")})
            if(P->Profile->TryGetArrayField(Key,Buffs))for(const auto& Buff:*Buffs)SpellIds.AddUnique(int32(Buff->AsNumber()));
        for(const TCHAR* Key:{TEXT("buff_items"),TEXT("item_buff_targets")})
            if(P->Profile->TryGetArrayField(Key,Buffs))for(const auto& Buff:*Buffs)if(Buff->Type==EJson::Object)SpellIds.AddUnique(int32(Number(Buff->AsObject(),TEXT("spell"))));
    }
    SpellIds.Sort();
    const int32 SkillIds[] = {0,34,33,32,31,43}; TArray<int32> SchoolSkills; SchoolSkills.Init(0,UE_ARRAY_COUNT(SkillIds));
    for(const auto& Entry:V.Skills)if(Entry.AdvancementClass>=2)
        for(int32 School=1;School<SchoolSkills.Num();++School)if(Entry.SkillId==SkillIds[School])SchoolSkills[School]=Entry.Current;
    const uint64 Revision=C->GetSession()?C->GetSession()->GetSpellDataRevision():0;
    if(CachedSpells.IsEmpty() || CachedSpellIds!=SpellIds || CachedSchoolSkills!=SchoolSkills || Revision!=CachedSpellRevision)
    {
    CachedSpells.Reset();CachedSpellIds=SpellIds;CachedSchoolSkills=SchoolSkills;CachedSpellRevision=Revision;
    CachedKnownSpellValues.Reset(Known.Num());for(int32 Id:Known)CachedKnownSpellValues.Add(MakeShared<FJsonValueNumber>(Id));
    for (int32 Id : SpellIds)
    {
        uint32 School = 0, Power = 0, Category = 0, Flags = 0; double Duration = 0;
        if (!D->TryGetPluginSpellInfo(Id, School, Power, Category, Flags, Duration)) continue;
        auto S = MakeShared<FJsonObject>(); S->SetNumberField(TEXT("id"), Id);
        S->SetBoolField(TEXT("known"), KnownIds.Contains(Id));
        TMap<FString,int32> ScarabCounts;auto Scarabs=MakeShared<FJsonObject>();
        S->SetBoolField(TEXT("components_known"),D->TryGetPluginSpellScarabs(Id,ScarabCounts));
        for(const auto& Pair:ScarabCounts)Scarabs->SetNumberField(Pair.Key,Pair.Value);
        S->SetObjectField(TEXT("scarabs"),Scarabs);
        uint32 Level=0,LevelSchool=0;D->TryGetSpellSchoolAndLevel(Id,LevelSchool,Level);
        S->SetNumberField(TEXT("level"),Level);
        S->SetNumberField(TEXT("school"), School); S->SetNumberField(TEXT("power"), Power); S->SetNumberField(TEXT("category"), Category);
        FString SpellName;uint32 Icon=0,TargetFlags=0,TargetType=0;bool Projectile=false;
        D->TryGetSpellInfo(Id,SpellName,Icon);D->TryGetRetailSpellTargeting(Id,TargetFlags,TargetType,Projectile);
        S->SetStringField(TEXT("name"),SpellName);S->SetNumberField(TEXT("icon"),Icon);
        S->SetNumberField(TEXT("target_type"),TargetType);S->SetNumberField(TEXT("flags"),Flags);
        S->SetNumberField(TEXT("duration"),Duration);S->SetBoolField(TEXT("projectile"),Projectile);
        S->SetBoolField(TEXT("beneficial"),(Flags&4)!=0);S->SetBoolField(TEXT("caster_target"),(Flags&8)!=0);
        S->SetBoolField(TEXT("self_buff"), (Flags & 8) && (Flags & 4) && Duration > 0);
        S->SetNumberField(TEXT("skill"), SchoolSkills.IsValidIndex(School)?SchoolSkills[School]:0);
        CachedSpells.Add(MakeShared<FJsonValueObject>(S));
    }
    }
    O->SetArrayField(TEXT("spells"), CachedSpells);
    O->SetArrayField(TEXT("known_spells"),CachedKnownSpellValues);
    TArray<TSharedPtr<FJsonValue>> Enchantments,Harmful;auto Cooldowns=MakeShared<FJsonObject>();
    for (const auto& E : C->GetActiveEnchantments())
    {
        if(E.bCooldown)Cooldowns->SetNumberField(FString::FromInt(uint32(E.SpellId)&0x7fff),E.Duration<0?1.e9:FMath::Max(0.,E.Duration+E.StartTime-(FPlatformTime::Seconds()-E.ReceivedAt)));
        if(!E.bCooldown&&!E.bBeneficial&&E.Duration>=0)
        {
            uint32 School=0,Power=0,Category=0,Flags=0;double Duration=0;
            const double Remaining=E.Duration+E.StartTime-(FPlatformTime::Seconds()-E.ReceivedAt);
            if(Remaining>0&&D->TryGetPluginSpellInfo(E.SpellId,School,Power,Category,Flags,Duration)&&!(Flags&8))
            {
                auto A=MakeShared<FJsonObject>();A->SetNumberField(TEXT("id"),E.SpellId);A->SetNumberField(TEXT("category"),E.SpellCategory);
                A->SetNumberField(TEXT("power"),Power);A->SetNumberField(TEXT("remaining"),Remaining);Harmful.Add(MakeShared<FJsonValueObject>(A));
            }
        }
        if (!E.bBeneficial || E.bCooldown) continue;
        auto A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("id"), E.SpellId); A->SetNumberField(TEXT("category"), E.SpellCategory);
        A->SetNumberField(TEXT("power"), E.PowerLevel);
        A->SetNumberField(TEXT("remaining"), E.Duration < 0 ? 1.e9 : FMath::Max(0., E.Duration + E.StartTime - (FPlatformTime::Seconds() - E.ReceivedAt)));
        Enchantments.Add(MakeShared<FJsonValueObject>(A));
    }
    O->SetArrayField(TEXT("enchantments"), Enchantments);
    O->SetArrayField(TEXT("harmful_enchantments"),Harmful);
    O->SetObjectField(TEXT("cooldowns"),Cooldowns);
    const auto Selection=C->GetSelectedObject();
    const int32 Dead=Selection.bShowHealth && Selection.HealthFraction<=0 ? Selection.Guid:0;
    const int32 Target = PC ? PC->FindNearbyTarget(true,0,Dead) : 0; FACEWorldObject Obj;
    O->SetNumberField(TEXT("nearest"), uint32(Target));
    if (Target && C->GetWorldObject(Target, Obj)) O->SetNumberField(TEXT("distance"), FVector::Distance(Pos.ToUnrealLocation(), Obj.Position.ToUnrealLocation()) / 100.);
    ExtendSnapshot(O);
    return O;
}
void UACEPluginSubsystem::CheckPendingSpellTimeout()
{
    if(!PendingSpell||FPlatformTime::Seconds()-PendingSpellAt<=30)return;
    auto Owner=Find(PendingSpellOwner);
    auto Intent=MakeShared<FJsonObject>();Intent->SetStringField(TEXT("action"),TEXT("cast"));
    Intent->SetStringField(TEXT("activity"),PendingSpellActivity);
    PendingSpell=0;PendingSpellOwner.Empty();PendingSpellActivity.Empty();FastCastOwner.Empty();
    if(Owner&&Owner->Running)ReportActivityFailure(*Owner,Intent,TEXT("Cast confirmation timed out; check connection and spell requirements"));
}
bool UACEPluginSubsystem::Tick(float)
{
    UpdateDesktopDock();
    RefreshAutomationData();
    DrawRoute();
    auto* C = GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    CheckPendingSpellTimeout();
    // VT ManaChargesWhenOff is deliberately independent of Start/Stop. Use a
    // separate VM restricted to equipment recharge; no meta/combat/nav can run.
    if(auto P=Find(TEXT("ucm"));P&&P->Enabled&&!P->Running&&C->GetSessionState()==EACESessionState::InWorld&&C->GetPlayerVitalsView().Health>0)
    {
        bool Enabled=false;P->Profile->TryGetBoolField(TEXT("mana_charges_when_off"),Enabled);
        const double Now=FPlatformTime::Seconds();
        if(Enabled&&!Plugins.ContainsByPredicate([](const auto& Other){return Other->Running;})&&Now>=NextIdleManaDecision)
        {
            NextIdleManaDecision=Now+2;
            const uint32 Hash=GetTypeHash(Encode(P->Profile));
            if(IdleManaSession.Pin()!=C->GetSession()||IdleManaPlayer!=C->GetPlayerGuid()||Hash!=IdleManaProfileHash)
            {IdleManaVM.Reset();IdleManaFailed=false;IdleManaSession=C->GetSession();IdleManaPlayer=C->GetPlayerGuid();IdleManaProfileHash=Hash;}
            if(!IdleManaFailed)
            {
                FString Error,Source;
                if(!IdleManaVM)
                {
                    IdleManaVM=MakeShared<FACEPluginVM>();
                    if(!FFileHelper::LoadFileToString(Source,*(P->Directory/TEXT("main.lua")))||!IdleManaVM->Load(Source,Error))IdleManaFailed=true;
                }
                if(!IdleManaFailed)
                {
                    auto Profile=MakeShared<FJsonObject>(*P->Profile);Profile->RemoveField(TEXT("vt_library"));Profile->SetBoolField(TEXT("ucm_mana_only"),true);
                    TSharedPtr<FJsonObject> Intent;
                    if(!IdleManaVM->Step(Snapshot(),Profile,Intent,Error))IdleManaFailed=true;
                    else if(Intent)
                    {
                        const FString Action=String(Intent,TEXT("action"));
                        if(Action==TEXT("stop")){IdleManaFailed=true;P->Status=String(Intent,TEXT("status"));}
                        else if(Action==TEXT("use_item")||Action==TEXT("apply_item"))Execute(*P,Intent);
                    }
                }
                if(!Error.IsEmpty())P->Status=TEXT("Idle mana recharge stopped: ")+Error;
            }
        }
        else if(!Enabled){IdleManaVM.Reset();IdleManaFailed=false;}
    }
    for (const auto& P : Plugins) if (P->Running)
    {
        if (C->GetSessionState() != EACESessionState::InWorld || C->GetPlayerGuid() != P->Player || C->GetServerName() != P->Server || (C->GetPlayerVitalsView().Health <= 0 && !P->Profile->HasField(TEXT("vt_meta"))))
        { Stop(P->Id, TEXT("Stopped: character died, changed, or left world")); continue; }
        // UCM cannot issue a second action while the first is pending. Avoid
        // copying a large spellbook/inventory into Lua just to return "waiting".
        RefreshActionWait(*P);
        if(FPlatformTime::Seconds()<P->NextDecision)continue;
        auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
        if(PC&&!PC->IsUseApproachActive())UseApproachOwner.Empty();
        if(P->Id==TEXT("ucm") && !P->Profile->HasField(TEXT("vt_meta")) && (C->IsUseBusy() || PendingSpell || FPlatformTime::Seconds()<P->NextAction || (PC&&PC->IsUseApproachActive())))continue;
        TSharedPtr<FJsonObject> Intent; FString Error;
        auto TickProfile=MakeShared<FJsonObject>(*P->Profile);TickProfile->RemoveField(TEXT("vt_library"));
        if(P->Id==TEXT("ucm")){TickProfile->SetArrayField(TEXT("ucm_commands"),UCMCommands);TickProfile->SetBoolField(TEXT("ucm_command_only"),bUCMCommandOnly);UCMCommands.Reset();}
        TickProfile->SetBoolField(TEXT("ucm_resume"),P->ResumeMetaPending);P->ResumeMetaPending=false;
        if(P->ActivityFailure){TickProfile->SetObjectField(TEXT("ucm_activity_failure"),P->ActivityFailure);P->ActivityFailure.Reset();}
        if (!P->VM->Step(Snapshot(), TickProfile, Intent, Error)) { Stop(P->Id, TEXT("Script error: ") + Error); continue; }
        if (Intent) Execute(*P, Intent);
        // Idle/blocked policies need not rebuild world sight and inventory every frame.
        bool ContinueWork=false;if(Intent)Intent->TryGetBoolField(TEXT("continue_work"),ContinueWork);
        P->NextDecision=FPlatformTime::Seconds()+(ContinueWork?.05:.25);
    }
    return true;
}
void UACEPluginSubsystem::ReportActivityFailure(FACEClientPlugin& P, const TSharedPtr<FJsonObject>& I, const FString& Reason, bool NotifyPolicy)
{
    if(P.Id!=TEXT("ucm")){Stop(P.Id,Reason);return;}
    FString Activity=String(I,TEXT("activity"));
    if(Activity.IsEmpty())
    {
        // Legacy intents have no activity tag. New policy intents always carry
        // one, since equip/cast/identify may belong to several activities.
        const FString Action=String(I,TEXT("action"));
        if(Action==TEXT("buy")||Action==TEXT("sell_note")||Action==TEXT("split_note"))Activity=TEXT("vendors");
        else if(Action==TEXT("combine_salvage"))Activity=TEXT("combine");
        else if(Action==TEXT("loot")||Action==TEXT("salvage")||Action==TEXT("read")||Action==TEXT("sell")||Action==TEXT("open_corpse")||Action==TEXT("close_corpse"))Activity=TEXT("loot");
        else if(Action==TEXT("store_item")||Action==TEXT("merge"))Activity=TEXT("inventory");
        else if(Action==TEXT("attack"))Activity=TEXT("combat");
        else if(Action==TEXT("move")||Action==TEXT("jump")||I->HasField(TEXT("runtime_route")))Activity=TEXT("navigation");
        else Activity=TEXT("meta");
    }
    P.Status=NotifyPolicy?Activity+TEXT(" failed: ")+Reason+TEXT(". UCM remains active"):Reason;
    Notice=P.Status;
    if(NotifyPolicy)
    {
        P.ActivityFailure=MakeShared<FJsonObject>();
        P.ActivityFailure->SetStringField(TEXT("activity"),Activity);
        P.ActivityFailure->SetStringField(TEXT("status"),Reason);
    }
    if(MovementOwner==P.Id){MovementOwner.Empty();MoveExpires=0;FaceHeading.Reset();}
    if(auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
    {if(UseApproachOwner==P.Id)PC->EndUseApproach();if(Activity==TEXT("navigation"))PC->CancelPluginJump();}
    if(UseApproachOwner==P.Id)UseApproachOwner.Empty();
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    if(C&&C->GetSession())
    {
        if(Activity==TEXT("combat")){C->GetSession()->SendCancelAttack();PhysicalAttackOwner.Empty();PhysicalAttackTarget=0;}
        if(Activity==TEXT("loot"))
        {
            const int32 Container=C->GetOpenExternalContainerGuid();FACEWorldObject Object;
            if(Container&&C->GetWorldObject(Container,Object)&&Object.IsCorpse())
                C->SendNoLongerViewingContents(Container);
        }
    }
    P.NextAction=0;P.NextDecision=0;
}
void UACEPluginSubsystem::Execute(FACEClientPlugin& P, const TSharedPtr<FJsonObject>& I)
{
    if(P.Id==TEXT("ucm"))I->TryGetBoolField(TEXT("route_join_pending"),bRouteJoinRequested);
    if(P.Id==TEXT("ucm") && I->HasField(TEXT("route_point")))RoutePoint=FMath::Clamp(int32(Number(I,TEXT("route_point"),1)),1,2049);
    if(P.Id==TEXT("ucm")&&I->HasField(TEXT("runtime_route")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Route=nullptr;
        if(!I->TryGetArrayField(TEXT("runtime_route"),Route)||Route->Num()>2048){ReportActivityFailure(P,I,TEXT("Invalid runtime route"));return;}
        for(const auto& V:*Route)if(V->Type!=EJson::Object){ReportActivityFailure(P,I,TEXT("Invalid runtime waypoint"));return;}
        RuntimeRoute=MakeShared<FJsonObject>();RuntimeRoute->SetArrayField(TEXT("route"),*Route);RouteRebuiltAt=0;MovementOwner.Empty();MoveExpires=0;
        bool Loop=false,Reverse=false;I->TryGetBoolField(TEXT("runtime_loop_route"),Loop);I->TryGetBoolField(TEXT("runtime_reverse_route"),Reverse);
        RuntimeRoute->SetBoolField(TEXT("loop_route"),Loop);RuntimeRoute->SetBoolField(TEXT("reverse_route"),Reverse);
    }
    const FString MetaState=String(I,TEXT("meta_state"));
    if(!MetaState.IsEmpty())P.MetaState=MetaState;
    const FString Status = String(I, TEXT("status")); if (!Status.IsEmpty()) P.Status = Status;
    const FString Action = String(I, TEXT("action"));
    if(P.Id==TEXT("ucm")&&Action==TEXT("activity_failed")){ReportActivityFailure(P,I,Status,false);return;}
    if(P.Id==TEXT("ucm"))if(auto* Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>()){bool Updates=false;I->TryGetBoolField(TEXT("helper_updates"),Updates);Client->SetPluginFellowshipUpdates(Updates);}
    if(P.Id==TEXT("ucm")&&Action==TEXT("force_buff_done"))
    {
        if(ForceBuffRequest&&Number(I,TEXT("request"))==ForceBuffRequest)
        {
            ForceBuffRequest=0;
            if(bForceBuffOnly)Stop(P.Id,Status.IsEmpty()?TEXT("Force Buff complete"):Status,true);
            bForceBuffOnly=false;
        }
        return;
    }
    if(P.Id==TEXT("ucm")&&Action==TEXT("notice")){Notice=String(I,TEXT("text")).Left(1024);return;}
    if(P.Id==TEXT("ucm")&&Action==TEXT("pause_navigation"))
    {
        // A blocked route must not discard the VM or stop combat/recovery.
        // Reflect the safety pause in the same switch used by UCM and Micro.
        P.Profile->SetBoolField(TEXT("navigation"),false);
        if(MovementOwner==P.Id){MovementOwner.Empty();MoveExpires=0;FaceHeading.Reset();}
        if(auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
        {PC->CancelPluginJump();if(UseApproachOwner==P.Id)PC->EndUseApproach();}
        if(UseApproachOwner==P.Id)UseApproachOwner.Empty();
        bRouteJoinRequested=true;RouteVisibilityOffset=0;P.NextAction=0;P.NextDecision=0;
        Notice=Status;
        return;
    }
    if(P.Id==TEXT("ucm") && Action==TEXT("buff_request_start"))
    {
        if(BuffRequests.Num()&&uint32(Number(I,TEXT("request")))==uint32(Number(BuffRequests[0]->AsObject(),TEXT("id"))))
        {
            auto Request=BuffRequests[0]->AsObject();bool Started=false;Request->TryGetBoolField(TEXT("started"),Started);
            if(!Started){Request->SetBoolField(TEXT("started"),true);ReplyBuffRequest(Guid(Request,TEXT("player")),String(Request,TEXT("name")),TEXT("UCM: it is your turn for buffs. Please stay nearby with your equipment on."));}
        }
        return;
    }
    if(P.Id==TEXT("ucm") && Action==TEXT("buff_request_done"))
    {
        const uint32 Request=uint32(Number(I,TEXT("request")));
        if(BuffRequests.Num() && uint32(Number(BuffRequests[0]->AsObject(),TEXT("id")))==Request)
        {const auto Completed=BuffRequests[0]->AsObject();ReplyBuffRequest(Guid(Completed,TEXT("player")),String(Completed,TEXT("name")),TEXT("UCM: ")+Status);BuffRequests.RemoveAt(0);BuffRequestStatus=Status;Notice=Status;NotifyBuffQueuePositions();}
        return;
    }
    if (Action.IsEmpty()) return;
    if (Action == TEXT("stop")) { bool Preserve=false;I->TryGetBoolField(TEXT("preserve_meta"),Preserve);Stop(P.Id, Status.IsEmpty() ? TEXT("Stopped by plugin") : Status,Preserve);return; }
    if(Action==TEXT("profile_load")&&P.Id==TEXT("ucm"))
    {
        const FString Name=String(I,TEXT("profile")),Kind=String(I,TEXT("kind"));const TSharedPtr<FJsonObject>* Library=nullptr;
        if(!TArray<FString>{TEXT("met"),TEXT("nav"),TEXT("utl"),TEXT("usd")}.Contains(Kind)||!Name.EndsWith(TEXT(".")+Kind)){ReportActivityFailure(P,I,TEXT("Invalid profile load type"));return;}
        if(!P.Profile->TryGetObjectField(TEXT("vt_library"),Library)||!(*Library)->HasTypedField<EJson::Object>(Name))
        {ReportActivityFailure(P,I,TEXT("Referenced profile was not validated during import: ")+Name);return;}
        const auto Loaded=(*Library)->GetObjectField(Name);auto Updated=MakeShared<FJsonObject>(*P.Profile);
        if(Kind==TEXT("usd")){const TArray<TSharedPtr<FJsonValue>>* Keys=nullptr;if(Updated->TryGetArrayField(TEXT("usd_keys"),Keys)){const auto Copy=*Keys;for(const auto& Key:Copy)Updated->RemoveField(Key->AsString());}}
        if(Kind==TEXT("utl")){Updated->RemoveField(TEXT("salvage_policy"));Updated->RemoveField(TEXT("loot_profile"));Updated->RemoveField(TEXT("loot_modified"));}
        // Loading a dependency replaces its route, not the meta's current
        // navigation switch. Converted files are deliberately inactive.
        for(const auto& Pair:Loaded->Values)if(Kind!=TEXT("nav")||Pair.Key!=TEXT("navigation"))Updated->Values.Add(Pair.Key,Pair.Value);
        Updated->SetStringField(Kind+TEXT("_source"),Name);
        Updated->SetNumberField(TEXT("vt_revision"),Number(P.Profile,TEXT("vt_revision"))+1);Updated->SetStringField(TEXT("vt_loaded_kind"),Kind);
        if(Kind==TEXT("usd")){auto* Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();if(Client&&Client->GetSession())Client->GetSession()->SendCancelAttack();}
        P.Profile=Updated;RuntimeRoute.Reset();RouteRebuiltAt=0;bRouteJoinRequested=true;MovementOwner.Empty();MoveExpires=0;return;
    }
    const FString Permission = ((Action == TEXT("move") || Action == TEXT("face") || Action == TEXT("jump") || Action == TEXT("recall") || Action == TEXT("use_world") || Action == TEXT("select") || Action == TEXT("logout")) ? TEXT("navigation") : (Action == TEXT("attack") || Action == TEXT("cancel_attack") || Action == TEXT("combat_mode")) ? TEXT("combat") : (Action == TEXT("store_item") || Action == TEXT("give") || Action == TEXT("apply_item") || Action == TEXT("equip") || Action == TEXT("use_item") || Action == TEXT("identify") || Action == TEXT("merge")) ? TEXT("inventory") : (Action == TEXT("buy") || Action == TEXT("split_note") || Action == TEXT("sell_note") || Action == TEXT("combine_salvage") || Action == TEXT("salvage") || Action == TEXT("sell") || Action == TEXT("read") || Action == TEXT("loot") || Action == TEXT("open_corpse") || Action == TEXT("close_corpse")) ? TEXT("loot") : Action);
    if (!P.Permissions.Contains(Action==TEXT("attack_bar")?TEXT("combat"):Permission)) { ReportActivityFailure(P,I, TEXT("Action not permitted: ") + Action); return; }
    auto* C = GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    auto* PC = Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    if (!PC || !C->GetSession() || (C->IsUseBusy() && Action!=TEXT("confirm")) || PC->IsWorldTransitionActive()) return;
    const double Now = FPlatformTime::Seconds();
    if (Now < P.NextAction) return;
    P.NextAction = Now + .5;
    P.WaitAction.Empty();
    if(Action!=TEXT("move")&&Action!=TEXT("face")){MovementOwner.Empty();MoveExpires=0;FaceHeading.Reset();}
    if(Action==TEXT("confirm"))
    {
        bool Accept=false;const double Type=Number(I,TEXT("type"),-1),Context=Number(I,TEXT("context"),-1);
        if(Type<0||Type>MAX_uint32||Context<0||Context>MAX_uint32||FMath::FloorToDouble(Type)!=Type||FMath::FloorToDouble(Context)!=Context||!I->TryGetBoolField(TEXT("accept"),Accept)||!C->GetSession()->RespondToConfirmation(uint32(Type),uint32(Context),Accept))ReportActivityFailure(P,I,TEXT("Server confirmation changed or disappeared"));
        return;
    }
    if (ExecuteInventory(P,I,Action)) return;
    if(Action==TEXT("face")){MovementOwner=P.Id;FaceHeading=float(Number(I,TEXT("heading")));MoveExpires=Now+1.5;return;}
    if(Action==TEXT("fellowship"))
    {
        const FString Op=String(I,TEXT("operation")),Name=String(I,TEXT("name"));
        if(Op==TEXT("quit")||Op==TEXT("disband"))C->SendFellowshipQuit(Op==TEXT("disband"));
        else if(Op==TEXT("open")||Op==TEXT("close"))C->SendFellowshipChangeOpenness(Op==TEXT("open"));
        else if(Op==TEXT("create")&&!Name.IsEmpty()&&Name.Len()<=80)C->SendFellowshipCreate(Name);
        else if(Op==TEXT("recruit")){FACEWorldObject Target;if(C->GetWorldObject(Guid(I,TEXT("target")),Target)&&Target.bIsPlayer)C->SendFellowshipRecruit(Target.Guid);else ReportActivityFailure(P,I,TEXT("Recruit target is unavailable"));}
        else ReportActivityFailure(P,I,TEXT("Invalid fellowship action"));return;
    }
    if(Action==TEXT("select"))
    {
        FACEWorldObject Target;const int32 Id=Guid(I,TEXT("target"));
        if(C->GetWorldObject(Id,Target)&&(Target.IsSelectableWorldObject()||IsOwnedPluginItem(Target)))C->SelectObject(Id);
        else ReportActivityFailure(P,I,TEXT("Meta selection target disappeared"));return;
    }
    if(Action==TEXT("cancel_attack")){PhysicalAttackUntil=Now+2;C->GetSession()->SendCancelAttack();return;}
    if(Action==TEXT("combat_mode")){const int Mode=int(Number(I,TEXT("mode")));if(Mode==1||Mode==2||Mode==4||Mode==8)C->SendChangeCombatMode(Mode);return;}
    if(Action==TEXT("attack_bar")){if(PC)PC->SetPluginAttackPower(Number(I,TEXT("power")));return;}
    if(Action==TEXT("logout")){Stop(P.Id,TEXT("Logged out by meta"));C->Logout();return;}
    if(Action==TEXT("recall"))
    {
        const FString Destination=String(I,TEXT("destination"));
        if(Destination==TEXT("marketplace"))C->SendTeleToMarketplace();else if(Destination==TEXT("lifestone"))C->SendTeleToLifestone();else if(Destination==TEXT("allegiance"))C->SendRecallAllegianceHometown();else if(Destination==TEXT("house"))C->SendTeleToHouse();else if(Destination==TEXT("mansion"))C->SendTeleToMansion();else ReportActivityFailure(P,I,TEXT("Unknown recall destination"));return;
    }
    if(Action==TEXT("say"))
    {
        const FString Text=String(I,TEXT("text"));
        if(!Text.IsEmpty()&&Text.Len()<=500&&!Text.Contains(TEXT("\n"))&&!Text.Contains(TEXT("\r"))){if(Number(I,TEXT("channel"))==2048)C->SendChatChannel(2048,Text);else C->GetSession()->SendTalk(Text);}
        return;
    }
    if (Action == TEXT("move"))
    {
        FACEPosition Dest; Dest.CellId = Guid(I,TEXT("cell"));
        Dest.Location = FVector(Number(I, TEXT("x")), Number(I, TEXT("y")), Number(I, TEXT("z")));
        FACEPosition Pos = C->GetPlayerPosition(); PC->TryGetLocallyPredictedPosition(Pos);
        bool CoordinateRoute=false;
        if(I->TryGetBoolField(TEXT("coordinate_route"),CoordinateRoute)&&CoordinateRoute&&(uint32(Pos.CellId)&0xffff)>=0x100)
        {
            const uint32 From=uint32(Dest.CellId)>>16,To=uint32(Pos.CellId)>>16;
            Dest.Location.X+=(int32(From>>8)-int32(To>>8))*192;
            Dest.Location.Y+=(int32(From&255)-int32(To&255))*192;
            Dest.CellId=Pos.CellId;
        }
        if (!Dest.IsValid() || (((uint32(Dest.CellId)>>16)!=(uint32(Pos.CellId)>>16)) && ((uint32(Dest.CellId)&0xffff)>=0x100 || (uint32(Pos.CellId)&0xffff)>=0x100)) || FVector::Distance(Dest.ToUnrealLocation(), Pos.ToUnrealLocation()) > 40000.)
        { ReportActivityFailure(P,I, TEXT("Route needs a portal between these cells, or a closer waypoint (maximum 400m)")); return; }
        if (MovementOwner != P.Id || FVector::Distance(Dest.ToUnrealLocation(), MoveTarget.ToUnrealLocation()) > .1)
        { LastMovePosition = Pos.ToUnrealLocation(); LastProgress = Now; }
        // Stay inside the policy's arrival radius. A fixed 70cm host stop
        // stranded routes configured with a smaller NavCloseStopRange.
        MoveArrivalRadius = FMath::Clamp(float(Number(I,TEXT("arrival_radius"),.8)) * 100.f,1.f,500.f) * .875f;
        FaceHeading.Reset();MovementOwner = P.Id; MoveTarget = Dest; MoveExpires = Now + 1.5; return;
    }
    MovementOwner.Empty(); MoveExpires = 0;
    if (Action == TEXT("cast"))
    {
        const int32 Spell = Guid(I,TEXT("spell")); int32 Target = 0;
        if (!C->GetKnownSpells().Contains(Spell)) { P.Status = TEXT("Spell is not known"); return; }
        const int32 Selected = Guid(I,TEXT("target"),uint32(C->GetSelectedObject().Guid));
        if (!C->ResolveSpellCastTarget(Spell, Selected, Target))
        {
            FACEWorldObject Item;uint32 Flags=0,Type=0;bool Projectile=false;
            auto* Dat=GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
            if(Dat->TryGetRetailSpellTargeting(Spell,Flags,Type,Projectile) && (Flags&4) && C->GetWorldObject(Selected,Item)
                && (IsOwnedPluginItem(Item) || (BuffRequests.Num() && (Item.WielderId==int32(uint32(Number(BuffRequests[0]->AsObject(),TEXT("player"))))
                    || (Item.CurrentWieldedLocation && Item.ParentGuid==int32(uint32(Number(BuffRequests[0]->AsObject(),TEXT("player"))))))))
                && (uint32(Item.ItemType)&Type) && Item.StackSize<=1) Target=Selected;
            else {P.Status=TEXT("Spell target is unavailable");return;}
        }
        FACEWorldObject CombatTarget;
        if(C->GetWorldObject(Target,CombatTarget)&&CombatTarget.IsAttackable()&&!CombatTarget.bIsPlayer&&!CombatTarget.PetOwnerId
            && !HasClearCombatSight(CombatTarget)){C->GetSession()->SendCancelAttack();P.Status=TEXT("Target behind an obstruction; continuing route");return;}
        if(Target && Target!=C->GetPlayerGuid() && Target!=C->GetSelectedObject().Guid)C->SelectObject(Target);
        if(C->GetPlayerVitalsView().CombatMode!=8)C->SendChangeCombatMode(8);
        // SendCastSpell's public UI path selects only in VR. Automation uses the same
        // resolved target and session cast action in both modes, without a fake trigger.
        PhysicalAttackTarget=0;PhysicalAttackOwner.Empty();
        PendingSpell=Spell;PendingSpellTarget=Target;PendingSpellAt=Now;PendingSpellOwner=P.Id;PendingSpellActivity=String(I,TEXT("activity"));
        PendingSpellConfirmation.Empty();PendingSpellConfirmed=false;PendingSpellFizzled=false;
        // UseDone also succeeds for resisted spells. Track the specific server
        // magic-chat confirmation before considering an offensive enchantment active.
        uint32 School=0,Power=0,Category=0,Flags=0,Icon=0;double Duration=0;FString SpellName;
        auto* Dat=GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
        bool FastCast=false;I->TryGetBoolField(TEXT("fast_cast"),FastCast);FastCastOwner.Empty();FastCastStarted=false;
        FastCastStartedAt=0;FastCastMovementApplied=false;
        if(Dat->TryGetSpellInfo(Spell,SpellName,Icon))
        {
            const bool Self=Target==C->GetPlayerGuid();
            FACEWorldObject Recipient;
            if(Self||C->GetWorldObject(Target,Recipient))
                PendingSpellConfirmation=TEXT("You cast ")+SpellName+TEXT(" on ")+(Self?TEXT("yourself"):Recipient.Name);
        }
        if(C->GetWorldObject(Target,CombatTarget)&&CombatTarget.IsAttackable()&&!CombatTarget.bIsPlayer&&!CombatTarget.PetOwnerId
            &&Dat->IsPluginSingleTargetOffensiveSpell(Spell)&&Dat->TryGetSpellInfo(Spell,SpellName,Icon))
        {
            FOffensiveCast Record;Record.Spell=Spell;Record.Target=Target;Record.Owner=P.Id;
            Record.SpellName=SpellName;Record.TargetName=CombatTarget.Name;Record.Sent=Now;
            OffensiveCasts.Add(MoveTemp(Record));if(OffensiveCasts.Num()>8)OffensiveCasts.RemoveAt(0);
        }
        if(FastCast&&P.Permissions.Contains(TEXT("navigation"))&&Dat->TryGetPluginSpellInfo(Spell,School,Power,Category,Flags,Duration)
            &&ACEPluginCastMotion::FastBuff(School,Power,Flags,Duration))FastCastOwner=P.Id;
        if(Target&&Dat->TryGetPluginSpellInfo(Spell,School,Power,Category,Flags,Duration)
            &&!(Flags&4)&&Duration>0&&Dat->TryGetSpellInfo(Spell,SpellName,Icon)&&C->GetWorldObject(Target,CombatTarget))
        {
            auto Record=MakeShared<FJsonObject>();Record->SetNumberField(TEXT("target"),uint32(Target));Record->SetNumberField(TEXT("spell"),Spell);
            Record->SetStringField(TEXT("confirmation"),TEXT("You cast ")+SpellName+TEXT(" on ")+CombatTarget.Name);
            Record->SetStringField(TEXT("resist"),CombatTarget.Name+TEXT(" resists your spell"));
            Record->SetNumberField(TEXT("category"),Category);Record->SetNumberField(TEXT("power"),Power);
            Record->SetNumberField(TEXT("duration"),Duration);Record->SetNumberField(TEXT("sent"),Now);
            PendingDebuffCasts.Add(MakeShared<FJsonValueObject>(Record));if(PendingDebuffCasts.Num()>8)PendingDebuffCasts.RemoveAt(0);
        }
        C->GetSession()->SendCastSpell(Spell, Target); P.NextAction = Now + 3.5;
    }
    else if (Action == TEXT("attack"))
    {
        const auto Selection=C->GetSelectedObject();
        const int32 Target = Guid(I,TEXT("target"),uint32(PC->FindNearbyTarget(true,0,Selection.bShowHealth && Selection.HealthFraction<=0?Selection.Guid:0)));
        FACEWorldObject Monster;if(!C->GetWorldObject(Target,Monster)||!Monster.IsAttackable()||Monster.bIsPlayer||Monster.PetOwnerId)return;
        if(!HasClearCombatSight(Monster)){C->GetSession()->SendCancelAttack();P.Status=TEXT("Target behind an obstruction; continuing route");return;}
        if (!Target) { P.Status = TEXT("No eligible monster"); return; }
        const int32 Mode = Guid(I,TEXT("mode"),2);
        if (Mode != 2 && Mode != 4) { ReportActivityFailure(P,I, TEXT("Attack mode must be melee (2) or missile (4)")); return; }
        if(C->GetSelectedObject().Guid!=Target)C->SelectObject(Target);
        if(C->GetPlayerVitalsView().CombatMode!=Mode)C->SendChangeCombatMode(Mode);
        const int32 Height = int32(FMath::Clamp(Number(I, TEXT("height"), 2), 1., 3.));
        const float Power = FMath::Clamp(float(Number(I, TEXT("power"), .5)), 0.f, 1.f);
        PhysicalAttackTarget=Target;PhysicalAttackOwner=P.Id;PhysicalAttackUntil=Now+30;
        if (Mode == 2) C->SendTargetedMeleeAttack(Target, Height, Power); else C->SendTargetedMissileAttack(Target, Height, Power);
        PC->ShowPluginAttack(Target, Height, Power);
        P.NextAction = Now + 3.5;
    }
    else ReportActivityFailure(P,I, TEXT("Unknown action: ") + Action);
}
void UACEPluginSubsystem::ApplyMovement(AACEPlayerController* PC, float& F, float& R, float& T, bool Manual, bool Blocked, bool VR, const FVector& Facing)
{
    if (Manual)
    {
        // Player input wins this frame, but independent activities (especially
        // loot during manual combat) keep their VM, queues and running state.
        // Discard stale steering so it cannot snap back after input is released;
        // the next normal plugin decision can request fresh movement.
        MovementOwner.Empty();MoveExpires=0;FaceHeading.Reset();
        FastCastOwner.Empty();FastCastStarted=false;
        if(PC)
        {
            PC->CancelPluginJump();
            if(!UseApproachOwner.IsEmpty())PC->EndUseApproach();
        }
        UseApproachOwner.Empty();
        return;
    }
    if(!FastCastOwner.IsEmpty())
    {
        // VT's result watchdog is about four seconds after the spell words.
        // Never hold an input for the full network action timeout. A result can
        // share a packet with the words: deliver one movement edge, then release
        // it on the following frame so the normal motion path cancels recoil.
        if(!PendingSpell || FPlatformTime::Seconds()-PendingSpellAt>30
            || (FastCastStarted && (FPlatformTime::Seconds()-FastCastStartedAt>=4
                || PendingSpellFizzled || (PendingSpellConfirmed&&FastCastMovementApplied))))
        {FastCastOwner.Empty();FastCastStarted=false;}
        else
        {
            // Use the normal movement/collision/network path, never inject keys
            // or alter spell speed. Chat, UI blocking and jumps suspend the input.
            if(FastCastStarted&&!Blocked&&PC&&PC->GetPawn()){F=-1;R=T=0;FastCastMovementApplied=true;}return;
        }
    }
    if (MovementOwner.IsEmpty()) return;
    const double Now = FPlatformTime::Seconds();
    if (Blocked || Now > MoveExpires || !PC->GetPawn()) { MovementOwner.Empty(); return; }
    FACEPosition Pos; if (!PC->TryGetLocallyPredictedPosition(Pos)) Pos = GetGameInstance()->GetSubsystem<UACEClientSubsystem>()->GetPlayerPosition();
    if(FaceHeading.IsSet())
    {
        const float Delta=FMath::FindDeltaAngleDegrees(Pos.ToUnrealQuat().Rotator().Yaw,FaceHeading.GetValue());
        F=R=0;T=FMath::Abs(Delta)<1?0:FMath::Clamp(Delta/45.f,-1.f,1.f);return;
    }
    const FVector Location = Pos.ToUnrealLocation();
    if (FVector::Dist2D(Location, LastMovePosition) > 20) { LastMovePosition = Location; LastProgress = Now; }
    if (Now - LastProgress > 5)
    {
        // Let UCM backtrack the approach and blacklist an unreachable target.
        // Other plugins retain the safety stop; no teleport or collision bypass.
        if(MovementOwner==TEXT("ucm"))
        {
            ++MovementBlockedSerial;bRouteJoinRequested=true;RouteVisibilityOffset=0;MovementOwner.Empty();MoveExpires=0;F=R=T=0;
            if(auto P=Find(TEXT("ucm")))P->NextDecision=0;
            if(auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>())if(auto Session=C->GetSession())Session->SendCancelAttack();
        }
        else StopAll(TEXT("Route blocked; manual reposition required"),true);
        return;
    }
    const FVector Delta = MoveTarget.ToUnrealLocation() - Location;
    if (Delta.Size() < MoveArrivalRadius || Delta.Size2D() < 1.)
    {
        F=R=T=0;
        // Use the policy's 3D arrival metric on ramps. An XY-only stop at
        // 70cm could remain almost a metre away along a slope forever.
        // If the recorded height is unreachable, retain the progress watchdog.
        // Recreating an expired movement every tick used to wait forever here.
        return;
    }
    // The AC avatar travels along local +Y, not Unreal's actor +X. Steer
    // from the predicted physics pose, independently of camera/visual rotation.
    const FVector Direction = Delta.GetSafeNormal2D(), Forward = (VR ? Facing : Pos.GetAceForwardVector()).GetSafeNormal2D();
    const float Dot = FVector::DotProduct(Direction, Forward);
    if (VR) { F = Dot; R = FVector::DotProduct(Direction, FVector::CrossProduct(FVector::UpVector, Forward)); }
    else
    {
        const float Angle = FMath::Atan2(FVector::CrossProduct(Forward, Direction).Z, Dot);
        // Controller integrates -T in AC space; the X reflection reverses
        // that sign in Unreal space. Positive T therefore increases UE yaw.
        T = FMath::Clamp(Angle * 2.f, -1.f, 1.f); F = FMath::Abs(Angle) < .3 ? 1.f : 0.f; R = 0;
    }
}
