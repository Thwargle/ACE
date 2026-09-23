#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "Engine/GameInstance.h"
#include "Dat/ACEDatCursor.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "UObject/StrongObjectPtr.h"
#include "Dat/ACEPolygonMeshBuilder.h"
#include "Dat/ACEEnvCellTileCache.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "ShaderCompiler.h"
#include "RenderingThread.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEFoliageSamplingTest,"ACE.RetailParity.FoliageSampling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEFoliageSamplingTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
    TStrongObjectPtr<UACEDatSubsystem> Owner(NewObject<UACEDatSubsystem>(Instance.Get()));
    auto* Dat=Owner.Get();
    if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    Dat->GetOrCreateTexturedMaterial(0x08000708);
    // A shared image may be a clamped card on one face and tiled on another.
    // Keep these sections apart, including positive/negative polygon sides.
    TMap<uint16,FACEDatSWVertex> Vertices;
    for(uint16 I=0;I<3;++I) { FACEDatSWVertex V; V.Origin=FVector3f(I==1?1:0,I==2?1:0,0); V.UVs.Add(FVector2f(0,0)); Vertices.Add(I,V); }
    FACEDatPolygon Polygon; Polygon.VertexIds={0,1,2}; Polygon.SidesType=EACECullMode::Clockwise;
    Polygon.Stippling=EACEStipplingType::Positive; Polygon.PosSurface=0; Polygon.NegSurface=0;
    TArray<FACEBuiltMeshSection> Sections;
    ACEPolygonMeshBuilder::Append({{0,Polygon}},Vertices,{0x08000018},nullptr,0,nullptr,100,Sections);
    TestEqual(TEXT("Opposite sampler modes cannot merge into one section"),Sections.Num(),2);
    if(Sections.Num()==2)
    {
        TestTrue(TEXT("Positive stippling requests a repeating texture"),Sections[0].bWrapTexture);
        TestFalse(TEXT("Unflagged back side clamps its image"),Sections[1].bWrapTexture);
    }
    FACEBuiltEnvCellMesh Tile; Tile.EnvCellId=0x7D640100; Tile.Sections=Sections;
    const FString TilePath=FPaths::ProjectSavedDir()/TEXT("Automation/Foliage/SamplerCache.bin");
    TestTrue(TEXT("Addressing fixture writes to the cell cache"),ACEEnvCellTileCache::Save(TilePath,Tile,100,1));
    FACEBuiltEnvCellMesh Reloaded;
    TestTrue(TEXT("Addressing fixture reloads from the cell cache"),ACEEnvCellTileCache::Load(TilePath,Reloaded,100,1));
    if(Reloaded.Sections.Num()==2)
    {
        TestTrue(TEXT("Disk tiles preserve wrapped faces"),Reloaded.Sections[0].bWrapTexture);
        TestFalse(TEXT("Disk tiles preserve clamped faces"),Reloaded.Sections[1].bWrapTexture);
    }
    TSet<uint32> Models, Surfaces;
    for (uint32 X=0xC7;X<=0xC9;++X) for(uint32 Y=0xA7;Y<=0xA9;++Y)
    {
        const uint32 Block=(X<<24)|(Y<<16);
        TArray<FACEDatRegionSceneryItem> Scenery; Dat->CollectRegionScenery(Block,100,Scenery);
        for(const auto& Item:Scenery) Models.Add(Item.SetupId);
        FACEDatLandblockInfo Info;
        if(Dat->LoadLandblockInfo(Block,Info)) for(const auto& Item:Info.Objects) Models.Add(Item.Id);
    }
    int32 FalseBorderSamples=0, TiledFoliage=0;
    for(uint32 Id:Models)
    {
        const auto Mesh=Dat->GetOrBuildSetupMeshShared(Id,100);
        if(!Mesh) continue;
        for(const auto& Part:Mesh->Parts)
        {
            TArray<uint8> Blob; FACEDatGfxObj Gfx;
            if(!Dat->GetPortalDat()->ReadFile(Part.GfxObjId,Blob)) continue;
            FACEDatCursor Cur(Blob); if(!ACEDatUnpack::UnpackGfxObj(Cur,Gfx)) continue;
            for(const auto& Section:Part.Sections)
            {
                if(!Section.bClipMap || Surfaces.Contains(Section.SurfaceId)) continue;
                Surfaces.Add(Section.SurfaceId);
                FACEDatDecodedSurface Surface;
                if(!Dat->GetTextureResolver()->ResolveSurface(Section.SurfaceId,Surface)) continue;
                int32 Wrapped=0, Plain=0;
                for(const auto& Pair:Gfx.Polygons)
                {
                    const auto& P=Pair.Value;
                    if(Gfx.Surfaces.IsValidIndex(P.PosSurface) && Gfx.Surfaces[P.PosSurface]==Section.SurfaceId)
                    { if(EnumHasAnyFlags(P.Stippling,EACEStipplingType::Positive)) ++Wrapped; else ++Plain; }
                }
                TestEqual(TEXT("DAT foliage addressing reaches its mesh section"),Section.bWrapTexture,Wrapped>0);
                auto* Material=Dat->GetOrCreateTexturedMaterial(Section.SurfaceId,Section.bWrapTexture);
                UTexture* Image=nullptr;
                if(!TestTrue(TEXT("Foliage material binds the DAT texture"),Material && Material->GetTextureParameterValue(TEXT("ACETexture"),Image))) continue;
                auto* Texture=Cast<UTexture2D>(Image);
                if(!TestNotNull(TEXT("Foliage has a 2D texture"),Texture)) continue;
                TestEqual(TEXT("GPU sampler follows retail polygon addressing"),Texture->AddressX.GetValue(),Section.bWrapTexture?TA_Wrap:TA_Clamp);
                TestEqual(TEXT("Both sampler axes follow retail"),Texture->AddressY.GetValue(),Section.bWrapTexture?TA_Wrap:TA_Clamp);
                if(Section.bWrapTexture) ++TiledFoliage;
                else if(Surface.bHasPixels)
                {
                    // At UV=0 or 1, a wrap sampler averages opposite edges. These
                    // authored empty edge texels become visible with the old sampler.
                    const float Clip=Material->GetMaterial()->OpacityMaskClipValue*255.f;
                    auto CheckEdge=[&](uint8 A,uint8 B)
                    {
                        if(A==0 && (A+B)*.5f>=Clip) ++FalseBorderSamples;
                        if(B==0 && (A+B)*.5f>=Clip) ++FalseBorderSamples;
                    };
                    for(int32 X=0;X<Surface.Width;++X) CheckEdge(Surface.Pixels[X].A,Surface.Pixels[(Surface.Height-1)*Surface.Width+X].A);
                    for(int32 Y=0;Y<Surface.Height;++Y) CheckEdge(Surface.Pixels[Y*Surface.Width].A,Surface.Pixels[Y*Surface.Width+Surface.Width-1].A);
                }
                FVector2D Lo(1e9,1e9),Hi(-1e9,-1e9);
                for(const auto& UV:Section.UVs){Lo.X=FMath::Min(Lo.X,UV.X);Lo.Y=FMath::Min(Lo.Y,UV.Y);Hi.X=FMath::Max(Hi.X,UV.X);Hi.Y=FMath::Max(Hi.Y,UV.Y);}
                AddInfo(FString::Printf(TEXT("Foliage model=%08X gfx=%08X surface=%08X size=%dx%d alphaRef=%.3f wrap=%d clamp=%d UV=%s..%s"),
                    Id,Part.GfxObjId,Section.SurfaceId,Surface.Width,Surface.Height,Surface.AlphaTestReference,Wrapped,Plain,*Lo.ToString(),*Hi.ToString()));
                if(Surface.bHasPixels)
                {
                    TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Surface.Width,Surface.Height,Surface.Pixels,PNG);
                    FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Automation/Foliage/Surface_%08X.png"),Section.SurfaceId)));
                }
            }
        }
    }
    TestTrue(TEXT("Reported landscape contains cutout foliage"),Surfaces.Num()>0);
    TestTrue(TEXT("Real tree textures reproduce false opaque lines with the old wrap sampler"),FalseBorderSamples>100);
    TestTrue(TEXT("Fixture also covers authored repeating foliage"),TiledFoliage>0);
    AddInfo(FString::Printf(TEXT("Retail clamp removes %d false edge samples across %d foliage surfaces; %d intentionally tiled surface(s) retained"),FalseBorderSamples,Surfaces.Num(),TiledFoliage));
    // Both states can coexist without the last requested sampler changing trees
    // that already reference the same surface elsewhere in the world.
    auto* Clamped=Dat->GetTextureResolver()->GetOrCreateUTexture(0x08000018,Dat,false);
    auto* Wrapped=Dat->GetTextureResolver()->GetOrCreateUTexture(0x08000018,Dat,true);
    TestTrue(TEXT("Sampler variants do not overwrite each other"),Clamped && Wrapped && Clamped!=Wrapped && Clamped->AddressX==TA_Clamp && Wrapped->AddressX==TA_Wrap);
    if(Clamped && Wrapped)
    {
        TestEqual(TEXT("Edge correction keeps full DAT detail"),Clamped->GetSizeX(),Wrapped->GetSizeX());
        TestEqual(TEXT("Edge correction retains the anti-shimmer mip chain"),Clamped->GetNumMips(),Wrapped->GetNumMips());
    }
    if(FApp::CanEverRender())
    {
        const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
        auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
        auto* Actor=World->SpawnActor<AActor>();
        auto* Tree=NewObject<UProceduralMeshComponent>(Actor);Tree->RegisterComponent();
        auto* Backdrop=NewObject<UProceduralMeshComponent>(Actor);Backdrop->RegisterComponent();
        TArray<FColor> Blue; Blue.Add(FColor(85,153,218));
        auto* SkyTex=FACEDatTextureResolver::CreateTransientRgbaWithMips(1,1,Blue,false);
        auto* Sky=UMaterialInstanceDynamic::Create(Dat->EnsureAceUnlitTexturedMaterialBase(),Actor);
        Sky->SetTextureParameterValue(TEXT("ACETexture"),SkyTex); Sky->SetScalarParameterValue(TEXT("FogAmount"),0);
        Backdrop->CreateMeshSection_LinearColor(0,{{3000,-10000,-10000},{3000,10000,-10000},{3000,10000,10000},{3000,-10000,10000}},
            {0,1,2,0,2,3},{},{{0,0},{1,0},{1,1},{0,1}},{},{},false);
        Backdrop->SetMaterial(0,Sky);
        auto* Target=NewObject<UTextureRenderTarget2D>(Actor); Target->InitCustomFormat(1024,1024,PF_B8G8R8A8,false);
        auto* Capture=NewObject<USceneCaptureComponent2D>(Actor);Capture->TextureTarget=Target;
        Capture->CaptureSource=SCS_FinalColorLDR; Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
        Capture->ShowFlags.SetEyeAdaptation(false);Capture->ShowFlags.SetAtmosphere(false);Capture->ShowFlags.SetFog(false);
        Capture->ShowFlags.SetAntiAliasing(false);Capture->ShowFlags.SetBloom(false);Capture->RegisterComponent();
        for(uint32 Model:{0x0200062Bu,0x020002F2u})
        {
            const auto Mesh=Dat->GetOrBuildSetupMeshShared(Model,100); if(!Mesh) continue;
            Tree->ClearAllMeshSections();FBox Bounds(ForceInit);TArray<FACEBuiltMeshSection> Draw;
            for(const auto& Part:Mesh->Parts) for(const auto& Source:Part.Sections)
            {
                if(Source.bCollisionOnly || Source.IsEmpty()) continue;
                auto Section=Source;
                for(auto& V:Section.Vertices){V=Part.BindTransform.TransformPosition(V);Bounds+=V;}
                Tree->CreateMeshSection_LinearColor(Draw.Num(),Section.Vertices,Section.Triangles,{},Section.UVs,{},{},false);
                Draw.Add(MoveTemp(Section));
            }
            const FVector Center=Bounds.GetCenter(),Eye=Center+FVector(-Bounds.GetSize().GetMax()*.9,0,0);
            Capture->SetWorldLocationAndRotation(Eye,(Center-Eye).Rotation());Capture->FOVAngle=70;
            for(bool OldWrap:{true,false})
            {
                for(int32 I=0;I<Draw.Num();++I)
                {
                    auto* M=Cast<UMaterialInstanceDynamic>(Dat->GetOrCreateTexturedMaterial(Draw[I].SurfaceId,OldWrap || Draw[I].bWrapTexture));
                    if(M) M->SetScalarParameterValue(TEXT("FogAmount"),0);
                    Tree->SetMaterial(I,M);
                }
                if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
                World->SendAllEndOfFrameUpdates();Capture->CaptureScene();FlushRenderingCommands();
                TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
                TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(1024,1024,Pixels,PNG);
                FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Automation/Foliage/Tree_%08X_%s.png"),Model,OldWrap?TEXT("Before"):TEXT("RetailClamp"))));
            }
        }
        World->DestroyWorld(false);
    }
    return true;
}
#endif
