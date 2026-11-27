#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEVisibleObjectPick.h"
#include "Mods/ACEPluginSight.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "Components/BoxComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/LightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ShaderCompiler.h"
#include "Materials/MaterialInterface.h"
#include "AssetCompilingManager.h"
#include "MaterialShaderPrecompileMode.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FACEWaterTargetTest, "ACE.RetailParity.SubmergedMoar",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FACEWaterTargetTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    Names.Add(TEXT("Coast")); Commands.Add(TEXT("Coast"));
    Names.Add(TEXT("Yaraq")); Commands.Add(TEXT("Yaraq"));
}

bool FACEWaterTargetTest::RunTest(const FString& Place)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* GI = NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); Context.OwningGameInstance=GI;
    GI->OnWorldChanged(nullptr,World); GI->Init();
    ON_SCOPE_EXIT
    {
        GI->Shutdown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        CollectGarbage(RF_NoFlags);
        FlushRenderingCommands();
    };
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    if (!TestTrue(TEXT("Retail DAT opens"), Dat && Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    const bool bYaraq = Place == TEXT("Yaraq");
    const uint32 Block = bYaraq ? 0x7C630000u : 0xDC560000u;
    const auto* Built = Dat->GetOrBuildLandblockMesh(Block, 100);
    if (!TestNotNull(TEXT("Wading landblock"), Built)) return false;
    auto* Land = World->SpawnActor<AACELandblockActor>();
    auto* Mesh = Land->TerrainMesh.Get();
    const FVector Origin = FACEPosition::AceVectorToUnreal(FVector((Block>>24)*192,((Block>>16)&255)*192,0),100);
    Land->SetActorLocation(Origin); Mesh->bUseAsyncCooking = false;
    auto* LandShader=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/ACE/RuntimeMaterials/M_ACELandLit_v44.M_ACELandLit_v44"));
    if (!TestNotNull(TEXT("Packaged terrain material is available"),LandShader)) return false;
    auto* PreviewParent=Dat->EnsureAceLandMaterialBase();
    Dat->LandMaterialBase=LandShader;
    // A cold editor shader cache can substitute WorldGridMaterial for a loaded
    // asset. Compile the on-disk parent before comparing its water depth on GPU.
    LandShader->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
    LandShader->EnsureIsComplete();
    for (const auto& Sec : Built->Sections)
    {
        const int32 Index = Mesh->GetNumSections();
        Mesh->CreateMeshSection(Index, Sec.Vertices, Sec.Triangles, Sec.Normals, Sec.UVs, {}, {}, true);
        auto* MID=Cast<UMaterialInstanceDynamic>(Dat->GetOrCreateLandMaterial(Sec.PCode,Sec.BakedPixels,Sec.BakeWidth,Sec.BakeHeight,Sec.SharedBake));
        if (!TestNotNull(TEXT("Retail terrain material"),MID)) return false;
        TestEqual(TEXT("Packaged terrain parent used for this tile"),MID->Parent.Get(),LandShader);
        Mesh->SetMaterial(Index,MID);
    }
    FVector Feet;
    int32 Cell = INDEX_NONE;
    for (int32 I=0; I<64 && Cell==INDEX_NONE; ++I)
    {
        if (Built->CellWaterType[I] != 2) continue;
        Feet = Origin + FVector(-((I/8)*24+12)*100,((I%8)*24+12)*100,0);
        float Z; FVector N;
        if (Dat->SampleOutdoorGroundZ(Feet.X,Feet.Y,100,Z,&N) && N.Z>.999)
        { Feet.Z=Z; Cell=I; }
    }
    if (!TestTrue(TEXT("Flat, 90 cm deep water in a coastal block"), Cell!=INDEX_NONE && Built->BlockWaterType==1)) return false;

    FACEWorldObject Obj; Obj.Guid=0x7100a5a0; Obj.bHasPosition=true; Obj.ItemType=ACEItemType::Creature;
    Obj.Name=TEXT("Pallid Moar"); Obj.WeenieClassId=34869; Obj.SetupId=0x0200149F; Obj.MotionTableId=0x09000192;
    Obj.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;
    float Step=0,Height=0,Radius=0; uint32 Anim=0;
    if (!TestTrue(TEXT("Moar has retail physics dimensions"),Dat->TryGetSetupPhysics(Obj.SetupId,Step,Height,Radius,Anim) && Height>0)) return false;
    Obj.Scale=1.f; // stock Pallid Moar
    Obj.Position.CellId=Block | uint32(Cell+1); Obj.Position.SetLocationFromUnreal(Feet);
    auto* Body=World->SpawnActor<AACEWorldEntityActor>(); Body->InitializeFromObject(Obj,100,true);
    Body->SetActorLocation(Feet);
    FBox Bounds(ForceInit);
    if (!TestTrue(TEXT("Actual Moar drawing mesh is loaded"),Body->Appearance->GetVisualWorldBounds(Bounds))) return false;
    const FVector Eye=Feet+FVector(0,-240,180), Aim=Feet+FVector(0,0,Height*Obj.Scale*100*2/3);
    const FVector Center=Bounds.GetCenter();
    TestTrue(TEXT("Aim and visible mesh are below the surface"),Aim.Z<Feet.Z+90 && Bounds.Max.Z<Feet.Z+90);
    FACEPluginSightQuery Sight(*World,nullptr);
    TestFalse(TEXT("Unrefined collision hits the original water sheet"),Sight.Clear(Eye,Aim,15));
    TestTrue(TEXT("Combat corridor reaches the submerged aim point"),Sight.Clear(Eye,Aim,15,true));
    FACEPosition Source=Obj.Position;
    Source.SetLocationFromUnreal(Feet+FVector(0,-240,0));
    GI->GetSubsystem<UACEClientSubsystem>()->GetSession()->SetLocalPosition(Source);
    auto* Host=GI->GetSubsystem<UACEPluginSubsystem>();
    TestTrue(TEXT("The live plugin attack and cast gate accepts the submerged Moar"),Host->HasClearCombatSight(Obj));
    TestFalse(TEXT("Solid floor still blocks a shot below the bed"),Sight.Clear(Eye,Feet-FVector(0,0,10),15,true));
    TestTrue(TEXT("A clear underwater sight line remains usable"),Sight.Clear(Feet+FVector(0,-80,30),Aim,10,true));
    TestFalse(TEXT("An underwater ray cannot continue into solid bed"),Sight.Clear(Feet+FVector(0,-80,30),Feet-FVector(0,0,10),10,true));
    // Start just above the middle of the wet cell. The first heightfield hit is
    // water; a second, dry cell in the very same mesh must still stop the ray.
    bool CheckedBank=false;
    const FVector WaterStart=Feet+FVector(100,0,92);
    for (int32 I=0; I<64 && !CheckedBank; ++I)
    {
        if (Built->CellWaterType[I]!=0) continue;
        FVector Bank=Origin+FVector(-((I/8)*24+12)*100,((I%8)*24+12)*100,0);
        float Ground=0;
        if (!Dat->SampleOutdoorGroundZ(Bank.X,Bank.Y,100,Ground)) continue;
        Bank.Z=FMath::Min<double>(Ground-20,Feet.Z+35);
        FHitResult Hit;
        if (!World->LineTraceSingleByChannel(Hit,WaterStart,Bank,ECC_Camera) || Hit.GetComponent()!=Mesh) continue;
        double SolidDistance;
        if (!ACEWaterSight::RefineTerrainHit(*World,Hit,WaterStart,Bank,0,SolidDistance)) continue;
        CheckedBank=true;
        TestTrue(TEXT("A dry bank beyond the water on the same component still occludes"),SolidDistance<FVector::Distance(WaterStart,Bank));
        TestFalse(TEXT("Plugin cannot shoot through the bank"),Sight.Clear(WaterStart,Bank,0,true));
    }
    TestTrue(TEXT("Exercised a water intersection followed by a dry bank"),CheckedBank);
    const FVector End=Center+(Center-Eye).GetSafeNormal()*160;
    TestEqual(TEXT("Manual desktop/VR pick reaches the submerged creature"),ACEVisibleObjectPick::Trace(*World,Eye,End,nullptr),static_cast<AActor*>(Body));

    auto* Wall=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box); Box->SetBoxExtent(FVector(100,8,60));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent(); Wall->SetActorLocation(FMath::Lerp(Eye,Aim,.75));
    TestFalse(TEXT("A submerged obstruction still blocks combat"),Sight.Clear(Eye,Aim,15,true));
    TestFalse(TEXT("The plugin dispatch gate still rejects an obstructed Moar"),Host->HasClearCombatSight(Obj));
    TestNull(TEXT("A submerged obstruction still blocks selection"),ACEVisibleObjectPick::Trace(*World,Eye,End,nullptr));
    Wall->Destroy();

    if (GUsingNullRHI) return true;
    auto* Sun=World->SpawnActor<ADirectionalLight>(); Sun->SetActorRotation(FRotator(-55,45,0));
    Sun->GetLightComponent()->SetIntensity(8.f);
    auto* Capture=NewObject<USceneCaptureComponent2D>(World->SpawnActor<AActor>());
    auto* RT=NewObject<UTextureRenderTarget2D>(Capture); RT->TargetGamma=2.2f;
    RT->InitCustomFormat(256,192,PF_B8G8R8A8,true); RT->UpdateResourceImmediate(true);
    Capture->TextureTarget=RT; Capture->CaptureSource=SCS_FinalColorLDR; Capture->FOVAngle=45;
    Capture->bCaptureEveryFrame=false; Capture->bCaptureOnMovement=false;
    Capture->ShowFlags.SetEyeAdaptation(false); Capture->ShowFlags.SetAntiAliasing(false); Capture->ShowFlags.SetFog(false);
    Capture->RegisterComponent(); Capture->SetWorldLocationAndRotation(Eye,(Center-Eye).Rotation());
    World->SendAllEndOfFrameUpdates();
    UMaterialInterface::SubmitRemainingJobsForWorld(World);
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    const auto Frame=[&](AActor* HiddenActor=nullptr, bool bHideTerrain=false)
    {
        Capture->HiddenActors.Reset();
        if (HiddenActor) Capture->HiddenActors.Add(HiddenActor);
        if (bHideTerrain) Capture->HiddenActors.Add(Land);
        World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
        TArray<FColor> Pixels; RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels); return Pixels;
    };
    // This fixture renders within one automation tick. Let UE submit the
    // registered meshes before taking either of the two reference frames.
    auto* FirstPart=Cast<UProceduralMeshComponent>(Body->Appearance->GetPartMesh(0));
    if (!TestNotNull(TEXT("Moar has a renderable first part"),FirstPart)) return false;
    const double Deadline=FPlatformTime::Seconds()+5;
    do
    {
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        World->SendAllEndOfFrameUpdates();
        FlushRenderingCommands();
        if (Mesh->GetSceneProxy() && FirstPart->GetSceneProxy() && !Mesh->IsPSOPrecaching() && !FirstPart->IsPSOPrecaching()) break;
        FPlatformProcess::SleepNoStats(.001f);
    } while (FPlatformTime::Seconds()<Deadline);
    Frame();
    const auto Difference=[](const TArray<FColor>& A,const TArray<FColor>& B)
    {
        int32 Changed=0;
        for (int32 I=0; I<A.Num() && I<B.Num(); ++I)
            if (FMath::Abs(int32(A[I].R)-B[I].R)+FMath::Abs(int32(A[I].G)-B[I].G)+FMath::Abs(int32(A[I].B)-B[I].B)>12) ++Changed;
        return Changed;
    };
    const auto EmptyWater = Frame(Body);
    const auto Submerged = Frame();
    TestTrue(TEXT("Water occludes the fully submerged attackable Moar"), Difference(Submerged,EmptyWater)<5);
    Body->SetSelectionHighlight(1.f);
    TestTrue(TEXT("Selecting the Moar keeps its mesh below the waterline"), Difference(Frame(),EmptyWater)<5);
    const int32 UncoveredPixels = Difference(Frame(nullptr,true),Frame(Body,true));
    TestTrue(TEXT("Removing the water from the capture reveals the same selected Moar"), UncoveredPixels>25);

    // Move the same body and camera together. Only the intersection with the
    // stationary water surface changes, so a partially exposed body is measurable.
    const FVector Rise(0,0,Feet.Z+90-Center.Z);
    Body->SetActorLocation(Feet+Rise);
    Capture->SetWorldLocation(Eye+Rise);
    const auto PartlySubmerged = Frame();
    const int32 ExposedPixels = Difference(PartlySubmerged,Frame(Body));
    TestTrue(TEXT("Only the part of the Moar above water is visible"), ExposedPixels>5 && ExposedPixels<UncoveredPixels);
    AddInfo(FString::Printf(TEXT("%s: fully submerged %d, partially submerged %d, unobstructed %d creature pixels"),
        *Place,Difference(Submerged,EmptyWater),ExposedPixels,UncoveredPixels));
    Body->SetActorLocation(Feet);
    // Recreate the reported 45 cm water beside Yaraq pier. The second fixture
    // keeps both objects on the 90 cm Moar bed for deep-water coverage.
    FVector PierFeet=Feet;
    if (bYaraq)
    {
        PierFeet=Origin+FVector(-10080,14160,0);
        float Bed=0;
        if (!TestTrue(TEXT("The reported pier position has a floor"),Dat->SampleOutdoorGroundZ(PierFeet.X,PierFeet.Y,100,Bed))) return false;
        PierFeet.Z=Bed;
        TestEqual(TEXT("Reported pier has 45 cm water"),Dat->GetOutdoorWaterDepthCm(PierFeet.X,PierFeet.Y,100),45.f);
    }
    const FVector PierEye=Eye+(PierFeet-Feet);
    auto Hole=Obj; Hole.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck; Hole.Guid++; Hole.Name=TEXT("Fishing Hole"); Hole.WeenieClassId=22257;
    Hole.SetupId=0x02000F0D; Hole.MotionTableId=0x09000122; Hole.PhysicsState=0x0020041C;
    Hole.Position.SetLocationFromUnreal(PierFeet);
    auto* Fishing=World->SpawnActor<AACEWorldEntityActor>(); Fishing->InitializeFromObject(Hole,100,true); Fishing->SetActorLocation(PierFeet);
    Body->SetActorHiddenInGame(true);
    FBox HoleBounds(ForceInit);
    if (!TestTrue(TEXT("Stock fishing hole has its authored model"),Fishing->Appearance->GetVisualWorldBounds(HoleBounds))) return false;
    const FVector HoleEnd=HoleBounds.GetCenter()+(HoleBounds.GetCenter()-PierEye).GetSafeNormal()*160;
    TestNull(TEXT("Noninteractive submerged scenery cannot intercept the pointer"),ACEVisibleObjectPick::Trace(*World,PierEye,HoleEnd,nullptr));
    Hole.ItemUseable=0x20; // Stock Fishing Hole is a remote-use creature.
    Fishing->InitializeFromObject(Hole,100,true);
    TestTrue(TEXT("Rod targeting can pick the submerged Fishing Hole"),ACEVisibleObjectPick::Trace(*World,PierEye,HoleEnd,nullptr)==Fishing);
    if (bYaraq)
    {
        const FVector DockEye=Origin+FVector(-9464.9928,17629.6532,86.2+165);
        const FVector DockEnd=HoleBounds.GetCenter()+(HoleBounds.GetCenter()-DockEye).GetSafeNormal()*160;
        TestTrue(TEXT("The Fishing Hole is selectable from the reported pier position"),ACEVisibleObjectPick::Trace(*World,DockEye,DockEnd,nullptr)==Fishing);
    }
    Capture->SetWorldLocationAndRotation(PierEye,(HoleBounds.GetCenter()-PierEye).Rotation());
    const auto FishShown=Frame();
    TestTrue(TEXT("Water hides the fishing hole mesh"), Difference(FishShown,Frame(Fishing))<5);
    TestTrue(TEXT("Fishing hole control is in frame under the water"), Difference(Frame(nullptr,true),Frame(Fishing,true))>5);
    Fishing->Destroy();
    auto Player=Obj; Player.Position=Hole.Position; Player.Guid+=2; Player.SetupId=0x02000001; Player.MotionTableId=0x09000001;
    Player.bIsPlayer=true; Player.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;
    auto* Wader=World->SpawnActor<AACEWorldEntityActor>(); Wader->InitializeFromObject(Player,100,true); Wader->SetActorLocation(PierFeet);
    Capture->SetWorldRotation((PierFeet+FVector(0,0,90)-PierEye).Rotation());
    const auto Wading=Frame();
    const int32 WadingPixels=Difference(Wading,Frame(Wader));
    const int32 FullPlayerPixels=Difference(Frame(nullptr,true),Frame(Wader,true));
    TestTrue(TEXT("The wading player's upper body is visible and the submerged portion is occluded"),
        WadingPixels>100 && WadingPixels<FullPlayerPixels);
    Wader->Destroy();
    Capture->SetWorldLocationAndRotation(Eye,(Center-Eye).Rotation());
    Body->SetActorHiddenInGame(false);
    Dat->LandMaterialBase=PreviewParent;
    Dat->LandMaterialCache.Reset();
    for (int32 I=0; I<Built->Sections.Num(); ++I)
    {
        const auto& Sec=Built->Sections[I];
        Mesh->SetMaterial(I,Dat->GetOrCreateLandMaterial(Sec.PCode,Sec.BakedPixels,Sec.BakeWidth,Sec.BakeHeight,Sec.SharedBake));
    }
    TestTrue(TEXT("The editor material also occludes the selected submerged Moar"), Difference(Frame(),Frame(Body))<5);
    TestTrue(TEXT("The Moar is still in frame after switching materials"), Difference(Frame(nullptr,true),Frame(Body,true))>25);
    FString Report;
    if (FParse::Value(FCommandLine::Get(),TEXT("ReportExportPath="),Report) && Submerged.Num()==256*192 && PartlySubmerged.Num()==Submerged.Num() && Wading.Num()==Submerged.Num())
    {
        TArray64<uint8> WadingPNG; FImageUtils::PNGCompressImageArray(256,192,Wading,WadingPNG);
        IFileManager::Get().MakeDirectory(*Report,true);
        FFileHelper::SaveArrayToFile(WadingPNG,*(Report/(Place+TEXT("-Wading.png"))));
        TArray<FColor> Pair;
        for (int32 Y=0; Y<192; ++Y)
        {
            Pair.Append(Submerged.GetData()+Y*256,256); Pair.Append(PartlySubmerged.GetData()+Y*256,256);
        }
        IFileManager::Get().MakeDirectory(*Report,true);
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(512,192,Pair,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(Report/(Place+TEXT("-SubmergedMoar.png"))));
    }
    return true;
}
#endif
