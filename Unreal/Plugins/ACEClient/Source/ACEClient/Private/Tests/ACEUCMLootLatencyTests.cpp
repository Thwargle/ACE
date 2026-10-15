#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEUCMProfiles.h"
#include "Mods/ACEVTRegex.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMLootLatencyTest,"ACE.Plugins.LootProfileInteraction",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMLootLatencyTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
    auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);
    Context.SetCurrentWorld(World);Context.OwningGameInstance=GI;GI->OnWorldChanged(nullptr,World);GI->Init();
    const FString Root=FPaths::ProjectSavedDir()/TEXT("Automation/LootProfileInteraction")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);IFileManager::Get().DeleteDirectory(*Root,false,true);};
    auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();Host->StorageRoot=Root;Host->Settings=MakeShared<FJsonObject>();
    if(!TestTrue(TEXT("Isolated profiles initialize"),Host->InitializeUCMSelection()))return false;
    auto P=Host->Find(TEXT("ucm"));if(!P)return false;P->Enabled=true;
    auto* Client=GI->GetSubsystem<UACEClientSubsystem>();Client->Session=MakeShared<FACESession>();
    Client->Session->State=EACESessionState::InWorld;Client->Session->PlayerGuid=0x50000001;
    Client->Session->PlayerVitals.bValid=true;Client->Session->PlayerVitals.Health=100;
    Client->Session->PlayerPosition.CellId=0x7d640001;Client->Session->PlayerPosition.Location=FVector(84,84,12);

    TSharedPtr<FJsonObject> Loot=MakeShared<FJsonObject>();FString Filename;
    if(FParse::Value(FCommandLine::Get(),TEXT("UCMLootProfile="),Filename))
    {
        FString Text;
        if(!TestTrue(TEXT("Read benchmark profile without modifying it"),FFileHelper::LoadFileToString(Text,*Filename)
            &&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Loot)))return false;
    }
    else
    {
        // Larger than the runtime matcher cache, with repeated patterns like an
        // imported LootSnob profile. Validation must not compile them per rule.
        TArray<TSharedPtr<FJsonValue>> Rules;
        for(int32 I=0;I<1600;++I)
        {
            auto Rule=MakeShared<FJsonObject>();Rule->SetStringField(TEXT("label"),FString::Printf(TEXT("Rule %d"),I));
            Rule->SetStringField(TEXT("action"),TEXT("keep"));TArray<TSharedPtr<FJsonValue>> Conditions;
            for(int32 J=0;J<4;++J)
            {
                auto Condition=MakeShared<FJsonObject>();Condition->SetStringField(TEXT("field"),TEXT("string"));
                Condition->SetNumberField(TEXT("key"),1);
                Condition->SetStringField(TEXT("pattern"),FString::Printf(TEXT("^(?:Major|Epic|Legendary) (?:Strength|Coordination|Focus|Willpower|Armor) %d$"),(I+J)%304));
                Conditions.Add(MakeShared<FJsonValueObject>(Condition));
            }
            Rule->SetArrayField(TEXT("conditions"),Conditions);Rules.Add(MakeShared<FJsonValueObject>(Rule));
        }
        Loot->SetArrayField(TEXT("loot_rules"),Rules);
    }
    FString Error;
    if(!TestTrue(TEXT("Create independent large Loot profile"),ACEUCMProfiles::SaveSectionAs(Root,TEXT("Loot"),TEXT("Large"),Loot,Error)))return false;
    for(const TCHAR* Name:{TEXT("Default"),TEXT("Large")})
    {
        if(!TestTrue(TEXT("Select benchmark profile"),Host->SelectUCMSection(TEXT("Loot"),Name)))return false;
        for(bool Looting:{false,true})
        {
            P->Profile->SetBoolField(TEXT("looting"),Looting);
            TestTrue(TEXT("Save activity toggle"),Host->SaveCurrentProfile(P->Id,false));
            const uint64 Compiles=ACEVTRegex::ValidationCompilationsForTests();
            const double ValidateStart=FPlatformTime::Seconds();
            TestTrue(TEXT("Selected profile remains valid"),Host->ValidatePublicProfile(P->Profile));
            const double ValidateMs=(FPlatformTime::Seconds()-ValidateStart)*1000;
            const double StartTime=FPlatformTime::Seconds();
            TestTrue(TEXT("Start works with looting on or off"),Host->Start(P->Id));
            const double StartMs=(FPlatformTime::Seconds()-StartTime)*1000;Host->Stop(P->Id);
            const int32 Points=P->Profile->GetArrayField(TEXT("route")).Num();
            const double RouteTime=FPlatformTime::Seconds();Host->RecordRouteAction(TEXT("walk"));
            const double RouteMs=(FPlatformTime::Seconds()-RouteTime)*1000;
            TestEqual(TEXT("Waypoint persists with selected loot profile"),P->Profile->GetArrayField(TEXT("route")).Num(),Points+1);
            auto Monsters=P->Profile->GetArrayField(TEXT("monsters"));auto Monster=MakeShared<FJsonObject>();
            Monster->SetStringField(TEXT("name"),FString::Printf(TEXT("Rat %d"),Monsters.Num()));
            Monsters.Add(MakeShared<FJsonValueObject>(Monster));P->Profile->SetArrayField(TEXT("monsters"),Monsters);
            const double MonsterTime=FPlatformTime::Seconds();
            TestTrue(TEXT("Monster button save succeeds"),Host->SaveCurrentProfile(P->Id,false));
            const double MonsterMs=(FPlatformTime::Seconds()-MonsterTime)*1000;
            AddInfo(FString::Printf(TEXT("Loot profile=%s looting=%d: validation %.2f ms, Start %.2f ms, waypoint %.2f ms, monster save %.2f ms"),Name,Looting,ValidateMs,StartMs,RouteMs,MonsterMs));
            TestEqual(TEXT("Unrelated interactions do not recompile selected loot patterns"),ACEVTRegex::ValidationCompilationsForTests(),Compiles);
            TestEqual(TEXT("Recorded action is saved with its kind"),P->Profile->GetArrayField(TEXT("route")).Last()->AsObject()->GetStringField(TEXT("kind")),FString(TEXT("walk")));
        }
    }
    TestTrue(TEXT("Selected rules survive unrelated edits"),FJsonValue::CompareEqual(
        *P->Profile->TryGetField(TEXT("loot_rules")),*Loot->TryGetField(TEXT("loot_rules"))));
    // Cached pattern validity must never exempt other fields or edited patterns.
    auto LiveRule=P->Profile->GetArrayField(TEXT("loot_rules"))[0]->AsObject();
    FString Action;const bool HadAction=LiveRule->TryGetStringField(TEXT("action"),Action);
    LiveRule->SetStringField(TEXT("action"),TEXT("invalid"));
    TestFalse(TEXT("In-place edits cannot mutate the cached validation snapshot"),Host->ValidatePublicProfile(P->Profile));
    if(HadAction)LiveRule->SetStringField(TEXT("action"),Action);else LiveRule->RemoveField(TEXT("action"));
    auto Changed=ACEUCMProfiles::Copy(P->Profile);
    Changed->GetArrayField(TEXT("loot_rules"))[0]->AsObject()->SetStringField(TEXT("action"),TEXT("invalid"));
    TestFalse(TEXT("Invalid action still rejected after warming validation"),Host->ValidatePublicProfile(Changed));
    Changed=ACEUCMProfiles::Copy(P->Profile);
    auto Condition=MakeShared<FJsonObject>();Condition->SetStringField(TEXT("field"),TEXT("string"));
    Condition->SetNumberField(TEXT("key"),1);Condition->SetStringField(TEXT("pattern"),TEXT("["));
    Changed->GetArrayField(TEXT("loot_rules"))[0]->AsObject()->SetArrayField(TEXT("conditions"),{MakeShared<FJsonValueObject>(Condition)});
    TestFalse(TEXT("Edited invalid regex is not accepted from previous validation"),Host->ValidatePublicProfile(Changed));
    const auto BeforeInvalid=P->Profile;P->Profile=Changed;
    TestFalse(TEXT("Native save still rejects invalid rules"),Host->SaveCurrentProfile(P->Id,false));
    P->Profile=BeforeInvalid;
    // The native save path must keep shared-file refresh/conflict protection.
    auto Disk=ACEUCMProfiles::Copy(Loot);
    Disk->GetArrayField(TEXT("loot_rules"))[0]->AsObject()->SetStringField(TEXT("label"),TEXT("External edit"));
    FString Text;FJsonSerializer::Serialize(Disk.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
    TestTrue(TEXT("Simulate another editor updating shared loot"),FFileHelper::SaveStringToFile(Text,*(Root/TEXT("Profiles/Loot/Large.json"))));
    TestTrue(TEXT("Unrelated native save refreshes externally edited loot"),Host->SaveCurrentProfile(P->Id,false));
    TestEqual(TEXT("External loot changes remain visible"),P->Profile->GetArrayField(TEXT("loot_rules"))[0]->AsObject()->GetStringField(TEXT("label")),FString(TEXT("External edit")));
    Disk->GetArrayField(TEXT("loot_rules"))[0]->AsObject()->SetStringField(TEXT("label"),TEXT("External conflict"));
    Text.Empty();FJsonSerializer::Serialize(Disk.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
    FFileHelper::SaveStringToFile(Text,*(Root/TEXT("Profiles/Loot/Large.json")));
    P->Profile->GetArrayField(TEXT("loot_rules"))[0]->AsObject()->SetStringField(TEXT("label"),TEXT("Local conflict"));
    TestFalse(TEXT("Native save rejects conflicting loot edits"),Host->SaveCurrentProfile(P->Id,false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERegexValidationCacheTest,"ACE.Plugins.RegexValidationCache",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACERegexValidationCacheTest::RunTest(const FString&)
{
    FString Error;const FString Prefix=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    for(int32 Pass=0;Pass<2;++Pass)
    {
        const uint64 Before=ACEVTRegex::ValidationCompilationsForTests();
        for(int32 I=0;I<600;++I)
            TestTrue(TEXT("Validate many unique patterns"),ACEVTRegex::Validate(Prefix+FString::Printf(TEXT("(?:Fire|Cold)%d"),I),Error));
        TestEqual(TEXT("Repeated validation does not thrash the matcher cache"),ACEVTRegex::ValidationCompilationsForTests()-Before,Pass?uint64(0):uint64(600));
    }
    const FString Bad=Prefix+TEXT("[");
    TestFalse(TEXT("Invalid pattern rejected"),ACEVTRegex::Validate(Bad,Error));const FString Diagnostic=Error;
    const uint64 Before=ACEVTRegex::ValidationCompilationsForTests();
    TestFalse(TEXT("Cached invalid pattern remains invalid"),ACEVTRegex::Validate(Bad,Error));
    TestEqual(TEXT("Cached failure preserves the diagnostic"),Error,Diagnostic);
    TestEqual(TEXT("Invalid pattern is not recompiled"),ACEVTRegex::ValidationCompilationsForTests(),Before);
    for(int32 I=0;I<4100;++I)TestTrue(TEXT("Cache eviction preserves validity"),ACEVTRegex::Validate(Prefix+FString::Printf(TEXT("evict%d"),I),Error));
    TestTrue(TEXT("Validation cache is bounded"),ACEVTRegex::ValidationCacheEntriesForTests()<=4096);
    TestFalse(TEXT("Evicted invalid pattern is revalidated"),ACEVTRegex::Validate(Bad,Error));
    TestEqual(TEXT("Eviction preserves error text"),Error,Diagnostic);
    return true;
}
#endif
