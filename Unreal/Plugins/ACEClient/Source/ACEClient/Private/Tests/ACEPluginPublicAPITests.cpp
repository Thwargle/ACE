#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Interfaces/IPluginManager.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginVM.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/FileManager.h"

namespace
{
    TSharedPtr<FJsonObject> ParseAPI(const TCHAR* Text)
    {TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPluginPublicAPITest,"ACE.Plugins.PublicAPI",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEPluginPublicAPITest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);auto* GI=NewObject<UGameInstance>(GEngine);W->SetGameInstance(GI);
    Context.OwningGameInstance=GI;Context.SetCurrentWorld(W);GI->OnWorldChanged(nullptr,W);GI->Init();
    auto* H=GI->GetSubsystem<UACEPluginSubsystem>();auto* C=GI->GetSubsystem<UACEClientSubsystem>();
    const FString Root=FPaths::ProjectSavedDir()/TEXT("Automation/PublicAPI")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
    H->StorageRoot=Root;H->Settings=MakeShared<FJsonObject>();
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(W);W->DestroyWorld(false);IFileManager::Get().DeleteDirectory(*Root,false,true);};
    const FString Installed=Root/TEXT("Installed/thirdparty");IFileManager::Get().MakeDirectory(*Installed,true);
    FFileHelper::SaveStringToFile(TEXT(R"({"api":2,"mode":"automation","policy":"ucm","id":"thirdparty","name":"API Workbench","permissions":["cast","combat","navigation","inventory","loot","fellowship","confirm","say","chat","state","ui","markers","waypoint","messaging","commands","profiles"]})"),*(Installed/TEXT("plugin.json")));
    FFileHelper::SaveStringToFile(TEXT(R"({"combat":"off","buffing":false,"recovery":false,"looting":false,"navigation":false})"),*(Installed/TEXT("default.json")));
    FFileHelper::SaveStringToFile(TEXT("assert(type(builtinpolicy)=='function' and type(builtinprofile)=='table');return function(s,p) return {status='custom'} end"),*(Installed/TEXT("main.lua")));
    H->Discover();auto P=H->Find(TEXT("thirdparty"));if(!TestTrue(TEXT("API 2 discovered"),P&&P->Api==2&&P->IsPolicy()))return false;
    C->Session=MakeShared<FACESession>();auto& Session=*C->Session;Session.State=EACESessionState::InWorld;Session.PlayerGuid=0x50000001;Session.ServerName=TEXT("Public API Test");
    Session.PlayerPosition.CellId=0x7D64000D;Session.PlayerPosition.Location=FVector(34,104,12);Session.PlayerVitals.bValid=true;Session.PlayerVitals.Health=100;Session.PlayerVitals.MaxHealth=100;
    FACEWorldObject Player;Player.Guid=Session.PlayerGuid;Player.Name=TEXT("API character");Player.Position=Session.PlayerPosition;Player.bIsPlayer=true;Session.WorldObjects.Add(Player.Guid,Player);
    FACEWorldObject Rat;Rat.Guid=12;Rat.Name=TEXT("Red Rat");Rat.ItemType=ACEItemType::Creature;Rat.bHasPosition=true;Rat.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;Rat.Position=Session.PlayerPosition;Rat.Position.Location.X+=5;Session.WorldObjects.Add(Rat.Guid,Rat);
    H->SetEnabled(TEXT("ucm"),false);H->SetEnabled(P->Id,true);
    TestTrue(TEXT("Independent UCM policy library loads with built-in UCM disabled"),H->Start(P->Id));
    TestTrue(TEXT("Custom ID owns native policy data"),H->PolicyPlugin()==P);H->RefreshAutomationData();
    C->OnChatMessage.Broadcast(TEXT("Quest message"),TEXT(""),ACEChatMessageType::System);
    auto Snapshot=H->PublicSnapshot(*P);
    for(const TCHAR* Field:{TEXT("inventory"),TEXT("spells"),TEXT("skills"),TEXT("targets"),TEXT("corpses"),TEXT("vendor_stock"),TEXT("fellowship"),TEXT("it_objects"),TEXT("packs"),TEXT("chat_events"),TEXT("events"),TEXT("capabilities")})TestTrue(FString(TEXT("Public snapshot includes "))+Field,Snapshot->HasField(Field));
    TestEqual(TEXT("Chat independent of UCM"),Snapshot->GetArrayField(TEXT("chat_events")).Num(),1);
    for(int32 I=0;I<300;++I)C->OnChatMessage.Broadcast(FString::FromInt(I),TEXT(""),ACEChatMessageType::System);
    int32 QuestLine=0;
    for(int32 Count:{128,128,44})
    {
        const auto Batch=H->PublicSnapshot(*P);const auto& Events=Batch->GetArrayField(TEXT("chat_events"));
        TestEqual(TEXT("API 2 receives bounded chat pages"),Events.Num(),Count);
        TestFalse(TEXT("API 2 burst retains every event"),Batch->GetBoolField(TEXT("chat_gap")));
        for(const auto& Event:Events)TestEqual(TEXT("Full automation snapshot preserves paged chat order"),Event->AsObject()->GetStringField(TEXT("raw_text")),FString::FromInt(QuestLine++));
        TestEqual(TEXT("Public snapshot exposes remaining chat"),Batch->GetNumberField(TEXT("chat_pending")),double(300-QuestLine));
    }
    FString Error;TSharedPtr<FJsonObject> Output;
    const auto UI=ParseAPI(TEXT(R"JSON({"state":{"kills":12},"ui":[
      {"id":"title","text":"Kill tasks & automation","size":24,"bold":true},
      {"id":"tabs","type":"tabs","children":[{"id":"tracker","type":"column","text":"Tracker","children":[
        {"id":"progress","type":"progress","text":"Red Rat: 12 / 50","value":0.24,"color":"green"},
        {"id":"settings","type":"section","text":"Hunting preferences","children":[
          {"id":"combat","type":"checkbox","text":"Combat enabled","value":true},
          {"id":"range","type":"slider","text":"Acquisition range (metres)","min":0,"max":240,"step":5,"value":60},
          {"id":"count","type":"number","text":"Task goal","min":1,"max":1000,"value":50},
          {"id":"filter","type":"input","text":"Monster name","value":"Red Rat","live":true},
          {"id":"mode","type":"select","text":"Strategy","value":"Automatic","options":["Automatic","Melee","Magic"]}]},
        {"id":"buttons","type":"row","children":[{"id":"rat","type":"button","text":"Select Red Rat","action":"select","target":12},{"id":"refresh","type":"button","text":"Refresh tasks"}]}]},
        {"id":"library","type":"column","text":"Profiles","children":[{"id":"note","text":"Named routes, loot rules and settings are reusable."}]}]}]})JSON"));
    TestTrue(TEXT("All structured controls validate"),H->ApplyPublicOutput(*P,UI,Error));
    TestTrue(TEXT("Nested selection works"),H->ClickPluginRow(P->Id,TEXT("rat")));TestEqual(TEXT("Native target selected"),C->GetSelectedObject().Guid,12);
    TestTrue(TEXT("Checkbox change accepted"),H->ChangePluginControl(P->Id,TEXT("combat"),MakeShared<FJsonValueBoolean>(false)));
    TestTrue(TEXT("Slider snaps and clamps"),H->ChangePluginControl(P->Id,TEXT("range"),MakeShared<FJsonValueNumber>(999)));
    TestEqual(TEXT("Slider event contains clamped value"),P->PluginEvents.Last()->AsObject()->GetNumberField(TEXT("value")),240.);
    TestFalse(TEXT("Unsupported dropdown value refused"),H->ChangePluginControl(P->Id,TEXT("mode"),MakeShared<FJsonValueString>(TEXT("invalid"))));
    TestFalse(TEXT("Wrong control data type refused"),H->ChangePluginControl(P->Id,TEXT("combat"),MakeShared<FJsonValueNumber>(1)));
    TestFalse(TEXT("Duplicate nested IDs refused"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"ui":[{"id":"x","type":"column","children":[{"id":"x","text":"duplicate"}]}]})")),Error));
    H->FlushPluginState(*P);TestTrue(TEXT("Automation state saves independently"),IFileManager::Get().FileExists(*P->StatePath));
    H->Stop(P->Id);H->Start(P->Id);TestEqual(TEXT("Automation state restored"),P->SavedState->GetNumberField(TEXT("kills")),12.);
    H->ApplyPublicOutput(*P,UI,Error);
    auto Request=[&](const TCHAR* Text){P->NextService=0;return H->ApplyPublicOutput(*P,ParseAPI(Text),Error);};
    TestTrue(TEXT("Save service accepted"),Request(TEXT(R"({"requests":[{"id":"save","service":"profiles","operation":"save","group":"Routes","name":"Loop","data":{"route":[{"cell":2103705613,"x":34,"y":104,"z":12}]}}]})")));
    TestTrue(TEXT("Profile write confirmed"),P->PluginEvents.Last()->AsObject()->GetBoolField(TEXT("ok")));
    Request(TEXT(R"({"requests":[{"id":"read","service":"profiles","operation":"read","group":"Routes","name":"Loop"}]})"));
    TestTrue(TEXT("Profile returns editable data"),P->PluginEvents.Last()->AsObject()->GetObjectField(TEXT("data"))->HasField(TEXT("route")));
    Request(TEXT(R"({"requests":[{"id":"bad","service":"profiles","operation":"save","group":"../ucm","name":"Default"}]})"));
    TestFalse(TEXT("Cannot escape plugin profile directory"),P->PluginEvents.Last()->AsObject()->GetBoolField(TEXT("ok")));
    Request(TEXT(R"({"requests":[{"id":"delete","service":"profiles","operation":"delete","group":"Routes","name":"Loop"}]})"));
    TestTrue(TEXT("Delete archives profile"),P->PluginEvents.Last()->AsObject()->GetBoolField(TEXT("ok")));
    TArray<FString> Archived;IFileManager::Get().FindFiles(Archived,*(Root/TEXT("Profiles/thirdparty/Deleted/Routes/*.json")),true,false);TestEqual(TEXT("Archived file preserved"),Archived.Num(),1);
    const FString Inbox=Root/TEXT("ImportInbox");IFileManager::Get().MakeDirectory(*Inbox,true);H->Settings->SetStringField(TEXT("ucm.folder"),Inbox);
    FFileHelper::SaveStringToFile(TEXT("uTank2 NAV 1.2\n2\n2\n0\n0\n0\n0.05\n0\n3\n0.01\n0\n0.05\n0\n1500\n"),*(Inbox/TEXT("Loop.nav")));
    Request(TEXT(R"({"requests":[{"id":"import","service":"import","operation":"convert","name":"Loop.nav"}]})"));
    const auto Imported=P->PluginEvents.Last()->AsObject();TestTrue(TEXT("Public import uses native VT adapter"),Imported->GetBoolField(TEXT("ok")));
    if(Imported->HasField(TEXT("data")))TestEqual(TEXT("Converted route retains both points"),Imported->GetObjectField(TEXT("data"))->GetArrayField(TEXT("route")).Num(),2);
    TestTrue(TEXT("Public import preserves original"),IFileManager::Get().FileExists(*(Inbox/TEXT("Loop.nav"))));
    Request(TEXT(R"({"requests":[{"id":"activate","service":"import","operation":"activate","name":"Loop.nav","profile_name":"ImportedLoop"}]})"));
    TestTrue(TEXT("Import activation saves own profile"),P->PluginEvents.Last()->AsObject()->GetBoolField(TEXT("ok"))&&P->ProfileName==TEXT("ImportedLoop"));
    TestEqual(TEXT("Activation applies native route"),P->Profile->GetArrayField(TEXT("route")).Num(),2);
    P->Profile->SetArrayField(TEXT("route"),{});
    Request(TEXT(R"({"requests":[{"id":"record","service":"route","operation":"record","kind":"walk"}]})"));
    TestTrue(TEXT("Native route point recorded in custom profile"),P->Profile->GetArrayField(TEXT("route")).Num()==1);
    Request(TEXT(R"({"requests":[{"id":"force","service":"force_buff","operation":"start"}]})"));TestTrue(TEXT("Custom force buff reaches snapshot"),H->PublicSnapshot(*P)->GetNumberField(TEXT("force_buff_request"))>0);
    Request(TEXT(R"({"requests":[{"id":"cancel","service":"force_buff","operation":"cancel"}]})"));
    H->Execute(*P,ParseAPI(TEXT(R"({"runtime_route":[{"cell":2103705613,"x":34,"y":104,"z":12},{"cell":2103705613,"x":40,"y":104,"z":12}],"route_point":2})")));
    TestEqual(TEXT("Custom plugin controls route point"),H->RoutePoint,2);H->DrawRoute();TestTrue(TEXT("Custom plugin route rendered with UCM geometry"),H->RouteMesh&&H->RouteMesh->GetNumSections()>0);
    H->CombatTarget=Rat.Guid;P->NextDecision=FPlatformTime::Seconds()+10;H->ObserveTargetHealth(Rat.Guid,0);TestTrue(TEXT("Target death wakes custom policy immediately"),P->NextDecision==0&&H->CombatTarget==0);
    H->ReportActivityFailure(*P,ParseAPI(TEXT(R"({"action":"loot","activity":"loot"})")),TEXT("Transfer rejected"));TestTrue(TEXT("Custom activity failure does not kill automation"),P->Running&&P->ActivityFailure.IsValid());
    auto FailureSnapshot=H->PublicSnapshot(*P);
    TestTrue(TEXT("Custom activity failure is observable"),FailureSnapshot->HasField(TEXT("activity_failure")));
    TestTrue(TEXT("Wire action error remains numeric for reused UCM policy"),FailureSnapshot->HasTypedField<EJson::Number>(TEXT("action_error")));
    TestTrue(TEXT("Native failures logged to custom plugin ring"),!P->LogEntries.IsEmpty());
    P->Permissions.Remove(TEXT("profiles"));TestFalse(TEXT("Profile patch permission enforced"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"profile_patch":{"combat":"off"}})")),Error));
    Request(TEXT(R"({"requests":[{"id":"deny","service":"profiles","operation":"save","name":"No"}]})"));TestFalse(TEXT("File service permission enforced"),P->PluginEvents.Last()->AsObject()->GetBoolField(TEXT("ok")));P->Permissions.Add(TEXT("profiles"));
    TestFalse(TEXT("Malformed route profile rejected before native use"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"profile_patch":{"route":[42]}})")),Error));
    TestFalse(TEXT("Malformed restock data rejected before native snapshot"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"profile_patch":{"restock":[42]}})")),Error));
    P->Permissions.Remove(TEXT("navigation"));TestFalse(TEXT("Route drawing permission enforced"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"runtime_route":[]})")),Error));P->Permissions.Add(TEXT("navigation"));
    auto Watcher=MakeShared<FACEClientPlugin>();Watcher->Id=TEXT("observer");Watcher->Companion=true;Watcher->Api=2;Watcher->Running=true;Watcher->Enabled=true;Watcher->Permissions={TEXT("ui"),TEXT("profiles")};H->Plugins.Add(Watcher);
    TestFalse(TEXT("Observer never becomes automation owner"),Watcher->IsPolicy());TestTrue(TEXT("Observer basic snapshot avoids inventory work"),!H->PublicSnapshot(*Watcher)->HasField(TEXT("inventory")));
    const auto PreviousRoute=H->RuntimeRoute;H->Stop(Watcher->Id);TestTrue(TEXT("Stopping observer preserves automation route"),H->RuntimeRoute==PreviousRoute&&P->Running);
    H->Stop(TEXT("ucm"));TestTrue(TEXT("Stopping inactive UCM preserves custom automation route"),H->RuntimeRoute==PreviousRoute&&P->Running);
    FACEPluginVM Library;TestTrue(TEXT("Reusable policy is the shipped implementation"),Library.Load(TEXT("return function(s,p) return builtinpolicy(s,p) end"),Error,TEXT("ucm")));
    auto Off=ParseAPI(TEXT(R"({"combat":"off","buffing":false,"recovery":false,"looting":false,"navigation":false,"meta_enabled":false})"));
    TestTrue(TEXT("Reused UCM policy executes on full native API snapshot"),Library.Step(H->PublicSnapshot(*P),Off,Output,Error));
    FACEPluginVM Workbench;const FString Example=IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("Docs/Plugins/Examples/api_workbench");
    FString Source,Defaults;FFileHelper::LoadFileToString(Source,*(Example/TEXT("main.lua")));FFileHelper::LoadFileToString(Defaults,*(Example/TEXT("default.json")));
    TestTrue(TEXT("Documented author example loads"),Workbench.Load(Source,Error,TEXT("ucm")));P->PluginEvents.Reset();P->ResumeMetaPending=false;
    TestTrue(TEXT("Documented author example executes"),Workbench.Step(H->PublicSnapshot(*P),ParseAPI(*Defaults),Output,Error));
    if(Output){TestTrue(TEXT("Documented author UI/output accepted"),H->ApplyPublicOutput(*P,Output,Error));TestFalse(TEXT("Empty patch does not restart route/buff work"),P->ResumeMetaPending);}
    FACEPluginVM Empty;TestTrue(TEXT("Empty collections script loads"),Empty.Load(TEXT("return function() return {profile_patch={route={},loot_rules={}},ui={{id='empty',type='select',options={}}},requests={}} end"),Error));
    TestTrue(TEXT("Empty Lua collections decode"),Empty.Step(Snapshot,Off,Output,Error));
    if(Output)TestTrue(TEXT("Empty route/rules/UI lists validate"),H->ApplyPublicOutput(*P,Output,Error));
    FACEPluginVM LongRoute;TestTrue(TEXT("Maximum route script loads"),LongRoute.Load(TEXT("return function() local r={} for i=1,2048 do r[i]={cell=2103705613,x=34,y=104,z=12,kind='walk'} end return {profile_patch={route=r}} end"),Error));
    TestTrue(TEXT("Structured output supports UCM's full route length"),LongRoute.Step(Snapshot,Off,Output,Error));
    if(Output)TestTrue(TEXT("Maximum route profile accepted"),H->ApplyPublicOutput(*P,Output,Error));
    if(FApp::CanEverRender())
    {
        H->ApplyPublicOutput(*P,UI,Error);FWidgetRenderer Renderer(false,true);auto Panel=H->MakePanel(P->Id);const FVector2D Size(740,880);auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
        for(int I=0;I<3;++I){Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();}
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(740,880,Pixels,Png);FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("PublicAPI-panel.png")));Target->ReleaseResource();
    }
    H->Stop(P->Id);TestFalse(TEXT("Public automation stops normally"),P->Running);
    P->Profile=Off;P->Profile->SetBoolField(TEXT("mana_charges_when_off"),true);H->NextIdleManaDecision=0;H->Tick(0);
    TestTrue(TEXT("Custom plugin can opt into stopped gear recharge"),H->IdleManaOwner==P->Id&&H->IdleManaVM.IsValid()&&!H->IdleManaFailed&&!P->Running);
    return true;
}
#endif
