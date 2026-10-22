#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACECharacterCreation.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEWorldPresenterComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACEVisibleObjectPick.h"
#include "ACEScriptComponent.h"
#include "Dat/ACEOutdoorPortalPlan.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "ShaderCompiler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECustomWorldPresentationTest,"ACE.RetailParity.CustomWorldPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACECustomWorldPresentationTest::RunTest(const FString&)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* GI = NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); Context.OwningGameInstance=GI;
    GI->OnWorldChanged(nullptr,World); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    if (!TestTrue(TEXT("Retail DAT opens"),Dat && Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    for(uint32 Block:{0x01100000u,0x7F020000u})
        if(const auto Info=Dat->GetLandblockInfo(Block))
            for(uint32 I=0;I<Info->NumCells;++I)Dat->GetOrBuildEnvCellMesh(Block|(0x100+I),100);
    FACEWorldObject Object;
    Object.Guid=0x71001001; Object.WeenieClassId=900001; Object.ItemType=ACEItemType::Misc;
    Object.bHasPosition=true; Object.Position.CellId=0x016C0101; Object.Position.Location=FVector(50,50,1.5);
    const FVector Authored=Object.Position.ToUnrealLocation(100);
    auto* Floor=World->SpawnActor<AActor>();
    auto* Box=NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(1000,1000,20)); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
    Floor->SetActorLocation(Authored-FVector(0,0,170));
    auto* Entity=World->SpawnActor<AACEWorldEntityActor>();
    Entity->InitializeFromObject(Object,100,false);
    TestTrue(TEXT("Gravity-free custom object keeps its elevated server origin"),Entity->GetActorLocation().Equals(Authored,.01));
    Object.PhysicsState=ACEPhysicsState::Gravity;
    Entity->Destroy(); Entity=World->SpawnActor<AACEWorldEntityActor>(); Entity->InitializeFromObject(Object,100,false);
    TestTrue(TEXT("Gravity alone cannot lower a loose item's server placement"),Entity->GetActorLocation().Equals(Authored,.01));
    Entity->Destroy();Object.Position.Location.Z=.005;
    Entity=World->SpawnActor<AACEWorldEntityActor>();Entity->InitializeFromObject(Object,100,false);
    TestTrue(TEXT("Loot already touching its floor retains contact skin"),FMath::IsNearlyEqual(Entity->GetActorLocation().Z,Authored.Z-149,.01));
    Object.Position.Location.Z=1.5;
    Entity->Destroy();
    Object.PhysicsState=0;
    FTransform Actual;

    // ACE-World-16PY-Patches: the usable gateway and quest hand-in deposit
    // are both object-shaped creatures using the same retail motion table.
    for(bool Deposit:{false,true})
    {
        FACEWorldObject Gateway=Object;
        Gateway.WeenieClassId=Deposit?40809:40810; Gateway.Name=Deposit?TEXT("Apostate Aetherium Deposit"):TEXT("Apostate Nexus Gateway");
        Gateway.ItemType=ACEItemType::Creature; Gateway.ItemUseable=32;Gateway.UseRadius=3;
        Gateway.bHasServerPhysicsDescription=true;
        Gateway.SetupId=Deposit?0x02000B82:0x02000443; Gateway.MotionTableId=0x0900013D;
        Gateway.InitialMotionStyle=ACEMotion::StanceNonCombat;
        Gateway.InitialMotionCommand=ACEMotion::Ready;
        Gateway.PhysicsState=6292504; Gateway.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck;
        Gateway.Position.CellId=Deposit?0x7F020107:0x0110028A;
        Gateway.Position.Location=Deposit?FVector(40,-20,.055):FVector(40,-140,12.055);
        if(Deposit)
        {
            FACECharacterCreation Creation;FString Error;
            TestTrue(TEXT("Deposit clothing table loads"),Creation.Load(*Dat->GetPortalDat(),Error));
            TestTrue(TEXT("Deposit clothing resolves"),Creation.ApplyClothing(0x10000338,Gateway.SetupId,0,0,Gateway.Appearance));
        }
        auto* Owner=World->SpawnActor<AActor>();
        auto* Presenter=NewObject<UACEWorldPresenterComponent>(Owner);
        Owner->AddInstanceComponent(Presenter);Presenter->RegisterComponent();Owner->DispatchBeginPlay();
        auto Session=GI->GetSubsystem<UACEClientSubsystem>()->GetSession();
        Session->WorldObjects.Add(Gateway.Guid,Gateway);
        Presenter->SpawnOrUpdateEntity(Gateway,false);
        Presenter->DrainPendingDatAppearance();
        auto* Actor=Presenter->FindEntityActor(Gateway.Guid);
        if(!TestNotNull(TEXT("Deferred Apostate object spawns"),Actor))return false;
        TestTrue(TEXT("Apostate gateway has DAT geometry"),Actor->Appearance->HasAppearance());
        for(int32 Frame=0;Frame<120;++Frame)
        {
            Actor->Tick(1.f/60);
            Actor->Appearance->TickComponent(1.f/60,LEVELTICK_All,nullptr);
            if(Actor->ScriptComponent->IsComponentTickEnabled())Actor->ScriptComponent->TickComponent(1.f/60,LEVELTICK_All,nullptr);
        }
        for(int32 Part=0;auto* Mesh=Actor->Appearance->GetPartMesh(Part);++Part)
        {
            TestTrue(TEXT("Apostate part stays visible"),Mesh->IsVisible()&&!Mesh->bHiddenInGame);
            if(auto* Primitive=Cast<UPrimitiveComponent>(Mesh))
                if(auto* Material=Cast<UMaterialInstanceDynamic>(Primitive->GetMaterial(0)))
                    TestTrue(TEXT("Apostate material is not transparent"),Material->K2_GetScalarParameterValue(TEXT("OpacityMul"))>.01f);
        }
        FBox Bounds;
        TestTrue(TEXT("Apostate gateway has visible geometry after idle animation"),Actor->Appearance->GetVisualWorldBounds(Bounds));
        TestFalse(TEXT("Apostate gateway actor is not hidden"),Actor->IsHidden());
        if(Bounds.IsValid)
        {
            const FVector Center=Bounds.GetCenter();
            TArray<AACEEnvCellActor*> Rooms;
            const uint32 Block=uint32(Gateway.Position.CellId)&0xFFFF0000u;
            if(const auto Info=Dat->GetLandblockInfo(Block))
                for(uint32 I=0;I<Info->NumCells;++I)
                {
                    const uint32 Id=Block|(0x100+I);const auto* Cell=Dat->FindEnvCellMesh(Id,100);
                    if(!Cell || FVector::DistSquared(FVector(Cell->Origin),Gateway.Position.Location)>FMath::Square(35.))continue;
                    auto* Room=World->SpawnActor<AACEEnvCellActor>();
                    Room->LoadEnvCell(Id,FACEPosition::AceVectorToUnreal(FVector((Block>>24)*192,((Block>>16)&255)*192,0),100),100);
                    Room->SetEnvCellCollisionActive(true);Room->SetEnvCellHiddenInGame(false);Rooms.Add(Room);
                }
            auto* CaptureOwner=World->SpawnActor<AActor>();
            auto* Capture=NewObject<USceneCaptureComponent2D>(CaptureOwner);
            auto* RenderTarget=NewObject<UTextureRenderTarget2D>(Capture);RenderTarget->InitAutoFormat(512,512);
            Capture->TextureTarget=RenderTarget;Capture->CaptureSource=SCS_FinalColorLDR;
            Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
            Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
            Capture->ShowFlags.SetEyeAdaptation(false);Capture->ShowFlags.SetAtmosphere(false);
            Capture->ShowFlags.SetFog(false);Capture->ShowFlags.SetAntiAliasing(false);Capture->ShowFlags.SetLighting(false);
            Capture->RegisterComponent();Capture->ShowOnlyActorComponents(Actor);
            for(auto* Room:Rooms)Capture->ShowOnlyComponent(Room->CellMesh);
            auto Snap=[&](const FVector& Eye,const TCHAR* Label)
            {
                if(!FApp::CanEverRender())return;
                Capture->SetWorldLocationAndRotation(Eye,(Center-Eye).Rotation());
                if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
                World->SendAllEndOfFrameUpdates();Capture->CaptureScene();FlushRenderingCommands();
                TArray<FColor> Pixels;RenderTarget->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
                Actor->SetActorHiddenInGame(true);World->SendAllEndOfFrameUpdates();Capture->CaptureScene();FlushRenderingCommands();
                TArray<FColor> Without;RenderTarget->GameThread_GetRenderTargetResource()->ReadPixels(Without);
                Actor->SetActorHiddenInGame(false);
                int32 VisiblePixels=0;for(int32 I=0;I<Pixels.Num() && I<Without.Num();++I)
                    VisiblePixels+=FMath::Abs(int32(Pixels[I].R)-Without[I].R)>3 || FMath::Abs(int32(Pixels[I].G)-Without[I].G)>3 || FMath::Abs(int32(Pixels[I].B)-Without[I].B)>3;
                TestTrue(*FString::Printf(TEXT("%s has rendered pixels in %s view"),*Gateway.Name,Label),VisiblePixels>20);
                if(FParse::Param(FCommandLine::Get(),TEXT("ACESaveApostateCaptures")))
                {
                    TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(512,512,Pixels,Png);
                    FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()+FString::Printf(TEXT("Apostate-%s-%s.png"),Deposit?TEXT("deposit"):TEXT("gateway"),Label)));
                }
            };
            const FVector CloseEye=Center+FVector(200,0,150);
            Snap(CloseEye,TEXT("close"));
            TestEqual(TEXT("Apostate gateway can be picked for interaction and item drops"),ACEVisibleObjectPick::Trace(*World,
                CloseEye,Center+(Center-CloseEye).GetSafeNormal()*100,nullptr),static_cast<AActor*>(Actor));
            // Standard landblock placements: acid-pit gateway and final deposit.
            // Exercise PView from nearby valid cells, looking toward the model.
            FACEPosition At;At.CellId=Deposit?0x7F020107:0x0110028A;
            At.Location=Deposit?FVector(40,-20,.055):FVector(40,-140,12.055);
            TestNotNull(TEXT("Apostate object's authored dungeon cell loads"),Dat->GetOrBuildEnvCellMesh(At.CellId,100));
            const FVector Origin=At.ToUnrealLocation();
            const FVector Focus=Origin+Center-Actor->GetActorLocation();
            int32 Views=0;
            for(int32 Yaw=0;Yaw<360;Yaw+=45)for(double Radius:{100.,250.})
            {
                const FVector Eye=Origin+FRotator(0,Yaw,0).RotateVector(FVector(Radius,0,150));
                uint32 ViewCell=0;if(!Dat->ResolveContainingEnvCell(At.CellId,Eye,100,ViewCell))continue;
                const FQuat Rotation=(Focus-Eye).Rotation().Quaternion();FConvexVolume Frustum;
                for(const FVector& N:{FVector(-1,1,0),FVector(-1,-1,0),FVector(-1,0,1),FVector(-1,0,-1),FVector(-1,0,0)})
                    Frustum.Planes.Add(FPlane(Eye,Rotation.RotateVector(N.GetSafeNormal())));
                Frustum.Init();TSet<int32> Visible;bool Outside=false;
                ACEOutdoorPortalPlan::CollectIndoorPViewCells(*Dat,ViewCell,Eye,Frustum,100,Visible,Outside,128);
                TestTrue(TEXT("Nearby dungeon views admit the Apostate object's cell"),Visible.Contains(At.CellId));++Views;
            }
            TestTrue(TEXT("Apostate visibility checked from multiple valid dungeon viewpoints"),Views>=4);
            if(!Deposit)
            {
                FACEPosition Reported;Reported.CellId=0x011002E7;
                Reported.Location=FVector(45.227190,-136.856896,24.006001);
                TestNotNull(TEXT("Reported gateway ledge cell loads"),Dat->GetOrBuildEnvCellMesh(Reported.CellId,100));
                for(double Height:{150.,300.})
                {
                    const FVector Eye=Reported.ToUnrealLocation()+FVector(0,0,Height);
                    uint32 ViewCell=Reported.CellId;Dat->ResolveContainingEnvCell(Reported.CellId,Eye,100,ViewCell);
                    const FQuat Rotation=(Focus-Eye).Rotation().Quaternion();FConvexVolume Frustum;
                    for(const FVector& N:{FVector(-1,1,0),FVector(-1,-1,0),FVector(-1,0,1),FVector(-1,0,-1),FVector(-1,0,0)})
                        Frustum.Planes.Add(FPlane(Eye,Rotation.RotateVector(N.GetSafeNormal())));
                    Frustum.Init();TSet<int32> Visible;bool Outside=false;
                    ACEOutdoorPortalPlan::CollectIndoorPViewCells(*Dat,ViewCell,Eye,Frustum,100,Visible,Outside,128);
                    TestTrue(*FString::Printf(TEXT("Reported ledge view %.0f cm from cell %08X admits gateway cell"),Height,ViewCell),Visible.Contains(At.CellId));
                    TestEqual(TEXT("Gateway can be picked from reported ledge height"),ACEVisibleObjectPick::Trace(*World,
                        Eye,Center+(Center-Eye).GetSafeNormal()*100,nullptr),static_cast<AActor*>(Actor));
                }
                Snap(Reported.ToUnrealLocation()+FVector(0,0,150),TEXT("ledge"));
            }
            for(int32 Part=0;auto* Mesh=Cast<UProceduralMeshComponent>(Actor->Appearance->GetPartMesh(Part));++Part)
            {
                for(float Distance:{300.f,1500.f,3000.f,6000.f})
                {
                    const int32 Level=Mesh->GetDistanceLevel(Center+FVector(Distance,0,0));
                    TestTrue(TEXT("Quest object has drawable geometry at interaction distances"),
                        Level==INDEX_NONE?Mesh->GetNumSections()>0:Mesh->GetDistanceLevels()[Level].SectionCount>0);
                }
            }
            Actor->SetCellVisible(false);
            Actor->InitializeFromObject(Gateway,100,true);
            Actor->SetCellVisible(true);
            TestEqual(TEXT("Quest object remains pickable after reentering its room"),ACEVisibleObjectPick::Trace(*World,
                CloseEye,Center+(Center-CloseEye).GetSafeNormal()*100,nullptr),static_cast<AActor*>(Actor));
            CaptureOwner->Destroy();
            for(auto* Room:Rooms)Room->Destroy();
        }
        Presenter->ClearSpawned();Owner->Destroy();Session->WorldObjects.Remove(Gateway.Guid);
    }

    // Daralet's supplied Tou-Tou weenie (1050067): an object-looking Creature,
    // not a static prop. Its setup, clothing and motion all come from retail DAT.
    {
        FACEWorldObject Crystal=Object;
        Crystal.WeenieClassId=1050067; Crystal.Name=TEXT("Tou-Tou");
        Crystal.ItemType=ACEItemType::Creature; Crystal.SetupId=0x02001AC5;
        Crystal.MotionTableId=0x090001FC; Crystal.Scale=.75f;
        Crystal.UseRadius=2;
        Crystal.InitialMotionStyle=ACEMotion::StanceNonCombat;
        Crystal.InitialMotionCommand=ACEMotion::Ready;
        Crystal.PhysicsState=ACEPhysicsState::Gravity | ACEPhysicsState::Ethereal | ACEPhysicsState::ReportCollisions;
        Crystal.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck;
        Crystal.Appearance.PaletteBaseId=0x04000BEF;
        FACECharacterCreation Creation; FString Error;
        TestTrue(TEXT("Tou-Tou clothing decoder loads"),Creation.Load(*Dat->GetPortalDat(),Error));
        TestTrue(TEXT("Tou-Tou clothing resolves"),Creation.ApplyClothing(0x100007D8,Crystal.SetupId,2,0,Crystal.Appearance));
        auto* CrystalActor=World->SpawnActor<AACEWorldEntityActor>();
        CrystalActor->InitializeFromObject(Crystal,100,true);
        TestTrue(TEXT("Tou-Tou creation preserves the elevated server origin above nearby terrain"),CrystalActor->GetActorLocation().Equals(Authored,.01));
        CrystalActor->Appearance->GetPartCurrentTransform(0,Actual);
        TestEqual(TEXT("Tou-Tou has its authored idle lift before the first draw"),Actual.GetLocation().Z,50.);
        TArray<FTransform> Pose; int32 Count=0;
        for (int32 Frame=0;Frame<3;++Frame)
        {
            if(Frame) CrystalActor->Appearance->TickComponent(.5f,LEVELTICK_All,nullptr);
            CrystalActor->Appearance->GetPartCurrentTransform(0,Actual);
            TestTrue(TEXT("Retail DAT supplies Tou-Tou's rotating Ready pose"),Dat->EvaluateMotionCommand(
                Crystal.MotionTableId,ACEMotion::Ready,Frame*.5f,1,Pose,100,Count,nullptr,nullptr,ACEMotion::StanceNonCombat));
            TestTrue(TEXT("Object-looking NPC retains the complete DAT pose"),Count==1 && Actual.Equals(Pose[0],.001));
            TestTrue(TEXT("Crystal animation offset receives server scale exactly once"),
                FMath::IsNearlyEqual(CrystalActor->Appearance->GetPartMesh(0)->GetComponentLocation().Z-Authored.Z,37.5,.01));
            FBox Bounds; CrystalActor->Appearance->GetVisualWorldBounds(Bounds);
            const FVector Center=Bounds.GetCenter();
            TestEqual(TEXT("Visible Tou-Tou crystal can be clicked at its animated height"),ACEVisibleObjectPick::Trace(*World,
                Center-FVector(200,0,0),Center+FVector(200,0,0),nullptr),static_cast<AActor*>(CrystalActor));
        }
        CrystalActor->InitializeFromObject(Crystal,100,true);
        TestTrue(TEXT("Repeated object descriptions do not lower the crystal"),CrystalActor->GetActorLocation().Equals(Authored,.01));
        for(float Scale : {.25f,.75f,2.f})
        {
            Crystal.Scale=Scale; CrystalActor->InitializeFromObject(Crystal,100,true);
            FACEPosition Contact=Crystal.Position; Contact.bHasContactState=true; Contact.bIsGrounded=true;
            for(int32 Packet=0;Packet<3;++Packet)
            {
                CrystalActor->ApplyACEPosition(Contact);
                for(int32 Frame=0;Frame<60;++Frame) CrystalActor->Tick(1.f/60);
                TestTrue(TEXT("Scaled crystal position updates do not project its elevated origin onto distant terrain"),CrystalActor->GetActorLocation().Equals(Authored,.01));
            }
            FBox Bounds; CrystalActor->Appearance->GetVisualWorldBounds(Bounds);
            const FVector Center=Bounds.GetCenter();
            TestEqual(TEXT("Scaled crystal remains clickable after contact updates"),ACEVisibleObjectPick::Trace(*World,
                Center-FVector(300,0,0),Center+FVector(300,0,0),nullptr),static_cast<AActor*>(CrystalActor));
        }
        CrystalActor->Destroy();
        CrystalActor=World->SpawnActor<AACEWorldEntityActor>(); CrystalActor->InitializeFromObject(Crystal,100,true);
        TestTrue(TEXT("Recreating after relog preserves the same origin"),CrystalActor->GetActorLocation().Equals(Authored,.01));
        CrystalActor->Destroy();

        // Exercise the login presenter, not only synchronous actor construction:
        // Ready can arrive before the queued mesh, and a relog keeps DAT caches.
        auto* Owner=World->SpawnActor<AActor>();
        auto* Presenter=NewObject<UACEWorldPresenterComponent>(Owner);
        Owner->AddInstanceComponent(Presenter); Presenter->RegisterComponent(); Owner->DispatchBeginPlay();
        auto Session=GI->GetSubsystem<UACEClientSubsystem>()->GetSession();
        for(int32 Login=0;Login<3;++Login)
        {
            Presenter->ClearSpawned();
            Crystal.Scale=.75f;
            Session->WorldObjects.Add(Crystal.Guid,Crystal);
            FACEObjectMotionState Ready; Ready.CurrentStyle=ACEMotion::StanceNonCombat;
            Ready.ForwardCommand=ACEMotion::Ready;
            Presenter->HandleMotionUpdate(Crystal.Guid,Ready);
            Presenter->SpawnOrUpdateEntity(Crystal,false);
            auto* Deferred=Presenter->FindEntityActor(Crystal.Guid);
            if(!TestNotNull(TEXT("Relog creates the deferred crystal"),Deferred)) return false;
            // Positions can be received while the model is still in the queue.
            FACEPosition Contact=Crystal.Position; Contact.bHasContactState=true;Contact.bIsGrounded=true;
            Presenter->HandlePositionUpdate(Crystal.Guid,Contact);
            Presenter->DrainPendingDatAppearance();
            for(int32 Frame=0;Frame<120;++Frame)
            {
                Deferred->Tick(1.f/60);
                Deferred->Appearance->TickComponent(1.f/60,LEVELTICK_All,nullptr);
                // Other creatures continually change the shared DAT motion reader.
                TArray<FTransform> Other; int32 OtherCount=0;
                Dat->EvaluateMotionCommand(0x09000001,ACEMotion::Ready,Frame/60.f,1,Other,100,OtherCount);
            }
            TestTrue(TEXT("Deferred relog keeps the server origin"),Deferred->GetActorLocation().Equals(Authored,.01));
            TestTrue(TEXT("Deferred relog keeps the animated crystal elevation"),FMath::IsNearlyEqual(
                Deferred->Appearance->GetPartMesh(0)->GetComponentLocation().Z-Authored.Z,37.5,.01));
        }
        Presenter->ClearSpawned(); Owner->Destroy(); Session->WorldObjects.Remove(Crystal.Guid);

        // Updated Tou-Tou sets GravityStatus=false. ACE's CalculatedPhysicsState
        // clears Gravity even though the stored integer still contains 1032.
        // A floating creature close to a surface must not acquire walking
        // support merely because a position correction wakes its actor tick.
        for(bool Gravity : {false,true})
        {
            Crystal.PhysicsState=ACEPhysicsState::Ethereal | ACEPhysicsState::ReportCollisions
                | (Gravity ? ACEPhysicsState::Gravity : 0);
            // Clear the capsule's rounded bottom, but stay within the setup's
            // 8.67 cm scaled step-down range so this exercises ground seating.
            Crystal.Position.Location=FVector(50,50,.08);
            auto* Floating=World->SpawnActor<AACEWorldEntityActor>();
            Floating->InitializeFromObject(Crystal,100,true);
            FACEPosition Correction=Crystal.Position; Correction.Location.X+=.25;
            Floating->ApplyACEPosition(Correction);
            TestTrue(TEXT("Position correction wakes floating prop prediction"),Floating->IsActorTickEnabled());
            for(int32 Frame=0;Frame<120;++Frame) Floating->Tick(1.f/60);
            const double ExpectedZ=Gravity ? 1. : Correction.ToUnrealLocation(100).Z;
            AddInfo(FString::Printf(TEXT("Floating fixture gravity=%d received Z=%.4f actual Z=%.4f"),
                Gravity,Correction.ToUnrealLocation(100).Z,Floating->GetActorLocation().Z));
            TestTrue(Gravity ? TEXT("Gravity-enabled grounded prediction still seats within its step range")
                : TEXT("Gravity-disabled creature retains server height while smoothing near a floor"),
                FMath::IsNearlyEqual(Floating->GetActorLocation().Z,ExpectedZ,.02));
            // A server's contact hint is not permission to enable gravity on a
            // non-gravitating display object during reconciliation either.
            Correction.bHasContactState=true; Correction.bIsGrounded=true;
            Floating->ApplyACEPosition(Correction);
            for(int32 Frame=0;Frame<120;++Frame) Floating->Tick(1.f/60);
            TestTrue(Gravity ? TEXT("Gravity-enabled contact retains floor support")
                : TEXT("Gravity-disabled contact does not lower a floating creature"),
                FMath::IsNearlyEqual(Floating->GetActorLocation().Z,ExpectedZ,.02));
            Floating->Destroy();
        }
    }

    // ACE-World-16PY/Database/3-Core/9 WeenieDefaults/SQL/Creature:
    // Human/22257 Fishing Hole, Crystal/06788 Nexus Crystal, Statue/24161 Fir Tree.
    // Creature is a network category, not permission to replace these origins
    // with a distant floor. Exercise real stock setups alongside the custom one.
    struct FProp { uint32 Wcid, Setup, Motion, Physics; float Scale; };
    for(const FProp Prop : {FProp{22257,0x02000F0D,0x09000122,2098204,1},
        FProp{6788,0x02000700,0x09000097,3080,1.5f},FProp{24161,0x02000F06,0x090000CB,6292504,1.2f}})
    {
        FACEWorldObject Stock=Object; Stock.ItemType=ACEItemType::Creature;
        Stock.WeenieClassId=Prop.Wcid; Stock.SetupId=Prop.Setup; Stock.MotionTableId=Prop.Motion;
        Stock.PhysicsState=Prop.Physics; Stock.Scale=Prop.Scale; Stock.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck;
        float Up=0,Height=0,Radius=0,Down=0;uint32 Anim=0;
        TestTrue(TEXT("Stock creature prop has readable DAT physics"),Dat->TryGetSetupPhysics(Prop.Setup,Up,Height,Radius,Anim,&Down));
        Stock.Position.Location.Z=FMath::Max(1.5f,Down*Prop.Scale+1.f);
        const FVector Origin=Stock.Position.ToUnrealLocation(100);
        auto* PropActor=World->SpawnActor<AACEWorldEntityActor>(); PropActor->InitializeFromObject(Stock,100,true);
        TestTrue(TEXT("Stock creature prop has a DAT appearance"),PropActor->Appearance->HasAppearance());
        FACEPosition Contact=Stock.Position; Contact.bHasContactState=true;Contact.bIsGrounded=true;
        PropActor->ApplyACEPosition(Contact);
        for(int32 Frame=0;Frame<120;++Frame)PropActor->Tick(1.f/60);
        TestTrue(*FString::Printf(TEXT("Stock prop %u preserves server elevation beyond its step range"),Prop.Wcid),PropActor->GetActorLocation().Equals(Origin,.01));
        PropActor->Destroy();
    }
    Floor->Destroy();

    // Named decorative bodies use the same held pose as lootable corpses,
    // without requiring the Corpse flag or making UIHidden scenery selectable.
    FACECharacterCreation Humans; FString HumanError;
    if (!TestTrue(TEXT("Retail character definitions load"),Humans.Load(*Dat->GetPortalDat(),HumanError))) return false;
    Humans.SelectHeritage(1);
    for (uint32 Sex : {1u,2u})
    {
        if (!TestTrue(TEXT("Retail human body is available"),Humans.SelectSex(Sex))) continue;
        FACEWorldObject Body=Object; Body.Name=TEXT("Decorative remains");
        Body.SetupId=Humans.Sex()->Setup; Body.MotionTableId=Humans.Sex()->Motion;
        Body.ItemType=ACEItemType::Container; Body.ItemUseable=0x20;
        Body.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck|ACEObjectDescFlag::Openable|ACEObjectDescFlag::UiHidden;
        Body.InitialMotionCommand=ACEMotion::Dead; Body.InitialMotionStyle=ACEMotion::StanceNonCombat;
        Body.bHasServerPhysicsDescription=true;
        auto* BodyActor=World->SpawnActor<AACEWorldEntityActor>(); BodyActor->InitializeFromObject(Body,100,true);
        TArray<FTransform> DeadPose; int32 Count=0;
        TestTrue(TEXT("Retail DAT supplies held dead pose"),Dat->EvaluateMotionCommand(Body.MotionTableId,
            ACEMotion::Dead,1000,BodyActor->Appearance->GetPartCount(),DeadPose,100,Count,nullptr,nullptr,ACEMotion::StanceNonCombat,false));
        for (int32 Pass=0;Pass<2;++Pass)
        {
            for(int32 Frame=0;Frame<60;++Frame) BodyActor->Appearance->TickComponent(1.f/60,LEVELTICK_All,nullptr);
            for(int32 Part=0;Part<Count;++Part)
                TestTrue(TEXT("Decorative body retains retail dead part pose"),BodyActor->Appearance->GetPartCurrentTransform(Part,Actual)
                    && Actual.GetLocation().Equals(DeadPose[Part].GetLocation(),.01)
                    && Actual.GetRotation().Equals(DeadPose[Part].GetRotation(),.001));
            BodyActor->InitializeFromObject(Body,100,true);
        }
        TestFalse(TEXT("Decorative body remains visible"),BodyActor->IsHidden());
        TestFalse(TEXT("Named UIHidden body stays out of target cycling"),Body.IsSelectableWorldObject());
        BodyActor->Destroy();
    }

    // User-provided WCIDs 13370453 and 41803 share 090001A0: its Ready
    // cycle is frame zero of 03000397 at zero FPS (already lying down).
    // The Chest receives Off, which that table does not implement; the quest
    // creature is remotely usable and must not be mistaken for slain loot.
    for(bool Chest:{true,false})
    {
        FACEWorldObject Body=Object; Body.bHasServerPhysicsDescription=true;
        Body.WeenieClassId=Chest?13370453:41803; Body.Name=Chest?TEXT("Dead Adventurer"):TEXT("Corpse of Celestial Hand Agent");
        Body.SetupId=Chest?0x0200004E:0x02000001; Body.MotionTableId=0x090001A0;
        Body.ItemType=Chest?ACEItemType::Container:ACEItemType::Creature;
        Body.ItemUseable=Chest?48:32; Body.PhysicsState=Chest?1052:6292492;
        Body.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck | (Chest?ACEObjectDescFlag::Openable:0);
        Body.InitialMotionCommand=Chest?ACEMotion::OffCommandU16:0;
        Body.InitialMotionStyle=ACEMotion::StanceNonCombat;
        auto* Actor=World->SpawnActor<AACEWorldEntityActor>(); Actor->InitializeFromObject(Body,100,true);
        TArray<FTransform> Pose;int32 Count=0;
        TestTrue(TEXT("Supplied corpse table default loads"),Dat->EvaluateIdleMotion(Body.MotionTableId,0,Actor->Appearance->GetPartCount(),Pose,100,Count));
        TestTrue(TEXT("Retail corpse idle has head near the floor"),Pose.IsValidIndex(16) && Pose[16].GetLocation().Z<20);
        for(int32 Pass=0;Pass<3;++Pass)
        {
            for(int32 Frame=0;Frame<60;++Frame) Actor->Appearance->TickComponent(1.f/60,LEVELTICK_All,nullptr);
            for(int32 Part=0;Part<Count;++Part)
                TestTrue(TEXT("Supplied weenie retains authored corpse pose"),Actor->Appearance->GetPartCurrentTransform(Part,Actual)
                    && Actual.GetLocation().Equals(Pose[Part].GetLocation(),.01) && Actual.GetRotation().Equals(Pose[Part].GetRotation(),.001));
            Actor->InitializeFromObject(Body,100,true);
            if(Chest) Actor->Appearance->PlayDoorMotion(ACEMotion::OnCommandU16);
        }
        if(!Chest) TestTrue(TEXT("Quest reward corpse remains interactable"),Body.IsSelectableWorldObject() && Body.AllowsDeadPoseInteraction());
        Actor->Destroy();
    }

    // Real setup 020014CA has a zero-height placement but a 2 m idle part
    // offset in 09000198/03000B5E. The weenie need not be a creature/lifestone.
    Object.SetupId=0x020014CA; Object.MotionTableId=0x09000198;
    Object.Scale=1; Object.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck;
    Entity=World->SpawnActor<AACEWorldEntityActor>(); Entity->InitializeFromObject(Object,100,true);
    TArray<FTransform> Idle; int32 IdleCount=0;
    TestTrue(TEXT("DAT supplies the custom prop's elevated idle"),Dat->EvaluateIdleMotion(Object.MotionTableId,0,1,Idle,100,IdleCount));
    TestTrue(TEXT("Custom prop is posed before first draw, above its origin"),IdleCount==1
        && Entity->Appearance->GetPartCurrentTransform(0,Actual) && Actual.Equals(Idle[0],.001)
        && Actual.GetLocation().Z>199);
    TestTrue(TEXT("Elevated custom prop retains the authoritative object origin"),Entity->GetActorLocation().Equals(Authored,.01));
    Entity->Destroy();

    // A Switch sends Twitch1. A quiet prop must wake up and evaluate its
    // authored action even when no creature/openable bits exist on the weenie.
    Object.SetupId=0x020004B5; Object.MotionTableId=0x0900006D; // retail Lever (WCID 285)
    Entity=World->SpawnActor<AACEWorldEntityActor>(); Entity->InitializeFromObject(Object,100,true);
    Entity->Appearance->GetPartCurrentTransform(1,Actual); // part 0 is the stationary base
    const FTransform BeforeAction=Actual;
    Entity->Appearance->SetComponentTickEnabled(false);
    FACEObjectMotionState Switch; Switch.ActionCommand=0x10000051; Switch.CurrentStyle=ACEMotion::StanceNonCombat;
    Entity->ApplyMotionState(Switch);
    TestTrue(TEXT("Server switch action wakes animation ticking"),Entity->Appearance->IsComponentTickEnabled());
    bool Moved=false;
    for(int32 Frame=0;Frame<60;++Frame)
    {
        if(Entity->Appearance->IsComponentTickEnabled()) Entity->Appearance->TickComponent(1.f/60,LEVELTICK_All,nullptr);
        Entity->Appearance->GetPartCurrentTransform(1,Actual);
        Moved |= !Actual.Equals(BeforeAction,.001);
    }
    TestTrue(TEXT("Server Twitch1 visibly moves the prop's actual DAT parts"),Moved);
    Entity->Destroy();

    // Transparent/UnHide must never make a ghost more opaque than the
    // server's original translucency. This applies during death motions too.
    Object.SetupId=0x02000001; Object.MotionTableId=0x09000001;
    Object.ItemType=ACEItemType::Creature; Object.Translucency=.6f;
    Entity=World->SpawnActor<AACEWorldEntityActor>(); Entity->InitializeFromObject(Object,100,true);
    auto Opacity=[&]()
    {
        auto* Part=Cast<UMeshComponent>(Entity->Appearance->GetPartMesh(0));
        float Value=-1; if(Part && Part->GetMaterial(0)) Part->GetMaterial(0)->GetScalarParameterValue(TEXT("OpacityMul"),Value);
        return Value;
    };
    if (auto* Part=Cast<UMeshComponent>(Entity->Appearance->GetPartMesh(0)))
        AddInfo(FString::Printf(TEXT("Ghost fixture part=%s materials=%d first=%s opacity=%.4f"),*Part->GetClass()->GetName(),
            Part->GetNumMaterials(),*GetNameSafe(Part->GetMaterial(0)),Opacity()));
    TestEqual(TEXT("Creature starts with server opacity"),Opacity(),.4f);
    FACEObjectMotionState Death; Death.ActionCommand=ACEMotion::Dead;
    Entity->ApplyMotionState(Death);
    FACEDatAnimationHook Fade; Fade.Type=EACEAnimationHookType::Transparent; Fade.Start=Fade.End=.9f;
    Entity->ScriptComponent->DispatchAnimationHook(Fade);
    TestEqual(TEXT("Temporary fade can reduce ghost opacity"),Opacity(),.1f);
    Fade.Start=Fade.End=0;
    Entity->ScriptComponent->DispatchAnimationHook(Fade);
    TestEqual(TEXT("Death/unhide retains original ghost opacity"),Opacity(),.4f);
    Entity->ScriptComponent->StopAllEffects();
    TestEqual(TEXT("Effect cleanup retains original ghost opacity"),Opacity(),.4f);
    Entity->Destroy(); Object.ItemType=ACEItemType::Misc; Object.Translucency=0; Object.MotionTableId=0;
    for(uint32 Surface : {0x08000590u,0x08000618u,0x0800061Du})
    {
        auto* AuthoredMaterial=Dat->GetOrCreateResolvedMaterial(Surface,0,FACEObjDesc(),0);
        TestTrue(TEXT("Corpse/default object material retains small authored DAT translucency"),
            AuthoredMaterial && AuthoredMaterial->GetBlendMode()==BLEND_Translucent);
        auto* FadedMaterial=Dat->GetOrCreateResolvedMaterial(Surface,0,FACEObjDesc(),.6f);
        UTexture* AuthoredTexture=nullptr; UTexture* FadedTexture=nullptr;
        if(AuthoredMaterial) AuthoredMaterial->GetTextureParameterValue(TEXT("Texture"),AuthoredTexture);
        if(FadedMaterial) FadedMaterial->GetTextureParameterValue(TEXT("Texture"),FadedTexture);
        TestTrue(TEXT("Default and network-faded body both retain their authored texture"),AuthoredTexture && FadedTexture);
    }

    // The actual blade setup has distinct default/resting frames. Use it as
    // arbitrary custom scenery to catch replacement of server frame zero.
    Object.SetupId=0x0200087D; Object.PhysicsState=0; Object.PlacementId=0;
    Object.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck;
    Entity=World->SpawnActor<AACEWorldEntityActor>(); Entity->InitializeFromObject(Object,100,true);
    auto* Appearance=Entity->Appearance.Get();
    const auto* Default=Dat->GetOrBuildSetupMesh(Object.SetupId,100,0);
    const auto* Resting=Dat->GetOrBuildSetupMesh(Object.SetupId,100,101);
    TestTrue(TEXT("Fixture has genuinely different authored placements"),Default && Resting && !Default->Parts.IsEmpty()
        && !Resting->Parts.IsEmpty() && !Default->Parts[0].BindTransform.Equals(Resting->Parts[0].BindTransform,.001));
    TestTrue(TEXT("Custom world props use exact server default placement"),Default && Appearance->GetPartCurrentTransform(0,Actual)
        && Actual.Equals(Default->Parts[0].BindTransform,.001));
    const uint64 Revision=Appearance->GetAppearanceRevision();
    FBox Bounds; Appearance->GetVisualWorldBounds(Bounds);
    for (float Scale : {.1f,8.f,1.f})
    {
        Object.Scale=Scale; Entity->InitializeFromObject(Object,100,true);
        FBox Scaled; Appearance->GetVisualWorldBounds(Scaled);
        TestTrue(TEXT("Server scale is applied once to every custom part"),Scaled.GetSize().Equals(Bounds.GetSize()*Scale,.05));
        TestEqual(TEXT("Scale-only custom update keeps cached geometry"),Appearance->GetAppearanceRevision(),Revision);
    }
    Entity->Destroy();

    // UIHidden excludes picking while retaining visual geometry and physics.
    Object.SetupId=0; Object.Scale=1; Object.ObjectDescriptionFlags=0;
    Entity=World->SpawnActor<AACEWorldEntityActor>(); Entity->InitializeFromObject(Object,100,false);
    const FVector Start=Authored+FVector(-200,0,25), End=Authored+FVector(200,0,25);
    TestEqual(TEXT("Visible custom fallback can be selected"),ACEVisibleObjectPick::Trace(*World,Start,End,nullptr),static_cast<AActor*>(Entity));
    Object.ObjectDescriptionFlags=ACEObjectDescFlag::UiHidden; Entity->InitializeFromObject(Object,100,false);
    TestNull(TEXT("UIHidden pedestal is never a world click target"),ACEVisibleObjectPick::Trace(*World,Start,End,nullptr));
    TestFalse(TEXT("UIHidden retains the rendered object"),Entity->IsHidden());
    TestTrue(TEXT("UIHidden retains physical collision"),Entity->GetActorEnableCollision());
    Object.ObjectDescriptionFlags=0; Entity->InitializeFromObject(Object,100,false);

    auto* Window=World->SpawnActor<AACEEnvCellActor>();
    Window->SetActorLocation(Authored-FVector(100,0,0));
    auto Quad=[](UProceduralMeshComponent* Mesh,int32 Section,float LowY,float HighY)
    {
        const TArray<FVector> Vertices{{0,LowY,-100},{0,HighY,-100},{0,HighY,200},{0,LowY,200}};
        Mesh->CreateMeshSection(Section,Vertices,{0,1,2,0,2,3},{},{},{},{},true);
    };
    // Physics has one solid wall; DrawingBSP has a window aperture.
    Quad(Window->CellCollisionMesh,0,-200,200);
    Window->CellCollisionMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Window->CellCollisionMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    Window->CellCollisionMesh->SetCollisionResponseToChannel(ECC_Camera,ECR_Block);
    Window->CellCollisionMesh->bUseComplexAsSimpleCollision=true;
    Quad(Window->CellMesh,0,-200,-40); Quad(Window->CellMesh,1,40,200);
    Window->CellMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestEqual(TEXT("NPC is selectable through a drawn window despite solid movement BSP"),
        ACEVisibleObjectPick::Trace(*World,Start,End,nullptr),static_cast<AActor*>(Entity));
    Quad(Window->CellMesh,2,-40,40);
    TestNull(TEXT("A visible wall still blocks picking the NPC behind it"),ACEVisibleObjectPick::Trace(*World,Start,End,nullptr));
    return true;
}
#endif
