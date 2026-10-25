#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "UI/ACEUIGameplayBinder.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginVM.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUIWorkloadTest,"ACE.Performance.UIAndLuaWorkload",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEUIWorkloadTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
    auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
    Context.OwningGameInstance=GI;Context.SetCurrentWorld(World);GI->OnWorldChanged(nullptr,World);GI->Init();
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if(!TestTrue(TEXT("Retail DAT loads"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
    Client->Session=MakeShared<FACESession>();auto& Session=*Client->Session;
    Session.State=EACESessionState::InWorld;Session.PlayerGuid=0x50000001;Session.ServerName=TEXT("UI workload test");
    Session.PlayerPosition.CellId=0x7D64000D;Session.PlayerPosition.Location=FVector(34,104,12);
    Session.PlayerVitals.bValid=true;Session.PlayerVitals.Health=Session.PlayerVitals.MaxHealth=100;
    for(int32 Id=1;Id<7000&&Session.KnownSpells.Num()<2400;++Id)
    {FString Name;uint32 Icon=0;if(Dat->TryGetSpellInfo(Id,Name,Icon))Session.KnownSpells.Add(Id);}
    auto* Binder=NewObject<UACEUIGameplayBinder>();Binder->Client=Client;
    const auto Expected=Binder->FilteredSpellbook(Dat);
    TestEqual(TEXT("All filters retain every known spell"),Expected.Num(),Session.KnownSpells.Num());
    auto Measure=[&](const TCHAR* Name,int32 Count,auto Work)
    {
        const double Start=FPlatformTime::Seconds();
        for(int32 I=0;I<Count;++I)Work();
        AddInfo(FString::Printf(TEXT("UI workload %s: %.3f us/call (%d samples)"),Name,
            (FPlatformTime::Seconds()-Start)*1.e6/Count,Count));
    };
    // Same build/data: forcing invalidation recreates the former per-frame sort.
    Measure(TEXT("spellbook full filter/sort"),100,[&]{Binder->SpellbookOrderSchools=MAX_uint32;Binder->FilteredSpellbook(Dat);});
    Measure(TEXT("spellbook unchanged order"),1000,[&]{Binder->FilteredSpellbook(Dat);});
    TestTrue(TEXT("Unchanged spell order is preserved"),Expected==Binder->FilteredSpellbook(Dat));
    const int32 Removed=Session.KnownSpells.Pop();
    TestFalse(TEXT("Unlearning immediately removes the row"),Binder->FilteredSpellbook(Dat).Contains(Removed));
    Session.KnownSpells.Add(Removed);
    TestTrue(TEXT("Learning immediately inserts the sorted row"),Expected==Binder->FilteredSpellbook(Dat));
    Binder->SpellbookSchoolFilterMask=1;Binder->SpellbookLevelFilterMask=1;
    for(int32 Id:Binder->FilteredSpellbook(Dat))
    {
        uint32 School=0,Level=1;Dat->TryGetSpellSchoolAndLevel(Id,School,Level);
        TestTrue(TEXT("Filter changes are immediate"),(School<1||School>5||School==1)&&(Level<1||Level>8||Level==1));
    }
    Binder->SpellbookSchoolFilterMask=31;Binder->SpellbookLevelFilterMask=255;
    Binder->FilteredSpellbook(nullptr);
    TestTrue(TEXT("DAT becoming available restores authored order"),Expected==Binder->FilteredSpellbook(Dat));
    Session.KnownSpells.SetNum(1024); // Representative large spellbook for Lua heap measurements.
    FACEWorldObject Player;Player.Guid=Session.PlayerGuid;Player.Name=TEXT("Workload character");
    Player.Position=Session.PlayerPosition;Player.bHasPosition=true;Player.bIsPlayer=true;Player.ItemsCapacity=102;
    Session.WorldObjects.Add(Player.Guid,Player);
    for(int32 I=0;I<600;++I)
    {
        FACEWorldObject O;O.Guid=1000+I;O.Name=TEXT("Nearby creature");O.ItemType=ACEItemType::Creature;
        O.bHasPosition=true;O.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;O.Position=Session.PlayerPosition;
        O.Position.Location.X+=1+(I%100);O.Position.Location.Y+=I/100;Session.WorldObjects.Add(O.Guid,O);
    }
    for(int32 I=0;I<256;++I)
    {
        FACEWorldObject O;O.Guid=3000+I;O.Name=TEXT("Inventory supply");O.ItemType=ACEItemType::SpellComponents;
        O.ContainerId=Player.Guid;O.StackSize=50;O.WeenieClassId=100+I;Session.WorldObjects.Add(O.Guid,O);
    }
    Host->Plugins.Reset();
    for(int32 I=0;I<8;++I)
    {
        auto P=MakeShared<FACEClientPlugin>();P->Id=FString::Printf(TEXT("observer%d"),I);P->Api=2;
        P->Companion=P->Enabled=P->Running=true;P->Player=Player.Guid;P->Server=Session.ServerName;P->VMSession=Client->Session;
        P->Permissions={TEXT("data"),TEXT("chat"),TEXT("ui")};P->Profile=MakeShared<FJsonObject>();
        P->SavedState=MakeShared<FJsonObject>();Host->Plugins.Add(P);
    }
    Host->UpdateCompanionChatContext();
    Host->ObservePluginChat(TEXT("Quest progress"),TEXT(""),0);
    auto& First=*Host->Plugins[0];auto& Second=*Host->Plugins[1];
    UACEPluginSubsystem::FCompanionReadBatch Batch;
    TSharedPtr<FJsonObject> FirstData,SecondData;
    {
        TGuardValue<UACEPluginSubsystem::FCompanionReadBatch*> Guard(Host->CompanionReadBatch,&Batch);
        FirstData=Host->PublicSnapshot(First);SecondData=Host->PublicSnapshot(Second);
        TestTrue(TEXT("Observers share immutable native world rows"),FirstData->GetArrayField(TEXT("objects"))[0]==SecondData->GetArrayField(TEXT("objects"))[0]);
        TestTrue(TEXT("Observers share common native spell metadata"),FirstData->GetArrayField(TEXT("spells"))[0]==SecondData->GetArrayField(TEXT("spells"))[0]);
        TestEqual(TEXT("First reader receives its chat"),FirstData->GetArrayField(TEXT("chat_events")).Num(),1);
        TestEqual(TEXT("Second reader retains its independent cursor"),SecondData->GetArrayField(TEXT("chat_events")).Num(),1);
        TestTrue(TEXT("Each plugin still has its own state"),FirstData->GetObjectField(TEXT("state"))!=SecondData->GetObjectField(TEXT("state")));
        FirstData->SetNumberField(TEXT("health"),0);
        TestEqual(TEXT("Enrichment cannot mutate another snapshot root"),SecondData->GetNumberField(TEXT("health")),100.);
        TestEqual(TEXT("Shared views retain bounded object lists"),SecondData->GetArrayField(TEXT("objects")).Num(),512);
        TestTrue(TEXT("Shared view preserves truncation flag"),SecondData->GetBoolField(TEXT("objects_truncated")));
    }
    FString Error;FACEPluginVM Mutator,Reader;TSharedPtr<FJsonObject> Output;
    TestTrue(TEXT("Mutator loads"),Mutator.Load(TEXT("return function(s,p) s.objects[1].name='changed';s.spells[1].name='changed';s.state.bad=true end"),Error));
    TestTrue(TEXT("Mutating a Lua snapshot stays isolated"),Mutator.Step(FirstData,First.Profile,Output,Error));
    TestTrue(TEXT("Reader loads"),Reader.Load(TEXT("return function(s,p) assert(s.objects[1].name~='changed' and s.spells[1].name~='changed' and not s.state.bad) end"),Error));
    TestTrue(TEXT("Native sharing never shares mutable Lua tables"),Reader.Step(SecondData,Second.Profile,Output,Error));
    Measure(TEXT("Lua full-data input and empty callback"),30,[&]{if(!Reader.Step(SecondData,Second.Profile,Output,Error))AddError(Error);});
    AddInfo(FString::Printf(TEXT("Lua full-data heap: current %.2f MiB, peak %.2f MiB"),Reader.GetMemoryUsage()/1048576.,Reader.GetPeakMemoryUsage()/1048576.));
    // Accelerated 20-minute observer workload at the default 2 Hz, including
    // fresh Lua input tables and normal GC on every callback.
    double EarlyMs=0,LateMs=0;SIZE_T EarlyPeak=Reader.GetPeakMemoryUsage();
    for(int32 I=0;I<2400;++I)
    {
        const double Start=FPlatformTime::Seconds();
        if(!Reader.Step(SecondData,Second.Profile,Output,Error)){AddError(Error);return false;}
        const double Ms=(FPlatformTime::Seconds()-Start)*1000;
        if(I<100)EarlyMs+=Ms;if(I>=2300)LateMs+=Ms;
        if(I==99)EarlyPeak=Reader.GetPeakMemoryUsage();
    }
    TestTrue(TEXT("Repeated full-data callbacks retain a bounded heap"),Reader.GetPeakMemoryUsage()<=EarlyPeak+1024*1024);
    AddInfo(FString::Printf(TEXT("2400 full-data callbacks: first 100 avg %.3f ms; last 100 avg %.3f ms; peak %.2f MiB"),EarlyMs/100,LateMs/100,Reader.GetPeakMemoryUsage()/1048576.));
    First.SelectiveSnapshot=true;First.SnapshotFields={TEXT("health"),TEXT("character_name"),TEXT("char_ints"),TEXT("action_serial")};
    FACEPluginVM SmallReader;SmallReader.Load(TEXT("return function(s) assert(s.health==100 and s.inventory==nil and s.spells==nil) end"),Error);
    Measure(TEXT("selective character snapshot plus Lua callback"),100,[&]
    {
        if(!SmallReader.Step(Host->PublicSnapshot(First),First.Profile,Output,Error))AddError(Error);
    });
    First.SelectiveSnapshot=false;First.SnapshotFields.Reset();
    for(bool Shared:{false,true})
    {
        Measure(Shared?TEXT("eight observers sharing native reads"):TEXT("eight observers rebuilding native reads"),20,[&]
        {
            UACEPluginSubsystem::FCompanionReadBatch Pass;
            TGuardValue<UACEPluginSubsystem::FCompanionReadBatch*> Guard(Host->CompanionReadBatch,Shared?&Pass:nullptr);
            for(const auto& P:Host->Plugins)Host->PublicSnapshot(*P);
        });
    }
    Session.WorldObjects[1000].Name=TEXT("Updated creature");Session.PlayerVitals.Health=75;
    auto Next=Host->PublicSnapshot(First);
    TestEqual(TEXT("The next pass sees live vitals"),Next->GetNumberField(TEXT("health")),75.);
    TestEqual(TEXT("The next pass sees live object changes"),Next->GetArrayField(TEXT("objects"))[0]->AsObject()->GetStringField(TEXT("name")),FString(TEXT("Updated creature")));
    // Exercise the production batch boundary and invalidation after selection.
    for(const auto& P:Host->Plugins)
    {
        P->VM=MakeShared<FACEPluginVM>();P->VM->Load(TEXT("return function(s,p) return nil end"),Error);
        P->NextStateSave=FPlatformTime::Seconds()+1000;
    }
    First.VM->Load(TEXT("return function(s,p) return {action='select',target=1000} end"),Error);
    Second.VM->Load(TEXT("return function(s,p) assert(s.selected==1000);return {status='fresh'} end"),Error);
    Host->CompanionSession=Client->Session;Host->CompanionPlayer=Player.Guid;Host->CompanionServer=Session.ServerName;
    Host->TickCompanions();
    Host->TickCompanions(); // The second observer is intentionally scheduled in the next frame.
    TestTrue(TEXT("Action invalidation keeps later readers current"),Second.Running&&Second.Status==TEXT("fresh"));
    TestNull(TEXT("Observer cache cannot survive the callback pass"),Host->CompanionReadBatch);
    Session.State=EACESessionState::Disconnected;
    return !HasAnyErrors();
}
#endif
