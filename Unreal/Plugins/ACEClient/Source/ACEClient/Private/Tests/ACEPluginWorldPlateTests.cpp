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
    H->RefreshAutomationData();
    auto P=MakeShared<FACEClientPlugin>();P->Id=TEXT("plate_test");P->Api=2;P->Companion=true;P->Enabled=P->Running=true;
    P->Permissions={TEXT("ui"),TEXT("markers")};P->Player=S.PlayerGuid;P->Server=S.ServerName;P->VMSession=C->Session;P->Profile=MakeShared<FJsonObject>();H->Plugins.Add(P);
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
    TestTrue(TEXT("Omitted list retains native plate"),H->ApplyCompanionOutput(*P,Json(TEXT("{}")),Error)&&P->WorldPlates.Num()==1);
    const auto Valid=Output;
    for(const FString Invalid:{TEXT(R"({"world_plates":[{"id":12,"vitals":{"health":{"current":2,"max":1}}}]})"),
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
    if(FApp::CanEverRender())
    {
        FWidgetRenderer Renderer(false,true);const FVector2D Size(1024,512);auto* Texture=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("WorldPlateReview");IFileManager::Get().MakeDirectory(*Folder,true);
        for(int Variant=0;Variant<3;++Variant)
        {
            auto View=Plate->GetPlate();
            if(Variant==1)
            {
                View.Name=TEXT("An exceptionally long name for a powerful target with multiple confirmed effects");
                View.Health.Current=80;View.Health.Maximum=200;View.Effects.SetNum(16);
                for(auto& Effect:View.Effects)Effect={uint32(Spell),Icon,FPlatformTime::Seconds()+90};
            }
            if(Variant==2){View.Health={};View.Stamina={};View.Mana={};View.Effects.Reset();View.Name=TEXT("Unknown vitals");}
            Plate->SetPlate(View,H);
            for(int32 Pass=0;Pass<3;++Pass){Renderer.DrawWidget(Texture,Plate->GetSlateWidget().ToSharedRef(),Size,0);FlushRenderingCommands();}
            TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Texture->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
            TestEqual(TEXT("Native plate renders full surface"),Pixels.Num(),1024*512);
            int32 Painted=0,Blue=0;for(FColor Pixel:Pixels){if(Pixel.A>0)++Painted;if(Pixel.A>200&&Pixel.B>Pixel.R+40&&Pixel.B>Pixel.G)++Blue;}
            TestTrue(TEXT("Plate has visible frame/text/icon pixels"),Painted>2000);
            if(Variant==2)TestEqual(TEXT("Unknown mana does not paint a blue bar"),Blue,0);
            else TestTrue(TEXT("Known mana paints its blue bar"),Blue>100);
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
