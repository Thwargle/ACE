#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACEVisibleObjectPick.h"
#include "ACECreatureFixtures.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVisibilityEarlyOutTest,"ACE.Performance.VisibilityEarlyOut",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEVisibilityEarlyOutTest::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    ON_SCOPE_EXIT {World->DestroyWorld(false);};
    auto* Actor=World->SpawnActor<AActor>();auto* Mesh=NewObject<UProceduralMeshComponent>(Actor);
    Actor->SetRootComponent(Mesh);Mesh->RegisterComponent();
    TArray<FVector> V;TArray<int32> Indices;
    for(int32 I=0;I<2048;++I)
    {
        const double X=1000-I*.1;const int32 Base=V.Num();
        V.Append({FVector(X,-50,-50),FVector(X,50,-50),FVector(X,0,50)});Indices.Append({Base,Base+1,Base+2});
    }
    Mesh->CreateMeshSection(0,V,Indices,{},{},{},{},false);
    const FVector Start(0,0,0),End(2000,0,0);double Closest=2000,Any=2000;
    TestTrue(TEXT("Selection finds blocking geometry"),ACEVisibleObjectPick::TraceMesh(*Mesh,Start,End,Closest));
    TestTrue(TEXT("Visibility also finds blocking geometry"),ACEVisibleObjectPick::TraceMesh(*Mesh,Start,End,Any,nullptr,false,true));
    TestTrue(TEXT("Selection retains nearest hit despite far-first triangles"),FMath::IsNearlyEqual(Closest,795.3,.01));
    TestTrue(TEXT("Visibility may stop at the first blocking triangle"),FMath::IsNearlyEqual(Any,1000.,.01));
    for(bool Early:{false,true})
    {
        double Miss=2000;TestFalse(TEXT("Early-out preserves clear sight rays"),ACEVisibleObjectPick::TraceMesh(*Mesh,{0,100,0},{2000,100,0},Miss,nullptr,false,Early));
        const double Begin=FPlatformTime::Seconds();
        for(int32 I=0;I<200;++I){double Distance=2000;ACEVisibleObjectPick::TraceMesh(*Mesh,Start,End,Distance,nullptr,false,Early);}
        AddInfo(FString::Printf(TEXT("2048-triangle wall, %s: %.2f us/query"),Early?TEXT("any-hit visibility"):TEXT("nearest-hit selection"),(FPlatformTime::Seconds()-Begin)*1e6/200));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACESelectionSphereTest, "ACE.RetailParity.SelectionSpheres",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACESelectionSphereTest::RunTest(const FString&)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto* GI = NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
    GI->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(GI);
    ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); };
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    if (!TestTrue(TEXT("Retail DAT opens"), Dat && Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;

    int32 Guid = 0x71002000;
    for (const auto& Fixture : ACECreatureFixtures::Models)
    {
        FACEWorldObject Object; Object.Guid = ++Guid; Object.ItemType = ACEItemType::Creature;
        Object.SetupId = Fixture.Setup; Object.MotionTableId = Fixture.Motion;
        Object.Scale = Fixture.Scale; Object.Name = Fixture.Name;
        Object.InitialMotionStyle = ACEMotion::StanceNonCombat; Object.InitialMotionCommand = ACEMotion::Ready;
        auto* Entity = World->SpawnActor<AACEWorldEntityActor>(); Entity->InitializeFromObject(Object, 100, true);
        Entity->SetActorLocation(FVector(8000, 0, 2000));
        auto* Appearance = Entity->FindComponentByClass<UACECharacterAppearanceComponent>();
        const auto* Built = Dat->GetOrBuildSetupMesh(Object.SetupId, 100);
        if (!TestNotNull(TEXT("Creature mesh loads"), Built) || !TestNotNull(TEXT("Creature appearance"), Appearance)) return false;
        bool FoundMiss = false;
        FVector MissStart, MissEnd;
        // Look for a real silhouette miss inside an authored part sphere, at
        // 80m. No fabricated aim assist radius or collision-body enlargement.
        for (int32 P = 0; P < Built->Parts.Num() && !FoundMiss; ++P)
        {
            const auto* Part = Appearance->GetPartMesh(P);
            const FSphere& Sphere = Built->Parts[P].DrawingSphere;
            if (!Part || Sphere.W <= 1) continue;
            const FTransform Transform = Part->GetComponentTransform();
            const FVector Center = Transform.TransformPosition(Sphere.Center);
            const double Radius = Sphere.W * Transform.GetScale3D().GetAbsMin();
            for (int32 Y = -8; Y <= 8 && !FoundMiss; ++Y) for (int32 Z = -8; Z <= 8 && !FoundMiss; ++Z)
            {
                const FVector Offset(0, Y * Radius / 10, Z * Radius / 10);
                const FVector Start = Center + Offset - FVector(8000,0,0), End = Center + Offset + FVector(1000,0,0);
                double SphereDistance = 9000, PolygonDistance = 9000;
                if (!Appearance->TraceDrawingSpheres(Start, End, SphereDistance)) continue;
                bool Polygon = false;
                TInlineComponentArray<UProceduralMeshComponent*> Meshes(Entity);
                for (auto* Mesh : Meshes) Polygon |= ACEVisibleObjectPick::TraceMesh(*Mesh, Start, End, PolygonDistance);
                if (Polygon) continue;
                FoundMiss = true; MissStart = Start; MissEnd = End;
            }
        }
        TestTrue(*FString::Printf(TEXT("%s has a sphere fallback outside its polygons"), Fixture.Name), FoundMiss);
        if (FoundMiss)
        {
            TestTrue(TEXT("Desktop distant silhouette miss selects creature"), ACEVisibleObjectPick::Trace(*World,MissStart,MissEnd,nullptr) == Entity);
            TestTrue(TEXT("VR uses the same forgiving selection"), ACEVisibleObjectPick::Trace(*World,MissStart,MissEnd,nullptr,nullptr,false) == Entity);
            // A farther direct hit outranks the nearer creature's sphere,
            // regardless of broad-phase order (Render::GetMouseSelectionObjectID).
            FACEWorldObject BehindObject; BehindObject.Guid = ++Guid;
            auto* Behind = World->SpawnActor<AACEWorldEntityActor>(); Behind->InitializeFromObject(BehindObject,100,false);
            auto* BehindProxy = NewObject<UBoxComponent>(Behind); Behind->AddInstanceComponent(BehindProxy);
            BehindProxy->SetBoxExtent(FVector(20,100,100)); BehindProxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            BehindProxy->SetCollisionResponseToAllChannels(ECR_Ignore); BehindProxy->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
            BehindProxy->RegisterComponent(); BehindProxy->SetWorldLocation(MissEnd-FVector(100,0,0));
            auto* BehindMesh = NewObject<UProceduralMeshComponent>(Behind); Behind->AddInstanceComponent(BehindMesh); BehindMesh->RegisterComponent();
            BehindMesh->SetWorldLocation(BehindProxy->GetComponentLocation());
            BehindMesh->CreateMeshSection(0,{{0,-80,-80},{0,80,-80},{0,0,80}},{0,1,2},{},{},{},{},false);
            TestTrue(TEXT("Direct polygon behind a sphere-only hit wins"),ACEVisibleObjectPick::Trace(*World,MissStart,MissEnd,nullptr)==Behind);
            Behind->Destroy();
            Entity->ObjectDescriptionFlags |= ACEObjectDescFlag::UiHidden;
            TestNull(TEXT("UiHidden cannot be selected by its drawing sphere"), ACEVisibleObjectPick::Trace(*World,MissStart,MissEnd,nullptr));
            Entity->ObjectDescriptionFlags &= ~ACEObjectDescFlag::UiHidden;
            auto* Wall = World->SpawnActor<AActor>();
            auto* Box = NewObject<UBoxComponent>(Wall); Wall->AddInstanceComponent(Box); Wall->SetRootComponent(Box);
            Box->SetBoxExtent(FVector(10,500,500)); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Box->SetCollisionResponseToAllChannels(ECR_Ignore); Box->SetCollisionResponseToChannel(ECC_Camera,ECR_Block);
            Box->RegisterComponent(); Wall->SetActorLocation((MissStart + MissEnd) / 2);
            TestNull(TEXT("Wall occludes sphere fallback"), ACEVisibleObjectPick::Trace(*World,MissStart,MissEnd,nullptr));
            Wall->Destroy();
            Entity->SetActorHiddenInGame(true);
            TestNull(TEXT("Hidden creature cannot be selected by sphere"), ACEVisibleObjectPick::Trace(*World,MissStart,MissEnd,nullptr));
        }
        Entity->Destroy();
    }
    return true;
}
#endif
