#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterCreation.h"
#include "ACECharacterAppearanceComponent.h"
#include "Dat/ACEDatCursor.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"
#include "ImageUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEArmorDistanceTest,"ACE.RetailParity.ArmorDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEArmorDistanceTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false); };
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if(!TestTrue(TEXT("Retail DAT loads"),Dat && Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
    auto* Portal=Dat->GetPortalDat();
    FACESetupMeshBuilder Builder(Portal,nullptr,nullptr);
    FACECharacterCreation Creation;FString Error;
    if(!TestTrue(TEXT("Clothing decoder loads"),Creation.Load(*Portal,Error)))return false;
    TArray<uint32> CeldonColors;FACEObjDesc ColorProbe;
    Creation.ApplyClothing(0x10000188,0x02000001,0,0,ColorProbe,&CeldonColors);
    if(!TestTrue(TEXT("Celdon has authored dye choices"),!CeldonColors.IsEmpty()))return false;
    const uint32 CeldonColor=CeldonColors.Contains(2)?2:CeldonColors[0];

    // Enumerate every clothing table, including both genders and non-human
    // setups. Checking each independent part covers arbitrary server-composed
    // combinations without naming armor families in production code.
    TSet<uint32> Roots,Setups;int32 Tables=0,Combinations=0,LevelsChecked=0;
    TArray<uint8> Bytes;
    for(const auto& Entry:Portal->Files)
    {
        if((Entry.Key>>24)!=0x10)continue;
        ++Tables;Portal->ReadFile(Entry.Key,Bytes);FACEDatCursor C(Bytes);bool Ok=true;
        C.ReadU32(Ok);const uint16 Count=C.ReadU16(Ok);C.ReadU16(Ok);
        for(uint16 I=0;I<Count && Ok;++I)
        {
            Setups.Add(C.ReadU32(Ok));const uint32 Parts=C.ReadU32(Ok);++Combinations;
            for(uint32 P=0;P<Parts && Ok;++P)
            {
                C.ReadU32(Ok);const uint32 Root=C.ReadU32(Ok);if(Root)Roots.Add(Root);
                const uint32 Textures=C.ReadU32(Ok);Ok &= C.Skip(Textures*8);
            }
        }
        if(!TestTrue(TEXT("Clothing part map parses"),Ok))return false;
    }
    for(uint32 Root:Roots)
    {
        if(!Portal->ReadFile(Root,Bytes))continue; // Unavailable optional DAT art.
        FACEDatCursor C(Bytes);FACEDatGfxObj Raw;
        if(!TestTrue(TEXT("Clothing root decodes"),ACEDatUnpack::UnpackGfxObj(C,Raw)))return false;
        FACEBuiltSetupMesh Built;
        if(!TestTrue(TEXT("Clothing part builds all distance levels"),Builder.BuildSetupWithAppearance(Root,FACEObjDesc(),100,Built,0,true)))return false;
        FACEDatGfxObj Close;
        if(!TestTrue(TEXT("Clothing close model resolves"),Builder.LoadGfxObj(Root,Close)))return false;
        TArray<uint8> DegradeBytes;
        if(!Raw.DIDDegrade || !Portal->ReadFile(Raw.DIDDegrade,DegradeBytes))continue;
        FACEDatCursor D(DegradeBytes);bool Ok=true;D.ReadU32(Ok);uint32 Count=D.ReadU32(Ok);
        if(!TestEqual(TEXT("All authored levels retained"),Built.Parts[0].DistanceLevels.Num(),int32(Count)))return false;
        for(uint32 I=0;I<Count;++I)
        {
            const uint32 Id=D.ReadU32(Ok),Mode=D.ReadU32(Ok);
            const float Min=D.ReadF32(Ok),Ideal=D.ReadF32(Ok),Max=D.ReadF32(Ok);
            const auto& L=Built.Parts[0].DistanceLevels[I];++LevelsChecked;
            TestEqual(TEXT("Authored model selected without recursively degrading it"),L.GfxObjId,Id);
            TestEqual(TEXT("Authored draw mode retained"),L.DrawMode,Mode);
            TestEqual(TEXT("Minimum threshold retained"),L.MinDistance,Min);
            TestEqual(TEXT("Ideal threshold retained"),L.IdealDistance,Ideal);
            TestEqual(TEXT("Maximum threshold retained"),L.MaxDistance,Max);
            if(I==0)TestEqual(TEXT("Every clothing root resolves its highest detail model"),Close.Id,Id);
            if(!Id){TestTrue(TEXT("Null level draws nothing"),L.Sections.IsEmpty());continue;}
            if(I==0 || !Portal->ReadFile(Id,Bytes))continue;
            FACEDatCursor G(Bytes);FACEDatGfxObj LevelGfx;
            if(!ACEDatUnpack::UnpackGfxObj(G,LevelGfx))return false;
            int32 ExpectedIndices=0,ActualIndices=0;
            for(const auto& Pair:LevelGfx.Polygons)
            {
                const auto& P=Pair.Value;
                if(LevelGfx.DrawingPortalPolygonIds.Contains(Pair.Key))continue;
                if(Close.Surfaces.IsValidIndex(P.PosSurface))ExpectedIndices+=FMath::Max(0,P.VertexIds.Num()-2)*3;
                if(P.SidesType==EACECullMode::None && Close.Surfaces.IsValidIndex(P.PosSurface))ExpectedIndices+=FMath::Max(0,P.VertexIds.Num()-2)*3;
                if(P.SidesType==EACECullMode::Clockwise && Close.Surfaces.IsValidIndex(P.NegSurface))ExpectedIndices+=FMath::Max(0,P.VertexIds.Num()-2)*3;
            }
            for(const auto& S:L.Sections)
            {
                ActualIndices+=S.Triangles.Num();
                TestFalse(TEXT("Lower levels never add collision"),S.bCollisionOnly);
                TestTrue(TEXT("All levels use the close model's surface array"),Close.Surfaces.Contains(S.SurfaceId));
            }
            if(!TestEqual(FString::Printf(TEXT("Authored geometry at %08X level %u"),Root,I),ActualIndices,ExpectedIndices))return false;
        }
        // These are world-camera distances, not already biased DAT distances.
        // Check every clothing part, including vendor clothing and mixed armor,
        // against retail's 50m near-detail region. Null/zero-distance parts keep
        // their authored hiding behavior rather than getting a clothing exception.
        if(Count>0 && Built.Parts[0].DistanceLevels[0].IdealDistance>0)
        {
            TArray<FProcMeshDistanceLevel> Levels;
            for(const auto& L:Built.Parts[0].DistanceLevels)
                Levels.Add({L.MinDistance,L.IdealDistance,L.MaxDistance,L.DrawMode,0,0});
            for(float Distance:{1.f,4.f,6.f,10.f,25.f,50.f})
                if(!TestEqual(FString::Printf(TEXT("Clothing %08X retains close model at %.0fm"),Root,Distance),
                    ProceduralMeshDistanceLOD::Select(Levels,Distance),0))return false;
        }
        Builder.TrimGfxCache(128);
    }
    AddInfo(FString::Printf(TEXT("Audited %d clothing tables, %d setup/table combinations, %d setups, %d distinct part roots, %d levels"),Tables,Combinations,Setups.Num(),Roots.Num(),LevelsChecked));

    TArray<FProcMeshDistanceLevel> Thresholds={{0,4,8,1,0,1},{0,6,12,1,1,1},{84,84,84,5,2,1},{MAX_flt,MAX_flt,MAX_flt,1,3,0}};
    TestEqual(TEXT("Retail's default distance offset is 50 meters"),ProceduralMeshDistanceLOD::DefaultDistanceOffset,50.f);
    TestEqual(TEXT("Near is highest detail after retail's default offset"),ProceduralMeshDistanceLOD::Select(Thresholds,53.99f),0);
    TestEqual(TEXT("Threshold equality advances after the near-detail region"),ProceduralMeshDistanceLOD::Select(Thresholds,54.f),1);
    TestEqual(TEXT("Positive bias interpolates toward maximum"),ProceduralMeshDistanceLOD::Select(Thresholds,56.f,false,.5f),1);
    TestEqual(TEXT("Negative bias interpolates toward minimum"),ProceduralMeshDistanceLOD::Select(Thresholds,52.f,false,-.5f),1);
    TestEqual(TEXT("Distance offset matches retail"),ProceduralMeshDistanceLOD::Select(Thresholds,5.f,false,0,2),0);
    TestEqual(TEXT("Beyond final drawable level is invisible"),ProceduralMeshDistanceLOD::Select(Thresholds,134),3);
    TestEqual(TEXT("Local player never degrades"),ProceduralMeshDistanceLOD::Select(Thresholds,10000,true),0);
    TestEqual(TEXT("Distance includes part/object scale"),ProceduralMeshDistanceLOD::Distance(FTransform(FQuat::Identity,FVector(0,0,0),FVector(2)),FVector(0,0,50),FVector(0,0,900),100),4.f);

    auto* Actor=World->SpawnActor<AActor>();
    auto* Root=NewObject<USceneComponent>(Actor);Actor->SetRootComponent(Root);Root->RegisterComponent();
    auto* App=NewObject<UACECharacterAppearanceComponent>(Actor);Actor->AddInstanceComponent(App);App->RegisterComponent();
    FACEWorldObject Object;Object.bIsPlayer=true;Object.Appearance.PaletteBaseId=0x0400007E;
    for(uint32 Setup:{0x02000001u,0x0200004Eu})for(bool Mixed:{false,true})
    {
        Object.SetupId=Setup;Object.Appearance=FACEObjDesc();Object.Appearance.PaletteBaseId=0x0400007E;
        for(uint32 Table:{0x10000188u,0x10000187u,0x10000184u,0x10000183u})
            TestTrue(TEXT("Celdon pieces compose"),Creation.ApplyClothing(Table,Setup,CeldonColor,.5,Object.Appearance));
        if(Mixed)TestTrue(TEXT("Over-robe replaces overlapping Celdon parts"),Creation.ApplyClothing(0x100007E2,Setup,61,1,Object.Appearance));
        TestTrue(TEXT("Composed armor reaches final renderer"),App->ApplyWorldObject(Object,100,false));
        for(const auto& Replacement:Object.Appearance.AnimPartChanges)
        {
            auto* Mesh=Cast<UProceduralMeshComponent>(App->GetPartMesh(Replacement.PartIndex));
            if(!TestNotNull(TEXT("Replaced armor part exists"),Mesh))return false;
            FACEDatGfxObj Expected;Builder.LoadGfxObj(Replacement.PartId,Expected);
            const auto& Levels=Mesh->GetDistanceLevels();
            if(Mesh->GetNumSections()==0)
            {
                TestTrue(TEXT("Layering may hide a body part only with null geometry"),Expected.Polygons.Num()<=1);
                continue;
            }
            TestEqual(TEXT("Every composed part retains its own distance table"),Levels.Num(),Expected.DistanceLevels.Num());
            if(Levels.IsEmpty())continue;
            for(int32 I=0;I<Levels.Num();++I)
            {
                const float Distance=I==0?0:50.f+Levels[I-1].IdealDistance+.001f;
                const FVector Viewer=Mesh->GetComponentTransform().TransformPosition(FACEPosition::AceVectorToUnreal(FVector(Expected.SortCenter),100))
                    +FVector(Distance*100*Mesh->GetComponentScale().Z,0,0);
                const int32 Level=Mesh->GetDistanceLevel(Viewer);
                TestEqual(TEXT("World camera chooses authored level"),Level,ProceduralMeshDistanceLOD::Select(Levels,Distance));
            }
            for(int32 I=Levels[0].SectionCount;I<Mesh->GetNumSections();++I)
                TestFalse(TEXT("Distant meshes cannot change picking/collision"),Mesh->GetProcMeshSection(I)->bEnableCollision);
        }
        const uint64 Revision=App->GetAppearanceRevision();
        Object.bIsSelf=true;App->ApplyWorldObject(Object,100,false);
        auto* Chest=Cast<UProceduralMeshComponent>(App->GetPartMesh(9));
        const int32 Highest=Chest->GetDistanceLevels().IsEmpty()?INDEX_NONE:0;
        TestEqual(TEXT("Local player stays highest detail at any zoom"),Chest->GetDistanceLevel(FVector(100000,0,0)),Highest);
        Object.bIsSelf=false;App->ApplyWorldObject(Object,100,false);App->SetPreviewCapture(true);
        TestEqual(TEXT("Paperdoll uses highest detail"),Chest->GetDistanceLevel(FVector(100000,0,0)),Highest);
        App->SetPreviewCapture(false);
        TestEqual(TEXT("Changing viewer policy does not rebuild armor or reset animation"),App->GetAppearanceRevision(),Revision);
    }
    // Exercise the render proxy itself with the Celdon chest: compare its image
    // against a single-level reference mesh at identical camera coordinates.
    Object.SetupId=0x02000001;Object.Appearance=FACEObjDesc();Object.Appearance.PaletteBaseId=0x0400007E;
    Creation.ApplyClothing(0x10000188,Object.SetupId,CeldonColor,.5,Object.Appearance);
    TestTrue(TEXT("Render comparison includes real Celdon palette substitutions"),!Object.Appearance.SubPalettes.IsEmpty());
    App->ApplyWorldObject(Object,100,false);
    auto* Chest=Cast<UProceduralMeshComponent>(App->GetPartMesh(9));
    auto* Reference=NewObject<UProceduralMeshComponent>(Actor);Reference->RegisterComponent();
    Reference->SetWorldTransform(Chest->GetComponentTransform());
    auto* CaptureOwner=World->SpawnActor<AActor>();
    auto* Capture=NewObject<USceneCaptureComponent2D>(CaptureOwner);
    auto* Target=NewObject<UTextureRenderTarget2D>(Capture);Target->InitAutoFormat(384,384);
    Capture->TextureTarget=Target;Capture->CaptureSource=SCS_FinalColorLDR;
    Capture->ProjectionType=ECameraProjectionMode::Orthographic;Capture->OrthoWidth=90;
    Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->ShowFlags.SetEyeAdaptation(false);Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetFog(false);Capture->ShowFlags.SetAntiAliasing(false);Capture->ShowFlags.SetLighting(false);
    Capture->RegisterComponent();
    const uint64 Revision=App->GetAppearanceRevision();
    const auto Levels=Chest->GetDistanceLevels();
    FACEDatGfxObj Close;Builder.LoadGfxObj(0x0100120D,Close);
    const FVector Center=Chest->GetComponentTransform().TransformPosition(FACEPosition::AceVectorToUnreal(FVector(Close.SortCenter),100));
    auto Snap=[&](UProceduralMeshComponent* Mesh)
    {
        Capture->ClearShowOnlyComponents();Capture->ShowOnlyComponent(Mesh);
        if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
        World->SendAllEndOfFrameUpdates();Capture->CaptureScene();FlushRenderingCommands();
        TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);return Pixels;
    };
    TArray<FColor> FirstNear;
    // Cross the former close-range thresholds repeatedly, then the real retail
    // transitions. The proxy must select the same level as a single-mesh reference.
    for(const FIntPoint Fixture:{FIntPoint(300,0),FIntPoint(390,0),FIntPoint(410,0),FIntPoint(390,0),
        FIntPoint(900,0),FIntPoint(4000,0),FIntPoint(5300,0),FIntPoint(5900,3),
        FIntPoint(9000,4),FIntPoint(14000,5),FIntPoint(300,0)})
    {
        const FVector Eye=Center+FVector(.6,.8,0)*Fixture.X;
        Capture->SetWorldLocationAndRotation(Eye,(Center-Eye).Rotation());
        Reference->ClearAllMeshSections();
        const auto& L=Levels[Fixture.Y];
        for(int32 I=0;I<L.SectionCount;++I)
        {
            Reference->SetProcMeshSection(I,*Chest->GetProcMeshSection(L.FirstSection+I));
            Reference->SetMaterial(I,Chest->GetMaterial(L.FirstSection+I));
        }
        Reference->SetViewFacing(L.DrawMode,FACEPosition::AceVectorToUnreal(FVector(Close.SortCenter),100));
        const auto Actual=Snap(Chest),Expected=Snap(Reference);
        int32 Different=0,Lit=0;
        if(!TestEqual(TEXT("Both rendered images are complete"),Actual.Num(),384*384))return false;
        for(int32 I=0;I<Actual.Num();++I)
        {
            const auto A=Actual[I],B=Expected[I];
            Different+=FMath::Abs(int32(A.R)-B.R)>2 || FMath::Abs(int32(A.G)-B.G)>2 || FMath::Abs(int32(A.B)-B.B)>2;
            Lit+=FMath::Max3(A.R,A.G,A.B)>10;
        }
        TestEqual(FString::Printf(TEXT("Rendered Celdon at %.0fm matches only authored level %d"),Fixture.X/100.f,Fixture.Y),Different,0);
        if(Fixture.Y==0)
        {
            TestTrue(TEXT("Close-up armor is visible in rendered capture"),Lit>500);
            if(Fixture.X==300)
            {
                if(FirstNear.IsEmpty())FirstNear=Actual;
                else TestTrue(TEXT("Returning close restores identical detail and coloring"),Actual==FirstNear);
            }
        }
        if(Fixture.X==5900)
        {
            Chest->SetForceHighestDetail(true);const auto Self=Snap(Chest);Chest->SetForceHighestDetail(false);
            TestTrue(TEXT("Local player's rendered silhouette stays detailed at distance"),Self!=Actual);
        }
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(384,384,Actual,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Automation/ArmorDistance-%dcm.png"),Fixture.X)));
    }
    TestEqual(TEXT("Camera traversal causes no geometry/animation rebuilds"),App->GetAppearanceRevision(),Revision);
    return !HasAnyErrors();
}
#endif
