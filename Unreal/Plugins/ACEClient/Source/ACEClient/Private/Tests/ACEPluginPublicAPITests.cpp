#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACETestDatDirectory.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Interfaces/IPluginManager.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginVM.h"
#include "Mods/ACEPluginPresentation.h"
#include "Mods/ACEPluginDesktop.h"
#include "Mods/ACEPluginStyle.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIResourceResolver.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ScrollBox.h"
#include "ACEDatSubsystem.h"
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
    FFileHelper::SaveStringToFile(TEXT(R"({"api":2,"mode":"automation","policy":"ucm","id":"thirdparty","name":"API Workbench","presentation":{"title":"Quest Tracker","icon":"tracker.png"},"permissions":["cast","combat","navigation","inventory","loot","fellowship","confirm","say","chat","state","ui","markers","waypoint","messaging","commands","profiles"]})"),*(Installed/TEXT("plugin.json")));
    FFileHelper::SaveStringToFile(TEXT(R"({"combat":"off","buffing":false,"recovery":false,"looting":false,"navigation":false})"),*(Installed/TEXT("default.json")));
    FFileHelper::SaveStringToFile(TEXT("assert(type(builtinpolicy)=='function' and type(builtinprofile)=='table');return function(s,p) return {status='custom'} end"),*(Installed/TEXT("main.lua")));
    H->Discover();auto P=H->Find(TEXT("thirdparty"));if(!TestTrue(TEXT("API 2 discovered"),P&&P->Api==2&&P->IsPolicy()))return false;
    TestEqual(TEXT("Manifest title is visible before starting Lua"),P->DisplayTitle(),FString(TEXT("Quest Tracker")));
    TestEqual(TEXT("Manifest supplies custom toolbar artwork"),P->DisplayIcon(),FString(TEXT("tracker.png")));
    C->Session=MakeShared<FACESession>();auto& Session=*C->Session;Session.State=EACESessionState::InWorld;Session.PlayerGuid=0x50000001;Session.ServerName=TEXT("Public API Test");
    Session.PlayerPosition.CellId=0x7D64000D;Session.PlayerPosition.Location=FVector(34,104,12);Session.PlayerVitals.bValid=true;Session.PlayerVitals.Health=100;Session.PlayerVitals.MaxHealth=100;
    FACEWorldObject Player;Player.Guid=Session.PlayerGuid;Player.Name=TEXT("API character");Player.Position=Session.PlayerPosition;Player.bIsPlayer=true;Session.WorldObjects.Add(Player.Guid,Player);
    FACEWorldObject Rat;Rat.Guid=12;Rat.Name=TEXT("Red Rat");Rat.ItemType=ACEItemType::Creature;Rat.bHasPosition=true;Rat.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;Rat.Position=Session.PlayerPosition;Rat.Position.Location.X+=5;Session.WorldObjects.Add(Rat.Guid,Rat);
    H->SetEnabled(TEXT("ucm"),false);H->SetEnabled(P->Id,true);
    TestTrue(TEXT("Independent UCM policy library loads with built-in UCM disabled"),H->Start(P->Id));
    TestTrue(TEXT("Custom ID owns native policy data"),H->PolicyPlugin()==P);H->RefreshAutomationData();
    C->OnChatMessage.Broadcast(TEXT("Quest message"),TEXT(""),ACEChatMessageType::System);
    auto Snapshot=H->PublicSnapshot(*P);
    {
        FACEClientPlugin Observer;Observer.Api=2;Observer.Companion=true;Observer.SelectiveSnapshot=true;
        Observer.Permissions={TEXT("data")};Observer.SnapshotFields={TEXT("char_ints"),TEXT("health"),TEXT("character_name"),TEXT("action_serial")};
        UACEPluginSubsystem::FCompanionReadBatch Batch;
        TGuardValue<UACEPluginSubsystem::FCompanionReadBatch*> Guard(H->CompanionReadBatch,&Batch);
        const auto Small=H->PublicSnapshot(Observer);
        TestTrue(TEXT("Selective data exposes character qualities and action counter"),Small->HasField(TEXT("char_ints"))&&Small->HasField(TEXT("action_serial")));
        TestEqual(TEXT("Selective data keeps requested health"),Small->GetNumberField(TEXT("health")),100.);
        TestFalse(TEXT("Character observer never builds full gameplay data"),Batch.Data.IsValid());
        TestFalse(TEXT("Unused inventory never crosses into Lua"),Small->HasField(TEXT("inventory")));
        TestFalse(TEXT("Unused nearby objects never cross into Lua"),Small->HasField(TEXT("objects")));
        TestTrue(TEXT("Events and capability envelope survive field selection"),Small->HasField(TEXT("events"))&&Small->HasField(TEXT("capabilities")));
        Observer.Permissions.Reset();const auto Restricted=H->PublicSnapshot(Observer);
        TestFalse(TEXT("Field requests cannot grant data permission"),Restricted->HasField(TEXT("char_ints"))||Restricted->HasField(TEXT("action_serial")));
    }
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
    {
        FACEWorldObject Pet=Rat;Pet.Guid=999;Pet.PetOwnerId=Session.PlayerGuid;
        Session.WorldObjects.Add(Pet.Guid,Pet);
        TestTrue(TEXT("Live owned pet suppresses another summon"),H->PublicSnapshot(*P)->GetBoolField(TEXT("owned_pet")));
        Session.WorldObjects[Pet.Guid].PhysicsState|=ACEPhysicsState::NoDraw;
        TestFalse(TEXT("Despawned pet awaiting delete no longer blocks summoning"),H->PublicSnapshot(*P)->GetBoolField(TEXT("owned_pet")));
        Session.WorldObjects.Remove(Pet.Guid);
    }
    {
        FACEPluginVM FilterScript;
        TestTrue(TEXT("Chat-filter Lua loads"),FilterScript.Load(TEXT("return function() return {chat_filters={{id='quests',contains=' solves (',sender='',chat_type=6,window=0,seconds=10}}} end"),Error));
        TestTrue(TEXT("Lua decodes declarative chat filters"),FilterScript.Step(Snapshot,P->Profile,Output,Error));
        TestFalse(TEXT("Read-only chat permission alone cannot hide messages"),H->ApplyPublicOutput(*P,Output,Error));
        P->Permissions.Add(TEXT("chat_filter"));
        TestTrue(TEXT("Opt-in display filters are accepted"),H->ApplyPublicOutput(*P,Output,Error));
        const FString Dump=TEXT("samplequest - 1 solves (12345)\"Quest\" -1 0");
        TestEqual(TEXT("Quest dump is hidden in main chat only"),int32(H->FilterPluginChatWindows(Dump,TEXT(""),6)),1);
        TestEqual(TEXT("Other message types remain visible"),int32(H->FilterPluginChatWindows(Dump,TEXT(""),3)),0);
        TestEqual(TEXT("Player messages cannot match server-only filter"),int32(H->FilterPluginChatWindows(Dump,TEXT("Friend"),6)),0);
        TestEqual(TEXT("Unrelated server output remains visible"),int32(H->FilterPluginChatWindows(TEXT("Bonus XP active"),TEXT(""),6)),0);
        auto* Chat=NewObject<UACEUIGameplayBinder>(GI);Chat->Client=C;
        Chat->Canvas=NewObject<UACEUICanvasWidget>(GI);Chat->Canvas->WidgetTree=NewObject<UWidgetTree>(Chat->Canvas);
        Chat->ChatLog=NewObject<UScrollBox>(Chat->Canvas);Chat->MainChatTypeFilter=~uint64(0);Chat->GlobalChatTypeFilter=~uint64(0);
        C->OnChatMessage.Broadcast(Dump,TEXT(""),6);
        Chat->HandleChatMessage(Dump,TEXT(""),6);
        TestEqual(TEXT("Filtered response never creates a visible chat row"),Chat->ChatLog->GetChildrenCount(),0);
        const auto Read=H->PublicSnapshot(*P)->GetArrayField(TEXT("chat_events"));
        TestTrue(TEXT("Hidden response still reaches Lua intact"),Read.Num()==1&&Read[0]->AsObject()->GetStringField(TEXT("raw_text"))==Dump);
        C->OnChatMessage.Broadcast(TEXT("Bonus XP active"),TEXT(""),6);
        Chat->HandleChatMessage(TEXT("Bonus XP active"),TEXT(""),6);
        TestEqual(TEXT("Unrelated server output is drawn normally"),Chat->ChatLog->GetChildrenCount(),1);
        TestFalse(TEXT("Invalid replacement is rejected"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"chat_filters":[{"id":"bad","window":5,"contains":"x"}]})")),Error));
        TestEqual(TEXT("Invalid replacement retains active filters"),int32(H->FilterPluginChatWindows(Dump,TEXT(""),6)),1);
        P->ChatFilters[0].Expires=FPlatformTime::Seconds()-1;
        TestEqual(TEXT("Expired filters stop hiding immediately"),int32(H->FilterPluginChatWindows(Dump,TEXT(""),6)),0);
        H->ApplyPublicOutput(*P,Output,Error);H->Stop(P->Id);
        TestTrue(TEXT("Stopping removes every filter owned by that plugin"),P->ChatFilters.IsEmpty());H->Start(P->Id);
        H->ApplyPublicOutput(*P,Output,Error);Session.PlayerGuid++;
        TestEqual(TEXT("Previous character filters cannot hide current chat"),int32(H->FilterPluginChatWindows(Dump,TEXT(""),6)),0);
        H->UpdateCompanionChatContext();TestTrue(TEXT("Character transition clears filter registrations"),P->ChatFilters.IsEmpty());Session.PlayerGuid--;H->UpdateCompanionChatContext();
        H->ApplyPublicOutput(*P,Output,Error);
        TestTrue(TEXT("Explicit empty list clears filters"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"chat_filters":[]})")),Error)&&P->ChatFilters.IsEmpty());
        for(const TCHAR* Bad:{TEXT(R"({"chat_filters":[{"id":"x","chat_type":6}]})"),TEXT(R"({"chat_filters":[{"id":"x","contains":"a","seconds":301}]})"),TEXT(R"({"chat_filters":[{"id":"x","contains":"a"},{"id":"x","contains":"b"}]})")})
            TestFalse(TEXT("Malformed or unbounded display rules rejected"),H->ApplyPublicOutput(*P,ParseAPI(Bad),Error));
        TestTrue(TEXT("Literal exact/prefix filters accepted"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"chat_filters":[{"id":"bonus","prefix":"BONUS","exact":"Bonus active","window":4,"case_sensitive":false}]})")),Error));
        TestEqual(TEXT("Extra window mask and case-insensitive conjunction work"),int32(H->FilterPluginChatWindows(TEXT("Bonus active"),TEXT(""),6)),16);
        TestEqual(TEXT("Partial exact matches do not hide other lines"),int32(H->FilterPluginChatWindows(TEXT("Bonus active tomorrow"),TEXT(""),6)),0);
        H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"chat_filters":[]})")),Error);
    }
    FACEPluginVM Presentation;
    TestTrue(TEXT("Lua presentation script loads"),Presentation.Load(TEXT("return function() return {presentation={title='Quest Tracker - 12 kills',icon='active.png',window={opacity=0.35,pinned=true}}} end"),Error));
    TestTrue(TEXT("Lua decoder exposes presentation output"),Presentation.Step(Snapshot,P->Profile,Output,Error));
    TestTrue(TEXT("Lua presentation applies"),Output&&H->ApplyPublicOutput(*P,Output,Error));
    TestEqual(TEXT("Lua overrides the title"),P->DisplayTitle(),FString(TEXT("Quest Tracker - 12 kills")));
    TestEqual(TEXT("Lua overrides the toolbar icon"),P->DisplayIcon(),FString(TEXT("active.png")));
    TestEqual(TEXT("Lua supplies window opacity"),H->GetPluginWindowOpacity(P->Id),.35f);
    TestTrue(TEXT("Lua can request a pinned window"),H->IsPluginWindowPinned(P->Id));
    H->SetPluginWindowOpacity(P->Id,.6f);H->SetPluginWindowPinned(P->Id,false);
    TestEqual(TEXT("Player opacity overrides plugin request"),H->GetPluginWindowOpacity(P->Id),.6f);
    TestFalse(TEXT("Player can unpin a plugin-pinned window"),H->IsPluginWindowPinned(P->Id));
    H->SetPluginWindowOpacity(FString(),.2f);H->SetPluginOpacityGlobal(true);
    TestEqual(TEXT("Global override wins over user and plugin values"),H->GetPluginWindowOpacity(P->Id),.2f);
    H->SetPluginOpacityGlobal(false);H->ResetPluginWindowOpacity(P->Id);
    TestEqual(TEXT("Clearing override restores plugin opacity"),H->GetPluginWindowOpacity(P->Id),.35f);
    for(const TCHAR* Invalid:{TEXT(R"({"presentation":{"window":{"opacity":2}}})"),TEXT(R"({"presentation":{"window":{"opacity":-0.1}}})"),
        TEXT(R"({"presentation":{"window":{"pinned":"yes"}}})"),TEXT(R"({"presentation":{"window":{"width":10}}})")})
        TestFalse(TEXT("Invalid window requests are rejected"),H->ApplyPublicOutput(*P,ParseAPI(Invalid),Error));
    TestEqual(TEXT("Rejected appearance preserves previous output"),H->GetPluginWindowOpacity(P->Id),.35f);
    H->SetPluginWindowOpacity(FString(),1.f);
    TestTrue(TEXT("Other outputs retain presentation"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"status":"Counting"})")),Error));
    TestEqual(TEXT("Omitted presentation is unchanged"),P->DisplayTitle(),FString(TEXT("Quest Tracker - 12 kills")));
    TestFalse(TEXT("Icon path traversal refused"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"presentation":{"title":"Bad","icon":"../secret.png"}})")),Error));
    TestEqual(TEXT("Invalid presentation is atomic"),P->DisplayTitle(),FString(TEXT("Quest Tracker - 12 kills")));
    P->Permissions.Remove(TEXT("ui"));
    TestFalse(TEXT("Runtime presentation requires UI permission"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"presentation":{"title":"Bad"}})")),Error));P->Permissions.Add(TEXT("ui"));
    TestTrue(TEXT("Empty presentation script loads"),Presentation.Load(TEXT("return function() return {presentation={}} end"),Error));
    TestTrue(TEXT("Empty Lua presentation remains an object"),Presentation.Step(Snapshot,P->Profile,Output,Error));
    TestTrue(TEXT("Empty presentation restores manifest defaults"),Output&&H->ApplyPublicOutput(*P,Output,Error));
    TestEqual(TEXT("Default title restored"),P->DisplayTitle(),FString(TEXT("Quest Tracker")));
    IFileManager::Get().MakeDirectory(*(Installed/TEXT("images")),true);
    TArray<FColor> IconPixels;IconPixels.Init(FColor(240,170,35),32*16);TArray64<uint8> IconPng;
    FImageUtils::PNGCompressImageArray(32,16,IconPixels,IconPng);FFileHelper::SaveArrayToFile(IconPng,*(Installed/TEXT("images/tracker.png")));
    P->Images=MakeShared<FACEPluginImageCache>();auto* IconTexture=P->Images->Load(P->Directory,P->DisplayIcon());
    TestTrue(TEXT("Plugin PNG loads with original proportions"),IconTexture&&IconTexture->GetSizeX()==32&&IconTexture->GetSizeY()==16);
    TestTrue(TEXT("Toolbar and UI share decoded textures"),IconTexture&&P->Images->Load(P->Directory,P->DisplayIcon())==IconTexture);
    TestNull(TEXT("Missing icon falls back without breaking plugin"),P->Images->Load(P->Directory,TEXT("missing.png")));
    TestNull(TEXT("Windows alternate streams cannot be icon paths"),P->Images->Load(P->Directory,TEXT("tracker.png:evil.png")));
    Error.Reset();
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
    TestTrue(TEXT("Item icon metadata accepted"),H->ValidatePluginUI(ParseAPI(TEXT(R"({"ui":[{"id":"item","type":"item_icon","icon":100663297,"overlay":0,"underlay":0,"effects":1,"item_type":1}]})"))->GetArrayField(TEXT("ui")),Error));
    TestFalse(TEXT("Item icon metadata cannot wrap negative IDs"),H->ValidatePluginUI(ParseAPI(TEXT(R"({"ui":[{"id":"item","type":"item_icon","effects":-1}]})"))->GetArrayField(TEXT("ui")),Error));
    TestFalse(TEXT("Invalid control mode rejected"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"presentation":{"controls":"invalid"}})")),Error));
    TestFalse(TEXT("Settings visibility must be boolean"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"presentation":{"settings":"false"}})")),Error));
    TestFalse(TEXT("Scroll policy must be boolean"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"presentation":{"scroll":"false"}})")),Error));
    TestFalse(TEXT("Row sizing must be boolean"),H->ValidatePluginUI(ParseAPI(TEXT(R"({"ui":[{"id":"row","type":"row","compact":"yes"}]})"))->GetArrayField(TEXT("ui")),Error));
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
    {
        auto* Chat=NewObject<UACEUIGameplayBinder>(GI);Chat->Client=C;
        Chat->Canvas=NewObject<UACEUICanvasWidget>(GI);Chat->Canvas->WidgetTree=NewObject<UWidgetTree>(Chat->Canvas);
        Chat->ChatLog=NewObject<UScrollBox>(Chat->Canvas);Chat->MainChatTypeFilter=~uint64(0);Chat->GlobalChatTypeFilter=~uint64(0);
        C->OnRoutedChatMessage.AddDynamic(Chat,&UACEUIGameplayBinder::HandleChatMessage);
        const int32 Before=C->ChatHistory.Num();
        TestTrue(TEXT("Local plugin notice accepted"),Request(TEXT(R"({"requests":[{"id":"notice","service":"notice","operation":"show","text":"Plugin routing regression"}]})")));
        TestEqual(TEXT("Plugin notice reaches routed desktop/mobile/VR chat"),Chat->ChatLog->GetChildrenCount(),1);
        TestEqual(TEXT("Plugin notice is retained for chat rebind"),C->ChatHistory.Num(),Before+1);
        if(C->ChatHistory.Num()>Before)TestEqual(TEXT("Plugin notice retains its routing category"),C->ChatHistory.Last().FilterType,ACEChatMessageType::Plugin);
        C->OnRoutedChatMessage.RemoveDynamic(Chat,&UACEUIGameplayBinder::HandleChatMessage);
    }
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
    {
        FACEPluginVM ChatTools;const FString Folder=FPaths::GetPath(Example)/TEXT("chat_tools");
        FFileHelper::LoadFileToString(Source,*(Folder/TEXT("main.lua")));FFileHelper::LoadFileToString(Defaults,*(Folder/TEXT("default.json")));
        TestTrue(TEXT("Chat Tools example loads"),ChatTools.Load(Source,Error));
        auto ChatState=ParseAPI(TEXT(R"JSON({"time":10,"events":[],"chat_events":[{"color":0,"sender":"","raw_text":"quest - 1 solves (123)"}]})JSON"));
        if(!TestTrue(TEXT("Chat Tools example reads quest dump"),ChatTools.Step(ChatState,ParseAPI(*Defaults),Output,Error)))AddError(Error);
        if(Output){TestTrue(TEXT("Chat Tools example output validates"),H->ApplyPublicOutput(*P,Output,Error));TestEqual(TEXT("Example polling starts disabled"),Output->GetArrayField(TEXT("requests")).Num(),0);}
        ChatState=ParseAPI(TEXT(R"({"time":12,"events":[{"type":"click","id":"quests"}],"chat_events":[]})"));
        TestTrue(TEXT("Chat Tools example handles an on-demand command"),ChatTools.Step(ChatState,ParseAPI(*Defaults),Output,Error));
        if(Output)TestTrue(TEXT("Command response filtering precedes quest request"),Output->GetArrayField(TEXT("chat_filters")).Num()==1&&Output->GetArrayField(TEXT("requests")).Num()==1&&Output->GetArrayField(TEXT("requests"))[0]->AsObject()->GetStringField(TEXT("text"))==TEXT("/myquests"));
        H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"chat_filters":[]})")),Error);
    }
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
        TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>,FName)> FindVisible;
        FindVisible=[&](TSharedRef<SWidget> Widget,FName Tag)->TSharedPtr<SWidget>
        {
            if(!Widget->GetVisibility().IsVisible())return nullptr;
            if(Widget->GetTag()==Tag)return Widget;
            auto* Children=Widget->GetChildren();for(int32 I=0;I<Children->Num();++I)if(auto Found=FindVisible(Children->GetChildAt(I),Tag))return Found;
            return nullptr;
        };
        const auto StartButton=FindVisible(Panel,TEXT("PluginStartToggle"));
        TestTrue(TEXT("Default plugin controls retain separate Start and Stop"),StartButton&&FindVisible(Panel,TEXT("PluginStop")));
        TestTrue(TEXT("Default plugin retains settings and profiles"),FindVisible(Panel,TEXT("PluginSettings")).IsValid());
        TestTrue(TEXT("Runtime control options accepted"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"presentation":{"controls":"toggle","start_label":"Resume tracker","stop_label":"Pause tracker","settings":false}})")),Error));
        Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();
        TestFalse(TEXT("Toggle removes the separate Stop button"),FindVisible(Panel,TEXT("PluginStop")).IsValid());
        TestFalse(TEXT("Settings opt-out hides the complete section"),FindVisible(Panel,TEXT("PluginSettings")).IsValid());
        TestTrue(TEXT("Presentation changes preserve existing controls"),StartButton==FindVisible(Panel,TEXT("PluginStartToggle")));
        if(StartButton)
        {
            auto Toggle=StaticCastSharedPtr<SButton>(StartButton);Toggle->SimulateClick();
            TestFalse(TEXT("Running toggle stops the plugin"),P->Running);Toggle->SimulateClick();
            TestTrue(TEXT("Stopped toggle starts the plugin"),P->Running);
            const auto Text=StaticCastSharedRef<STextBlock>(Toggle->GetChildren()->GetChildAt(0));
            TestEqual(TEXT("Toggle uses the running label"),Text->GetText().ToString(),FString(TEXT("Pause tracker")));
        }
        H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"presentation":{"controls":"none","settings":false,"status":false}})")),Error);
        ++GFrameCounter;Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();
        TestFalse(TEXT("HUD plugins can hide all native lifecycle buttons"),FindVisible(Panel,TEXT("PluginStartToggle")).IsValid()||FindVisible(Panel,TEXT("PluginStop")).IsValid());
        H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"ui":[{"id":"pagination_probe","type":"input","text":"Draft","value":"Retain this"}]})")),Error);
        ++GFrameCounter;Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();
        const auto RetainedInput=FindVisible(Panel,TEXT("PluginControl.pagination_probe"));
        TestTrue(TEXT("Default host still scrolls"),FindVisible(Panel,TEXT("PluginScroll")).IsValid());
        H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"presentation":{"controls":"none","settings":false,"status":false,"scroll":false}})")),Error);
        for(int I=0;I<3;++I){++GFrameCounter;Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();}
        TestFalse(TEXT("Paginated host removes scroll box"),FindVisible(Panel,TEXT("PluginScroll")).IsValid());
        TestTrue(TEXT("Changing scroll policy retains live controls"),RetainedInput&&FindVisible(Panel,TEXT("PluginControl.pagination_probe"))==RetainedInput);
        TestTrue(TEXT("Available panel size reaches Lua snapshots"),H->PublicSnapshot(*P)->HasTypedField<EJson::Object>(TEXT("panel"))&&P->PanelSize.Y>0&&P->PanelSize.Y<=Size.Y);
        H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"presentation":{}})")),Error);
        ++GFrameCounter;Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();
        TestTrue(TEXT("Empty presentation restores default controls and settings"),FindVisible(Panel,TEXT("PluginStop"))&&FindVisible(Panel,TEXT("PluginSettings")));
        TestTrue(TEXT("Default scrolling restored without replacing controls"),FindVisible(Panel,TEXT("PluginScroll"))&&FindVisible(Panel,TEXT("PluginControl.pagination_probe"))==RetainedInput);
        H->ApplyPublicOutput(*P,UI,Error);
        {
            auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();TestTrue(TEXT("Inventory icon test loads DAT artwork"),Dat->LoadDatDirectory(ACETestDatDirectory()));
            FACESupplyEntry Item;Item.Icon=0x06004CF7;Item.ItemType=ACEItemType::Container;Item.Overlay=0x06006C37;Item.Underlay=0x060010F9;Item.Effects=1;
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("id"),TEXT("inventory_icon"));Row->SetStringField(TEXT("type"),TEXT("item_icon"));
            Row->SetNumberField(TEXT("icon"),uint32(Item.Icon));Row->SetNumberField(TEXT("item_type"),uint32(Item.ItemType));Row->SetNumberField(TEXT("overlay"),uint32(Item.Overlay));Row->SetNumberField(TEXT("effects"),Item.Effects);
            Row->SetNumberField(TEXT("underlay"),uint32(Item.Underlay));
            auto IconOutput=MakeShared<FJsonObject>();IconOutput->SetArrayField(TEXT("ui"),{MakeShared<FJsonValueObject>(Row)});
            TestTrue(TEXT("Inventory artwork control applies"),H->ApplyPublicOutput(*P,IconOutput,Error));
            auto Icons=H->MakeCompanionPanel(P->Id);TSharedPtr<SWidget> Layers;
            TFunction<void(TSharedRef<SWidget>)> FindLayers=[&](TSharedRef<SWidget> Widget){if(Widget->GetType()==TEXT("SOverlay"))Layers=Widget;auto* Ch=Widget->GetChildren();for(int32 I=0;I<Ch->Num();++I)FindLayers(Ch->GetChildAt(I));};FindLayers(Icons);
            if(TestTrue(TEXT("Item icon contains both retail layers"),Layers&&Layers->GetChildren()->Num()==2))
            {
                const auto Theme=FACEPluginStyle::Use(H,P->Id);
                auto Reference=SNew(SOverlay)+SOverlay::Slot()[SNew(SImage).Image(Theme->ItemImage(Item,false))]+SOverlay::Slot()[SNew(SImage).Image(Theme->ItemImage(Item,true))];
                auto* IconTarget=FWidgetRenderer::CreateTargetFor(FVector2D(32,32),TF_Bilinear,true);
                TArray<FColor> Expected,Actual;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
                Renderer.DrawWidget(IconTarget,Reference,FVector2D(32,32),0);FlushRenderingCommands();IconTarget->GameThread_GetRenderTargetResource()->ReadPixels(Expected,Flags);
                Renderer.DrawWidget(IconTarget,Layers.ToSharedRef(),FVector2D(32,32),0);FlushRenderingCommands();IconTarget->GameThread_GetRenderTargetResource()->ReadPixels(Actual,Flags);
                TestTrue(TEXT("Lua item artwork matches Supply Ledger pixels including effects and overlay"),Actual.Num()==1024&&Actual==Expected);
                TestTrue(TEXT("Rendered icon contains visible artwork"),Actual.ContainsByPredicate([](const FColor& C){return C.R>32||C.G>32||C.B>32;}));
                TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(32,32,Actual,Png);FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("PublicAPI-item-icon.png")));IconTarget->ReleaseResource();
            }
            H->ApplyPublicOutput(*P,UI,Error);
        }
        TestTrue(TEXT("Title can change with the window already open"),H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"presentation":{"title":"Live quest progress"}})")),Error));
        for(int32 I=0;I<3;++I){++GFrameCounter;Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();}
        int32 TitleCount=0;TFunction<void(TSharedRef<SWidget>)> CountTitles=[&](TSharedRef<SWidget> Widget)
        {
            if(!Widget->GetVisibility().IsVisible())return;
            if(Widget->GetType()==TEXT("SACEPluginText")&&StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString()==TEXT("Live quest progress"))++TitleCount;
            auto* Children=Widget->GetChildren();for(int32 I=0;I<Children->Num();++I)CountTitles(Children->GetChildAt(I));
        };CountTitles(Panel);TestEqual(TEXT("Standalone plugin content does not duplicate its window title"),TitleCount,0);
        H->SetWaypointOption(TEXT("arrow"),false);H->SetWaypointOption(TEXT("dungeon_overlay"),false);
        H->SavePluginWindowPosition(P->Id,FVector2D(50,10));H->SavePluginWindowPosition(P->Id+TEXT(".size"),FVector2D(680,860));
        auto Desktop=SNew(SACEPluginDesktop).Host(H);Desktop->Toggle(P->Id);
        for(int32 I=0;I<3;++I){++GFrameCounter;Renderer.DrawWidget(Target,Desktop,Size,0);FlushRenderingCommands();}
        TitleCount=0;CountTitles(Desktop);TestEqual(TEXT("Plugin window shows its title exactly once in the frame"),TitleCount,1);
        H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"presentation":{"title":"Updated tracker title"}})")),Error);
        for(int32 I=0;I<3;++I){++GFrameCounter;Renderer.DrawWidget(Target,Desktop,Size,0);FlushRenderingCommands();}
        TitleCount=0;CountTitles(Desktop);TestEqual(TEXT("Open frame no longer displays the old title"),TitleCount,0);
        H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"presentation":{"title":"Live quest progress"}})")),Error);
        for(int32 I=0;I<3;++I){++GFrameCounter;Renderer.DrawWidget(Target,Desktop,Size,0);FlushRenderingCommands();}
        TitleCount=0;CountTitles(Desktop);TestEqual(TEXT("Title bar updates live without adding a content heading"),TitleCount,1);
        TArray<FColor> DesktopPixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(DesktopPixels);
        TArray64<uint8> DesktopPng;FImageUtils::PNGCompressImageArray(740,880,DesktopPixels,DesktopPng);
        FFileHelper::SaveArrayToFile(DesktopPng,*(FPaths::ProjectSavedDir()/TEXT("PublicAPI-window.png")));
        auto Badge=SNew(SACEPluginBadge).Plugin(P);Renderer.DrawWidget(Target,Badge,FVector2D(26,26),0);FlushRenderingCommands();
        TestEqual(TEXT("Custom plugin icon uses aspect-preserving scale widget"),Badge->GetChildren()->GetChildAt(0)->GetChildren()->GetChildAt(0)->GetType(),FName(TEXT("SScaleBox")));
        H->ApplyPublicOutput(*P,ParseAPI(TEXT(R"({"presentation":{"icon":"missing.png"}})")),Error);
        ++GFrameCounter;Renderer.DrawWidget(Target,Badge,FVector2D(26,26),0);FlushRenderingCommands();
        TestEqual(TEXT("Unavailable image displays native plugin emblem"),Badge->GetChildren()->GetChildAt(0)->GetChildren()->GetChildAt(0)->GetType(),FName(TEXT("SACEPluginIcon")));
        Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(740,880,Pixels,Png);FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("PublicAPI-panel.png")));Target->ReleaseResource();
    }
    H->Stop(P->Id);TestFalse(TEXT("Public automation stops normally"),P->Running);
    P->Profile=Off;P->Profile->SetBoolField(TEXT("mana_charges_when_off"),true);H->NextIdleManaDecision=0;H->Tick(0);
    TestTrue(TEXT("Custom plugin can opt into stopped gear recharge"),H->IdleManaOwner==P->Id&&H->IdleManaVM.IsValid()&&!H->IdleManaFailed&&!P->Running);
    return true;
}
#endif
