#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Dat/ACEPolygonMeshBuilder.h"
#include "Dat/ACEPortalViewMask.h"
#include "Dat/ACEDatDatabase.h"
#include "ACEDatSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailPolygonSidesTest, "ACE.RetailParity.PolygonSides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailPolygonSidesTest::RunTest(const FString& Parameters)
{
    TMap<uint16, FACEDatSWVertex> Vertices;
    for (uint16 I = 0; I < 3; ++I)
    {
        FACEDatSWVertex V;
        V.Origin = FVector3f(I == 1 ? 1 : 0, I == 2 ? 1 : 0, 2);
        V.Normal = FVector3f::UpVector;
        V.UVs = { FVector2f(0.25f, 0.5f), FVector2f(0.75f, 1.f) };
        Vertices.Add(I, V);
    }
    FACEDatPolygon Poly;
    Poly.NumPts = 3; Poly.VertexIds = {0,1,2}; Poly.PosSurface = 0; Poly.NegSurface = 1;
    Poly.SidesType = EACECullMode::Clockwise;
    Poly.Stippling = EACEStipplingType::NoPos;
    Poly.NegUVIndices = {1,1,1};
    TArray<FACEBuiltMeshSection> Sections;
    ACEPolygonMeshBuilder::Append({{0, Poly}}, Vertices, {101,102}, nullptr, 0, nullptr, 100.f, Sections);
    if (!TestEqual(TEXT("Distinct surfaces on both authored sides"), Sections.Num(), 2)) return false;
    TestEqual(TEXT("Front surface survives NoPos UV flag"), Sections[0].SurfaceId, 101u);
    TestEqual(TEXT("Back uses its own surface"), Sections[1].SurfaceId, 102u);
    TestTrue(TEXT("Missing UV array selects vertex UV zero"), Sections[0].UVs[0].Equals(FVector2D(0.25,0.5)));
    TestTrue(TEXT("Back uses negative UV array"), Sections[1].UVs[0].Equals(FVector2D(0.75,1)));
    TestTrue(TEXT("Back normal is reversed"), Sections[1].Normals[0].Equals(-Sections[0].Normals[0]));
    TestTrue(TEXT("No artificial ceiling displacement"), Sections[1].Vertices[0].Equals(FVector(0,0,200)));
    TestEqual(TEXT("Front winding"), Sections[0].Triangles[1], 1);
    TestEqual(TEXT("Back winding"), Sections[1].Triangles[1], 2);
    Poly.SidesType = EACECullMode::None; Sections.Reset();
    ACEPolygonMeshBuilder::Append({{0, Poly}}, Vertices, {101,102}, nullptr, 0, nullptr, 100.f, Sections);
    TestEqual(TEXT("Cull None shares the positive surface"), Sections.Num(), 1);
    TestEqual(TEXT("Cull None emits both windings"), Sections[0].Triangles.Num(), 6);
    TestTrue(TEXT("Cull None shares positive UVs"), Sections[0].UVs[3].Equals(Sections[0].UVs[0]));
    Poly.SidesType = EACECullMode::CounterClockwise; Sections.Reset();
    ACEPolygonMeshBuilder::Append({{0, Poly}}, Vertices, {101,102}, nullptr, 0, nullptr, 100.f, Sections);
    TestEqual(TEXT("One-sided polygon remains one-sided"), Sections[0].Triangles.Num(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailMeshApplicationTest, "ACE.RetailParity.MeshApplication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACERetailMeshApplicationTest::RunTest(const FString& Parameters)
{
    FString DatDir = TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(), TEXT("RetailDatDir="), DatDir);
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    UACEDatSubsystem* Dat = NewObject<UACEDatSubsystem>(GameInstance);
    if (!TestTrue(TEXT("Load retail DAT for runtime application"), Dat->LoadDatDirectory(DatDir))) return false;
    // Login's portal-only index must not dispatch destructive cache maintenance.
    // Exercise the real subsystem with its cell handle deferred, then restore it.
    {
        const uint64 FullFingerprint=Dat->ComputeDatFingerprint();
        auto Cell=MoveTemp(Dat->CellDat);
        Dat->CachedDatFingerprint=0;
        Dat->bWorldStreamingAllowed=true;Dat->bPendingDiskCacheMaintenance=true;
        Dat->TryStartDiskCacheMaintenance();
        TestTrue(TEXT("Cache purge waits for the deferred terrain DAT"),Dat->bPendingDiskCacheMaintenance);
        TestEqual(TEXT("Deferred maintenance does not cache an incomplete identity"),Dat->CachedDatFingerprint,uint64(0));
        Dat->CellDat=MoveTemp(Cell);
        TestEqual(TEXT("Restored terrain index uses the original complete identity"),Dat->ComputeDatFingerprint(),FullFingerprint);
        Dat->bWorldStreamingAllowed=false;Dat->TryStartDiskCacheMaintenance();
        TestTrue(TEXT("Login screen defers disk maintenance until world entry"),Dat->bPendingDiskCacheMaintenance);
        Dat->bPendingDiskCacheMaintenance=false;
    }
    // Material/static-mesh compilation may pump async game-thread completions.
    // Retain a real setup while the cache grows, gets a late duplicate, and evicts it.
    {
        const uint32 Id = 0x020002F3;
        const uint64 Key = Dat->MakeSetupCacheKey(Id, 100, 0);
        const auto Held = Dat->GetOrBuildSetupMeshShared(Id, 100);
        if (!TestTrue(TEXT("Pin real setup for concurrent completion fixture"), Held.IsValid())) return false;
        const int32 PartCount = Held->Parts.Num();
        for (uint32 I=0; I<128; ++I)
            Dat->OnSetupBuildComplete(I+1, 100, I, Dat->SetupBuildGeneration, true, MakeShared<FACEBuiltSetupMesh>());
        TestTrue(TEXT("Cache growth does not relocate an active setup"), Dat->FindSetupMesh(Id,100)==Held.Get());
        Dat->OnSetupBuildComplete(Id,100,Key,Dat->SetupBuildGeneration,true,MakeShared<FACEBuiltSetupMesh>());
        TestTrue(TEXT("Late async completion cannot replace an already completed setup"), Dat->FindSetupMesh(Id,100)==Held.Get());
        Dat->SetupMeshCache.Remove(Key);
        TestTrue(TEXT("Cache can evict the active setup entry"), Dat->FindSetupMesh(Id,100)==nullptr);
        TestTrue(TEXT("Pinned geometry remains readable after eviction"), PartCount>0 && Held->Parts.Num()==PartCount);
        Dat->PendingSetupKeys.Add(Key);
        Dat->OnSetupBuildComplete(Id,100,Key,Dat->SetupBuildGeneration-1,true,MakeShared<FACEBuiltSetupMesh>());
        TestTrue(TEXT("Stale generation cannot consume a new request's pending flag"), Dat->PendingSetupKeys.Contains(Key));
        Dat->PendingSetupKeys.Remove(Key);
        Dat->SetupMeshCache.Reset();
    }
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
        .RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.OwningGameInstance = GameInstance;
    Context.SetCurrentWorld(World);
    World->SetGameInstance(GameInstance);
    GameInstance->OnWorldChanged(nullptr, World);
    AActor* Owner = World->SpawnActor<AActor>();
    UProceduralMeshComponent* Proc = NewObject<UProceduralMeshComponent>(Owner);
    Owner->SetRootComponent(Proc); Proc->RegisterComponent();
    for (uint32 Id : {0x010025E3u, 0x020002F3u, 0x020002F4u, 0x020002F5u})
    {
        const FACEBuiltSetupMesh* Built = Dat->GetOrBuildSetupMesh(Id, 100.f);
        if (!TestNotNull(TEXT("Build runtime fixture"), Built)) continue;
        int32 Expected = 0;
        for (const auto& Part : Built->Parts)
            for (const auto& Section : Part.Sections)
                if (Section.bCollisionOnly) Expected += Section.Triangles.Num() / 3;
        for (bool bLegacyStairOverride : {false, true})
        {
            Dat->ApplySetupToProceduralMesh(Proc, Id, 100.f, true, 0, bLegacyStairOverride, bLegacyStairOverride);
            FTriMeshCollisionData Data;
            Proc->GetPhysicsTriMeshData(&Data, false);
            TestEqual(*FString::Printf(TEXT("%08X PMC preserves physics, stair override %d"), Id, bLegacyStairOverride), Data.Indices.Num(), Expected);
            for (int32 I = 0; I < Proc->GetNumSections(); ++I)
            {
                const auto* Section = Proc->GetProcMeshSection(I);
                if (Section && Section->bSectionVisible && Proc->GetMaterial(I))
                    TestFalse(TEXT("Authored polygon sides use backface culling"), Proc->GetMaterial(I)->IsTwoSided());
            }
        }
        UStaticMesh* Physics = Dat->GetOrCreateSetupStaticMesh(Id, 100.f, true, 0, false, false, false, true);
        if (Expected > 0)
        {
            if (TestNotNull(TEXT("Cook independent static physics mesh"), Physics))
            {
                TestTrue(TEXT("Runtime collision mesh resolves the game world"), Physics->GetWorld() == World);
                FTriMeshCollisionData Data;
                Physics->GetPhysicsTriMeshData(&Data, false);
                TestEqual(TEXT("Static collision keeps every PhysicsBSP triangle"), Data.Indices.Num(), Expected);
            }
        }
        UStaticMesh* Visual = Dat->GetOrCreateSetupStaticMesh(Id, 100.f, true);
        // First-time material graph initialization can invalidate the setup cache.
        // Compare against the current physics entry after visual material creation.
        Physics = Dat->GetOrCreateSetupStaticMesh(Id, 100.f, true, 0, false, false, false, true);
        if (Visual) TestTrue(*FString::Printf(TEXT("%08X visual static mesh uses the physics body's collision"), Id),
            Visual->GetBodySetup() == (Physics ? Physics->GetBodySetup() : nullptr));

        for (auto* Mesh : {Visual, Physics}) if (Mesh)
        {
            TestFalse(TEXT("Runtime mesh must not enqueue redundant editor material rebuilds"),Mesh->IsCompiling());
            const auto Bounds=Mesh->GetBounds();
            TestFalse(TEXT("Completed runtime mesh has finite origin"),Bounds.Origin.ContainsNaN());
            TestFalse(TEXT("Completed runtime mesh has finite extent"),Bounds.BoxExtent.ContainsNaN());
            TestTrue(TEXT("Completed runtime mesh has a positive finite radius"),FMath::IsFinite(Bounds.SphereRadius) && Bounds.SphereRadius>0);
        }
        Built=Dat->GetOrBuildSetupMesh(Id,100.f);
        if (!TestNotNull(TEXT("Current setup after material initialization"),Built)) continue;
        TArray<UProceduralMeshComponent*> Parts;
        for (int32 P = 0; P < Built->Parts.Num(); ++P)
        {
            auto* Part = NewObject<UProceduralMeshComponent>(Owner);
            Part->RegisterComponent(); Parts.Add(Part);
        }
        TArray<FTransform> BindTransforms; uint32 MotionTable = 0;
        Dat->ApplySetupParts(Parts, Id, FACEObjDesc(), 100.f, BindTransforms, MotionTable, 0, true, 0.f, Built);
        int32 PartTriangles = 0;
        for (auto* Part : Parts)
        {
            FTriMeshCollisionData Data; Part->GetPhysicsTriMeshData(&Data, false);
            PartTriangles += Data.Indices.Num(); Part->DestroyComponent();
        }
        TestEqual(TEXT("Entity parts preserve authored collision without drawing colliders"), PartTriangles, Expected);
        Proc->ClearAllMeshSections();
        if (Visual && Expected > 0)
        {
            auto* Static = NewObject<UStaticMeshComponent>(Owner);
            Static->SetStaticMesh(Visual);
            Static->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Static->SetCollisionResponseToAllChannels(ECR_Block);
            Static->RegisterComponent();
            bool bCheckedFloor = false;
            for (const auto& Part : Built->Parts)
            {
                for (const auto& Section : Part.Sections)
                {
                    if (!Section.bCollisionOnly || bCheckedFloor) continue;
                    for (int32 I = 0; I + 2 < Section.Triangles.Num(); I += 3)
                    {
                        const FVector A = Part.BindTransform.TransformPosition(Section.Vertices[Section.Triangles[I]]);
                        const FVector B = Part.BindTransform.TransformPosition(Section.Vertices[Section.Triangles[I+1]]);
                        const FVector C = Part.BindTransform.TransformPosition(Section.Vertices[Section.Triangles[I+2]]);
                        if (FMath::Abs(FVector::CrossProduct(B-A,C-A).GetSafeNormal().Z) < 0.9) continue;
                        const FVector Center = (A+B+C) / 3;
                        FHitResult Hit;
                        TestTrue(*FString::Printf(TEXT("%08X shared static body blocks downward pawn sweep"), Id), World->SweepSingleByChannel(Hit,
                            Center + FVector(0,0,10), Center - FVector(0,0,10), FQuat::Identity, ECC_Pawn,
                            FCollisionShape::MakeSphere(1.f), FCollisionQueryParams(SCENE_QUERY_STAT(RetailStaticFloor), true)));
                        bCheckedFloor = true; break;
                    }
                }
            }
            TestTrue(TEXT("Static physics fixture includes a tested floor"), bCheckedFloor);
            Static->DestroyComponent();
        }
    }
    // A visible building keeps its authored height while crossing terrain rings.
    // The rendered ground must retain intermediate samples, even at ring five.
    const uint32 HillBlock=0xA0440000;
    const auto* Hill=Dat->GetOrBuildLandblockMesh(HillBlock,100);
    if (TestNotNull(TEXT("Hillside terrain fixture loads"),Hill))
    {
        TArray<FVector> NearVertices;
        TArray<uint32> NearIndices;
        for (int32 PolySize : {1,2,4,8})
        {
            TestTrue(TEXT("Apply terrain at every distance ring"),Dat->ApplyLandblockToProceduralMesh(Proc,HillBlock,100,PolySize,0));
            TArray<FVector> Vertices;TArray<uint32> Indices;
            for (int32 I=0;I<Proc->GetNumSections();++I)
                if(const auto* Section=Proc->GetProcMeshSection(I))
                {
                    for(const auto& Vertex:Section->ProcVertexBuffer) Vertices.Add(FVector(Vertex.Position));
                    Indices.Append(Section->ProcIndexBuffer);
                }
            if(PolySize==1) { NearVertices=Vertices; NearIndices=Indices; }
            else TestTrue(TEXT("Distant ground retains the same elevations and diagonals under scenery"),Vertices==NearVertices && Indices==NearIndices);
        }
        TestTrue(TEXT("Hillside comparison includes real terrain vertices"),!NearVertices.IsEmpty());
    }
    GameInstance->OnWorldChanged(World, nullptr);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailPortalMaskTest, "ACE.RetailParity.PortalPolygonMask",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailPortalMaskTest::RunTest(const FString& Parameters)
{
    TArray<ACEOutdoorPortalPlan::FAdmittedAperture> Apertures;
    for (double Y : {-6.0, 0.0, 6.0})
    {
        auto& A = Apertures.AddDefaulted_GetRef();
        A.WorldNormal = FVector(1,0,0);
        A.WorldVerts = { FVector(5,Y-1,-1), FVector(5,Y+1,-1), FVector(5,Y,1) };
    }
    auto Mask = FACEPortalViewMask::Build(FVector::ZeroVector, Apertures);
    TestEqual(TEXT("All three disjoint portal views retained"), Mask.ViewCount, 3);
    for (double Y : {-12.0, 0.0, 12.0})
        TestTrue(TEXT("Outdoor point through each actual triangle survives"), Mask.Contains(FVector(10,Y,0)));
    TestFalse(TEXT("Triangle corner outside aperture stays hidden"), Mask.Contains(FVector(10,1.8,1.8)));
    TestFalse(TEXT("Gap between doorways stays hidden"), Mask.Contains(FVector(10,6,0)));
    TestFalse(TEXT("Indoor floor before doorway stays hidden"), Mask.Contains(FVector(2,0,0)));
    const FVector FarEye(-4800000,4800000,10000);
    for (auto& A : Apertures)
    {
        A.WorldNormal = -A.WorldNormal;
        for (auto& V : A.WorldVerts) V += FarEye;
    }
    Mask = FACEPortalViewMask::Build(FarEye, Apertures);
    TestTrue(TEXT("Camera-relative planes preserve distant-landblock precision"), Mask.Contains(FarEye + FVector(10,12,0)));
    TestFalse(TEXT("Reversed portal normal does not open triangle corners"), Mask.Contains(FarEye + FVector(10,1.8,1.8)));
    TestFalse(TEXT("Sealed interior exposes no terrain"), FACEPortalViewMask::Build(FarEye, {}).Contains(FarEye + FVector(10,0,0)));
    return true;
}

#endif
