#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACETestDatDirectory.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginVM.h"
#include "Mods/ACEActionDeckSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "ACEPlayerController.h"
#include "ACEWorldPresenterComponent.h"
#include "ACEWorldEntityActor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/GameModeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "ProceduralMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEPluginWorldPlateTest,"ACE.Plugins.WorldPlates",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEPluginWorldPlateTest::RunTest(const FString&)
{
    auto Json=[](const FString& Text){TSharedPtr<FJsonObject> J;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J);return J;};
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);auto* GI=NewObject<UGameInstance>(GEngine);W->SetGameInstance(GI);
    Context.OwningGameInstance=GI;Context.SetCurrentWorld(W);GI->OnWorldChanged(nullptr,W);GI->Init();
    auto* H=GI->GetSubsystem<UACEPluginSubsystem>();auto* C=GI->GetSubsystem<UACEClientSubsystem>();
    H->StorageRoot=FPaths::ProjectSavedDir()/TEXT("Automation/WorldPlateState")/FGuid::NewGuid().ToString();H->Settings=MakeShared<FJsonObject>();H->Discover();
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(W);W->DestroyWorld(false);};
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if(!TestTrue(TEXT("Retail DAT loaded for target metadata and icons"),Dat->LoadDatDirectory(ACETestDatDirectory())))return false;
    C->Session=MakeShared<FACESession>();auto& S=*C->Session;S.State=EACESessionState::InWorld;S.PlayerGuid=1234;S.ServerName=TEXT("Plate test");
    S.PlayerPosition.CellId=0x7D64000D;S.PlayerPosition.Location={34,104,12};
    S.PlayerVitals.Health=999;S.PlayerVitals.MaxHealth=1000;
    FACEWorldObject Object;Object.Guid=int32(0xF0000011u);Object.Name=TEXT("Royal Guard");Object.ItemType=ACEItemType::Creature;
    Object.bHasPosition=true;Object.Position=S.PlayerPosition;Object.Position.Location.X+=10;Object.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;
    S.WorldObjects.Add(Object.Guid,Object);S.SelectedObject.Guid=Object.Guid;S.SelectedObject.bValid=true;
    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);auto Address=Sockets->CreateInternetAddr();bool AddressValid=false;
    Address->SetIp(TEXT("127.0.0.1"),AddressValid);Address->SetPort(0);
    auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Plate tracking receiver"),false);
    if(!Receiver||!Receiver->Bind(*Address))return false;Receiver->GetAddress(*Address);
    S.SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Plate tracking sender"),false);S.ServerC2SAddr=Address;S.IssacClient=MakeUnique<FACEIsaac>(123u);
    ON_SCOPE_EXIT {Receiver->Close();Sockets->DestroySocket(Receiver);if(S.SocketC2S){S.SocketC2S->Close();Sockets->DestroySocket(S.SocketC2S);S.SocketC2S=nullptr;}};
    H->RefreshAutomationData();
    auto P=MakeShared<FACEClientPlugin>();P->Id=TEXT("plate_test");P->Api=2;P->Companion=true;P->Enabled=P->Running=true;
    P->Permissions={TEXT("ui"),TEXT("markers"),TEXT("state")};P->Player=S.PlayerGuid;P->Server=S.ServerName;P->VMSession=C->Session;P->Profile=MakeShared<FJsonObject>();H->Plugins.Add(P);
    const FString Key=FString::Printf(TEXT("%u"),uint32(Object.Guid));
    auto Snapshot=[&](){return H->CompanionSnapshot(*P);};
    auto Selection=[&](){return Snapshot()->GetObjectField(TEXT("selection"));};
    auto Vitals=[&](){return Selection()->GetObjectField(TEXT("vitals"));};
    TestTrue(TEXT("Unknown target vitals are omitted, never copied from player"),Vitals()->Values.IsEmpty());
    TestFalse(TEXT("Raw database type is not invented"),Selection()->HasField(TEXT("weenie_type")));
    TestEqual(TEXT("Descriptor exposes creature classification"),Selection()->GetStringField(TEXT("object_type")),FString(TEXT("creature")));
    for(const auto Kind:{ACEItemType::Portal,ACEItemType::Container,ACEItemType::LifeStone,ACEItemType::MeleeWeapon})
    {auto O=Object;O.ItemType=Kind;TestTrue(TEXT("Portal, chest, lifestone and item classify separately"),ACEPluginWorldPlate::ObjectType(O)!=TEXT("creature"));}
    const auto AppraisalHandle=S.OnAppraisal.AddLambda([&](const auto& A){H->ObserveAppraisal(A);});
    auto Assess=[&](uint32 Flags)
    {
        FACEBinaryWriter B;B.WriteUInt32(uint32(Object.Guid));B.WriteUInt32(0x100);B.WriteUInt32(1);B.WriteUInt32(Flags);
        B.WriteUInt32(0);B.WriteUInt32(200);
        if(Flags&8){for(int32 I=0;I<6;++I)B.WriteUInt32(100);B.WriteUInt32(40);B.WriteUInt32(50);B.WriteUInt32(80);B.WriteUInt32(100);}
        FACEBinaryReader R(B.GetData());S.HandleIdentifyObjectResponse(R);
    };
    Assess(0);
    TestEqual(TEXT("Zero assessed health is known, not hidden"),Vitals()->GetObjectField(TEXT("health"))->GetNumberField(TEXT("current")),0.);
    TestFalse(TEXT("Health-only assessment does not invent stamina"),Vitals()->HasField(TEXT("stamina")));
    Assess(8);
    TestEqual(TEXT("Creature profile exposes actual stamina"),Vitals()->GetObjectField(TEXT("stamina"))->GetNumberField(TEXT("current")),40.);
    TestEqual(TEXT("Creature profile exposes actual maximum mana"),Vitals()->GetObjectField(TEXT("mana"))->GetNumberField(TEXT("max")),100.);
    auto A=H->Appraisals[Object.Guid];A.Level=245;A.CreatureType=2;A.StringProperties.Add(5,TEXT("Royal Guard"));A.IntProperties.Add(9007,10);H->ObserveAppraisal(A);
    TestEqual(TEXT("Known title reaches companion"),Selection()->GetStringField(TEXT("title")),FString(TEXT("Royal Guard")));
    TestEqual(TEXT("Known raw type survives"),Selection()->GetNumberField(TEXT("weenie_type")),10.);
    S.WorldObjects[Object.Guid].bIsPlayer=true;
    for(const auto& Pair:TArray<TPair<int32,FString>>{{0,TEXT("NPK")},{ACEObjectDescFlag::PlayerKiller,TEXT("PK")},{ACEObjectDescFlag::PkLiteStatus,TEXT("PK Lite")}})
    {S.WorldObjects[Object.Guid].ObjectDescriptionFlags=Pair.Key;TestEqual(TEXT("PK status uses public descriptor"),Selection()->GetStringField(TEXT("pk_status")),Pair.Value);}
    S.WorldObjects[Object.Guid]=Object;
    S.OnObjectHealth.Broadcast(Object.Guid,.375f);
    TestEqual(TEXT("Live health percentage reaches companion"),Vitals()->GetObjectField(TEXT("health"))->GetNumberField(TEXT("fraction")),.375);
    TestFalse(TEXT("Percent-only update does not invent current hit points"),Vitals()->GetObjectField(TEXT("health"))->HasField(TEXT("current")));
    H->ObserveAppraisal(A);
    TestTrue(TEXT("Fresh assessment replaces old percentage"),Vitals()->GetObjectField(TEXT("health"))->HasField(TEXT("current")));
    int32 Spell=0;uint32 Icon=0;
    for(int32 Id=1;Id<6200;++Id){FString Name;uint32 I=0;if(Dat->TryGetSpellInfo(Id,Name,I)&&Name==TEXT("Imperil Other I")){Spell=Id;Icon=I;break;}}
    if(!TestTrue(TEXT("Known retail debuff found"),Spell!=0))return false;
    auto Cast=[&](){S.OnSpellCastRequested.Broadcast(Spell,Object.Guid);};
    auto Confirm=[&](){S.OnChatMessage.Broadcast(TEXT("You cast Imperil Other I on Royal Guard."),TEXT(""),ACEChatMessageType::Magic);};
    Cast();S.OnChatMessage.Broadcast(TEXT("Royal Guard resists your spell."),TEXT(""),ACEChatMessageType::Magic);Confirm();
    TestTrue(TEXT("Resist cannot start a public timer"),Snapshot()->GetObjectField(TEXT("target_effects"))->Values.IsEmpty());
    Cast();S.OnChatMessage.Broadcast(TEXT("Your spell fizzled."),TEXT(""),ACEChatMessageType::Magic);Confirm();
    TestTrue(TEXT("Fizzle cannot start a public timer"),Snapshot()->GetObjectField(TEXT("target_effects"))->Values.IsEmpty());
    Cast();Confirm();
    auto Data=Snapshot();const auto Effects=Data->GetObjectField(TEXT("target_effects"))->GetArrayField(Key);
    if(!TestEqual(TEXT("Confirmed cast appears once"),Effects.Num(),1))return false;
    double Remaining=0,Duration=0;TestTrue(TEXT("Action Deck reads the same record"),H->GetTargetEffectTimer(Object.Guid,Spell,Remaining,Duration));
    TestTrue(TEXT("API remaining agrees with Action Deck"),FMath::Abs(Effects[0]->AsObject()->GetNumberField(TEXT("remaining"))-Remaining)<.1);
    TestEqual(TEXT("DAT icon supplied without a second spell table"),Effects[0]->AsObject()->GetNumberField(TEXT("icon")),double(Icon));
    S.SelectedObject.Guid=0;Data=Snapshot();
    TestTrue(TEXT("Cleared selection is null"),Data->Values[TEXT("selection")]->IsNull());
    TestTrue(TEXT("Prior target remains available while timer runs"),Data->GetObjectField(TEXT("tracked_targets"))->HasField(Key));S.SelectedObject.Guid=Object.Guid;
    FString Source,Error;FFileHelper::LoadFileToString(Source,*(IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir()/TEXT("Docs/Plugins/Examples/target_plate/main.lua")));
    FACEPluginVM VM;TestTrue(TEXT("Example companion loads"),VM.Load(Source,Error));TSharedPtr<FJsonObject> Output;
    TestTrue(TEXT("Example reads real snapshot in sandbox"),VM.Step(Snapshot(),P->Profile,Output,Error));
    if(!TestTrue(*Error,H->ApplyCompanionOutput(*P,Output,Error)))return false;
    TestEqual(TEXT("Unsigned GUID survives Lua world plate output"),P->WorldPlates[0].Id,Object.Guid);
    TestEqual(TEXT("Example finds string-keyed target effects"),P->WorldPlates[0].Effects.Num(),1);
    TestEqual(TEXT("Standalone addon supplies its own four opacity sliders"),Output->GetArrayField(TEXT("ui")).Num(),4);
    auto Preferences=Snapshot();Preferences->SetArrayField(TEXT("events"),Json(TEXT(R"({"events":[{"type":"change","id":"opacity","value":90},{"type":"change","id":"vr_opacity","value":50}]})"))->GetArrayField(TEXT("events")));
    TSharedPtr<FJsonObject> SettingsOutput;
    TestTrue(TEXT("Addon opacity controls execute in Lua and native host"),VM.Step(Preferences,P->Profile,SettingsOutput,Error)&&H->ApplyCompanionOutput(*P,SettingsOutput,Error));
    TestEqual(TEXT("Addon controls desktop opacity"),P->WorldPlates[0].OpacityFor(false),.9);
    TestEqual(TEXT("Addon controls VR opacity separately"),P->WorldPlates[0].OpacityFor(true),.5);
    FACEPluginVM Restored;Restored.Load(Source,Error);Preferences->SetArrayField(TEXT("events"),{});Preferences->SetObjectField(TEXT("state"),SettingsOutput->GetObjectField(TEXT("state")));
    TestTrue(TEXT("Addon restores its own opacity preferences"),Restored.Step(Preferences,P->Profile,SettingsOutput,Error));
    TestEqual(TEXT("Restored addon retains VR opacity"),SettingsOutput->GetArrayField(TEXT("world_plates"))[0]->AsObject()->GetNumberField(TEXT("vr_opacity")),.5);
    H->ApplyCompanionOutput(*P,Output,Error);
    TestTrue(TEXT("Omitted list retains native plate"),H->ApplyCompanionOutput(*P,Json(TEXT("{}")),Error)&&P->WorldPlates.Num()==1);
    const auto Valid=Output;
    TArray<FACEPluginWorldPlate> Parsed;
    TestTrue(TEXT("Existing plate output without opacity still accepted"),ACEPluginWorldPlate::Parse(Json(TEXT(R"({"plates":[{"id":12}]})"))->GetArrayField(TEXT("plates")),Parsed,Error,0));
    TestEqual(TEXT("Default plate backing is opaque"),Parsed[0].BackgroundOpacityFor(false),1.);
    TestTrue(TEXT("Partial opacity supports inherited VR values"),ACEPluginWorldPlate::Parse(Json(TEXT(R"({"plates":[{"id":12,"opacity":0.4,"background_opacity":0}]})"))->GetArrayField(TEXT("plates")),Parsed,Error,0));
    TestEqual(TEXT("Omitted VR opacity inherits desktop"),Parsed[0].OpacityFor(true),.4);
    TestEqual(TEXT("Zero backing opacity is supported"),Parsed[0].BackgroundOpacityFor(true),0.);
    // A plate retains an unselected target even with no confirmed-cast timer.
    const auto SavedDebuffs=H->Debuffs;H->Debuffs.Reset();S.SelectedObject.Guid=0;
    P->WorldPlates[0].SelectionOnly=false;
    TestTrue(TEXT("Nonselected plate retains target independently of timers"),Snapshot()->GetObjectField(TEXT("tracked_targets"))->HasField(Key));
    S.CachedC2SPackets.Reset();H->LastAppraisalRequest=H->NextPlateAppraisal=0;H->AppraisalRequests.Remove(Object.Guid);
    H->RefreshPlateAppraisals();
    TestTrue(TEXT("Plates request background assessment without UCM"),C->BackgroundAppraisals.Contains(Object.Guid));
    TestEqual(TEXT("Plate assessment leaves local selection alone"),S.SelectedObject.Guid,0);
    TestEqual(TEXT("One assessment packet emitted"),S.CachedC2SPackets.Num(),1);
    for(const auto& Packet:S.CachedC2SPackets)
    {
        FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);
        TestEqual(TEXT("Tracking sends IdentifyObject, never changes health subscription"),R.ReadUInt32(),ACEGameAction::IdentifyObject);
        TestEqual(TEXT("Background assessment targets the tracked creature"),R.ReadUInt32(),uint32(Object.Guid));
    }
    H->RefreshPlateAppraisals();TestEqual(TEXT("Tracking respects global request budget"),S.CachedC2SPackets.Num(),1);
    S.OnObjectHealth.Broadcast(Object.Guid,.25f);
    TestEqual(TEXT("Unselected target accepts server health updates"),Snapshot()->GetObjectField(TEXT("tracked_targets"))->GetObjectField(Key)->GetObjectField(TEXT("vitals"))->GetObjectField(TEXT("health"))->GetNumberField(TEXT("fraction")),.25);
    P->WorldPlates[0].Refresh=false;H->LastAppraisalRequest=H->NextPlateAppraisal=0;H->AppraisalRequests.Remove(Object.Guid);
    H->RefreshPlateAppraisals();TestEqual(TEXT("Passive plates can opt out of assessment"),S.CachedC2SPackets.Num(),1);
    P->WorldPlates[0].Refresh=true;P->Enabled=false;
    TestTrue(TEXT("Disabled plugins do not keep targets tracked"),H->ActivePlateTargets().IsEmpty());P->Enabled=true;
    S.SelectedObject.Guid=Object.Guid;H->Debuffs=SavedDebuffs;H->ApplyCompanionOutput(*P,Valid,Error);

    // Optional terrain companion: real Lua output, capability validation,
    // retail thresholds, bounded native geometry and complete cleanup.
    auto Lens=H->Find(TEXT("derethlens"));
    if(!TestTrue(TEXT("Dereth Lens discovered and disabled by default"),Lens&&!Lens->Enabled&&Lens->Companion))return false;
    FString LensSource;FFileHelper::LoadFileToString(LensSource,*(Lens->Directory/TEXT("main.lua")));
    FACEPluginVM LensVM;TestTrue(TEXT("Dereth Lens compiles in sandbox"),LensVM.Load(LensSource,Error));
    auto LensInput=Json(TEXT(R"({"state":{"water":false,"radar":true},"events":[]})"));TSharedPtr<FJsonObject> LensOut;
    TestTrue(TEXT("Dereth Lens runs without gameplay data"),LensVM.Step(LensInput,Lens->Profile,LensOut,Error));
    TestFalse(TEXT("Saved false setting survives restart"),LensOut->GetObjectField(TEXT("terrain_overlay"))->GetBoolField(TEXT("water")));
    TestTrue(TEXT("Native host accepts terrain Lua UI and settings"),H->ApplyCompanionOutput(*Lens,LensOut,Error));
    const FACEPluginTerrain BeforeTerrain=Lens->Terrain;
    TestFalse(TEXT("Unbounded opacity rejected"),H->ApplyCompanionOutput(*Lens,Json(TEXT(R"({"terrain_overlay":{"opacity":2}})")),Error));
    TestEqual(TEXT("Rejected output preserves active settings"),Lens->Terrain.Opacity,BeforeTerrain.Opacity);
    Lens->Permissions.Remove(TEXT("terrain"));TestFalse(TEXT("Terrain requires explicit capability"),H->ApplyCompanionOutput(*Lens,LensOut,Error));Lens->Permissions.Add(TEXT("terrain"));
    FACEPluginTerrain TestSettings;TestSettings.Slopes=TestSettings.Water=true;
    TestEqual(TEXT("Dry level ground has no overlay"),ACEPluginTerrain::Shade(1,0,0,TestSettings).A,0.f);
    const auto Danger=ACEPluginTerrain::Shade(.6f,0,0,TestSettings),Warning=ACEPluginTerrain::Shade(.75f,0,0,TestSettings);
    TestTrue(TEXT("Only unwalkable slopes are colored"),Danger.A==TestSettings.Opacity&&Warning.A==0);
    TestEqual(TEXT("Exact retail floor threshold stays walkable"),ACEPluginTerrain::Shade(ACEPluginTerrain::WalkableZ,0,0,TestSettings).A,0.f);
    TestTrue(TEXT("Blocked slopes have a severity gradient"),ACEPluginTerrain::Shade(.05f,0,0,TestSettings).G<Danger.G);
    const auto Shore=ACEPluginTerrain::Shade(1,.9f,1,TestSettings),Ocean=ACEPluginTerrain::Shade(1,.9f,2,TestSettings);
    TestTrue(TEXT("Flooded coastal cell differs from impassable ocean block"),Shore.A==0&&Ocean.A==TestSettings.Opacity);
    Lens->Enabled=Lens->Running=true;Lens->Player=S.PlayerGuid;Lens->Server=S.ServerName;Lens->VMSession=C->Session;
    H->UpdatePluginTerrain();TestNotNull(TEXT("Native terrain actor created only when active"),H->TerrainOverlayActor.Get());
    if(H->TerrainRadarMesh)
    {
        TestEqual(TEXT("Radar overlay cannot intercept interaction"),H->TerrainRadarMesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
        const auto* Section=H->TerrainRadarMesh->GetProcMeshSection(0);
        if(TestNotNull(TEXT("Radar boundary has native geometry"),Section))
            TestTrue(TEXT("Outdoor radar boundary uses retail 75 m"),FMath::Abs(Section->ProcVertexBuffer[0].Position.Size2D()-7500)<1);
        const auto OldLocation=H->TerrainRadarMesh->GetComponentLocation();S.PlayerPosition.Location.X+=1;
        H->UpdatePluginTerrain();TestTrue(TEXT("Radar follows movement between Lua callbacks"),!H->TerrainRadarMesh->GetComponentLocation().Equals(OldLocation));S.PlayerPosition.Location.X-=1;
        TestEqual(TEXT("Terrain overlay material is translucent"),Dat->GetTerrainOverlayMaterial()->GetBlendMode(),BLEND_Translucent);
    }
    const auto TerrainBuilds=H->TerrainGeometryBuilds,RadarBuilds=H->RadarGeometryBuilds;
    for(int I=0;I<120;++I){H->NextTerrainBuild=0;H->UpdatePluginTerrain();}
    TestEqual(TEXT("Stationary Lens never rebuilds identical terrain"),H->TerrainGeometryBuilds,TerrainBuilds);
    TestEqual(TEXT("Stationary Lens never rebuilds identical radar mesh"),H->RadarGeometryBuilds,RadarBuilds);
    Lens->Terrain.Dungeon=true;S.PlayerPosition.CellId=0x7D640100;H->NextTerrainBuild=0;H->UpdatePluginTerrain();
    TestEqual(TEXT("Dungeon light activates indoors"),H->TerrainDungeonLight->BlendWeight,1.f);
    TestFalse(TEXT("Outdoor terrain is hidden in dungeon"),H->TerrainOverlayMesh->IsVisible());
    S.PlayerPosition.CellId=0x7D64000D;H->UpdatePluginTerrain();
    TestEqual(TEXT("Dungeon light leaves outdoor video settings alone"),H->TerrainDungeonLight->BlendWeight,0.f);
    if(FApp::CanEverRender())
    {
        FWidgetRenderer Renderer(false,true);const FVector2D Size(640,560);auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
        auto Panel=H->MakePanel(Lens->Id);
        for(int Pass=0;Pass<4;++Pass){Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();}
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(640,560,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("DerethLens-panel.png")));Target->ReleaseResource();
    }
    FACEPluginVM ClearLens;TSharedPtr<FJsonObject> ClearOutput;
    TestTrue(TEXT("Empty Lua terrain object clears without becoming an array"),ClearLens.Load(TEXT("return function() return {terrain_overlay={}} end"),Error)
        &&ClearLens.Step(LensInput,Lens->Profile,ClearOutput,Error)&&H->ApplyCompanionOutput(*Lens,ClearOutput,Error)&&!Lens->Terrain.Active());
    Lens->Enabled=false;H->UpdatePluginTerrain();TestNull(TEXT("Disabling lens releases meshes and light"),H->TerrainOverlayActor.Get());

    for(const FString Invalid:{TEXT(R"({"world_plates":[{"id":12,"vitals":{"health":{"current":2,"max":1}}}]})"),
        TEXT(R"({"world_plates":[{"id":12,"opacity":1.01}]})"),TEXT(R"({"world_plates":[{"id":12,"background_opacity":-1}]})"),
        TEXT(R"({"world_plates":[{"id":12,"vr_opacity":"0.5"}]})"),TEXT(R"({"world_plates":[{"id":12,"vr_background_opacity":null}]})"),
        TEXT(R"({"world_plates":[{"id":12,"effects":[{"icon":123,"remaining":20}]}]})"),
        TEXT(R"({"world_plates":[{"id":12},{"id":12}]})"),TEXT(R"({"world_plates":[{"id":12,"selection_only":"true"}]})")})
    {TestFalse(TEXT("Malformed plate is rejected atomically"),H->ApplyCompanionOutput(*P,Json(Invalid),Error));TestEqual(TEXT("Rejected replacement retains previous plate"),P->WorldPlates[0].Id,Object.Guid);}
    P->Permissions.Remove(TEXT("markers"));TestFalse(TEXT("Plate cannot bypass marker permission"),H->ApplyCompanionOutput(*P,Valid,Error));P->Permissions.Add(TEXT("markers"));
    // Real native renderer and actor transform, with no Lua callback between movement frames.
    W->SetGameMode(FURL(nullptr,TEXT("/Game/Test?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
    auto* GM=W->GetAuthGameMode();if(!TestNotNull(TEXT("World plate game mode"),GM))return false;
    auto* Presenter=NewObject<UACEWorldPresenterComponent>(GM);GM->AddInstanceComponent(Presenter);
    auto* PC=W->SpawnActor<AACEPlayerController>();W->AddController(PC);
    auto* LP=NewObject<ULocalPlayer>(GEngine);LP->PlayerController=PC;GI->AddLocalPlayer(LP,FPlatformUserId::CreateFromInternalId(0));PC->SetPlayer(LP);
    auto* Entity=W->SpawnActor<AACEWorldEntityActor>();Entity->InitializeFromObject(Object,100,false);Entity->SetActorLocation({1000,0,0});Presenter->Spawned.Add(Object.Guid,Entity);
    PC->PlayerCameraManager=W->SpawnActor<APlayerCameraManager>();PC->PlayerCameraManager->InitializeFor(PC);
    H->UpdatePluginWorldPlates();if(!TestEqual(TEXT("One native plate allocated"),H->WorldPlateComponents.Num(),1))return false;
    auto* Plate=H->WorldPlateComponents[0].Get();Plate->TickComponent(.016f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Plate draws known visible world actor"),Plate->IsVisible());
    TestEqual(TEXT("Plate is a stereo world-space surface"),Plate->GetWidgetSpace(),EWidgetSpace::World);
    TestEqual(TEXT("Plate cannot intercept inventory clicks or VR rays"),Plate->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    TestEqual(TEXT("Plate tick occurs after world movement"),Plate->PrimaryComponentTick.TickGroup,TG_PostUpdateWork);
    const FVector Before=Plate->GetComponentLocation();Entity->AddActorWorldOffset({80,40,0});Plate->TickComponent(.016f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Actor motion moves the plate without a Lua callback"),FVector::Dist(Plate->GetComponentLocation(),Before)>80);
    S.SelectedObject.Guid=0;Plate->TickComponent(.016f,LEVELTICK_All,nullptr);TestFalse(TEXT("Selection-only plate hides immediately"),Plate->IsVisible());S.SelectedObject.Guid=Object.Guid;
    Entity->bCellVisible=false;Plate->TickComponent(.016f,LEVELTICK_All,nullptr);TestFalse(TEXT("Hidden cells do not leave floating plates"),Plate->IsVisible());Entity->bCellVisible=true;
    Plate->TickComponent(.016f,LEVELTICK_All,nullptr);
    auto OpacityPlate=Plate->GetPlate();OpacityPlate.Opacity=.9;OpacityPlate.VROpacity=.5;
    Plate->SetPlate(OpacityPlate,H);
    if(auto* Material=Plate->GetMaterialInstance())
    {
        TestTrue(TEXT("Plate uses premultiplied alpha, not additive blending"),Material->GetBlendMode()==BLEND_AlphaComposite);
        TestEqual(TEXT("Desktop opacity applied once in material"),Material->K2_GetVectorParameterValue(TEXT("TintColorAndOpacity")).A,.9f);
        Plate->UpdatePresentation(true);
        TestEqual(TEXT("VR opacity changes without a Lua callback"),Material->K2_GetVectorParameterValue(TEXT("TintColorAndOpacity")).A,.5f);
        Plate->TickComponent(.016f,LEVELTICK_All,nullptr);
        TestEqual(TEXT("Returning to desktop restores its opacity"),Material->K2_GetVectorParameterValue(TEXT("TintColorAndOpacity")).A,.9f);
    }
    TestEqual(TEXT("Changing plate presentation preserves selection"),S.SelectedObject.Guid,Object.Guid);
    if(FApp::CanEverRender())
    {
        FWidgetRenderer Renderer(false,true);const FVector2D Size(1024,512);auto* Texture=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("WorldPlateReview");IFileManager::Get().MakeDirectory(*Folder,true);
        int32 OpaqueDesktop=0;
        for(int Variant=0;Variant<5;++Variant)
        {
            auto View=Plate->GetPlate();
            if(Variant==1)
            {
                View.Name=TEXT("An exceptionally long name for a powerful target with multiple confirmed effects");
                View.Health.Current=80;View.Health.Maximum=200;View.Effects.SetNum(16);
                for(auto& Effect:View.Effects)Effect={uint32(Spell),Icon,FPlatformTime::Seconds()+90};
            }
            if(Variant==2){View.Health={};View.Stamina={};View.Mana={};View.Effects.Reset();View.Name=TEXT("Unknown vitals");}
            if(Variant>=3){View.BackgroundOpacity=1;View.VRBackgroundOpacity=.25;View.Opacity=View.VROpacity=1;}
            Plate->SetPlate(View,H);
            Plate->UpdatePresentation(Variant==4);
            for(int32 Pass=0;Pass<3;++Pass){Renderer.DrawWidget(Texture,Plate->GetSlateWidget().ToSharedRef(),Size,0);FlushRenderingCommands();}
            TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Texture->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
            TestEqual(TEXT("Native plate renders full surface"),Pixels.Num(),1024*512);
            int32 Painted=0,Blue=0;for(FColor Pixel:Pixels){if(Pixel.A>0)++Painted;if(Pixel.A>200&&Pixel.B>Pixel.R+40&&Pixel.B>Pixel.G)++Blue;}
            TestTrue(TEXT("Plate has visible frame/text/icon pixels"),Painted>2000);
            if(Variant>=2)TestEqual(TEXT("Unknown mana does not paint a blue bar"),Blue,0);
            else TestTrue(TEXT("Known mana paints its blue bar"),Blue>100);
            int32 Opaque=0;for(FColor Pixel:Pixels)if(Pixel.A>=250)++Opaque;
            TestEqual(TEXT("Unused canvas remains transparent with opaque plate"),Pixels[0].A,uint8(0));
            if(Variant==3)OpaqueDesktop=Opaque;
            if(Variant==4)TestTrue(TEXT("VR can retain translucent backing independently of opaque desktop"),OpaqueDesktop>Opaque+4000);
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1024,512,Pixels,PNG);
            FFileHelper::SaveArrayToFile(PNG,*(Folder/FString::Printf(TEXT("TargetPlate-%d.png"),Variant)));
        }
        Texture->ReleaseResource();
    }
    for(auto& E:H->Debuffs)E->AsObject()->SetNumberField(TEXT("expires"),FPlatformTime::Seconds()-1);
    TestTrue(TEXT("Expired timers disappear without a maintenance tick"),Snapshot()->GetObjectField(TEXT("target_effects"))->Values.IsEmpty());
    Cast();Confirm();S.OnObjectDeleted.Broadcast(Object.Guid);
    TestTrue(TEXT("Loss of awareness clears data and plate definitions"),P->WorldPlates.IsEmpty()&&H->TargetHealthSamples.IsEmpty()&&!H->Appraisals.Contains(Object.Guid));
    H->UpdatePluginWorldPlates();TestTrue(TEXT("Removed plates release native components"),H->WorldPlateComponents.IsEmpty());
    H->ApplyCompanionOutput(*P,Valid,Error);H->UpdatePluginWorldPlates();
    H->StopCompanion(*P,TEXT("test stop"));
    TestTrue(TEXT("Stopping clears retained plate definitions"),P->WorldPlates.IsEmpty());
    H->UpdatePluginWorldPlates();TestTrue(TEXT("Stopping releases plate components"),H->WorldPlateComponents.IsEmpty());
    S.OnAppraisal.Remove(AppraisalHandle);
    TestTrue(TEXT("Empty list clears through real Lua serialization"),VM.Load(TEXT("return function() return {world_plates={}} end"),Error)&&VM.Step(Snapshot(),P->Profile,Output,Error)&&H->ApplyCompanionOutput(*P,Output,Error));
    return true;
}
#endif
