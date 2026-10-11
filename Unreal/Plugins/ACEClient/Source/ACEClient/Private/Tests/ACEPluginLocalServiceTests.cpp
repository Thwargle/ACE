#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Engine/GameInstance.h"
#include "Widgets/Text/STextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPluginLocalServiceTest,"ACE.Plugins.LocalService",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEPluginLocalServiceTest::RunTest(const FString&)
{
    auto* GI=NewObject<UGameInstance>();GI->Init();auto* H=GI->GetSubsystem<UACEPluginSubsystem>();
    const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation")/(TEXT("LocalService-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    ON_SCOPE_EXIT {GI->Shutdown();}; // Isolated, source-only diagnostic fixture.
    H->StorageRoot=Root;H->Settings=MakeShared<FJsonObject>();
    auto Write=[&](const FString& Id,const FString& Name,const FString& Text)
    {const FString Dir=Root/TEXT("Installed")/Id;IFileManager::Get().MakeDirectory(*Dir,true);return FFileHelper::SaveStringToFile(Text,*(Dir/Name));};
    const FString Script=TEXT("local scanned=false;local restored=false;return function(s,p) if s.busy then return nil end if not scanned then scanned=true;return {action='local_cosmetics',operation='scan'} end if not restored then restored=true;if s.has_selection then return {action='local_cosmetics',operation='reload'} end end end");
    Write(TEXT("sample_cosmetic"),TEXT("plugin.json"),TEXT(R"({"api":1,"id":"sample_cosmetic","name":"Sample Cosmetic","mode":"local","service_api":1,"permissions":["local_cosmetics"]})"));
    Write(TEXT("sample_cosmetic"),TEXT("main.lua"),Script);
    Write(TEXT("sample_cosmetic"),TEXT("default.json"),TEXT("{}"));
    Write(TEXT("bad"),TEXT("plugin.json"),TEXT(R"({"api":1,"id":"bad","mode":"local","service_api":1,"permissions":["local_cosmetics","cast"]})"));
    Write(TEXT("old"),TEXT("plugin.json"),TEXT(R"({"api":1,"id":"old","mode":"local","service_api":2,"permissions":["local_cosmetics"]})"));
    H->Discover();auto P=H->Find(TEXT("sample_cosmetic"));
    if(!TestTrue(TEXT("Dropped folder is discovered"),P.IsValid()))return false;
    TestFalse(TEXT("Local service defaults to disabled"),P->Enabled);
    TestFalse(TEXT("Mixed gameplay permissions rejected"),H->Find(TEXT("bad")).IsValid());
    TestFalse(TEXT("Unsupported bridge API rejected"),H->Find(TEXT("old")).IsValid());
    H->UnregisterLocalService(TEXT("sample_cosmetic"));H->SetEnabled(TEXT("sample_cosmetic"),true);H->TickLocalServices();
    TestFalse(TEXT("Missing capability does not run Lua"),P->Running);
    TestTrue(TEXT("Missing capability explains client update"),P->Status.Contains(TEXT("client build")));
    int32 Calls=0,Stops=0;bool Busy=false,Selected=true;
    auto Adapter=MakeShared<FACEPluginLocalService>();
    Adapter->Snapshot=[&](){auto S=MakeShared<FJsonObject>();S->SetBoolField(TEXT("busy"),Busy);S->SetBoolField(TEXT("has_selection"),Selected);S->SetStringField(TEXT("status"),TEXT("Ready"));return S;};
    Adapter->Execute=[&](const TSharedPtr<FJsonObject>& I,FString& E){++Calls;return I->GetStringField(TEXT("action"))==TEXT("local_cosmetics");};
    Adapter->Stop=[&](){++Stops;};Adapter->MakePanel=[]()->TSharedRef<SWidget>{return SNew(STextBlock).Text(FText::FromString(TEXT("Local controls")));};
    H->RegisterLocalService(TEXT("sample_cosmetic"),Adapter);H->TickLocalServices();
    TestTrue(TEXT("Enabled service auto-starts on login screen without a living character"),P->Running);
    TestEqual(TEXT("Independent Lua fixture starts discovery"),Calls,1);
    Busy=true;P->NextDecision=0;H->TickLocalServices();TestEqual(TEXT("Lua does not submit while busy"),Calls,1);
    Busy=false;P->NextDecision=0;H->TickLocalServices();TestEqual(TEXT("Saved selection reloaded after discovery"),Calls,2);
    // A cosmetic stop must not clear another plugin's movement/spell ownership.
    H->MovementOwner=TEXT("ucm");H->PendingSpellOwner=TEXT("ucm");H->PendingSpell=42;
    const int32 Before=Stops;H->SetEnabled(TEXT("sample_cosmetic"),false);
    TestEqual(TEXT("Disable calls native cleanup immediately"),Stops,Before+1);
    TestEqual(TEXT("Disable preserves UCM movement"),H->MovementOwner,FString(TEXT("ucm")));
    TestEqual(TEXT("Disable preserves UCM spell"),H->PendingSpell,42);
    H->TickLocalServices();TestFalse(TEXT("Disabled script stays stopped"),P->Running);
    H->SetEnabled(TEXT("sample_cosmetic"),true);H->TickLocalServices();
    const int32 BeforeRefresh=Stops;H->Discover();
    TestEqual(TEXT("Refresh stops old adapter"),Stops,BeforeRefresh+1);
    P=H->Find(TEXT("sample_cosmetic"));TestTrue(TEXT("Enable preference survives rediscovery"),P&&P->Enabled);
    H->TickLocalServices();TestTrue(TEXT("Rediscovered service restarts and re-verifies"),P&&P->Running);
    H->MakePanel(TEXT("sample_cosmetic"));
    // Unauthorized gameplay output is routed only to the adapter, which rejects it.
    H->Stop(TEXT("sample_cosmetic"));Write(TEXT("sample_cosmetic"),TEXT("main.lua"),TEXT("return function() return {action='cast',spell=42} end"));
    H->SetEnabled(TEXT("sample_cosmetic"),true);H->TickLocalServices();
    TestFalse(TEXT("Rejected service intent stops the script"),P->Running);
    const int32 FailedCalls=Calls;H->TickLocalServices();TestEqual(TEXT("Failing script does not restart in a loop"),Calls,FailedCalls);
    H->MovementOwner.Empty();H->PendingSpellOwner.Empty();H->PendingSpell=0;
    // Destroy callbacks capturing this test's stack before scope exit shuts down GI.
    H->UnregisterLocalService(TEXT("sample_cosmetic"));
    return true;
}
#endif
