#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dat/ACEOutdoorPortalPlan.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Dat/ACEPolygonMeshBuilder.h"
#include "ACEDatSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailPortalClipTest, "ACE.RetailParity.PortalClipping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailPortalClipTest::RunTest(const FString& Parameters)
{
    FConvexVolume Box;
    Box.Planes = { FPlane(1, 0, 0, 10), FPlane(-1, 0, 0, 10),
        FPlane(0, 1, 0, 10), FPlane(0, -1, 0, 10),
        FPlane(0, 0, 1, 10), FPlane(0, 0, -1, 10) };
    Box.Init();
    TArray<FVector> Polygon { FVector(5, -2, -2), FVector(5, 2, -2), FVector(5, 2, 2), FVector(5, -2, 2) };
    TArray<FVector> Clipped;
    TestTrue(TEXT("Unreal frustum contains the aperture center"), Box.IntersectBox(FVector(5,0,0), FVector::ZeroVector));
    TestTrue(TEXT("Polygon clip agrees with Unreal plane convention"), ACEOutdoorPortalPlan::ClipPolygonAgainstFrustum(Polygon, Box, Clipped));
    for (FVector& P : Polygon) P.Y += 20;
    TestFalse(TEXT("Offscreen door is rejected"), ACEOutdoorPortalPlan::ClipPolygonAgainstFrustum(Polygon, Box, Clipped));
    for (FVector& P : Polygon) P.Y -= 10;
    TestTrue(TEXT("Partially visible door survives"), ACEOutdoorPortalPlan::ClipPolygonAgainstFrustum(Polygon, Box, Clipped));
    for (const FVector& P : Clipped) TestTrue(TEXT("Clipped vertices remain inside"), P.Y <= 10.0001);

    const TArray<FVector> Entry { FVector(5,-1,-1), FVector(5,1,-1), FVector(5,1,1), FVector(5,-1,1) };
    FConvexVolume Empty;
    const FConvexVolume Cone = ACEOutdoorPortalPlan::MakePortalFrustum(FVector::ZeroVector, Entry, Empty);
    const TArray<FVector> Aligned { FVector(10,-1,-1), FVector(10,1,-1), FVector(10,1,1), FVector(10,-1,1) };
    TestTrue(TEXT("Aligned second room visible through first door"), ACEOutdoorPortalPlan::ClipPolygonAgainstFrustum(Aligned, Cone, Clipped));
    TArray<FVector> AroundCorner = Aligned;
    for (FVector& P : AroundCorner) P.Y += 5;
    TestFalse(TEXT("Second door around corner excluded by inherited portal view"), ACEOutdoorPortalPlan::ClipPolygonAgainstFrustum(AroundCorner, Cone, Clipped));
    TArray<FVector> ReverseEntry = Entry;
    Algo::Reverse(ReverseEntry);
    const FConvexVolume ReverseCone = ACEOutdoorPortalPlan::MakePortalFrustum(FVector::ZeroVector, ReverseEntry, Empty);
    TestTrue(TEXT("Cone construction independent of portal vertex winding"), ACEOutdoorPortalPlan::ClipPolygonAgainstFrustum(Aligned, ReverseCone, Clipped));
    // Real landblocks are millions of centimeters from the UE origin. Rounding
    // just the plane's D term to float can reject an entire adjoining doorway.
    const FVector TownOrigin(-2403612.37, 1909243.29, 1200.17);
    TArray<FVector> TownEntry = Entry, TownAligned = Aligned;
    const FQuat Turn(FVector::UpVector, 0.137);
    for (FVector& P : TownEntry) P = Turn.RotateVector(P) * 100.0 + TownOrigin;
    for (FVector& P : TownAligned) P = Turn.RotateVector(P) * 100.0 + TownOrigin;
    const auto TownCone = ACEOutdoorPortalPlan::MakePortalFrustum(TownOrigin, TownEntry, Empty);
    TestTrue(TEXT("World-space portal survives at Yaraq coordinates"), ACEOutdoorPortalPlan::ClipPolygonAgainstFrustum(TownAligned, TownCone, Clipped));
    TestTrue(TEXT("Coplanar reverse aperture survives its own cone"), ACEOutdoorPortalPlan::ClipPolygonAgainstFrustum(TownEntry, TownCone, Clipped));
    TestEqual(TEXT("No portal corners lost to world-coordinate rounding"), Clipped.Num(), 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailWorldDatTest, "ACE.RetailParity.WorldDat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailWorldDatTest::RunTest(const FString& Parameters)
{
    FString DatDir = TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(), TEXT("RetailDatDir="), DatDir);
    FACEDatDatabase Portal, Cell;
    if (!TestTrue(TEXT("Open retail portal DAT"), Portal.Open(FPaths::Combine(DatDir, TEXT("client_portal.dat"))))
        || !TestTrue(TEXT("Open retail cell DAT"), Cell.Open(FPaths::Combine(DatDir, TEXT("client_cell_1.dat"))))) return false;
    FString Json;
    const FString Path = FPaths::ProjectDir() / TEXT("Plugins/ACEClient/Tests/Fixtures/RetailWorld.json");
    if (!TestTrue(TEXT("Read independent server-DAT fixtures"), FFileHelper::LoadFileToString(Json, *Path))) return false;
    TSharedPtr<FJsonObject> Root;
    if (!TestTrue(TEXT("Parse fixture JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root))) return false;
    FACEEnvCellMeshBuilder Builder(&Cell, &Portal, nullptr);
    int32 Count = 0;
    for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("cells")))
    {
        const TSharedPtr<FJsonObject> Row = Value->AsObject();
        const FString IdText = Row->GetStringField(TEXT("id"));
        const uint32 Id = FCString::Strtoui64(*IdText, nullptr, 16);
        FACEBuiltEnvCellMesh Mesh;
        // An empty authored cell is still useful for transit. Its physics comparison
        // below is valid even if it has no drawable surface with no texture resolver.
        Builder.BuildEnvCell(Id, 100.f, Mesh);
        if (!TestEqual(*FString::Printf(TEXT("%s parsed id"), *IdText), Mesh.EnvCellId, Id)) return false;
        int32 Tris = 0, Verts = 0;
        int32 DrawTriangles = 0;
        for (const auto& Section : Mesh.Sections) DrawTriangles += Section.Triangles.Num() / 3;
        if (!TestEqual(*FString::Printf(TEXT("%s retail polygon sides"), *IdText), DrawTriangles,
            static_cast<int32>(Row->GetNumberField(TEXT("drawTriangles"))))) return false;
        FVector Sum = FVector::ZeroVector;
        for (const FACEBuiltMeshSection& Section : Mesh.CollisionSections)
        {
            Tris += Section.Triangles.Num() / 3;
            Verts += Section.Vertices.Num();
            for (const FVector& P : Section.Vertices) Sum += P;
        }
        if (!TestEqual(*FString::Printf(TEXT("%s authored physics triangles"), *IdText), Tris, static_cast<int32>(Row->GetNumberField(TEXT("triangles"))))) return false;
        if (!TestEqual(*FString::Printf(TEXT("%s authored physics vertices"), *IdText), Verts, static_cast<int32>(Row->GetNumberField(TEXT("vertexCount"))))) return false;
        const auto& ExpectedSum = Row->GetArrayField(TEXT("vertexSum"));
        for (int32 Axis = 0; Axis < 3; ++Axis)
            if (!TestTrue(*FString::Printf(TEXT("%s physics positions axis %d"), *IdText, Axis), FMath::IsNearlyEqual(Sum[Axis], ExpectedSum[Axis]->AsNumber(), 5.0))) return false;
        const auto& Portals = Row->GetArrayField(TEXT("portals"));
        if (!TestEqual(TEXT("Portal count"), Mesh.PortalApertureLocalNormals.Num(), Portals.Num())) return false;
        for (int32 I = 0; I < Portals.Num(); ++I)
        {
            const auto P = Portals[I]->AsObject();
            const auto& N = P->GetArrayField(TEXT("normal"));
            const FVector Expected(N[0]->AsNumber(), N[1]->AsNumber(), N[2]->AsNumber());
            if (!TestTrue(*FString::Printf(TEXT("%s portal %d side flag"), *IdText, I), Mesh.PortalApertureLocalNormals[I].Equals(Expected, 0.001))) return false;
            if (!TestTrue(*FString::Printf(TEXT("%s portal %d plane"), *IdText, I), FMath::IsNearlyEqual(static_cast<double>(Mesh.PortalApertureLocalD[I]), P->GetNumberField(TEXT("d")), 0.1))) return false;
        }
        ++Count;
    }
    AddInfo(FString::Printf(TEXT("Compared %d cells against independent ACE.DatLoader collision and portal data"), Count));
    FACESetupMeshBuilder SetupBuilder(&Portal, nullptr, nullptr);
    for (const auto& Value : Root->GetArrayField(TEXT("gfx")))
    {
        const auto Row = Value->AsObject();
        const FString IdText = Row->GetStringField(TEXT("id"));
        const uint32 Id = FCString::Strtoui64(*IdText, nullptr, 16);
        FACEBuiltSetupMesh Mesh;
        SetupBuilder.BuildSetup(Id, 100.f, Mesh);
        int32 Triangles = 0, DrawTriangles = 0, Apertures = 0;
        for (const auto& Part : Mesh.Parts) Apertures += Part.Portals.Num();
        if (!TestEqual(*FString::Printf(TEXT("%s GfxObj portal apertures"), *IdText), Apertures,
            static_cast<int32>(Row->GetNumberField(TEXT("portalPolygons"))))) return false;
        for (const auto& Part : Mesh.Parts)
            for (const auto& Section : Part.Sections)
                if (Section.bCollisionOnly) Triangles += Section.Triangles.Num() / 3;
                else DrawTriangles += Section.Triangles.Num() / 3;
        if (!TestEqual(*FString::Printf(TEXT("%s GfxObj drawing sides"), *IdText), DrawTriangles,
            static_cast<int32>(Row->GetNumberField(TEXT("drawTriangles"))))) return false;
        if (!TestEqual(*FString::Printf(TEXT("%s GfxObj physics triangles"), *IdText), Triangles,
            static_cast<int32>(Row->GetNumberField(TEXT("triangles"))))) return false;
    }
    AddInfo(FString::Printf(TEXT("Compared %d GfxObj physics meshes"), Root->GetArrayField(TEXT("gfx")).Num()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailTerrainTest, "ACE.RetailParity.TerrainHeight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailTerrainTest::RunTest(const FString& Parameters)
{
    FACEBuiltLandblockMesh Mesh;
    Mesh.LandblockId = 0xA9B40000;
    Mesh.bHasHeights = true;
    // An affine surface must interpolate identically regardless of retail's diagonal
    // hash, including cell seams and the final row/column at the landblock border.
    for (int32 X = 0; X < 9; ++X)
        for (int32 Y = 0; Y < 9; ++Y) Mesh.HeightsAc[X * 9 + Y] = 3.f * X + 7.f * Y + 12.f;
    for (int32 X = 0; X <= 192; X += 3)
        for (int32 Y = 0; Y <= 192; Y += 3)
        {
            float Height = 0;
            FVector Normal;
            if (!TestTrue(TEXT("Valid terrain height"), FACELandblockMeshBuilder::SampleHeightAc(Mesh, X, Y, Height, &Normal))) return false;
            if (!TestTrue(TEXT("Continuous terrain surface across cells"), FMath::IsNearlyEqual(Height, 3.f * X/24.f + 7.f * Y/24.f + 12.f, 0.001f))) return false;
            if (!TestTrue(TEXT("Slope normal uses AC distances and points up"), Normal.Equals(FVector(-3.f/24, -7.f/24, 1).GetSafeNormal(), .0001))) return false;
        }
    Mesh.bHasHeights = false;
    float Height = 0;
    TestFalse(TEXT("Unavailable terrain is not reported as zero-height floor"), FACELandblockMeshBuilder::SampleHeightAc(Mesh, 1, 1, Height));
    int32 Size = 0, Direction = 0;
    FACELandblockMeshBuilder::GetBlockOrient(2, 0, Size, Direction);
    TestEqual(TEXT("Retail east ring adjusts the X maximum edge"), Direction, 3);
    FACELandblockMeshBuilder::GetBlockOrient(0, 2, Size, Direction);
    TestEqual(TEXT("Retail north ring adjusts the Y maximum edge"), Direction, 1);
    Mesh.bHasHeights = Mesh.bHasTerrain = true;
    auto& FullSection=Mesh.Sections.AddDefaulted_GetRef();
    for (int32 X=0; X<9; ++X) for (int32 Y=0; Y<9; ++Y)
    {
        Mesh.HeightsAc[X*9+Y]=X*X+Y*Y;
        FullSection.Vertices.Add(FVector(-X*2400,Y*2400,Mesh.HeightsAc[X*9+Y]*100));
    }
    FullSection.Triangles={0,1,9};
    FACELandblockMeshBuilder Builder(nullptr,nullptr,nullptr);
    for (int32 PS : {1,2,4})
    {
        TArray<FACEBuiltLandblockSection> Sections;
        TestTrue(TEXT("Build retail transition ring"),Builder.BuildLodSections(Mesh,PS,3,100,Sections));
        bool bFound=false;
        for (const auto& Section : Sections) for (const FVector& V : Section.Vertices)
        {
            if (FMath::IsNearlyEqual(V.X,-19200.0) && FMath::IsNearlyEqual(V.Y,PS*2400.0))
            {
                bFound=true;
                const double Expected=(Mesh.HeightsAc[8*9]+Mesh.HeightsAc[8*9+PS*2])*50.0;
                TestTrue(TEXT("Each outer ring edge matches its coarser neighbor"),FMath::IsNearlyEqual(V.Z,Expected,.01));
            }
        }
        TestTrue(TEXT("Transition includes the tested seam vertex"),bFound);
    }
    TestEqual(TEXT("Rendering transitions preserve the source DAT heights"),Mesh.HeightsAc[8*9+1],65.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailCookTest, "ACE.RetailParity.CollisionCooking",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailCookTest::RunTest(const FString& Parameters)
{
    FString DatDir = TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(), TEXT("RetailDatDir="), DatDir);
    FACEDatDatabase Portal, Cell;
    if (!Portal.Open(DatDir / TEXT("client_portal.dat")) || !Cell.Open(DatDir / TEXT("client_cell_1.dat")))
    { AddError(TEXT("Retail DAT files unavailable")); return false; }
    FString Json;
    TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Json, *(FPaths::ProjectDir() / TEXT("Plugins/ACEClient/Tests/Fixtures/RetailWorld.json")))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root)) return false;
    const auto WorldValues = UWorld::InitializationValues().AllowAudioPlayback(false)
        .RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
        ERHIFeatureLevel::Num, &WorldValues);
    AActor* Owner = World->SpawnActor<AActor>();
    UProceduralMeshComponent* Collision = NewObject<UProceduralMeshComponent>(Owner);
    Owner->SetRootComponent(Collision);
    Collision->bUseAsyncCooking = false;
    Collision->bUseComplexAsSimpleCollision = true;
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Collision->SetCollisionObjectType(ECC_WorldStatic);
    Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    Collision->RegisterComponent();
    FACEEnvCellMeshBuilder Builder(&Cell, &Portal, nullptr);
    int32 CheckedFloors = 0;
    for (const auto& Value : Root->GetArrayField(TEXT("cells")))
    {
        if (CheckedFloors >= 32) break;
        const FString IdText = Value->AsObject()->GetStringField(TEXT("id"));
        FACEBuiltEnvCellMesh Mesh;
        Builder.BuildEnvCell(FCString::Strtoui64(*IdText, nullptr, 16), 100.f, Mesh);
        for (const auto& Section : Mesh.CollisionSections)
        {
            FVector FloorCenter;
            bool bHaveFloor = false;
            for (int32 I = 0; I + 2 < Section.Triangles.Num(); I += 3)
            {
                const FVector A = Section.Vertices[Section.Triangles[I]];
                const FVector B = Section.Vertices[Section.Triangles[I+1]];
                const FVector C = Section.Vertices[Section.Triangles[I+2]];
                if (FMath::Abs(FVector::CrossProduct(B-A,C-A).GetSafeNormal().Z) < 0.9) continue;
                FloorCenter = (A+B+C) / 3; bHaveFloor = true; break;
            }
            if (!bHaveFloor) continue;
            Collision->CreateMeshSection_LinearColor(0, Section.Vertices, Section.Triangles,
                Section.Normals, Section.UVs, Section.VertexColors, TArray<FProcMeshTangent>(), true);
            FHitResult Hit;
            const bool bHit = World->SweepSingleByChannel(Hit, FloorCenter + FVector(0,0,10),
                FloorCenter - FVector(0,0,10), FQuat::Identity, ECC_Pawn,
                FCollisionShape::MakeSphere(1.f), FCollisionQueryParams(SCENE_QUERY_STAT(RetailFloor), true));
            TestTrue(*FString::Printf(TEXT("%s cooked DAT floor blocks a downward pawn sweep"), *IdText), bHit);
            Collision->ClearAllMeshSections();
            ++CheckedFloors;
            break;
        }
    }
    TestEqual(TEXT("Cooked and queried 32 authored indoor floors"), CheckedFloors, 32);
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailUITest, "ACE.RetailParity.UILayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailUITest::RunTest(const FString& Parameters)
{
    UACEUIElementManager* Manager = NewObject<UACEUIElementManager>();
    Manager->Initialize();
    UACEUILayoutResolver* Resolver = NewObject<UACEUILayoutResolver>();
    Resolver->Initialize(nullptr, Manager);
    if (!TestTrue(TEXT("Load resolved retail gameplay"), Resolver->LoadLayout(0x21000005))) return false;
    int32 Nodes = 0, Typed = 0, Fonts = 0, States = 0;
    TFunction<void(TSharedPtr<FACEUIElement>)> Visit = [&](TSharedPtr<FACEUIElement> Node)
    {
        ++Nodes; if (Node->Type != 0) ++Typed; if (Node->FontId != 0) ++Fonts; States += Node->States.Num();
        for (auto Child : Node->Children) Visit(Child);
    };
    for (auto Root : Manager->GetSyntheticRoot()->Children) Visit(Root);
    TestEqual(TEXT("Gameplay node count"), Nodes, 1870);
    TestEqual(TEXT("Every inherited control has its retail type"), Typed, Nodes);
    TestTrue(TEXT("Inherited font properties reach runtime"), Fonts > 400);
    TestTrue(TEXT("Control state definitions reach runtime"), States > 100);
    auto Button = MakeShared<FACEUIElement>();
    Button->Type = ACEUI::ElementType::Button;
    Button->bRollover = true;
    for (uint32 State : {1u, 2u, 3u, 6u, 7u, 8u, 13u}) Button->States.Add(State, FACEUIStateMedia());
    Button->ResolvePaintState(false, false, false); TestEqual(TEXT("Button normal"), Button->PaintState, 1u);
    Button->ResolvePaintState(false, true, false); TestEqual(TEXT("Button rollover"), Button->PaintState, 2u);
    Button->ResolvePaintState(true, true, false); TestEqual(TEXT("Button pressed"), Button->PaintState, 3u);
    Button->bHighlighted = true;
    Button->ResolvePaintState(true, true, false); TestEqual(TEXT("Selected pressed"), Button->PaintState, 8u);
    Button->bGhosted = true;
    Button->ResolvePaintState(true, true, false); TestEqual(TEXT("Disabled state"), Button->PaintState, 13u);
    auto Child = MakeShared<FACEUIElement>();
    Button->AddChild(Child);
    Button->bPaintPassStateToChildren = true;
    Child->bDefaultHidden = true;
    FACEUIStateMedia Shown; Shown.bHidden = false;
    Child->States.Add(13, Shown);
    Child->ResolvePaintState(false, false, false);
    TestTrue(TEXT("Inherited state can show an authored hidden child"), Child->IsPaintVisible());
    Child->bVisible = false;
    TestFalse(TEXT("Game-module visibility still hides the control"), Child->IsPaintVisible());
    Resolver->Shutdown(); Manager->Shutdown();
    return true;
}
#endif
