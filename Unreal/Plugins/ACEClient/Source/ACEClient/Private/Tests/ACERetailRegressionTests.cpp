#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "Protocol/ACECombatChat.h"
#include "ACETypes.h"
#include "ACEBodySweep.h"
#include "ACEVisibleObjectPick.h"
#include "ACEMotionCommandNames.generated.h"
#include "Dat/ACECellTransit.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ACEWorldEntityActor.h"
#include "ACEScriptComponent.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACELandblockActor.h"
#include "ACEEnvCellActor.h"
#include "ACESkyDomeActor.h"
#include "ACETerrainPresenterComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "ProceduralMeshComponent.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ShaderCompiler.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "Engine/StaticMesh.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeExit.h"
#include "Scalability.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailCombatProtocolTest, "ACE.RetailParity.CombatProtocol",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailCombatProtocolTest::RunTest(const FString& Parameters)
{
    FACESession Session;
    auto Deliver=[&](uint32 Event, TOptional<uint32> Error={})
    {
        FACEBinaryWriter Writer; Writer.WriteUInt32(1); Writer.WriteUInt32(1); Writer.WriteUInt32(Event);
        if (Error.IsSet()) Writer.WriteUInt32(Error.GetValue());
        FACEBinaryReader Reader(Writer.GetData()); Session.HandleGameEvent(Reader);
    };
    Deliver(ACEGameEvent::CombatCommenceAttack);
    TestTrue(TEXT("Server repeat start marks the swing active"),Session.IsServerAttackInProgress());
    Deliver(ACEGameEvent::AttackDone,0u);
    TestFalse(TEXT("AttackDone releases the swing for power refill"),Session.IsServerAttackInProgress());
    TestEqual(TEXT("Successful refill has no cancellation error"),Session.GetLastAttackError(),0u);
    Deliver(ACEGameEvent::CombatCommenceAttack);
    Deliver(ACEGameEvent::AttackDone,0x400u);
    TestEqual(TEXT("Cancellation is retained so the UI stops repeating"),Session.GetLastAttackError(),0x400u);
    const uint32 Revision=Session.GetCombatEventRevision();
    Deliver(ACEGameEvent::AttackDone);
    TestEqual(TEXT("Truncated AttackDone cannot restart a power refill"),Session.GetCombatEventRevision(),Revision);
    TestEqual(TEXT("Pet-device level rejection explains why Summoning failed"),
        ACECombatChat::LookupWeenieErrorWithString(0x04C9,TEXT("Summoning")), FString(TEXT("Your Summoning is too low to use that item's magic.")));
    TestEqual(TEXT("Pet-device specialization rejection is not just the skill name"),
        ACECombatChat::LookupWeenieErrorWithString(0x04CB,TEXT("Summoning")), FString(TEXT("You must have Summoning specialized to use that item's magic.")));
    TestEqual(TEXT("Known free-text errors retain the server text"),
        ACECombatChat::LookupWeenieErrorWithString(0x055E,TEXT("Away")), FString(TEXT("Away")));
    TestTrue(TEXT("Unknown string errors retain the identifier and argument"),
        ACECombatChat::LookupWeenieErrorWithString(0xFFFF,TEXT("Summoning")).Contains(TEXT("0xFFFF")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailDoorwayRegressionTest, "ACE.RetailParity.YaraqDoorways",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailDoorwayRegressionTest::RunTest(const FString& Parameters)
{
    auto* Dat = NewObject<UACEDatSubsystem>(NewObject<UGameInstance>());
    if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    Dat->GetOrCreateTexturedMaterial(0x08000708);
    for (uint32 Id=0x7D640100; Id<0x7D640174; ++Id) Dat->GetOrBuildEnvCellMesh(Id,100);
    const FVector Origin=FACEPosition::AceVectorToUnreal(FVector(125*192,100*192,0),100);
    for (uint32 Id : {0x7D640101u,0x7D64010Cu})
    {
        const auto* Mesh=Dat->FindEnvCellMesh(Id,100);
        if (!TestNotNull(TEXT("Reported Yaraq shop cell"),Mesh)) continue;
        const FTransform Transform=Mesh->GetCellLocalToLandblock(100)*FTransform(Origin);
        for (int32 I=0; I<Mesh->CellPortals.Num(); ++I)
        {
            const auto& V=Mesh->PortalApertureLocalVerts[I];
            if (V.Num()<3) continue;
            FVector Center=FVector::ZeroVector;
            for (const FVector& P: V) Center+=P;
            Center/=V.Num();
            const FVector N=Transform.TransformVectorNoScale(Mesh->PortalApertureLocalNormals[I]);
            const FVector Door=Transform.TransformPosition(Center);
            const FVector Eye=Door-N*30;
            const bool bInside=ACECellTransit::SphereIntersectsEnvCell(*Dat,Id,Eye,0,100);
            TSet<int32> Draw; bool Outside=false;
            TArray<ACEOutdoorPortalPlan::FAdmittedAperture> Exits;
            ACEOutdoorPortalPlan::CollectIndoorPViewCells(*Dat,Id,Eye,{},100,Draw,Outside,128,&Exits);
            AddInfo(FString::Printf(TEXT("Door %08X:%d dest=%04X inside=%d draw=%d exits=%d"),
                Id,I,Mesh->CellPortals[I].OtherCellId,bInside,Draw.Num(),Exits.Num()));
            if (Mesh->CellPortals[I].IsOutsidePortal())
            {
                TestTrue(TEXT("Inside doorway admits outdoor view"),Outside);
                const auto Mask=FACEPortalViewMask::Build(Eye,Exits);
                TestTrue(TEXT("Outdoor ray beyond reported shop doorway survives land mask"),Mask.Contains(Door+N*500));
            }
            else TestTrue(TEXT("Room across an unobstructed portal is drawn"),Draw.Contains((Id&0xFFFF0000)|Mesh->CellPortals[I].OtherCellId));
        }
    }
    for (uint32 Setup : {0x02000059u,0x02000E08u})
    {
        const auto* Mesh=Dat->GetOrBuildSetupMesh(Setup,100);
        if (!TestNotNull(TEXT("Corpse setup"),Mesh)) continue;
        TArray<FTransform> Pose; int32 Count=0; bool Finished=false;
        // Server creature DIDs (the Setup's optional default MTable is zero).
        const uint32 MT=Setup==0x02000059u ? 0x09000025u : 0x09000007u;
        const bool Ok=Dat->EvaluateMotionCommand(MT,ACEMotion::Dead,1000,Mesh->Parts.Num(),Pose,100,Count,
            nullptr,nullptr,ACEMotion::StanceNonCombat,false,&Finished)
            || Dat->EvaluateMotionLink(MT,ACEMotion::Ready,ACEMotion::Dead,1000,Mesh->Parts.Num(),Pose,100,Count,Finished,
                nullptr,nullptr,ACEMotion::StanceNonCombat);
        AddInfo(FString::Printf(TEXT("Corpse %08X motion=%08X pose=%d parts=%d finished=%d"),Setup,MT,Ok,Count,Finished));
        TestTrue(TEXT("DAT corpse has a final death pose"),Ok && Count>0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailRuntimeRegressionTest, "ACE.RetailParity.RuntimeActors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACERetailRuntimeRegressionTest::RunTest(const FString& Parameters)
{
    // Saved editor scalability can disable shadows entirely. Exercise the
    // rendering contract at a known quality without changing the user's INI.
    const auto SavedQuality = Scalability::GetQualityLevels();
    auto RenderQuality = SavedQuality;
    RenderQuality.SetFromSingleQualityLevel(3);
    Scalability::SetQualityLevels(RenderQuality);
    ON_SCOPE_EXIT { Scalability::SetQualityLevels(SavedQuality); };
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
        .RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); GI->Init();
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if (!Dat || !Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))
    {
        GI->Shutdown(); World->DestroyWorld(false); return false;
    }
    Dat->GetOrCreateTexturedMaterial(0x08000708);
    // A visual-only Setup must retain its authored cylinder through every update.
    {
        // A large statue's proxy extends in front of an NPC, while its actual
        // visible surface is behind. Pick polygons by depth, not proxy order.
        auto MakePickActor=[&](int32 Guid,double X,double Extent)
        {
            FACEWorldObject Object;Object.Guid=Guid;
            auto* Actor=World->SpawnActor<AACEWorldEntityActor>();Actor->InitializeFromObject(Object,100,false);
            Actor->SetActorLocation(FVector(X,8000,2000));
            auto* Proxy=NewObject<UBoxComponent>(Actor);Actor->AddInstanceComponent(Proxy);
            Proxy->SetBoxExtent(FVector(Extent,100,100));Proxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Proxy->SetCollisionResponseToAllChannels(ECR_Ignore);Proxy->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
            Proxy->RegisterComponent();Proxy->SetWorldLocation(Actor->GetActorLocation());
            auto* Mesh=NewObject<UProceduralMeshComponent>(Actor);Actor->AddInstanceComponent(Mesh);Mesh->RegisterComponent();
            Mesh->SetWorldLocation(Actor->GetActorLocation());
            Mesh->CreateMeshSection(0,{{0,-80,-80},{0,80,-80},{0,0,80}},{0,1,2},{},{},{},{},false);
            return Actor;
        };
        auto* StatuePick=MakePickActor(0x71000900,300,280);
        auto* NPCPick=MakePickActor(0x71000901,100,20);
        TestTrue(TEXT("Visible NPC wins over nearer statue selection bounds"),
            ACEVisibleObjectPick::Trace(*World,FVector(0,8000,2000),FVector(1000,8000,2000),nullptr)==NPCPick);
        NPCPick->SetActorHiddenInGame(true);
        TestTrue(TEXT("Hidden NPC cannot intercept a statue click"),
            ACEVisibleObjectPick::Trace(*World,FVector(0,8000,2000),FVector(1000,8000,2000),nullptr)==StatuePick);
        TestNull(TEXT("Empty space in a large statue proxy cannot steal a VR selection"),
            ACEVisibleObjectPick::Trace(*World,FVector(0,8090,2000),FVector(1000,8090,2000),nullptr,nullptr,false));
        NPCPick->SetActorHiddenInGame(false);
        TestTrue(TEXT("VR shares the desktop model-surface priority"),
            ACEVisibleObjectPick::Trace(*World,FVector(0,8000,2000),FVector(1000,8000,2000),nullptr,nullptr,false)==NPCPick);
        TArray<AActor*> Crowd;
        for(int32 I=0;I<256;++I)
        {
            auto* Far=MakePickActor(0x71000A00+I,300,20);Far->SetActorLocation(FVector(300,10000+I*100,2000));
            // The fixture's parts are unattached: relocate them with the owner.
            TInlineComponentArray<UPrimitiveComponent*> Parts(Far);
            for(auto* Part:Parts) Part->SetWorldLocation(Far->GetActorLocation());
            Crowd.Add(Far);
        }
        const FVector PickStart(0,8000,2000),PickEnd(1000,8000,2000);
        const double IndexedStart=FPlatformTime::Seconds();
        for(int32 I=0;I<500;++I) ACEVisibleObjectPick::Trace(*World,PickStart,PickEnd,nullptr,nullptr,false);
        const double IndexedMs=(FPlatformTime::Seconds()-IndexedStart)*2;
        const double LinearStart=FPlatformTime::Seconds();
        for(int32 I=0;I<500;++I)
        {
            double Closest=1000;
            for(TActorIterator<AACEWorldEntityActor> It(World);It;++It)
            {
                TInlineComponentArray<UProceduralMeshComponent*> Meshes(*It);
                for(auto* Mesh:Meshes) ACEVisibleObjectPick::TraceMesh(*Mesh,PickStart,PickEnd,Closest);
            }
        }
        AddInfo(FString::Printf(TEXT("Selection microbenchmark, 258 objects: spatial %.4f ms/ray, linear mesh walk %.4f ms/ray (not frame time)"),
            IndexedMs,(FPlatformTime::Seconds()-LinearStart)*2));
        TestTrue(TEXT("Dense off-ray objects cannot change precise selection"),ACEVisibleObjectPick::Trace(*World,PickStart,PickEnd,nullptr,nullptr,false)==NPCPick);
        for(auto* Far:Crowd) Far->Destroy();
        NPCPick->Destroy();StatuePick->Destroy();
    }
    TArray<FString> Poses;Dat->GetChatPoseKeys(Poses);
    for (const FString& Pose:Poses)
    {
        FString Command,MyText,OtherText;Dat->TryGetChatPose(Pose,Command,MyText,OtherText);
        if (Command.IsEmpty()) continue; // authored text-only poses
        auto Normalize=[](FString S){S.ToLowerInline();S.ReplaceInline(TEXT(" "),TEXT(""));S.ReplaceInline(TEXT("_"),TEXT(""));S.ReplaceInline(TEXT("-"),TEXT(""));return S;};
        uint32 Motion=0;
        TestTrue(*FString::Printf(TEXT("Retail emote '%s' resolves motion '%s'"),*Pose,*Command),
            ACEMotionName::TryResolve(Normalize(Command),Motion) || ACEMotionName::TryResolve(Normalize(Pose),Motion));
    }
    AddInfo(FString::Printf(TEXT("Reviewed all %d retail DAT chat poses"),Poses.Num()));
    FACEWorldObject Statue; Statue.Guid=0x71000008; Statue.SetupId=0x0200042B;
    Statue.ItemType=ACEItemType::Misc; Statue.Name=TEXT("Small Creepy Statue collision regression");
    auto* StatueActor=World->SpawnActor<AACEWorldEntityActor>();
    StatueActor->InitializeFromObject(Statue,100,true);
    StatueActor->SetActorLocation(FVector(0,0,2000));
    World->Tick(LEVELTICK_All, .016f);
    AddInfo(FString::Printf(TEXT("Statue authored bodies %d BSP=%d"),StatueActor->AuthoredCollision.Num(),StatueActor->bSetupHasPhysicsBSP));
    for (int32 Pass=0; Pass<3; ++Pass)
    {
        StatueActor->ApplyPhysicsState(0);
        FHitResult Hit;
        TestTrue(TEXT("Retail statue cylinder blocks a horizontal body sweep after state refresh"),
            World->SweepSingleByChannel(Hit,FVector(-400,0,2100),FVector(400,0,2100),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(20),
                FCollisionQueryParams(SCENE_QUERY_STAT(ACEStatueMovement),Pass!=0)));
        TestTrue(TEXT("Statue sweep strikes its authored radius, not mouse selection bounds"),Hit.Time>.27f && Hit.Time<.33f);
    }
    auto* FloorActor=World->SpawnActor<AActor>();
    auto* Floor=NewObject<UBoxComponent>(FloorActor);
    FloorActor->SetRootComponent(Floor); FloorActor->AddInstanceComponent(Floor);
    Floor->SetBoxExtent(FVector(1000,1000,10));
    Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Floor->SetCollisionResponseToAllChannels(ECR_Block); Floor->RegisterComponent();
    FloorActor->SetActorLocation(FVector(0,0,1990));
    World->Tick(LEVELTICK_All,.016f);
    for (float Contact : {0.f,-.1f,-1.f})
    {
        FHitResult Hit;
        const bool bHit=ACEBodySweep::Sweep(*World,Hit,FVector(-400,0,2088+Contact),FVector(400,0,2088+Contact),
            FCollisionShape::MakeCapsule(34,88),FCollisionQueryParams(SCENE_QUERY_STAT(ACEGroundedBody),true));
        AddInfo(FString::Printf(TEXT("Grounded body contact %.2f hit=%d actor=%s normal=%s depth=%.3f time=%.3f"),
            Contact,bHit,*GetNameSafe(Hit.GetActor()),*Hit.ImpactNormal.ToString(),Hit.PenetrationDepth,Hit.Time));
        TestTrue(TEXT("Grounded player capsule reaches the statue wall"),bHit && Hit.GetActor()==StatueActor);
    }
    StatueActor->SetActorLocation(FVector(0,2000,2000));
    FACEWorldObject NPC; NPC.Guid=0x71000009; NPC.SetupId=0x02000A0B; NPC.ItemType=ACEItemType::Creature;
    auto* NPCActor=World->SpawnActor<AACEWorldEntityActor>();
    NPCActor->InitializeFromObject(NPC,100,true); NPCActor->SetActorLocation(FVector(0,0,2000));
    World->Tick(LEVELTICK_All,.016f);
    FHitResult NPCHit;
    TestTrue(TEXT("Grounded player is blocked by Drawohan's authored body"),
        ACEBodySweep::Sweep(*World,NPCHit,FVector(-400,0,2088),FVector(400,0,2088),
        FCollisionShape::MakeCapsule(34,88),FCollisionQueryParams(SCENE_QUERY_STAT(ACENPCBody),true)) && NPCHit.GetActor()==NPCActor);
    FACEWorldObject Moving = NPC; Moving.Guid++; Moving.bHasPosition=true;
    Moving.Position.CellId=0x00000001; Moving.Position.Location=FVector(4,0,20);
    auto* MovingActor=World->SpawnActor<AACEWorldEntityActor>();
    MovingActor->InitializeFromObject(Moving,100,true);
    MovingActor->ApplyPhysicsVelocity(FVector(-8,0,0));
    MovingActor->Tick(1.f);
    TestTrue(TEXT("Network velocity prediction cannot cross another creature"),MovingActor->GetActorLocation().X < -30.f);
    TestTrue(TEXT("Network velocity prediction advances up to the creature"),MovingActor->GetActorLocation().X > -400.f);
    // Local prediction may hit a creature, but a new F748 is authoritative even
    // when its correction crosses that collider (the server has already moved).
    FACEPosition Corrected=Moving.Position; Corrected.Location.X=-2.f;
    MovingActor->ApplyACEPosition(Corrected);
    MovingActor->Tick(.5f);
    TestTrue(TEXT("Server position correction is not trapped behind a client collider"),
        FVector::Dist2D(MovingActor->GetActorLocation(),Corrected.ToUnrealLocation(100))<2.f);
    FACEObjectMotionState CorrectionWalk; CorrectionWalk.bMoving=true; CorrectionWalk.ForwardUnitsPerSecond=.001f;
    MovingActor->ApplyMotionState(CorrectionWalk);
    Corrected.Location.X=-2.75f; MovingActor->ApplyACEPosition(Corrected); MovingActor->Tick(.5f);
    TestTrue(TEXT("Sub-two-metre correction converges during locomotion"),
        FVector::Dist2D(MovingActor->GetActorLocation(),Corrected.ToUnrealLocation(100))<2.f);
    MovingActor->Destroy(); NPCActor->Destroy(); FloorActor->Destroy();
    auto* CornerActor=World->SpawnActor<AActor>();
    auto* CornerWall=NewObject<UBoxComponent>(CornerActor);CornerActor->SetRootComponent(CornerWall);
    CornerActor->AddInstanceComponent(CornerWall);CornerWall->SetBoxExtent(FVector(5,200,200));
    CornerWall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);CornerWall->SetCollisionResponseToAllChannels(ECR_Block);
    CornerWall->RegisterComponent();CornerActor->SetActorLocation(FVector(4000,4000,200));
    World->Tick(LEVELTICK_All,.016f);
    const auto StepBody=FCollisionShape::MakeCapsule(20,88);
    const FCollisionQueryParams StepQuery(SCENE_QUERY_STAT(ACECornerStep),true);
    TestTrue(TEXT("Clear lateral tread adjustment remains reachable"),ACEBodySweep::CanTraverseStep(*World,
        FVector(3930,4000,140),FVector(3950,4000,140),FVector(3950,4000,108),StepBody,StepQuery,.664f));
    TestFalse(TEXT("A tread beside an object cannot move the player through the adjacent wall"),ACEBodySweep::CanTraverseStep(*World,
        FVector(3930,4000,140),FVector(4030,4000,140),FVector(4030,4000,108),StepBody,StepQuery,.664f));
    TestFalse(TEXT("A valid overhead reach cannot descend into a wall"),ACEBodySweep::CanTraverseStep(*World,
        FVector(3930,4000,500),FVector(4010,4000,500),FVector(4010,4000,108),StepBody,StepQuery,.664f));
    const FVector Embedded(3990,4000,200);
    FHitResult EmbeddedHit;
    World->SweepSingleByChannel(EmbeddedHit,Embedded,Embedded+FVector(1,0,0),FQuat::Identity,ECC_Pawn,StepBody,StepQuery);
    TestTrue(TEXT("Fixture reproduces a capsule initially embedded in a solid"),EmbeddedHit.bStartPenetrating);
    const FVector Recovered=ACEBodySweep::Recover(*World,Embedded,FVector(-10,0,0),EmbeddedHit,StepBody,StepQuery);
    TestTrue(TEXT("Deep overlap can make incremental progress out of the same wall"),Recovered.X<Embedded.X);
    auto* Second=NewObject<UBoxComponent>(CornerActor);CornerActor->AddInstanceComponent(Second);
    Second->SetBoxExtent(FVector(5,200,200));Second->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Second->SetCollisionResponseToAllChannels(ECR_Block);Second->RegisterComponent();
    Second->SetWorldLocation(FVector(3965,4000,200));
    TestTrue(TEXT("Penetration recovery cannot enter a second wall"),
        ACEBodySweep::Recover(*World,Embedded,FVector(-10,0,0),EmbeddedHit,StepBody,StepQuery).Equals(Embedded));
    CornerActor->Destroy();
    // Floors and walls share one cell PMC. Retrying must not ignore that component.
    auto* RoomActor=World->SpawnActor<AActor>();
    auto* JoinedRoomMesh=NewObject<UProceduralMeshComponent>(RoomActor); RoomActor->SetRootComponent(JoinedRoomMesh);
    RoomActor->AddInstanceComponent(JoinedRoomMesh); JoinedRoomMesh->bUseAsyncCooking=false; JoinedRoomMesh->bUseComplexAsSimpleCollision=true;
    JoinedRoomMesh->RegisterComponent();
    const TArray<FVector> Vertices={{-1000,-1000,2000},{1000,-1000,2000},{1000,1000,2000},{-1000,1000,2000},
        {0,-1000,2000},{0,1000,2000},{0,1000,2400},{0,-1000,2400}};
    JoinedRoomMesh->CreateMeshSection(0,Vertices,{0,2,1,0,3,2,4,5,6,4,6,7},{},{},{},{},true);
    JoinedRoomMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly); JoinedRoomMesh->SetCollisionResponseToAllChannels(ECR_Block);
    World->Tick(LEVELTICK_All,.016f);
    FHitResult WallHit;
    TestTrue(TEXT("Ground contact does not mask a wall in the same cell mesh"),
        ACEBodySweep::Sweep(*World,WallHit,FVector(-400,0,2088),FVector(400,0,2088),
        FCollisionShape::MakeCapsule(34,88),FCollisionQueryParams(SCENE_QUERY_STAT(ACECellWall),true))
        && WallHit.GetComponent()==JoinedRoomMesh && WallHit.ImpactNormal.X<-.9);
    TestTrue(TEXT("Wall contact stops the capsule before its center crosses"),WallHit.Location.X<=-33.f);
    FHitResult FloorHit;
    TestTrue(TEXT("Downward floor support remains intact"),ACEBodySweep::Sweep(*World,FloorHit,
        FVector(-400,0,2200),FVector(-400,0,2050),FCollisionShape::MakeCapsule(34,88),
        FCollisionQueryParams(SCENE_QUERY_STAT(ACECellFloor),true)) && FloorHit.ImpactNormal.Z>.9);
    RoomActor->Destroy();
    StatueActor->SetActorLocation(FVector(0,0,2000));
    StatueActor->ApplyPhysicsState(ACEPhysicsState::Ethereal);
    FHitResult EtherealHit;
    TestFalse(TEXT("Server ethereal statue remains walk-through"),World->SweepSingleByChannel(EtherealHit,
        FVector(-400,0,2100),FVector(400,0,2100),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(20)));
    StatueActor->Destroy();
    for (uint32 Setup : {0x02000059u,0x02000E08u})
    {
        FACEWorldObject Object; Object.Guid=Setup; Object.SetupId=Setup;
        Object.Name=TEXT("Corpse regression"); Object.ItemType=ACEItemType::Container;
        Object.ObjectDescriptionFlags=ACEObjectDescFlag::Corpse;
        Object.MotionTableId=Setup==0x02000059u ? 0x09000025 : 0x09000007;
        Object.InitialMotionCommand=ACEMotion::Dead; Object.InitialMotionStyle=ACEMotion::StanceNonCombat;
        TestFalse(TEXT("Corpse containers must not be posed as closed chests"),Object.UsesOnOffMotion());
        auto* Actor=World->SpawnActor<AACEWorldEntityActor>();
        Actor->InitializeFromObject(Object,100,true);
        const auto* Built=Dat->GetOrBuildSetupMesh(Setup,100);
        TArray<FTransform> Pose; int32 Count=0; bool Finished=false;
        const bool Ok=Dat->EvaluateMotionCommand(Object.MotionTableId,ACEMotion::Dead,1000,Built->Parts.Num(),Pose,100,Count,
            nullptr,nullptr,ACEMotion::StanceNonCombat,false,&Finished)
            || Dat->EvaluateMotionLink(Object.MotionTableId,ACEMotion::Ready,ACEMotion::Dead,1000,Built->Parts.Num(),Pose,100,Count,Finished,
                nullptr,nullptr,ACEMotion::StanceNonCombat);
        TestTrue(TEXT("Corpse runtime reference pose"),Ok && Count>0);
        for (int32 Pass=0; Pass<3; ++Pass)
        {
            if (Pass==1) { FACEObjectMotionState Ready; Ready.ForwardCommand=ACEMotion::Ready; Actor->ApplyMotionState(Ready); }
            if (Pass==2) Actor->Appearance->ApplyWorldObject(Object,100,false);
            // Let the same pose blending/tick path used in play settle.
            for (int32 Frame=0; Frame<20; ++Frame) Actor->Appearance->TickComponent(.05f,LEVELTICK_All,nullptr);
            int32 Mismatch=0;
            for (int32 I=0; I<Count; ++I)
            {
                FTransform Actual;
                if (!Actor->Appearance->GetPartCurrentTransform(I,Actual)
                    || !Actual.GetLocation().Equals(Pose[I].GetLocation(),.1)
                    || !Actual.GetRotation().Equals(Pose[I].GetRotation(),.001)) ++Mismatch;
            }
            TestEqual(*FString::Printf(TEXT("%08X corpse final frame survives update pass %d"),Setup,Pass),Mismatch,0);
        }
        // A creature receiving Dead must traverse the animation before holding it.
        FACEWorldObject Living = Object;
        Living.ObjectDescriptionFlags = 0; Living.ItemType = ACEItemType::Creature;
        Living.InitialMotionCommand = ACEMotion::Ready;
        Living.Position.CellId=0x7D64010C; Living.bHasPosition=true;
        auto* Live = World->SpawnActor<AACEWorldEntityActor>();
        Live->InitializeFromObject(Living,100,true);
        auto* ShadowPresenter=NewObject<UACETerrainPresenterComponent>(Live);
        Live->Appearance->SetPartsCastShadow(false,true);
        ShadowPresenter->RefreshEntityShadowCasters(Live->GetActorLocation());
        TestTrue(TEXT("Interior creature keeps shadow casting through a cell transition"),Live->Appearance->GetPartsCastShadow());
        FACEObjectMotionState Death; Death.ForwardCommand = ACEMotion::DeadCommandU16;
        Death.ActionSpeed = 1.f; Death.CurrentStyle = ACEMotion::StanceNonCombat;
        Live->ApplyMotionState(Death);
        TestFalse(TEXT("Live death starts playing instead of snapping to held frame"),Live->Appearance->bHoldActionFinal);
        for (int32 Frame=0; Frame<8; ++Frame) Live->Appearance->TickComponent(.025f,LEVELTICK_All,nullptr);
        TestTrue(TEXT("Live death advances its clock"),Live->Appearance->AnimTime>0.f);
        FACEObjectMotionState LateIdle; LateIdle.ForwardCommand=ACEMotion::Ready;
        Live->ApplyMotionState(LateIdle);
        TestFalse(TEXT("Slain creature cannot be picked or block movement"),Live->GetActorEnableCollision());
        for (int32 Frame=0; Frame<200; ++Frame) Live->Appearance->TickComponent(.05f,LEVELTICK_All,nullptr);
        TestTrue(TEXT("Live death eventually holds final pose"),Live->Appearance->bHoldActionFinal);
        Live->ApplyMotionState(LateIdle);
        TestTrue(TEXT("Late idle cannot cancel the final death pose"),Live->Appearance->bHoldActionFinal);
        Live->Destroy();

        Actor->Destroy();
    }
    // Remote chat state uses the same authored held cycle as the local player.
    FACEWorldObject RemotePlayer; RemotePlayer.Guid=0x50000005; RemotePlayer.bIsPlayer=true;
    RemotePlayer.ItemType=ACEItemType::Creature; RemotePlayer.SetupId=0x02000001;
    RemotePlayer.MotionTableId=0x09000001; RemotePlayer.bHasPosition=true;
    RemotePlayer.Position.CellId=0x7D640001; RemotePlayer.Position.Location=FVector(50,50,0);
    auto* RemoteActor=World->SpawnActor<AACEWorldEntityActor>();
    RemoteActor->InitializeFromObject(RemotePlayer,100,true);
    FACEObjectMotionState Emote; Emote.ActionCommand=0x420000F9; Emote.ActionSpeed=1;
    Emote.CurrentStyle=ACEMotion::StanceNonCombat; Emote.ForwardCommand=ACEMotion::Ready;
    RemoteActor->ApplyMotionState(Emote);
	TestFalse(TEXT("Live remote ATOYOT starts its transition before the hold"),RemoteActor->Appearance->bHoldActionFinal);
	RemoteActor->Appearance->TickComponent(.1f,LEVELTICK_All,nullptr);
	TestTrue(TEXT("Remote ATOYOT advances rather than jumping to its stance"),RemoteActor->Appearance->AnimTime > 0.f);
	TestFalse(TEXT("Remote ATOYOT has not skipped the transition"),RemoteActor->Appearance->bHoldActionFinal);
    for(int32 I=0; I<80; ++I) RemoteActor->Appearance->TickComponent(.025f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Networked ATOYOT reaches a held pose"),RemoteActor->Appearance->bHoldActionFinal);
    TArray<FTransform> HoldPose; int32 HoldCount=0;
    TestTrue(TEXT("Retail ATOYOT reference pose exists"),Dat->EvaluateMotionCommand(0x09000001,0x420000F9,0,
        RemoteActor->Appearance->PartMeshes.Num(),HoldPose,100,HoldCount,nullptr,nullptr,ACEMotion::StanceNonCombat));
    for(int32 I=0; I<HoldCount; ++I)
    {
        FTransform Actual; RemoteActor->Appearance->GetPartCurrentTransform(I,Actual);
        TestTrue(TEXT("Remote emote holds the DAT cycle frame"),Actual.Equals(HoldPose[I],.01));
    }
    RemoteActor->ApplyPhysicsState(ACEPhysicsState::Hidden);
    TestFalse(TEXT("Server-hidden player mesh is not rendered"),RemoteActor->Appearance->GetPartMesh(0)->IsVisible());
    RemoteActor->ApplyPhysicsState(0);
    TestTrue(TEXT("Server unhide restores player mesh"),RemoteActor->Appearance->GetPartMesh(0)->IsVisible());
    RemoteActor->Destroy();
    // NoDraw survives immediate/deferred construction, descriptor refresh and
    // visual restoration. Clearing it from the server makes the same object visible.
    FACEWorldObject Marker; Marker.Guid=0x70000001; Marker.SetupId=0x020010AC;
    Marker.bHasPosition=true; Marker.Position=RemotePlayer.Position;
    Marker.PhysicsState=ACEPhysicsState::NoDraw | ACEPhysicsState::Ethereal;
    auto* Invisible=World->SpawnActor<AACEWorldEntityActor>();
    Invisible->InitializeFromObject(Marker,100,false);
    TestFalse(TEXT("NoDraw suppresses the pre-DAT fallback"),Invisible->Mesh->IsVisible());
    Invisible->ApplyDatAppearanceFromObject(Marker);
    Invisible->Appearance->SetAppearanceVisible(true);
    TestFalse(TEXT("NoDraw survives deferred mesh restoration"),Invisible->Appearance->GetPartMesh(0)->IsVisible());
    Invisible->InitializeFromObject(Marker,100,true);
    TestFalse(TEXT("NoDraw survives descriptor refresh"),Invisible->Appearance->GetPartMesh(0)->IsVisible());
    Invisible->ApplyPhysicsState(ACEPhysicsState::Ethereal);
    TestTrue(TEXT("Server clearing NoDraw restores the model"),Invisible->Appearance->GetPartMesh(0)->IsVisible());
    Invisible->ApplyPhysicsState(ACEPhysicsState::Cloaked);
    Invisible->Appearance->SetAppearanceVisible(true);
    TestFalse(TEXT("Cloaked model cannot be revealed by visual restoration"),Invisible->Appearance->GetPartMesh(0)->IsVisible());
    Marker.PhysicsState=ACEPhysicsState::Ethereal;
    Marker.ObjectDescriptionFlags=ACEObjectDescFlag::HiddenAdmin;
    Invisible->InitializeFromObject(Marker,100,true);
    Invisible->ApplyPhysicsState(0);
    Invisible->Appearance->SetAppearanceVisible(true);
    TestFalse(TEXT("A physics update cannot expose a HiddenAdmin model"),Invisible->Appearance->GetPartMesh(0)->IsVisible());
    Marker.ObjectDescriptionFlags=0;
    Invisible->InitializeFromObject(Marker,100,true);
    TestTrue(TEXT("A new visible server descriptor restores the model"),Invisible->Appearance->GetPartMesh(0)->IsVisible());
    Invisible->Destroy();
    FACEWorldObject EffectOnly; EffectOnly.Guid=0x70000002;
    EffectOnly.PhysicsState=ACEPhysicsState::ParticleEmitter | ACEPhysicsState::Ethereal;
    auto* EffectProxy=World->SpawnActor<AACEWorldEntityActor>();
    EffectProxy->InitializeFromObject(EffectOnly,100,true);
    TestFalse(TEXT("Effect-only setup has no fallback body"),EffectProxy->Mesh->IsVisible());
    EffectProxy->ApplyPhysicsState(EffectOnly.PhysicsState);
    TestFalse(TEXT("Physics refresh cannot expose an effect-only fallback body"),EffectProxy->Mesh->IsVisible());
    EffectProxy->Destroy();
    // Prediction must cover a whole second at either 10 FPS or 60 FPS.
    FVector SlowResult, FastResult;
    for (const int32 Frames : {10, 60})
    {
        auto* Walker=World->SpawnActor<AACEWorldEntityActor>();
        Walker->InitializeFromObject(RemotePlayer,100,false);
        Walker->RemotePositionSmoothing=100000.f;
        FACEObjectMotionState Walk; Walk.bMoving=true; Walk.Forward=1.f;
        Walk.ForwardUnitsPerSecond=3.f; Walk.ForwardCommand=0x45000005;
        Walk.CurrentStyle=ACEMotion::StanceNonCombat;
        Walker->ApplyMotionState(Walk);
        const FVector Start=Walker->GetActorLocation();
        for (int32 Frame=0; Frame<Frames; ++Frame) Walker->Tick(1.f/Frames);
        FVector Travel=Walker->GetActorLocation()-Start; Travel.Z=0;
        TestTrue(TEXT("Network movement consumes the complete elapsed second"),FMath::IsNearlyEqual(Travel.Size(),300.,1.));
        if (Frames==10) SlowResult=Travel; else FastResult=Travel;
        for (int32 Frame=0; Frame<Frames*4; ++Frame) Walker->Tick(1.f/Frames);
        TestTrue(TEXT("Missing motion updates cannot extrapolate a remote fifty metres away"),
            FVector::Dist2D(Walker->GetActorLocation(),Start)<500.);
        FACEPosition Correction=RemotePlayer.Position; Correction.Location.X+=2; Correction.Location.Y+=1;
        Walker->ApplyACEPosition(Correction);
        Walker->ApplyMotionState(FACEObjectMotionState());
        Walker->Tick(.1f);
        TestTrue(TEXT("Stop and authoritative correction settle on the remote wire position"),
            FVector::Dist2D(Walker->GetActorLocation(),Correction.ToUnrealLocation(100))<.1);
        Walker->Destroy();
    }
    TestTrue(TEXT("Remote movement agrees across slow and fast rendering"),SlowResult.Equals(FastResult,1.));
    const auto* MarkerMesh = Dat->GetOrBuildSetupMesh(0x02000C39, 100.f);
    TestNotNull(TEXT("Tutorial spawn marker retains a valid placement setup"), MarkerMesh);
    if (MarkerMesh) TestTrue(TEXT("Zero draw-distance retail marker emits no visible geometry"), MarkerMesh->IsEmpty());
    FACEWorldObject Pedestal; Pedestal.Guid=0x77D6402D; Pedestal.WeenieClassId=16919;
    Pedestal.Name=TEXT("Pedestal Weak Spot"); Pedestal.SetupId=0x02000D55;
    Pedestal.MotionTableId=0x090000F9; Pedestal.PhysicsEffectTableId=0x3400002B;
    Pedestal.ItemType=ACEItemType::Misc; Pedestal.ObjectDescriptionFlags=ACEObjectDescFlag::Door;
    Pedestal.InitialMotionCommand=ACEMotion::OffCommandU16; Pedestal.InitialMotionStyle=ACEMotion::StanceNonCombat;
    auto* WeakSpot=World->SpawnActor<AACEWorldEntityActor>();
    WeakSpot->InitializeFromObject(Pedestal,100,true);
    TWeakObjectPtr<UMaterialInterface> ClosedMaterial = CastChecked<UMeshComponent>(
        WeakSpot->Appearance->GetPartMesh(0))->GetMaterial(0);
    auto Opacity=[](AACEWorldEntityActor* Actor)
    {
        auto* Part=Cast<UMeshComponent>(Actor->Appearance->GetPartMesh(0));
        auto* Material=Part ? Cast<UMaterialInstanceDynamic>(Part->GetMaterial(0)) : nullptr;
        return Material ? Material->K2_GetScalarParameterValue(TEXT("OpacityMul")) : -1.f;
    };
    for (int32 Cycle=0; Cycle<2; ++Cycle)
    {
        for (uint32 Command : {ACEMotion::On,ACEMotion::Off})
        {
            FACEObjectMotionState Door; Door.ForwardCommand=Command; Door.CurrentStyle=ACEMotion::StanceNonCombat;
            WeakSpot->ApplyMotionState(Door);
            for (int32 Frame=0; Frame<30; ++Frame)
            {
                WeakSpot->Appearance->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
                // Follow actual tick scheduling: calling a sleeping script masks this regression.
                if (WeakSpot->ScriptComponent->IsComponentTickEnabled())
                    WeakSpot->ScriptComponent->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
                if (Frame==5) TestTrue(TEXT("Pedestal performs its authored fade instead of popping"),Opacity(WeakSpot)>.05f && Opacity(WeakSpot)<.95f);
            }
            TestTrue(TEXT("Pedestal open/closed opacity matches retail"),FMath::IsNearlyEqual(Opacity(WeakSpot),Command==ACEMotion::On?0.f:1.f,.001f));
            if (Cycle == 0 && Command == ACEMotion::On)
            {
                CollectGarbage(RF_NoFlags);
                TestTrue(TEXT("Dissolve retains the replaced material through garbage collection"),ClosedMaterial.IsValid());
            }
            if (Command == ACEMotion::Off)
            {
                auto* Material = CastChecked<UMeshComponent>(WeakSpot->Appearance->GetPartMesh(0))->GetMaterial(0);
                TestTrue(TEXT("Closed pedestal restores its original material and shadow blend mode"),Material == ClosedMaterial.Get());
            }
        }
    }
    FACEObjectMotionState OpenDoor; OpenDoor.ForwardCommand=ACEMotion::On; OpenDoor.CurrentStyle=ACEMotion::StanceNonCombat;
    WeakSpot->ApplyMotionState(OpenDoor);
    for (int32 Frame=0; Frame<10; ++Frame) WeakSpot->ScriptComponent->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
    WeakSpot->ScriptComponent->NotifyAppearanceChanging();
    auto* ReplacedPart = CastChecked<UMeshComponent>(WeakSpot->Appearance->GetPartMesh(0));
    auto* NewMaterial = UMaterialInstanceDynamic::Create(ClosedMaterial->GetMaterial(), ReplacedPart);
    NewMaterial->CopyMaterialUniformParameters(ClosedMaterial.Get());
    NewMaterial->SetScalarParameterValue(TEXT("OpacityMul"), .8f);
    ReplacedPart->SetMaterial(0, NewMaterial);
    WeakSpot->ScriptComponent->StopAllEffects();
    TestTrue(TEXT("Stopping an old portal effect cannot restore the previous character material"), ReplacedPart->GetMaterial(0) == NewMaterial);
    WeakSpot->Destroy();
    // Network object translucency and a later Transparent hook replace one
    // scalar. Baking it into the image as well makes shadow creatures vanish.
    const auto* BodySetup=Dat->GetOrBuildSetupMesh(0x02000001,100.f);
    if (BodySetup && !BodySetup->Parts.IsEmpty() && !BodySetup->Parts[0].Sections.IsEmpty())
    {
        const uint32 Surface=BodySetup->Parts[0].Sections[0].SurfaceId;
        auto* Half=Cast<UMaterialInstanceDynamic>(Dat->GetOrCreateResolvedMaterial(Surface,0,FACEObjDesc(),.5f));
        if (TestNotNull(TEXT("Translucent creature material"),Half))
        {
            TestEqual(TEXT("Object translucency is applied once"),Half->K2_GetScalarParameterValue(TEXT("OpacityMul")),.5f);
            auto* Image=Cast<UTexture2D>(Half->K2_GetTextureParameterValue(TEXT("Texture")));
            if (TestNotNull(TEXT("Creature texture"),Image))
            {
                TestEqual(TEXT("Appearance patch edges clamp instead of sampling opposite face edge"),int32(Image->AddressX),int32(TA_Clamp));
                auto& Bulk=Image->GetPlatformData()->Mips[0].BulkData;
                const FColor* Pixels=static_cast<const FColor*>(Bulk.LockReadOnly());
                uint8 MaximumAlpha=0;
                for (int32 I=0;I<Image->GetSizeX()*Image->GetSizeY();++I) MaximumAlpha=FMath::Max(MaximumAlpha,Pixels[I].A);
                Bulk.Unlock();
                TestEqual(TEXT("Half-transparent creature retains opaque source texels"),int32(MaximumAlpha),255);
            }
        }
    }
    Pedestal.InitialMotionCommand=ACEMotion::OnCommandU16; Pedestal.PhysicsState=ACEPhysicsState::Ethereal;
    auto* OpenWeakSpot=World->SpawnActor<AACEWorldEntityActor>();
    OpenWeakSpot->InitializeFromObject(Pedestal,100,true);
    TestTrue(TEXT("Already open pedestal streams in invisible"),FMath::IsNearlyZero(Opacity(OpenWeakSpot),.001f));
    OpenWeakSpot->Destroy();
    Dat->GetOrBuildLandblockMesh(0x7D640000,100);
    FACEDatLandblockInfo TownInfo;
    Dat->LoadLandblockInfo(0x7D640000,TownInfo);
    for (const auto& Building : TownInfo.Buildings)
        Dat->GetOrBuildSetupMesh(Building.ModelId,100);
    TArray<ACEOutdoorPortalPlan::FAdmittedAperture> TownDoors;
    ACEOutdoorPortalPlan::CollectLandblockDoorwayApertures(*Dat,0x7D640000,100,TownDoors);
    if (TestTrue(TEXT("Yaraq has authored exterior portals"),!TownDoors.IsEmpty()))
    {
        const auto& Door=TownDoors[0];
        Dat->GetOrBuildEnvCellMesh(Door.DestEnvCellId,100);
        FVector Center=FVector::ZeroVector;
        for (const FVector& V : Door.WorldVerts) Center+=V;
        Center/=Door.WorldVerts.Num();
        TArray<uint32> ResidentBlocks{0x7D640000}; TSet<int32> Visible;
        ACEOutdoorPortalPlan::CollectOutdoorAdmittedEnvCells(*Dat,Center+Door.WorldNormal*15000,
            FConvexVolume(),100,ResidentBlocks,Visible);
        TestTrue(TEXT("Resident building interiors remain visible beyond 80 metres"),Visible.Contains(Door.DestEnvCellId));
    }
    auto* Land=World->SpawnActor<AACELandblockActor>();
    TestTrue(TEXT("Actual Yaraq landblock actor loads"),Land->LoadLandblock(0x7D640000,100,true,1,0));
    auto* Original=Land->TerrainMesh->GetMaterial(0);
    for (int32 I=0; I<6; ++I) Land->SetLandPortalLookOut((I&1)!=0);
    TestTrue(TEXT("Doorway updates preserve the cached land material instance"),Land->TerrainMesh->GetMaterial(0)==Original);
    const auto* Cell=Dat->GetOrBuildEnvCellMesh(0x7D64010C,100);
    if (Cell)
    {
        const FVector Origin=FACEPosition::AceVectorToUnreal(FVector(125*192,100*192,0),100);
        auto* Indoor=World->SpawnActor<AACEEnvCellActor>();
        TestTrue(TEXT("Actual Yaraq shop doorway actor loads"),Indoor->LoadEnvCell(0x7D64010C,Origin,100));
        Indoor->SetEnvCellCollisionActive(true,true,false);
        const FTransform Frame=Cell->GetCellLocalToLandblock(100)*FTransform(Origin);
        int32 FloorHits=0, FloorFaces=0;
        for (const auto& Section:Cell->CollisionSections)
        {
            for (int32 I=0; I+2<Section.Triangles.Num(); I+=3)
            {
                const FVector A=Frame.TransformPosition(Section.Vertices[Section.Triangles[I]]);
                const FVector B=Frame.TransformPosition(Section.Vertices[Section.Triangles[I+1]]);
                const FVector C=Frame.TransformPosition(Section.Vertices[Section.Triangles[I+2]]);
                if (FMath::Abs(FVector::CrossProduct(B-A,C-A).GetSafeNormal().Z)<.9) continue;
                const FVector Center=(A+B+C)/3;
                FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(ACERetailFloor),true);
                ++FloorFaces;
                if (Indoor->CellCollisionMesh->LineTraceComponent(Hit,Center+FVector(0,0,30),Center-FVector(0,0,30),Query)) ++FloorHits;
            }
        }
        TestTrue(TEXT("Authored indoor floors have cooked collision immediately on entry"),FloorFaces>0 && FloorHits==FloorFaces);
        TArray<ACEOutdoorPortalPlan::FAdmittedAperture> Exits;
        for (int32 I=0; I<Cell->CellPortals.Num(); ++I)
        {
            if (!Cell->CellPortals[I].IsOutsidePortal()) continue;
            auto& Exit=Exits.AddDefaulted_GetRef();
            Exit.WorldNormal=Frame.TransformVectorNoScale(Cell->PortalApertureLocalNormals[I]);
            for (const auto& V:Cell->PortalApertureLocalVerts[I]) Exit.WorldVerts.Add(Frame.TransformPosition(V));
        }
        Dat->SetLandLookOutClip(true,Frame.TransformPosition(FVector(0,0,100)),Exits);
        auto* MID=Cast<UMaterialInstanceDynamic>(Land->TerrainMesh->GetMaterial(0));
        TestTrue(TEXT("Attached terrain receives the live indoor portal mask"),MID && MID->K2_GetScalarParameterValue(TEXT("PortalViewCount"))==Exits.Num());
        Dat->SetLandLookOutClip(false,FVector::ZeroVector,{});
        TestTrue(TEXT("Attached terrain leaves the indoor mask when stepping out"),MID && MID->K2_GetScalarParameterValue(TEXT("LookOutEnable"))==0);

        if (FApp::CanEverRender())
        {
            TMap<uint32,AACEEnvCellActor*> Rooms;
            Rooms.Add(0x7D64010C,Indoor);
            int32 LitVertices=0;
            for (uint32 Id=0x7D640100; Id<0x7D640174; ++Id)
            {
                const auto* BuiltCell=Dat->GetOrBuildEnvCellMesh(Id,100);
                if (!BuiltCell) continue;
                for (const auto& Section:BuiltCell->Sections) for (const auto& Color:Section.VertexColors)
                    if (Color.R>0.01f || Color.G>0.01f || Color.B>0.01f) ++LitVertices;
                if (Rooms.Contains(Id)) continue;
                auto* Room=World->SpawnActor<AACEEnvCellActor>();
                if (Room->LoadEnvCell(Id,Origin,100)) Rooms.Add(Id,Room);
            }
            TestTrue(TEXT("Yaraq cell geometry contains authored static-light contributions"),LitVertices>0);
            // Exercise the actual streaming spawn path, including separate PhysicsBSP meshes.
            FACEDatLandblockInfo Info; Dat->LoadLandblockInfo(0x7D640000,Info);
            TArray<UStaticMeshComponent*> Shells;
            for (int32 I=0; I<Info.Buildings.Num(); ++I)
            {
                const auto& Building=Info.Buildings[I];
                Dat->GetOrBuildSetupMesh(Building.ModelId,100);
                AACELandblockActor::FPendingScenery Pending;
                Pending.ModelId=Building.ModelId; Pending.Origin=Building.Origin; Pending.Orientation=Building.Orientation;
                Pending.bBuilding=true; Pending.LandblockBuildingIndex=I;
                TestTrue(TEXT("Runtime building streaming spawn completes"),Land->TrySpawnOneScenery(Dat,Pending));
            }
            for (const auto& Shell:Land->BuildingShells) Shells.Add(Shell.Mesh);
            TestEqual(TEXT("Every authored building shell spawns"),Shells.Num(),Info.Buildings.Num());
            World->Tick(LEVELTICK_All,.016f);
            for (const auto& Shell : Land->BuildingShells)
            {
                const auto* Built=Dat->GetOrBuildSetupMesh(Info.Buildings[Shell.InfoIndex].ModelId,100);
                int32 Tested=0, Hits=0, SweepHits=0;
                if (Shell.CollisionMesh && Built) for (const auto& Part:Built->Parts) for(const auto& Sec:Part.Sections)
                {
                    if(!Sec.bCollisionOnly)continue;
                    for(int32 I=0;I+2<Sec.Triangles.Num() && Tested<12;I+=3)
                    {
                        auto X=Part.BindTransform*Shell.CollisionMesh->GetComponentTransform();
                        const FVector A=X.TransformPosition(Sec.Vertices[Sec.Triangles[I]]), B=X.TransformPosition(Sec.Vertices[Sec.Triangles[I+1]]), C=X.TransformPosition(Sec.Vertices[Sec.Triangles[I+2]]);
                        const FVector N=FVector::CrossProduct(B-A,C-A).GetSafeNormal();
                        if(FMath::Abs(N.Z)>.2 || FVector::CrossProduct(B-A,C-A).Size()<10000)continue;
                        const FVector Center=(A+B+C)/3; FHitResult Hit;
                        FCollisionQueryParams Query(SCENE_QUERY_STAT(ACEWallRegression),true);
                        ++Tested;
                        if(Shell.CollisionMesh->LineTraceComponent(Hit,Center+N*50,Center-N*50,Query)) ++Hits;
                        if(World->SweepSingleByChannel(Hit,Center+N*50,Center-N*50,FQuat::Identity,ECC_Pawn,
                            FCollisionShape::MakeSphere(10),Query)) ++SweepHits;
                    }
                }
                AddInfo(FString::Printf(TEXT("Building %08X actual wall collision hits %d/%d"),Info.Buildings[Shell.InfoIndex].ModelId,Hits,Tested));
                TestTrue(TEXT("Each building has cooked collision on its authored walls"),Tested>0 && Hits==Tested);
                TestTrue(TEXT("Player movement queries hit the streamed building walls"),Tested>0 && SweepHits==Tested);
            }
            const int32 OccupiedBuilding=Dat->FindBuildingInfoIndexForIndoorCell(0x7D640000,0x7D64010C,100);
            TestTrue(TEXT("Doorway cell resolves to its own building"),OccupiedBuilding!=INDEX_NONE);
            Land->SetBuildingShellsBlockPawn(true,{OccupiedBuilding});
            for(const auto& Shell:Land->BuildingShells) if(Shell.CollisionMesh)
                TestEqual(TEXT("Only the occupied shell yields to room collision; neighboring walls remain solid"),
                    Shell.CollisionMesh->GetCollisionResponseToChannel(ECC_Pawn),Shell.InfoIndex==OccupiedBuilding?ECR_Ignore:ECR_Block);
            Land->SetBuildingShellsBlockPawn(true,{});
            const int32 PendingBefore = Land->PendingScenery.Num();
            const auto ShellCollision = Shells[0]->GetCollisionEnabled();
            for (int32 I=0; I<4; ++I)
            {
                Land->SetFullResDetail(false);
                TestTrue(TEXT("Detail downgrade hides the resident shell"),Shells[0]->bHiddenInGame);
                Land->SetFullResDetail(true);
                TestTrue(TEXT("Detail return reuses the shell component"),Land->BuildingShells[0].Mesh==Shells[0]);
                TestEqual(TEXT("Detail return preserves the separate visual/physics collision roles"),Shells[0]->GetCollisionEnabled(),ShellCollision);
            }
            TestEqual(TEXT("Detail boundary does not requeue scenery"),Land->PendingScenery.Num(),PendingBefore);

            // Use the runtime sun construction; preconfiguring a test light masks
            // differences between game lighting and isolated shadow captures.
            auto* Sky = World->SpawnActor<AACESkyDomeActor>();
            Sky->DatSubsystem = Dat;
            const auto* Region = Dat->GetRegionSkyInfo();
            if (!Region || Region->DayGroups.IsEmpty() || Region->DayGroups[0].TimesOfDay.IsEmpty()) return false;
            const auto& InitialLight = Region->DayGroups[0].TimesOfDay[0];
            Sky->UpdateWorldLighting(InitialLight, InitialLight, 0.f, InitialLight.Begin);
            if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
            auto* Target=NewObject<UTextureRenderTarget2D>(Land); Target->ClearColor=FLinearColor(.1f,.15f,.25f);
            Target->InitCustomFormat(640,480,PF_B8G8R8A8,false);
            auto* Capture=NewObject<USceneCaptureComponent2D>(Land);
            Capture->TextureTarget=Target; Capture->FOVAngle=90; Capture->CaptureSource=SCS_FinalColorLDR;
            Capture->bCaptureEveryFrame=false; Capture->bCaptureOnMovement=false;
            // CSM needs a persistent view state even for explicit offscreen captures.
            Capture->bAlwaysPersistRenderingState=true;
            Capture->ShowFlags.SetEyeAdaptation(false); Capture->ShowFlags.SetBloom(false);
            Capture->ShowFlags.SetAtmosphere(false); Capture->ShowFlags.SetAntiAliasing(false);
            Capture->RegisterComponent();
            auto Render=[&](const FString& Name)
            {
                World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
                TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
                TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(640,480,Pixels,PNG);
                const auto Path=FPaths::ProjectSavedDir()/TEXT("Automation/RetailParity")/(Name+TEXT(".png"));
                FFileHelper::SaveArrayToFile(PNG,*Path); return Pixels;
            };
            // Isolated network bodies: a bright terrain pixel cannot satisfy these tests.
            for (uint32 Setup : {0x02000117u,0x02000A0Bu,0x02000001u})
            {
                FACEWorldObject Obj; Obj.Guid=Setup; Obj.SetupId=Setup;
                Obj.ItemType=Setup==0x02000117u ? ACEItemType::Misc : ACEItemType::Creature;
                Obj.bIsPlayer=Setup==0x02000001u;
                auto* Receiver=World->SpawnActor<AACEWorldEntityActor>(); Receiver->InitializeFromObject(Obj,100,true);
                const FVector Center=Origin+FVector(-3000,-3000,1500);
                Receiver->SetActorLocation(Center);
                auto* LampActor=World->SpawnActor<AActor>();
                auto* Lamp=NewObject<UPointLightComponent>(LampActor); LampActor->SetRootComponent(Lamp);
                Lamp->SetUseInverseSquaredFalloff(false); Lamp->SetIntensity(25); Lamp->SetAttenuationRadius(700);
                Lamp->SetCastShadows(false); Lamp->RegisterComponent(); Lamp->SetWorldLocation(Center+FVector(150,-200,250));
                Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
                Capture->ClearShowOnlyComponents(); Capture->ShowOnlyActorComponents(Receiver); Capture->ShowFlags.SetFog(false);
                const FVector Eye=Center+FVector(350,-550,300);
                Capture->SetWorldLocationAndRotation(Eye,(Center+FVector(0,0,70)-Eye).Rotation());
                if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
                Lamp->SetVisibility(false); const auto Dark=Render(FString::Printf(TEXT("Object_%08X_NoLamp"),Setup));
                Lamp->SetVisibility(true); const auto Bright=Render(FString::Printf(TEXT("Object_%08X_Lamp"),Setup));
                int64 Difference=0; int32 Changed=0;
                for (int32 I=0;I<Bright.Num();++I) { if (FMath::Max3(Dark[I].R,Dark[I].G,Dark[I].B)<40)continue; const int32 D=int32(Bright[I].R)-int32(Dark[I].R); Difference+=D; if(D>12)++Changed; }
                AddInfo(FString::Printf(TEXT("Object %08X receives local light: pixels=%d red=%lld"),Setup,Changed,Difference));
                TestTrue(TEXT("Pool, NPC and player body each receive world light"),Changed>100 && Difference>10000);
                Lamp->SetVisibility(false);
                auto* Blocker=World->SpawnActor<AActor>();
                auto* BlockMesh=NewObject<UProceduralMeshComponent>(Blocker); Blocker->SetRootComponent(BlockMesh);
                auto* Sun=Sky->SunLightActor->FindComponentByClass<UDirectionalLightComponent>();
                const FVector Ray=Sun->GetForwardVector(); FVector U,V; Ray.FindBestAxisVectors(U,V);
                BlockMesh->CreateMeshSection_LinearColor(0,{-U*350-V*350,U*350-V*350,U*350+V*350,-U*350+V*350},
                    {0,1,2,0,2,3,2,1,0,3,2,0},{},{},{},{},false);
                BlockMesh->SetMobility(EComponentMobility::Movable); BlockMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                BlockMesh->bCastShadowAsTwoSided=true; BlockMesh->RegisterComponent();
                Blocker->SetActorLocation(Center+FVector(0,0,90)-Ray*500);
                Capture->ShowOnlyActorComponents(Blocker);
                BlockMesh->SetCastShadow(false);
                const auto Unshadowed=Render(FString::Printf(TEXT("Object_%08X_NoShadow"),Setup));
                BlockMesh->SetCastShadow(true);
                const auto Shadowed=Render(FString::Printf(TEXT("Object_%08X_Shadow"),Setup));
                int64 ShadowChange=0; int32 ShadowPixels=0;
                for(int32 I=0;I<Dark.Num();++I)
                {
                    if(FMath::Max3(Dark[I].R,Dark[I].G,Dark[I].B)<40)continue;
                    const int32 D=int32(Unshadowed[I].R)+Unshadowed[I].G+Unshadowed[I].B-int32(Shadowed[I].R)-Shadowed[I].G-Shadowed[I].B;
                    ShadowChange+=D; if(D>24)++ShadowPixels;
                }
                AddInfo(FString::Printf(TEXT("Object %08X receives occluder shadow: pixels=%d rgb=%lld sun=%s intensity=%.2f"),Setup,ShadowPixels,ShadowChange,*Ray.ToString(),Sun->Intensity));
                TestTrue(TEXT("Pool, NPC and player body each receive cast shadows"),ShadowPixels>100 && ShadowChange>10000);
                if (Setup==0x02000117u)
                {
                    FACEObjDesc EmptyAppearance;
                    auto* Glass=Dat->GetOrCreateResolvedMaterial(0x08000708,0,EmptyAppearance,.5f);
                    BlockMesh->SetMaterial(0,Glass);
                    auto* GlassAppearance=NewObject<UACECharacterAppearanceComponent>(Blocker);
                    GlassAppearance->PartMeshes.Add(BlockMesh);
                    GlassAppearance->SetPartsCastShadow(false,false);
                    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
                    const auto NoGlassShadow=Render(TEXT("Translucent_NoShadow"));
                    GlassAppearance->SetPartsCastShadow(true,false);
                    TestTrue(TEXT("Translucent world material requests an ordinary alpha shadow"),Glass->GetMaterial()->bCastDynamicShadowAsMasked);
                    const auto GlassShadow=Render(TEXT("Translucent_Shadow"));
                    int32 ShadowedPixels=0; int64 ShadowLoss=0;
                    for (int32 I=0;I<GlassShadow.Num();++I)
                    {
                        if (FMath::Max3(NoGlassShadow[I].R,NoGlassShadow[I].G,NoGlassShadow[I].B)<50) continue;
                        const int32 Loss=int32(NoGlassShadow[I].R)+NoGlassShadow[I].G+NoGlassShadow[I].B
                            -int32(GlassShadow[I].R)-GlassShadow[I].G-GlassShadow[I].B;
                        ShadowLoss+=Loss; if (Loss>24) ++ShadowedPixels;
                    }
                    AddInfo(FString::Printf(TEXT("Translucent mesh casts ordinary shadow: %d pixels, loss=%lld"),ShadowedPixels,ShadowLoss));
                    TestTrue(TEXT("Translucent surface casts a visible ordinary shadow"),ShadowedPixels>100 && ShadowLoss>10000);
                    GlassAppearance->PartMeshes.Reset();
                }
                Blocker->Destroy();
                LampActor->Destroy(); Receiver->Destroy();
                Capture->ClearShowOnlyComponents(); Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
                Capture->ShowFlags.SetFog(true);
            }
            // The second-floor bench uses double-wound retail seat polygons. Compare
            // the same underside at the origin and at the real Yaraq coordinates;
            // floating-point depth errors must not punch holes in its texture.
            if (auto* BenchRoom=Rooms.FindRef(0x7D64013A))
            {
                const auto* BenchCell=Dat->FindEnvCellMesh(0x7D64013A,100);
                for (const auto& Stab:BenchCell->StaticObjects)
                    Dat->GetOrBuildSetupMesh(Stab.Id,100,UACEDatSubsystem::ACEPlacementResting);
                BenchRoom->EnsureStaticObjectsQueued();
                for (int32 I=0;I<20 && BenchRoom->HasPendingStaticObjects();++I) BenchRoom->Tick(.016f);
                BenchRoom->SetEnvCellHiddenInGame(false);
                TArray<UInstancedStaticMeshComponent*> Furniture; BenchRoom->GetComponents(Furniture);
                for (auto* Bench:Furniture)
                {
                    if (!Bench->GetName().Contains(TEXT("02000368"))) continue;
                    FTransform Placement; Bench->GetInstanceTransform(0,Placement,true);
                    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
                    Capture->ShowOnlyComponents.Add(Bench);
                    Capture->ShowFlags.SetFog(false);
                    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
                    if (FParse::Param(FCommandLine::Get(), TEXT("ACEBenchDepthProbe")))
                    {
                        TArray<UMaterialInterface*> OriginalMaterials;
                        TArray<UMaterialInterface*> NoDepthMaterials;
                        UMaterial* NoDepthBase=nullptr;
                        for (int32 I=0; I<Bench->GetNumMaterials(); ++I)
                        {
                            auto* BenchMaterial=Bench->GetMaterial(I);
                            OriginalMaterials.Add(BenchMaterial);
                            if (!NoDepthBase)
                            {
                                NoDepthBase=DuplicateObject<UMaterial>(BenchMaterial->GetMaterial(),GetTransientPackage());
                                NoDepthBase->GetEditorOnlyData()->PixelDepthOffset.Expression=nullptr;
                                NoDepthBase->PreEditChange(nullptr); NoDepthBase->PostEditChange();
                                NoDepthBase->ForceRecompileForRendering();
                            }
                            auto* Control=UMaterialInstanceDynamic::Create(NoDepthBase,Bench);
                            Control->CopyMaterialUniformParameters(BenchMaterial);
                            NoDepthMaterials.Add(Control);
                        }
                        if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
                        for (int32 View=0; View<24; ++View)
                        {
                            const float Distance=View<12 ? 220.f : 900.f;
                            const FVector EyeOffset=Placement.TransformVectorNoScale(FVector(-110+(View%4)*70,-Distance,-30+(View%12)/4*35));
                            const FVector AimOffset(0,0,50);
                            Capture->SetWorldLocationAndRotation(Placement.GetLocation()+EyeOffset,(AimOffset-EyeOffset).Rotation());
                            for (int32 I=0;I<OriginalMaterials.Num();++I) Bench->SetMaterial(I,OriginalMaterials[I]);
                            const auto WithDepth=Render(FString::Printf(TEXT("BenchDepth_Original_%d"),View));
                            for (int32 I=0;I<NoDepthMaterials.Num();++I) Bench->SetMaterial(I,NoDepthMaterials[I]);
                            const auto WithoutDepth=Render(FString::Printf(TEXT("BenchDepth_Control_%d"),View));
                            int32 Changed=0;
                            for (int32 I=0;I<WithDepth.Num();++I)
                                if (FMath::Abs(int32(WithDepth[I].R)-int32(WithoutDepth[I].R))>40
                                    || FMath::Abs(int32(WithDepth[I].G)-int32(WithoutDepth[I].G))>40) ++Changed;
                            AddInfo(FString::Printf(TEXT("Bench depth view %d changed pixels %d"),View,Changed));
                        }
                        for (int32 I=0;I<OriginalMaterials.Num();++I) Bench->SetMaterial(I,OriginalMaterials[I]);
                    }
                    for (int32 View=0;View<5;++View)
                    {
                        const FVector EyeOffset=Placement.TransformVectorNoScale(FVector(-90+View*45,-220,-45));
                        const FVector AimOffset=FVector(0,0,45);
                        BenchRoom->SetActorLocation(FVector::ZeroVector);
                        Capture->SetWorldLocationAndRotation(Placement.GetLocation()-Origin+EyeOffset,(AimOffset-EyeOffset).Rotation());
                        const auto Near=Render(FString::Printf(TEXT("BenchUnderside_Origin_%d"),View));
                        BenchRoom->SetActorLocation(Origin);
                        Capture->SetWorldLocationAndRotation(Placement.GetLocation()+EyeOffset,(AimOffset-EyeOffset).Rotation());
                        const auto Far=Render(FString::Printf(TEXT("BenchUnderside_Yaraq_%d"),View));
                        int32 Changed=0;
                        for (int32 I=0;I<Near.Num();++I)
                            if (FMath::Abs(int32(Near[I].R)-int32(Far[I].R))>40
                                || FMath::Abs(int32(Near[I].G)-int32(Far[I].G))>40) ++Changed;
                        AddInfo(FString::Printf(TEXT("Bench underside view %d coordinate-dependent pixels: %d"),View,Changed));
                        TestTrue(TEXT("Bench underside stays intact at real world coordinates"),Changed<300);
                    }
                    Capture->ClearShowOnlyComponents();
                    // Polygons 15/16 of 01000EB3 share the X=95 / Y=81.5
                    // planes of room 7D64013A. Isolate that depth tie with the
                    // actual room and furniture materials so geometry elsewhere
                    // in the bench cannot mask a failure of surface ordering.
                    auto* WallFace=NewObject<UProceduralMeshComponent>(BenchRoom);
                    auto* SeatFace=NewObject<UProceduralMeshComponent>(BenchRoom);
                    const FVector Center=Origin+FACEPosition::AceVectorToUnreal(FVector(95,82.75,15.65),100);
                    for (auto* Face:{WallFace,SeatFace})
                    {
                        Face->RegisterComponent();Face->SetCastShadow(false);
                        Face->CreateMeshSection_LinearColor(0,
                            {Center+FVector(0,-80,-80),Center+FVector(0,80,-80),Center+FVector(0,80,80),Center+FVector(0,-80,80)},
                            {0,1,2,0,2,3,0,2,1,0,3,2},{FVector(1,0,0),FVector(1,0,0),FVector(1,0,0),FVector(1,0,0)},
                            {FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)},{},{},false);
                    }
                    WallFace->SetMaterial(0,BenchRoom->CellMesh->GetMaterial(0));
                    const auto& RoomData=BenchRoom->CellMesh->GetCustomPrimitiveData().Data;
                    WallFace->SetDefaultCustomPrimitiveDataFloat(0,RoomData.IsEmpty()?0.f:RoomData[0]);
                    SeatFace->SetMaterial(0,Bench->GetMaterial(0));
                    auto* TieDepth=NewObject<UTextureRenderTarget2D>(Capture);
                    TieDepth->RenderTargetFormat=RTF_R32f; TieDepth->InitAutoFormat(32,32);
                    Capture->TextureTarget=TieDepth; Capture->CaptureSource=SCS_SceneDepth;
                    auto ReadTieDepth=[&]()
                    {
                        World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
                        TArray<FLinearColor> Pixels; TieDepth->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
                        return Pixels;
                    };
                    for (int32 View=0;View<3;++View)
                    {
                        const FVector Eye=Center+FVector(180,View*.2,-20);
                        Capture->SetWorldLocationAndRotation(Eye,(Center-Eye).Rotation());
                        Capture->ShowOnlyComponents={SeatFace};
                        const auto FurnitureOnly=ReadTieDepth();
                        Capture->ShowOnlyComponents={WallFace};
                        const auto WallOnly=ReadTieDepth();
                        Capture->ShowOnlyComponents.Add(SeatFace);
                        const auto Shared=ReadTieDepth();
                        if (TestEqual(TEXT("Coplanar scene depth readback"),Shared.Num(),32*32))
                        {
                            const int32 P=16*32+16;
                            AddInfo(FString::Printf(TEXT("Coplanar depth seat=%.4f wall=%.4f shared=%.4f"),FurnitureOnly[P].R,WallOnly[P].R,Shared[P].R));
                            TestTrue(TEXT("Room wall writes behind the seat"),WallOnly[P].R>FurnitureOnly[P].R+.1f);
                            TestTrue(TEXT("Furniture wins a coplanar room wall at sub-centimeter camera shifts"),FMath::Abs(Shared[P].R-FurnitureOnly[P].R)<.01f);
                        }
                    }
                    Capture->TextureTarget=Target; Capture->CaptureSource=SCS_FinalColorLDR;
                    WallFace->DestroyComponent();SeatFace->DestroyComponent();Capture->ClearShowOnlyComponents();
                    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
                    Capture->ShowFlags.SetFog(true);
                }
            }
            // Drive the presenter, not just the portal-mask helper, while the camera moves.
            auto* PC=World->SpawnActor<APlayerController>();
            World->AddController(PC);
            PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));
            PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();
            PC->PlayerCameraManager->InitializeFor(PC);
            auto* Pawn=World->SpawnActor<APawn>();
            auto* PawnRoot=NewObject<USceneComponent>(Pawn); Pawn->SetRootComponent(PawnRoot);
            PawnRoot->RegisterComponent(); PC->Possess(Pawn);
            auto* Camera=World->SpawnActor<ACameraActor>(); PC->SetViewTarget(Camera);
            auto* Presenter=NewObject<UACETerrainPresenterComponent>(Land);
            Presenter->bHasKnownCell=true; Presenter->LastKnownCellId=0x7D64010C;
            Presenter->Spawned.Add(0x7D640000,Land);
            for (const auto& Pair:Rooms) Presenter->SpawnedEnvCells.Add(Pair.Key,Pair.Value);
            const int32 ResidentBefore=Presenter->SpawnedEnvCells.Num();
            Presenter->LastKnownCellId=0x7D640135;
            const FVector BenchAim=Origin+FACEPosition::AceVectorToUnreal(FVector(94.5,82.75,15.7),100);
            for (int32 I=0;I<7;++I)
            {
                const FVector Eye=Origin+FACEPosition::AceVectorToUnreal(FVector(85.5,82.5+I*.5,15.1+I*.1),100);
                Pawn->SetActorLocation(Eye); Camera->SetActorLocation(Eye);
                Camera->SetActorRotation((BenchAim-Eye).Rotation()); PC->PlayerCameraManager->UpdateCamera(.016f);
                Presenter->UpdateCameraVisibility();
                Capture->SetWorldLocationAndRotation(Eye,(BenchAim-Eye).Rotation());
                const auto Culled=Render(FString::Printf(TEXT("BenchBalcony_PView_%d"),I));
                for (const auto& Pair:Rooms) Pair.Value->SetEnvCellHiddenInGame(false);
                const auto Complete=Render(FString::Printf(TEXT("BenchBalcony_All_%d"),I));
                int32 Missing=0;
                for (int32 P=0;P<Culled.Num();++P)
                    if (FMath::Abs(int32(Culled[P].R)-int32(Complete[P].R))>40
                        || FMath::Abs(int32(Culled[P].G)-int32(Complete[P].G))>40) ++Missing;
                AddInfo(FString::Printf(TEXT("Balcony view %d camera cell %08X culling-dependent pixels %d"),I,Presenter->ViewerCellId,Missing));
                TestTrue(TEXT("Balcony portal visibility preserves the rendered bench and room"),Missing<300);
            }
            Presenter->LastKnownCellId=0x7D64010C;
            if (!Exits.IsEmpty())
            {
                FVector Door=FVector::ZeroVector; for (const auto& V:Exits[0].WorldVerts) Door+=V;
                Door/=Exits[0].WorldVerts.Num();
                for (int32 I=0; I<4; ++I)
                {
                    const FVector Eye=Door-Exits[0].WorldNormal*(80.f+I*25.f);
                    Pawn->SetActorLocation(Eye); Camera->SetActorLocation(Eye);
                    Camera->SetActorRotation(Exits[0].WorldNormal.Rotation()); PC->PlayerCameraManager->UpdateCamera(.016f);
                    Presenter->UpdateCameraVisibility();
                    AddInfo(FString::Printf(TEXT("Camera fixture first=%d viewer=%08X eye=%s actual=%s"),
                        World->GetFirstPlayerController()==PC, Presenter->ViewerCellId, *Eye.ToString(),
                        *MID->K2_GetVectorParameterValue(TEXT("LookOutCam")).ToString()));
                    TestTrue(TEXT("Every camera move updates the attached terrain portal eye"),
                        MID->K2_GetVectorParameterValue(TEXT("LookOutCam")).Equals(FLinearColor(Eye),.1f));
                    TestEqual(TEXT("Camera movement does not unload room actors"),Presenter->SpawnedEnvCells.Num(),ResidentBefore);
                    Capture->SetWorldLocationAndRotation(Eye,Exits[0].WorldNormal.Rotation());
                    Render(FString::Printf(TEXT("YaraqCameraDoorway_%d"),I));
                }
            }
            PC->Destroy(); Pawn->Destroy(); Camera->Destroy();

            if (!Exits.IsEmpty())
            {
                const auto& Exit=Exits[0]; FVector Door=FVector::ZeroVector;
                for (const auto& V:Exit.WorldVerts) Door+=V;
                Door/=Exit.WorldVerts.Num();
                // Isolate actual room geometry/furnishings so exterior pixels do not
                // mask day/night changes in the indoor illumination regression.
                Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
                for (const auto& Pair:Rooms) Capture->ShowOnlyActorComponents(Pair.Value);
                Capture->ShowFlags.SetFog(false);
                Dat->SetLandLookOutClip(false,FVector::ZeroVector,{});
                Capture->SetWorldLocationAndRotation(Door+Exit.WorldNormal*180.f,(-Exit.WorldNormal).Rotation());
                Dat->SetInteriorAmbient(FLinearColor(.01f,.02f,.04f));
                const auto Night=Render(TEXT("YaraqUniformInterior_Night"));
                Dat->SetInteriorAmbient(FLinearColor(1.f,.7f,.3f));
                const auto Day=Render(TEXT("YaraqUniformInterior_Day"));
                int32 Textured=0; int64 TextureDifference=0;
                for (int32 I=0;I<Day.Num() && I<Night.Num();++I)
                {
                    // Ignore the empty scene background and tolerate sub-byte
                    // postprocess dithering; measure the actual warm room textures.
                    if (Day[I].R>80 && Day[I].R>Day[I].B+20)
                    {
                        ++Textured;
                        TextureDifference+=FMath::Abs(int32(Day[I].R)-int32(Night[I].R))
                            +FMath::Abs(int32(Day[I].G)-int32(Night[I].G))
                            +FMath::Abs(int32(Day[I].B)-int32(Night[I].B));
                    }
                }
                TestTrue(TEXT("Uniform interior fixture renders visible room textures"),Textured>2000);
                TestTrue(TEXT("Interior illumination stays stable as exterior ambient changes"),Textured>0 && double(TextureDifference)/(Textured*3)<1.0);
                TArray<TPair<UMaterialInstanceDynamic*,FLinearColor>> DoorParameters;
                for(int32 I=0;I<Indoor->CellMesh->GetNumMaterials();++I)
                    if(auto* M=Cast<UMaterialInstanceDynamic>(Indoor->CellMesh->GetMaterial(I)))
                    {
                        FLinearColor Value;
                        if(M->GetVectorParameterValue(TEXT("Door0"),Value) && Value.A>0)
                        { DoorParameters.Emplace(M,Value); M->SetVectorParameterValue(TEXT("Door0"),FLinearColor::Transparent); }
                    }
                const auto NoBlend=Render(TEXT("YaraqDoorThreshold_NoBlend"));
                for(const auto& Pair:DoorParameters) Pair.Key->SetVectorParameterValue(TEXT("Door0"),Pair.Value);
                const auto Blended=Render(TEXT("YaraqDoorThreshold_Blended"));
                int32 BlendedPixels=0;
                for(int32 I=0;I<Blended.Num();++I) if(NoBlend[I].R>Blended[I].R+8) ++BlendedPixels;
                AddInfo(FString::Printf(TEXT("Entrance blend softens %d actual room pixels"),BlendedPixels));
                TestTrue(TEXT("Authored doorway applies a spatial transition to room illumination"),BlendedPixels>100);
                const auto SpillLights=Indoor->DoorwayLights;
                TestTrue(TEXT("Exterior-connected room has bounded doorway spill lights"),SpillLights.Num()>0 && SpillLights.Num()<=2);
                Capture->ClearShowOnlyComponents();
                // Isolate actual terrain as the receiver: a bright room alone cannot
                // make the doorway-spill regression pass.
                Capture->ShowOnlyComponents.Add(Land->TerrainMesh);
                for (const auto& Pair:Rooms)
                {
                    for (auto Light:Pair.Value->DoorwayLights) Light->SetVisibility(false);
                }
                Indoor->SetEnvCellHiddenInGame(false);
                float Bottom=Door.Z; for (const auto& V:Exit.WorldVerts) Bottom=FMath::Min(Bottom,float(V.Z));
                const FVector Ground=FVector(Door.X,Door.Y,Bottom)+Exit.WorldNormal*140;
                const FVector SpillEye=Ground+Exit.WorldNormal*400+FVector(0,0,230);
                Capture->SetWorldLocationAndRotation(SpillEye,(Ground-SpillEye).Rotation());
                const auto NoSpill=Render(TEXT("YaraqDoorSpill_Off"));
                for (auto Light:SpillLights) Light->SetVisibility(true);
                const auto Spill=Render(TEXT("YaraqDoorSpill_On"));
                int32 Brighter=0; int64 AddedLight=0;
                for (int32 I=0;I<Spill.Num();++I)
                {
                    AddedLight+=int32(Spill[I].R)-int32(NoSpill[I].R);
                    if (Spill[I].R>NoSpill[I].R+20 && Spill[I].G>NoSpill[I].G+10) ++Brighter;
                }
                AddInfo(FString::Printf(TEXT("Doorway light brightens %d terrain pixels, net red %lld"),Brighter,AddedLight));
                TestTrue(TEXT("Doorway light visibly spills onto the exterior terrain"),Brighter>500 && AddedLight>20000);
                // Close the authored apertures with opaque door-sized meshes. The
                // street receiver must lose light even though emitters stay enabled.
                TArray<UProceduralMeshComponent*> DoorOccluders;
                auto* DoorFixture=World->SpawnActor<AActor>();
                auto* DoorRoot=NewObject<USceneComponent>(DoorFixture); DoorFixture->SetRootComponent(DoorRoot); DoorRoot->RegisterComponent();
                for (auto Light:SpillLights)
                {
                    auto* DoorMesh=NewObject<UProceduralMeshComponent>(DoorFixture);
                    DoorFixture->AddInstanceComponent(DoorMesh); DoorMesh->SetupAttachment(DoorRoot);
                    DoorMesh->CreateMeshSection_LinearColor(0,{FVector(0,-150,-200),FVector(0,150,-200),FVector(0,150,200),FVector(0,-150,200)},
                        {0,1,2,0,2,3,2,1,0,3,2,0},{},{},{},{},false);
                    DoorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                    DoorMesh->SetCastShadow(true); DoorMesh->bCastHiddenShadow=true;
                    DoorMesh->RegisterComponent();
                    DoorMesh->SetWorldLocationAndRotation(Light->GetComponentLocation()+Light->GetForwardVector()*100.f,Light->GetComponentQuat());
                    AddInfo(FString::Printf(TEXT("Spill location %s forward %s door %s capture %s cast %d dynamic %d"),
                        *Light->GetComponentLocation().ToString(),*Light->GetForwardVector().ToString(),*DoorMesh->GetComponentLocation().ToString(),*Capture->GetComponentLocation().ToString(),Light->CastShadows,Light->CastDynamicShadows));
                    DoorMesh->bCastShadowAsTwoSided=true;
                    DoorMesh->SetHiddenInGame(true); Capture->ShowOnlyComponents.Add(DoorMesh);
                    DoorOccluders.Add(DoorMesh);
                    TestTrue(TEXT("Spill fades across a broad range before distance culling"),Light->MaxDistanceFadeRange>=2000.f);
                }
                World->Tick(LEVELTICK_All,.016f);
                const auto ClosedSpill=Render(TEXT("YaraqDoorSpill_Closed"));
                int32 BlockedPixels=0;
                for(int32 I=0;I<Spill.Num();++I)
                    if(Spill[I].R>NoSpill[I].R+20 && Spill[I].R>ClosedSpill[I].R+15) ++BlockedPixels;
                AddInfo(FString::Printf(TEXT("Closed doors block spill on %d terrain pixels"),BlockedPixels));
                TestTrue(TEXT("Closed doors occlude interior light from the street"),BlockedPixels>500);
                for(auto* DoorMesh:DoorOccluders) { Capture->ShowOnlyComponents.Remove(DoorMesh); DoorMesh->DestroyComponent(); }
                DoorFixture->Destroy();
                Indoor->SetEnvCellHiddenInGame(true);
                const auto HiddenRoomSpill=Render(TEXT("YaraqDoorSpill_RoomCulled"));
                int64 HiddenRoomDifference=0;
                for (int32 I=0;I<Spill.Num();++I) HiddenRoomDifference+=FMath::Abs(int32(Spill[I].R)-int32(HiddenRoomSpill[I].R));
                TestTrue(TEXT("Culling the room cannot extinguish its visible exterior glow"),double(HiddenRoomDifference)/Spill.Num()<1.0);
                Indoor->SetEnvCellHiddenInGame(false);
                for (const auto& Pair:Rooms)
                {
                    for (auto Light:Pair.Value->DoorwayLights) Light->SetVisibility(true);
                }
                Capture->ClearShowOnlyComponents();
                Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
                Capture->ShowFlags.SetFog(true);
                Dat->SetInteriorAmbient(FLinearColor::White);
                // Also save the complete doorway at the darkest authored lighting
                // key so the warm spill can be judged against walls and the threshold.
                const FACEDatSkyTimeOfDay* NightKey=&InitialLight;
                for (const auto& Key:Region->DayGroups[0].TimesOfDay)
                    if (Key.DirBright<NightKey->DirBright) NightKey=&Key;
                Sky->UpdateWorldLighting(*NightKey,*NightKey,0,NightKey->Begin);
                for (const auto& Pair:Rooms) Pair.Value->SetEnvCellHiddenInGame(false);
                const FVector DoorEye=Door+Exit.WorldNormal*600+FVector(0,0,100);
                Capture->SetWorldLocationAndRotation(DoorEye,(Door-FVector(0,0,65)-DoorEye).Rotation());
                TArray<ACEOutdoorPortalPlan::FAdmittedAperture> LookIn={Exit};
                LookIn[0].WorldNormal=-LookIn[0].WorldNormal;
                TArray<ACEOutdoorPortalPlan::FAdmittedAperture> ReturnViews;
                Dat->SetLandLookOutClip(true,DoorEye,LookIn,&ReturnViews);
                Render(TEXT("YaraqDoorSpill_NightScene"));
                Dat->SetLandLookOutClip(false,FVector::ZeroVector,{});
            }

            if (!Exits.IsEmpty())
            {
                const auto Exit=Exits[0]; FVector Door=FVector::ZeroVector;
                for (const auto& V:Exit.WorldVerts) Door+=V;
                Door/=Exit.WorldVerts.Num();
                for (int32 Side : {-1,1})
                {
                    const FVector Eye=Door+Exit.WorldNormal*(Side*180.f);
                    const FRotator Rotation=(-Exit.WorldNormal*Side).Rotation();
                    TSet<int32> Draw; bool Outside=false; TArray<ACEOutdoorPortalPlan::FAdmittedAperture> Views;
                    if (Side<0)
                    {
                        ACEOutdoorPortalPlan::CollectIndoorPViewCells(*Dat,0x7D64010C,Eye,{},100,Draw,Outside,128,&Views);
                        Dat->SetLandLookOutClip(true,Eye,Views);
                    }
                    else
                    {
                        // Outdoor look-in mask from the exact authored entrance.
                        Views.Add(Exit); for (auto& V:Views) V.WorldNormal=-V.WorldNormal;
                        TArray<ACEOutdoorPortalPlan::FAdmittedAperture> ReturnViews;
                        Dat->SetLandLookOutClip(true,Eye,Views,&ReturnViews);
                    }
                    for (const auto& Pair:Rooms) Pair.Value->SetActorHiddenInGame(Side<0 && !Draw.Contains(Pair.Key));
                    Capture->SetWorldLocationAndRotation(Eye,Rotation);
                    Render(Side<0 ? TEXT("YaraqRuntime_InsideLookOut") : TEXT("YaraqRuntime_OutsideLookIn"));
                }
                for (const auto& Pair:Rooms) Pair.Value->SetActorHiddenInGame(false);
                Dat->SetLandLookOutClip(false,FVector::ZeroVector,{});
                if (FParse::Param(FCommandLine::Get(), TEXT("RetailShadowAtOrigin")))
                {
                    Land->SetActorLocation(Land->GetActorLocation()-Origin);
                    for (auto& Pair:Rooms) Pair.Value->SetActorLocation(Pair.Value->GetActorLocation()-Origin);
                    Door-=Origin;
                }
                const FVector Eye=Door+Exit.WorldNormal*2400+FVector(0,0,1600);
                Capture->SetWorldLocationAndRotation(Eye,(Door-Eye).Rotation());
                Land->ApplySceneryShadowWinners({}, {}, Eye,120000,8000);
                // Keep identical main/depth geometry in both captures. Signed total
                // darkening rejects dither/quantization noise that changes both ways.
                TestTrue(TEXT("Actual DAT lighting keys available"), Region && Region->DayGroups.Num() > 0);
                if (Region && Region->DayGroups.Num() > 0)
                for (const auto& Group : Region->DayGroups)
                for (const auto& Key : Group.TimesOfDay)
                {
                    Sky->UpdateWorldLighting(Key, Key, 0.f, Key.Begin);
                    for (auto* Shell : Shells) Shell->SetCastShadow(true);
                    const FString KeyName = FString::Printf(TEXT("YaraqRuntime_%s_Time%03d"), *Group.DayName, FMath::RoundToInt(Key.Begin * 1000));
                    const auto Shadows = Render(KeyName + TEXT("_Shadows"));
                    for (auto* Shell : Shells) Shell->SetCastShadow(false);
                    const auto NoShadows = Render(KeyName + TEXT("_NoShadows"));
                    int32 Darkened = 0; int64 NetDarkening = 0;
                    for (int32 I = 0; I < Shadows.Num(); ++I)
                    {
                        NetDarkening += int32(NoShadows[I].R)+int32(NoShadows[I].G)+int32(NoShadows[I].B)
                            -int32(Shadows[I].R)-int32(Shadows[I].G)-int32(Shadows[I].B);
                        if (NoShadows[I].R > Shadows[I].R + 32 && NoShadows[I].G > Shadows[I].G + 32) ++Darkened;
                    }
                    AddInfo(FString::Printf(TEXT("DAT %s time %.3f building shadows darken %d pixels"), *Group.DayName, Key.Begin, Darkened));
                    AddInfo(FString::Printf(TEXT("Signed shadow brightness difference: %lld"),NetDarkening));
                    TestTrue(TEXT("Buildings cast real shadows beyond frame noise at every DAT lighting key"), Darkened > 200 && NetDarkening > 100000);
                }

                // Isolate building receivers: terrain pixels cannot make this pass.
                Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
                for (auto* Shell : Shells)
                {
                    Capture->ShowOnlyComponents.Add(Shell);
                    Shell->SetCastShadow(true);
                }
                const auto LitShells=Render(TEXT("YaraqBuildingReceivers_Shadows"));
                for (auto* Shell : Shells) Shell->SetCastShadow(false);
                const auto UnshadowedShells=Render(TEXT("YaraqBuildingReceivers_NoShadows"));
                int64 ShellDarkening=0;
                for (int32 I=0; I<LitShells.Num(); ++I)
                    ShellDarkening+=int32(UnshadowedShells[I].R)+int32(UnshadowedShells[I].G)+int32(UnshadowedShells[I].B)
                        -int32(LitShells[I].R)-int32(LitShells[I].G)-int32(LitShells[I].B);
                AddInfo(FString::Printf(TEXT("Building-only receivers signed shadow darkening %lld"),ShellDarkening));
                TestTrue(TEXT("Building surfaces receive shadows, independently of terrain"),ShellDarkening>30000);
                Capture->ShowOnlyComponents.Reset();
                Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;

                // An instanced DAT tree/shrub exercises the same path as live flora.
                uint32 FoliageId=0;
                for (const auto& Object:Info.Objects)
                {
                    const auto Built=Dat->GetOrBuildSetupMeshShared(Object.Id,100,UACEDatSubsystem::ACEPlacementResting);
                    if (!Built) continue;
                    bool Clip=false; FBox Bounds(ForceInit);
                    for (const auto& Part:Built->Parts) for (const auto& Section:Part.Sections)
                        if (!Section.bCollisionOnly)
                        {
                            Clip|=Section.bClipMap;
                            for (const FVector& V:Section.Vertices) Bounds+=Part.BindTransform.TransformPosition(V);
                        }
                    if (Clip && Bounds.IsValid && Bounds.GetSize().Z>350 && Bounds.GetSize().Z<2500)
                    { FoliageId=Object.Id; break; }
                }
                if (TestTrue(TEXT("Town DAT contains foliage for the shadow regression"),FoliageId!=0))
                {
                    auto* Foliage=Land->GetOrCreateSceneryHism(Dat,FoliageId,false,UACEDatSubsystem::ACEPlacementResting);
                    if (TestNotNull(TEXT("Runtime instanced foliage created"),Foliage))
                    {
                        // Beside the doorway, above the authored ground. Keep the visual
                        // identical when switching only shadow casting.
                        Foliage->AddInstance(FTransform(Door+Exit.WorldNormal*1200),true);
                        Land->ApplySceneryShadowWinners({}, {}, Eye,120000,8000);
                        // The first masked foliage material may be compiled only now.
                        // Measure its leaf cutouts, not Unreal's temporary fallback.
                        if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
                        Foliage->SetCastShadow(true);
                        const auto With=Render(TEXT("YaraqFoliage_Shadows"));
                        Foliage->SetCastShadow(false);
                        const auto Without=Render(TEXT("YaraqFoliage_NoShadows"));
                        int64 Net=0;
                        for (int32 I=0; I<With.Num(); ++I)
                            Net+=int32(Without[I].R)+int32(Without[I].G)+int32(Without[I].B)
                                -int32(With[I].R)-int32(With[I].G)-int32(With[I].B);
                        AddInfo(FString::Printf(TEXT("Instanced foliage %08X signed shadow darkening %lld"),FoliageId,Net));
                        TestTrue(TEXT("Instanced foliage casts a real shadow at Yaraq world coordinates"),Net>30000);
                    }
                }

                // A retail character on a neutral receiver makes loss of silhouette
                // detail measurable independently of terrain texture or shadow tint.
                const FVector ShadowCenter=Door+Exit.WorldNormal*1800+FVector(0,0,40);
                auto* ReceiverActor=World->SpawnActor<AActor>();
                auto* Receiver=NewObject<UProceduralMeshComponent>(ReceiverActor);
                ReceiverActor->SetRootComponent(Receiver);
                Receiver->CreateMeshSection_LinearColor(0,
                    {FVector(-600,-600,0),FVector(-600,600,0),FVector(600,600,0),FVector(600,-600,0)},
                    {0,1,2,0,2,3,0,2,1,0,3,2},
                    {FVector::UpVector,FVector::UpVector,FVector::UpVector,FVector::UpVector},
                    {FVector2D(0,0),FVector2D(0,1),FVector2D(1,1),FVector2D(1,0)},
                    {FLinearColor::White,FLinearColor::White,FLinearColor::White,FLinearColor::White},{},false);
                Receiver->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
                Receiver->SetMobility(EComponentMobility::Movable);
                Receiver->SetCastShadow(false); Receiver->RegisterComponent();
                ReceiverActor->SetActorLocation(ShadowCenter);
                auto* ShadowPlayer=World->SpawnActor<AACEWorldEntityActor>();
                ShadowPlayer->InitializeFromObject(RemotePlayer,100,true);
                ShadowPlayer->SetActorLocation(ShadowCenter);
                ShadowPlayer->Appearance->SetPartsCastShadow(true,false);
                if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
                UDirectionalLightComponent* Sun=nullptr;
                for (TActorIterator<ADirectionalLight> It(World);It;++It)
                    if (It->ActorHasTag(FName(TEXT("ACERuntimeLight")))) Sun=Cast<UDirectionalLightComponent>(It->GetLightComponent());
                if (Sun)
                {
                    Sun->SetWorldRotation(FRotator(-60,0,0)); Sun->SetIntensity(3);
                    Sun->SetLightColor(FLinearColor::White);
                    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
                    Capture->ShowOnlyComponents.Add(Receiver); Capture->ShowOnlyActorComponents(ShadowPlayer);
                    Capture->ShowFlags.SetFog(false);
                    const FVector ShadowEye=ShadowCenter+FVector(140,-400,260);
                    Capture->SetWorldLocationAndRotation(ShadowEye,(ShadowCenter+FVector(60,0,30)-ShadowEye).Rotation());
                    const auto Detailed=Render(TEXT("PlayerShadow_Detailed"));
                    Sun->DynamicShadowCascades=3; Sun->CascadeDistributionExponent=2;
                    Sun->MarkRenderStateDirty();
                    const auto Coarse=Render(TEXT("PlayerShadow_Previous"));
                    ShadowPlayer->Appearance->SetPartsCastShadow(false,false);
                    const auto Unshadowed=Render(TEXT("PlayerShadow_Off"));
                    int64 DetailEdges=0, CoarseEdges=0;
                    int32 DetailPixels=0,CoarsePixels=0;
                    for (int32 Y=1;Y<479;++Y) for (int32 X=1;X<639;++X)
                    {
                        const int32 I=Y*640+X;
                        auto Mask=[&](const TArray<FColor>& A,int32 P)
                        {
                            const FColor Ref=Unshadowed[P];
                            // Measure the neutral receiver, excluding the colored
                            // character and empty background. Sub-byte temporal
                            // dithering over the whole frame is not shadow detail.
                            if (Ref.R<40 || FMath::Max3(Ref.R,Ref.G,Ref.B)-FMath::Min3(Ref.R,Ref.G,Ref.B)>5) return 0;
                            const int32 Delta=int32(Ref.R)-int32(A[P].R);
                            return Delta>=10 ? Delta : 0;
                        };
                        const int32 D=Mask(Detailed,I), C=Mask(Coarse,I);
                        DetailPixels+=D>30; CoarsePixels+=C>30;
                        DetailEdges+=FMath::Square(D-Mask(Detailed,I+1))+FMath::Square(D-Mask(Detailed,I+640));
                        CoarseEdges+=FMath::Square(C-Mask(Coarse,I+1))+FMath::Square(C-Mask(Coarse,I+640));
                    }
                    AddInfo(FString::Printf(TEXT("Player silhouette pixels detailed=%d previous=%d edge energy=%lld/%lld"),DetailPixels,CoarsePixels,DetailEdges,CoarseEdges));
                    TestTrue(TEXT("Retail player casts a visible detailed silhouette"),DetailPixels>500);
                    TestTrue(TEXT("Near shadow detail exceeds the previous broad cascades"),DetailEdges>CoarseEdges*1.3);
                    Sun->DynamicShadowCascades=4; Sun->CascadeDistributionExponent=4; Sun->MarkRenderStateDirty();
                    Capture->ClearShowOnlyComponents();
                    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
                }
                ShadowPlayer->Destroy(); ReceiverActor->Destroy();

            }
        }
        // A separately visible light must still follow the streaming cell lifetime.
        TWeakObjectPtr<AActor> OldGlow=Indoor->DoorwayLightActor;
        Indoor->LoadEnvCell(0x7D64010C,Origin,100);
        TestTrue(TEXT("Reloading a cell releases its previous glow actor"),!OldGlow.IsValid() || OldGlow->IsActorBeingDestroyed());
        TestTrue(TEXT("Reloaded entrance owns at most two light sources"),Indoor->DoorwayLights.Num()>0 && Indoor->DoorwayLights.Num()<=2);
        TWeakObjectPtr<UPointLightComponent> Light=Indoor->DoorwayLights[0];
        Indoor->Destroy();
        TestTrue(TEXT("Unloading the cell unregisters its exterior lights"),!Light.IsValid() || !Light->IsRegistered());
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("ACEBenchTemporalProbe")))
    {
        UInstancedStaticMeshComponent* TemporalBench=nullptr;
        for (TActorIterator<AACEEnvCellActor> It(World);It;++It)
        {
            TArray<UInstancedStaticMeshComponent*> Furniture; It->GetComponents(Furniture);
            for (auto* Part:Furniture) if (Part->GetName().Contains(TEXT("02000368"))) TemporalBench=Part;
        }
        if (TemporalBench)
        {
            TemporalBench->GetOwner()->SetActorHiddenInGame(false); TemporalBench->SetVisibility(true);
            FTransform Placement; TemporalBench->GetInstanceTransform(0,Placement,true);
            auto MakeCapture=[&](bool bAA)
            {
                auto* RT=NewObject<UTextureRenderTarget2D>(TemporalBench); RT->ClearColor=FLinearColor::Black;
                RT->InitCustomFormat(640,480,PF_B8G8R8A8,false);
                auto* SC=NewObject<USceneCaptureComponent2D>(TemporalBench->GetOwner());
                SC->TextureTarget=RT; SC->FOVAngle=90; SC->CaptureSource=SCS_FinalColorLDR;
                SC->bCaptureEveryFrame=false; SC->bCaptureOnMovement=false; SC->bAlwaysPersistRenderingState=true;
                SC->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
                SC->ShowOnlyComponents.Add(TemporalBench);
                SC->ShowFlags.SetAntiAliasing(bAA); SC->ShowFlags.SetTemporalAA(bAA);
                SC->ShowFlags.SetEyeAdaptation(false); SC->ShowFlags.SetBloom(false); SC->ShowFlags.SetFog(false);
                SC->ShowFlags.SetAtmosphere(false); SC->RegisterComponent(); return SC;
            };
            auto* Temporal=MakeCapture(true); auto* Reference=MakeCapture(false);
            ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this,World,GI,Placement,Temporal,Reference,Frame=0,MaxMissing=0,PreviousFrame=uint64(-1)]() mutable
            {
                if (PreviousFrame==GFrameCounter) return false;
                PreviousFrame=GFrameCounter;
                const FVector EyeOffset=Placement.TransformVectorNoScale(FVector(-90+Frame*1.5,-220,-30));
                const FVector AimOffset(0,0,45);
                TArray<FColor> Images[2];
                int32 Pass=0;
                for (auto* SC:{Reference,Temporal})
                {
                    SC->SetWorldLocationAndRotation(Placement.GetLocation()+EyeOffset,(AimOffset-EyeOffset).Rotation());
                    World->SendAllEndOfFrameUpdates(); SC->CaptureScene(); FlushRenderingCommands();
                    SC->TextureTarget->GameThread_GetRenderTargetResource()->ReadPixels(Images[Pass++]);
                }
                int32 Missing=0;
                for (int32 Y=3;Y<477;++Y) for (int32 X=3;X<637;++X)
                {
                    const int32 I=Y*640+X;
                    if (Images[0][I].B<70 || Images[1][I].B>=20) continue;
                    bool bInterior=true;
                    for (int32 DY=-2;DY<=2;++DY) for (int32 DX=-2;DX<=2;++DX)
                        bInterior &= Images[0][I+DY*640+DX].B>=70;
                    if (bInterior) ++Missing;
                }
                MaxMissing=FMath::Max(MaxMissing,Missing);
                if (Frame%20==0 || Missing>25)
                {
                    for (int32 I=0;I<2;++I)
                    {
                        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(640,480,Images[I],PNG);
                        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Automation/RetailParity")/FString::Printf(TEXT("BenchTemporal_%d_%d.png"),I,Frame)));
                    }
                }
                if (++Frame<120) return false;
                AddInfo(FString::Printf(TEXT("Temporal bench sweep: 120 distinct engine frames, maximum missing interior pixels %d"),MaxMissing));
                GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
                return true;
            }));
            return true;
        }
    }
    GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}
#endif
