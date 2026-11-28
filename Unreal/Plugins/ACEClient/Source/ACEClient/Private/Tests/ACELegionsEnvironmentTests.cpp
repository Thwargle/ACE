#if WITH_DEV_AUTOMATION_TESTS
#include "Sky/ACELegionsEnvironment.h"
#include "Sky/ACELegionsTreeFit.h"
#include "Personal/PersonalFarSceneryPlacement.h"
#include "Personal/PersonalFarTerrainBake.h"
#include "ACEDatSubsystem.h"
#include "ACELandblockActor.h"
#include "ACEEnvCellActor.h"
#include "ACESkyDomeActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ACETerrainPresenterComponent.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "Sky/ACELegionsGroundCover.h"
#include "ProceduralMeshComponent.h"
#include "ACERuntimeOptions.h"
#include "UI/ACEVideoSettingsWidget.h"
#include "Components/CheckBox.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "Dat/ACEDatDatabase.h"
#include "Dat/ACEPCodeTileCache.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "RenderingThread.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/Crc.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELegionsTreeFitTest, "ACE.Graphics.LegionsArtwork.TreeFit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACELegionsTreeFitTest::RunTest(const FString&)
{
	const FVector Tree[] = {{-1,-1,0},{1,-1,0},{0,0,4}, {1,1,0},{-1,1,0},{0,0,4},
		{-2,-1,8},{5,-2,9},{4,2,10}};
	const FQuat Turn(FVector::UpVector,UE_HALF_PI);
	const FVector Placement(17,4,0);
	auto Profile = [&](auto Transform)
	{
		FBox Bounds(ForceInit);
		for (const FVector& P : Tree) Bounds+=Transform(P);
		return ACELegionsSky::FTreeSilhouette(Bounds,[&](auto Visit)
		{
			for (int32 I=0; I<UE_ARRAY_COUNT(Tree); I+=3)
				Visit(Transform(Tree[I]),Transform(Tree[I+1]),Transform(Tree[I+2]));
		});
	};
	const auto Source=Profile([](const FVector& P){return P;});
	const auto Target=Profile([&](const FVector& P){return Placement+Turn.RotateVector(P)*3.;});
	TestTrue(TEXT("AC1 trunk offset survives scale and rotation"),Target.Trunk.Equals(FVector2D(Placement),.001));
	TestTrue(TEXT("Asymmetric crown recovers its authored quarter turn"),Target.RotationFrom(Source).Equals(Turn,.001));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELegionsEnvironmentTest, "ACE.Graphics.LegionsArtwork.Import",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACELegionsEnvironmentTest::RunTest(const FString&)
{
	using namespace ACELegionsSky;
	FString Source;
	if (!FParse::Value(FCommandLine::Get(), TEXT("AC2SkyTestDat="), Source))
	{
		AddInfo(TEXT("Supply -AC2SkyTestDat to verify the local Legions records."));
		return true;
	}
	const FString Pack = FPaths::ProjectSavedDir() / TEXT("Automation/LegionsArtwork/Legions.sky");
	FString Error;
	if (!Import(Source, Pack, Error)) { AddError(Error); return false; }
	FEnvironment Env;
	uint64 Keys[4]{};
	for (uint8 Mask = 0; Mask <= AllEnvironment; ++Mask)
	{
		if (!TestTrue(TEXT("Read selected artwork from the imported pack"), LoadEnvironment(Pack, Mask, Env))) return false;
		TestEqual(TEXT("Authored detail follows grass and ground"), !Env.GroundDetail.Mips.IsEmpty(), bool(Mask & (Grass | Ground)));
		const auto* SandLevels = Env.Surfaces.Find(0x40000256);
		TestEqual(TEXT("Sand follows ground when the full-size source is installed"), Env.Terrain(10) != nullptr,
			bool(Mask & Ground) && SandLevels && !SandLevels->IsEmpty() && (*SandLevels)[0].Width >= 512);
		TestEqual(TEXT("Grassland follows grass"), Env.Terrain(1) != nullptr, bool(Mask & Grass));
		TestEqual(TEXT("Boulder stone is independent of trees and terrain"), Env.BoulderTexture(0x020005c9), (Mask & Boulders) ? 0x400005beu : 0u);
		TestEqual(TEXT("Only selected scenery textures are decoded"), !Env.SceneryTextures.IsEmpty(), bool(Mask & (Trees | Boulders)));
		for (uint32 Id : {0x020005c9u,0x020005ceu,0x020005d0u,0x020006cfu,0x020014b5u})
			TestEqual(TEXT("Each selected boulder resolves its native texture"), Env.SceneryTextures.Contains(Env.BoulderTexture(Id)), bool(Mask & Boulders));
		TestEqual(TEXT("Mixed fern and rock setup retains its own material"), Env.BoulderTexture(0x02001065), 0u);
		TestEqual(TEXT("Scenery cache distinguishes all four selections"), uint8(Env.SceneryCacheKey()), uint8(Mask & (Trees | Boulders)));
		for (uint32 Id : {0x02000322u,0x02000257u,0x020003eau})
			TestEqual(TEXT("Yaraq tree has a loaded counterpart only with trees enabled"),Env.ReplacesTree(Id),bool(Mask & Trees));
		for (uint32 Id : {0x02000247u,0x0200032cu,0x020005ceu})
			TestFalse(TEXT("Cacti and rocks retain their original art"),Env.ReplacesTree(Id));
		TestEqual(TEXT("Small cut stump follows trees"), Env.ReplacesTree(0x020003df), bool(Mask & Trees));
		TestFalse(TEXT("Large cut stump retains its AC1 shape"), Env.ReplacesTree(0x020003de));
		TestEqual(TEXT("Tree mapping follows trees"), Env.ReplacesTree(0x02000258), bool(Mask & Trees));
		TestFalse(TEXT("Unmapped scenery retains its setup"), Env.ReplacesTree(0x02001063));
		TestFalse(TEXT("Water retains AC1 artwork"), Env.Terrain(16) != nullptr);
		if (Mask < Trees) Keys[Mask] = Env.TerrainCacheKey(123);
		else TestEqual(TEXT("Tree selection leaves terrain cache reusable"), Env.TerrainCacheKey(123), Keys[Mask & (Grass | Ground)]);
	}
	TestEqual(TEXT("Classic landscape keeps the existing disk cache"), Keys[0], uint64(123));
	TSet<uint64> Unique;
	for (uint64 Key : Keys) Unique.Add(Key);
	TestEqual(TEXT("Each terrain combination has a distinct cache"), Unique.Num(), 4);
	TestTrue(TEXT("Supported Sawato tree"), Env.ReplacesTree(0x020003ee));
	const auto* LegacyTree = Env.TreeModels.Find(0x060001ff);
	if (!LegacyTree || LegacyTree->Sections.Num() != 2) return false;
	TestEqual(TEXT("Native canopy triangles"), LegacyTree->Sections[0].Indices.Num()/3, 148);
	TestEqual(TEXT("Native trunk triangles"), LegacyTree->Sections[1].Indices.Num()/3, 70);
	if (!TestEqual(TEXT("Keep two authored distance levels"), LegacyTree->DistanceLevels.Num(), 2)) return false;
	TestEqual(TEXT("Use native distant canopy"), LegacyTree->DistanceLevels.Last()[0].Indices.Num()/3, 38);
	TestEqual(TEXT("Use native distant trunk"), LegacyTree->DistanceLevels.Last()[1].Indices.Num()/3, 4);

	TestTrue(TEXT("Common evergreen gets the native conifer"),Env.FindTree(0x02000258) == Env.FindTree(0x02000257) && Env.FindTree(0x02000258) != LegacyTree);
	for (uint32 Id : {0x020003a2u,0x020004b1u,0x0200062bu,0x0200129bu,0x020012feu})
		TestTrue(TEXT("Dereth biome tree mapped"), Env.ReplacesTree(Id));
	for (uint32 Id : {0x020002f3u,0x020002f4u,0x020002f5u,0x02001003u,0x0200035fu,0x020004beu,0x02001c39u,0x02000240u,0x02000246u,0x020002f1u,0x020002f2u,0x020002f6u,0x02001c31u,0x02001c34u,0x010014bbu,0x020004b2u,0x020004bbu,0x020004bcu,0x020004c7u,0x02000ab6u,0x02000badu})
		TestFalse(TEXT("Groves, low-growing scrub and scripted trees retain original art"), Env.ReplacesTree(Id));
	AddInfo(FString::Printf(TEXT("Imported ground: Yaraq grass %d px, sand %d px, shared 3 m detail %d px"),
		Env.Terrain(1) ? Env.Terrain(1)->Width : 0, Env.Terrain(10) ? Env.Terrain(10)->Width : 0, Env.GroundDetail.Width));
	for (const auto& Match : {TPair<uint32,double>(0x020007da,14.2),TPair<uint32,double>(0x02000a35,26.8),TPair<uint32,double>(0x020003ef,25.7)})
	{
		const FTree* Tree=Env.FindTree(Match.Key);
		TestTrue(TEXT("Counterpart is close to the AC1 tree's authored height before fitting"),
			Tree && FMath::IsNearlyEqual(Tree->Bounds.GetSize().Z,Match.Value,Match.Value*.25));
	}
	const auto* Grassland = Env.Terrain(1);
	const auto* Sand = Env.Terrain(10);
	FACEDatDatabase Portal;
	FString RetailDir = TEXT("C:/Turbine/Asheron's Call");
	FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),RetailDir);
	if (Portal.Open(RetailDir / TEXT("client_portal.dat")))
	{
		FACEDatTextureResolver Textures(&Portal,nullptr);
		auto Art = MakeShared<FEnvironment,ESPMode::ThreadSafe>(Env);
		FACELandblockMeshBuilder Builder(&Portal,nullptr,&Textures,nullptr,Art);
		if (!TestTrue(TEXT("Read AC1 terrain blend table"),Builder.EnsureTexMerge())) return false;
		const uint32 RoadCode=Builder.GetPalCode(1,1,0,0,1,1,1,1);
		TArray<FColor> RoadPixels;
		int32 RoadW=0,RoadH=0;
		if (!TestTrue(TEXT("Bake a road across Legions sand"),Builder.BakePCode(RoadCode,RoadPixels,RoadW,RoadH,Env.TerrainTileCells(RoadCode)))) return false;
		TestTrue(TEXT("Road pixels exclude terrain detail"),RoadPixels.ContainsByPredicate([](const FColor& C){return C.A==0;}));
		TestTrue(TEXT("Surrounding sand retains terrain detail"),RoadPixels.ContainsByPredicate([](const FColor& C){return C.A==255;}));
		TestTrue(TEXT("Detail blends over the road shoulder"),RoadPixels.ContainsByPredicate([](const FColor& C){return C.A>0 && C.A<255;}));
		const uint32 ComplexRoad = Builder.GetPalCode(1,1,1,0,1,3,2,16);
		auto Prepare = [&](FACELandblockMeshBuilder& SourceBuilder, uint32 Code)
		{
			FACEBuiltLandblockMesh Mesh;
			auto& Section = Mesh.Sections.AddDefaulted_GetRef();
			Section.PCode = Code;
			SourceBuilder.HydrateSectionBakes(Mesh, 0);
			return Section.SharedBake;
		};
		auto Mixed = Prepare(Builder, ComplexRoad);
		if (!TestTrue(TEXT("Four corners and a road get independent diffuse layers"), Mixed && Mixed->IsLayerMask())) return false;
		TestEqual(TEXT("Road masks retain their 24 m cell projection"), Mixed->CellsAcross, 1);
		TestEqual(TEXT("Cobblestone samples the installed AC2 artwork"), Mixed->Layers[4].SurfaceId, 0x40000b0fu);
		TestEqual(TEXT("Road paving repeats every 10 m"), Mixed->Layers[4].Repeat, 12.f);
		for (int32 I = 0; I < 4; ++I)
			TestTrue(TEXT("All four corners retain their source projection"), (I % 2 == 0) == (Mixed->Layers[I].Repeat > 0));
		const auto Masks = Mixed->CopyBasePixels();
		TestTrue(TEXT("Road includes opaque paving and uncovered shoulder"), Masks.ContainsByPredicate([](FColor P){return P.A==255;})
			&& Masks.ContainsByPredicate([](FColor P){return P.A==0;}));
		// A previously imported pack has no AC2 cobblestone. The same PCode
		// must bind the native road without downsampling it into a regional sheet.
		Art->Surfaces.Remove(0x40000b0f);
		FACELandblockMeshBuilder OldPackBuilder(&Portal, nullptr, &Textures, nullptr, Art);
		auto Old = Prepare(OldPackBuilder, ComplexRoad);
		if (!TestTrue(TEXT("Existing packs preserve the mixed terrain mask"), Old && Old->IsLayerMask())) return false;
		TestEqual(TEXT("AC1 road fallback uses its DAT surface"), Old->Layers[4].SurfaceId, 0x05001458u);
		TestEqual(TEXT("AC1 road fallback retains its two repeats per cell"), Old->Layers[4].Repeat, -2.f);
		Art->Surfaces.Add(0x40000b0f, Env.Surfaces.FindChecked(0x40000b0f));
		for (uint32 Type : {10u,32u})
		{
			if (!Env.Terrain(Type)) continue; // Lower-resolution portal-only installs use AC1 here.
			const uint32 Code = Type == 32 ? Builder.GetPalCode(1,1,1,1,10,10,10,10) : Builder.GetPalCode(0,0,0,0,Type,Type,Type,Type);
			const int32 Cells = Env.TerrainTileCells(Code);
			TestEqual(TEXT("120 m world tile covers five AC1 cells"),Cells,5);
			TArray<FColor> Pixels;
			int32 W,H;
			if (!TestTrue(TEXT("Bake imported sand"),Builder.BakePCode(Code,Pixels,W,H,Cells))) return false;
			const float Repeats = Type==10 ? 6.f : 12.f;
			TestEqual(TEXT("Near tile projects the native diffuse repeat"),Env.UniformTerrainRepeats(Code),Repeats);
			FACEBuiltLandblockMesh Land;
			auto& Section=Land.Sections.AddDefaulted_GetRef();
			Section.PCode=Code;
			Builder.HydrateSectionBakes(Land,0);
			if (!TestTrue(TEXT("Prepare native diffuse surface"),Section.SharedBake.IsValid())) return false;
			TestEqual(TEXT("Each diffuse repeat retains full resolution up close"),Section.BakeWidth,Env.Terrain(Type)->Width);
			TestEqual(TEXT("Native texture wraps across cells"),Section.SharedBake->CellsAcross,Cells);
			auto Filtered = [](const FACEDatDecodedSurface* Surface,float U,float V)
			{
				const float DU=.5f/Surface->Width, DV=.5f/Surface->Height;
				const float FX=FMath::Frac(U*Surface->Width+.5f), FY=FMath::Frac(V*Surface->Height+.5f);
				return FMath::BiLerp(Surface->SampleUV(U-DU,V-DV).ReinterpretAsLinear(),
					Surface->SampleUV(U+DU,V-DV).ReinterpretAsLinear(),Surface->SampleUV(U-DU,V+DV).ReinterpretAsLinear(),
					Surface->SampleUV(U+DU,V+DV).ReinterpretAsLinear(),FX,FY);
			};
			const auto* Expected = Env.Terrain(Type,FMath::CeilToInt(W/Repeats));
			for (int32 Y : {3, H/3, H-2})
				for (int32 X : {7, W/5+17, W/2+23, W-5})
				{
					const float U=(X+.5f)/W, V=(Y+.5f)/H;
					auto Ref=Filtered(Expected,U*Repeats,V*Repeats);
					Ref.A=Type==32 ? 0 : 1;
					TestTrue(TEXT("Mixed tile samples the native diffuse at world positions"),
						Ref.ToFColor(false).ReinterpretAsLinear().Equals(Pixels[Y*W+(W-1-X)].ReinterpretAsLinear(),1.1f/255.f));
				}
		}
	}
	TestTrue(TEXT("Decoded grass and beach have distinct pixels"), Grassland && Grassland->bHasPixels &&
		(!Sand || (Sand->bHasPixels && Grassland->Pixels != Sand->Pixels)));
	for (uint32 Type : {3u,4u,8u,9u,11u,12u,13u,16u,17u,18u,19u,20u,21u,22u,23u,24u,26u,27u,28u,31u})
		TestNull(TEXT("Unmatched terrain retains AC1"), Env.Terrain(Type));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELegionsRefreshTest, "ACE.Graphics.LegionsArtwork.AutoRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACELegionsRefreshTest::RunTest(const FString&)
{
	using namespace ACELegionsSky;
	FString Source, Legacy;
	if (!FParse::Value(FCommandLine::Get(), TEXT("AC2SkyTestDat="), Source) ||
		!FParse::Value(FCommandLine::Get(), TEXT("AC2LegacyPack="), Legacy))
	{
		AddInfo(TEXT("Supply AC2SkyTestDat and AC2LegacyPack to test migration of a previous installation."));
		return true;
	}
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("Automation/LegionsRefresh") / FGuid::NewGuid().ToString();
	auto& Files = IFileManager::Get();
	Files.MakeDirectory(*Folder,true);
	ON_SCOPE_EXIT { Files.DeleteDirectory(*Folder, false, true); };
	const FString Pack = Folder / TEXT("Legions.sky");
	TestFalse(TEXT("No existing import leaves AC2 disabled"), RefreshImport(Pack,Source));
	if (!TestTrue(TEXT("Install legacy cache without source metadata"), Files.Copy(*Pack,*Legacy) == COPY_OK)) return false;
	if (!TestTrue(TEXT("Find matching DATs and upgrade previous import"), RefreshImport(Pack,Source))) return false;
	FEnvironment Env;
	if (!TestTrue(TEXT("Refreshed pack loads with ground only"),LoadEnvironment(Pack,Ground,Env))) return false;
	TestTrue(TEXT("Road diffuse present after automatic migration"),Env.Terrain(32) && Env.Terrain(32)->SurfaceId==0x40000b0f);
	TestFalse(TEXT("Grass selection remains independent"),Env.Terrain(1) != nullptr);
	TestFalse(TEXT("Subsequent launch reuses the refreshed pack"),RefreshImport(Pack,Source));
	return !HasAnyErrors();
}

// Run with the desired saved options in an isolated user directory, as on the next client launch.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELegionsWorldTest, "ACE.Graphics.LegionsArtwork.World",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACELegionsWorldTest::RunTest(const FString&)
{
	using namespace ACELegionsSky;
	int32 Expected = 0;
	if (!FParse::Value(FCommandLine::Get(), TEXT("AC2TestArtworkMask="), Expected))
	{
		AddInfo(TEXT("Supply -AC2TestArtworkMask to check the current session selection in the world renderer."));
		return true;
	}
	FString Retail = TEXT("C:/Turbine/Asheron's Call");
	FParse::Value(FCommandLine::Get(), TEXT("RetailDatDir="), Retail);
	RefreshEnvironment();
	const auto Active = ActiveEnvironment();
	if (!TestEqual(TEXT("Loaded the saved artwork choices"), int32(Active->Enabled), Expected)) return false;
	const uint64 DatKey = 0x0123456789ABCDEFull;
	const FString Filename = FPaths::GetCleanFilename(ACEPCodeTileCache::MakeCacheFilePath(42,DatKey,Active->TerrainCacheKey(DatKey)));
	TestTrue(TEXT("Cache maintenance recognizes the AC1 DAT fingerprint"), Filename.Contains(TEXT("0123456789ABCDEF")));
	TestEqual(TEXT("Only terrain selections add a separate texture cache"), Filename != TEXT("0000002A_0123456789ABCDEF.acpx"), bool(Expected & (Ground | Grass)));

	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
	auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	auto* Game = NewObject<UGameInstance>(GEngine);
	World->SetGameInstance(Game);
	Context.OwningGameInstance = Game;
	Context.SetCurrentWorld(World);
	Game->OnWorldChanged(nullptr, World);
	Game->Init();
	ON_SCOPE_EXIT { Game->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	auto* Dat = Game->GetSubsystem<UACEDatSubsystem>();
	if (!Dat || !TestTrue(TEXT("Load retail landscape and scenery"), Dat->LoadDatDirectory(Retail))) return false;
	if (Expected & Trees && FParse::Param(FCommandLine::Get(), TEXT("AC2TreeBenchmark")))
	{
		TArray<uint32> Models = Dat->GetPortalDat()->GetFileIds(0x01);
		Models.Append(Dat->GetPortalDat()->GetFileIds(0x02));
		Models.Sort();
		double BuildSeconds = 0.;
		int32 Count = 0, NearTriangles = 0, FarTriangles = 0, NearVertices = 0;
		for (uint32 Id : Models)
		{
			if (!Active->FindTree(Id)) continue;
			Dat->GetOrBuildSetupMeshShared(Id, 100, 0);
			const double Start = FPlatformTime::Seconds();
			auto* Mesh = Dat->GetOrCreateSetupStaticMesh(Id, 100, false, 0, false, false, true);
			BuildSeconds += FPlatformTime::Seconds() - Start;
			if (!TestNotNull(TEXT("Build imported tree for rendering"), Mesh)) return false;
			++Count;
			NearTriangles += Mesh->GetNumTriangles(0);
			FarTriangles += Mesh->GetNumTriangles(Mesh->GetNumLODs() - 1);
			NearVertices += Mesh->GetRenderData()->LODResources[0].GetNumVertices();
		}
		AddInfo(FString::Printf(TEXT("Built %d imported tree setups in %.1f ms; near %d vertices, %d triangles; distant %d triangles"),
			Count, BuildSeconds * 1000., NearVertices, NearTriangles, FarTriangles));
	}

	auto* Options = CreateWidget<UACEVideoSettingsWidget>(World,UACEVideoSettingsWidget::StaticClass());
	if (!TestNotNull(TEXT("Open Graphics Options"), Options)) return false;
	const auto Slate = Options->TakeWidget();
	bool HasEnvironment = false, HasBoulders = false;
	HasCache(nullptr,&HasEnvironment,nullptr,nullptr,nullptr,&HasBoulders);
	for (const TCHAR* Key : {TEXT("LegionsGround"),TEXT("LegionsGrass"),TEXT("LegionsTrees"),TEXT("LegionsBoulders")})
	{
		const auto* Toggle = Cast<UCheckBox>(Options->GetWidgetFromName(FName(Key)));
		if (!TestNotNull(TEXT("Outdoor artwork has its own control"), Toggle)) return false;
		TestEqual(TEXT("Only a world-art import enables the control"), Toggle->GetIsEnabled(),FCString::Strcmp(Key,TEXT("LegionsBoulders"))==0 ? HasBoulders : HasEnvironment);
		TestEqual(TEXT("Display saved component choice"), Toggle->IsChecked(), Toggle->GetIsEnabled() && ACERuntimeOptions::Get(Key)>.5f);
	}
	const FString Preview = FPaths::ProjectSavedDir() / TEXT("Automation/LegionsArtwork");
	auto SavePreview = [&](const FString& Name, TArray<FColor>& Pixels, int32 W, int32 H)
	{
		for (FColor& P : Pixels) P.A = 255;
		IFileManager::Get().MakeDirectory(*Preview, true);
		TArray64<uint8> PNG;
		FImageUtils::PNGCompressImageArray(W,H,Pixels,PNG);
		TestTrue(TEXT("Save rendered sample"), FFileHelper::SaveArrayToFile(PNG, *(Preview / Name)));
	};
	const bool Capture = FParse::Param(FCommandLine::Get(),TEXT("AC2TestArtworkPreview"));
	FACELandblockMeshBuilder Builder(Dat->GetPortalDat(), nullptr, Dat->GetTextureResolver());
	if (!TestTrue(TEXT("Load AC1 terrain blend rules"), Builder.EnsureTexMerge())) return false;
	for (const int32 Type : {1,10,16})
	{
		TArray<FColor> Pixels;
		int32 W,H;
		const uint32 Code = FACELandblockMeshBuilder::GetPalCode(0,0,0,0,Type,Type,Type,Type);
		if (!TestTrue(TEXT("Bake the terrain tile"), Builder.BakePCode(Code, Pixels, W, H))) return false;
		FACEBuiltLandblockMesh Mesh;
		auto& Section=Mesh.Sections.AddDefaulted_GetRef();
		Section.PCode=Code;
		Builder.HydrateSectionBakes(Mesh,0);
		if (!TestTrue(TEXT("Baked a fresh terrain material"),Section.SharedBake.IsValid())) return false;
		auto* Material=Dat->GetOrCreateLandMaterial(Code,Section.BakedPixels,Section.BakeWidth,Section.BakeHeight,Section.SharedBake);
		float RegionUV=-1,DetailScale=-1;
		if (!TestNotNull(TEXT("Create initial landscape material"),Material)) return false;
		Material->GetScalarParameterValue(TEXT("UseRegionTerrainUV"),RegionUV);
		Material->GetScalarParameterValue(TEXT("GroundDetailScale"),DetailScale);
		const auto Art=ActiveEnvironment();
		const bool Imported=Art->Terrain(Type)!=nullptr;
		TestEqual(TEXT("Initial material uses the saved landscape coordinate system"),RegionUV,Imported ? 1.f : 0.f);
		const float ExpectedDetail = Imported && !Art->GroundDetail.Mips.IsEmpty() ? Art->TerrainRepeatMetres(Art->Terrain(Type)->SurfaceId)/Art->DetailRepeatMetres : 0.f;
		TestTrue(TEXT("Initial material uses 3 m detail only on supported packs and types"),FMath::IsNearlyEqual(DetailScale,ExpectedDetail));
		if (Capture) SavePreview(FString::Printf(TEXT("terrain-%d.png"),Type),Pixels,W,H);
		AddInfo(FString::Printf(TEXT("Terrain type %d, %dx%d, CRC %08x"), Type, W,H, FCrc::MemCrc32(Pixels.GetData(), Pixels.Num()*sizeof(FColor))));
	}
	for (uint32 Id : {0x02000258u, 0x020003eeu, 0x020003dfu, 0x020005c9u})
	{
		if (!TestTrue(TEXT("Load the original scenery setup"), Dat->GetOrBuildSetupMeshShared(Id,100.f).IsValid())) return false;
		auto* Mesh = Dat->GetOrCreateSetupStaticMesh(Id,100.f,true,0,false,false,true);
		if (!TestNotNull(TEXT("Build outdoor scenery mesh"), Mesh)) return false;
		TArray<FACEDatCollisionShape> Shapes;
		bool HasBsp = false;
		Dat->GetSetupCollisionShapes(Id,Shapes,HasBsp);
		if (auto* Physics = Dat->GetOrCreateSetupStaticMesh(Id,100.f,true,0,false,false,false,true))
			TestTrue(TEXT("Render mesh shares the authored physics body"), Mesh->GetBodySetup() == Physics->GetBodySetup());
		else if (!Shapes.IsEmpty())
			TestTrue(TEXT("Original AC1 trunk collision shapes"),Mesh->GetBodySetup() &&
				Mesh->GetBodySetup()->AggGeom.GetElementCount() == Shapes.Num());
		if (Id == 0x020005c9)
		{
			auto* Original = Dat->GetOrCreateSetupStaticMesh(Id,100.f,true,0);
			if (!TestNotNull(TEXT("Retail boulder mesh"), Original)) return false;
			TestEqual(TEXT("Boulder retains AC1 topology"), Mesh->GetNumTriangles(0), Original->GetNumTriangles(0));
			TestTrue(TEXT("Boulder retains AC1 dimensions"), Mesh->GetBoundingBox().Equals(Original->GetBoundingBox(), .01));
			if (Expected & Boulders)
				TestTrue(TEXT("Outdoor boulder uses its Legions material"), Mesh->GetMaterial(0) == Dat->GetImportedSceneryMaterial(Active->BoulderTexture(Id), false));
		}
		else if ((Expected & Trees) && (Id != 0x020003df || HasBoulders))
		{
			const FTree* Tree = ActiveEnvironment()->FindTree(Id);
			if (!Tree || !TestEqual(TEXT("All imported materials"),Mesh->GetStaticMaterials().Num(),Tree->Sections.Num())) return false;
			int32 Triangles = 0;
			for (int32 Slot=0; Slot<Tree->Sections.Num(); ++Slot)
			{
				const auto& Section = Tree->Sections[Slot];
				const auto* Material = Mesh->GetMaterial(Slot);
				TestEqual(TEXT("Material preserves foliage cutouts"),Material->GetBlendMode()==BLEND_Masked,Section.Masked);
				Triangles += Section.Indices.Num()/3 * (Section.Masked && !Material->IsTwoSided() ? 2 : 1);
			}
			TestEqual(TEXT("Draw imported topology"),Mesh->GetNumTriangles(0),Triangles);
			if (!TestEqual(TEXT("Build every selected tree LOD"), Mesh->GetNumLODs(), 1 + Tree->DistanceLevels.Num())) return false;
			for (int32 Level = 0; Level < Mesh->GetNumLODs(); ++Level)
			{
				const auto& Sections = Level ? Tree->DistanceLevels[Level-1] : Tree->Sections;
				const auto& RenderLOD = Mesh->GetRenderData()->LODResources[Level];
				if (!TestEqual(TEXT("Tree LOD preserves materials"), RenderLOD.Sections.Num(), Sections.Num())) return false;
				TSet<int32> ReferencedVertices;
				for (const auto& Section : Sections)
					for (int32 Index : Section.Indices) ReferencedVertices.Add(Index);
				TestTrue(TEXT("Tree LOD reuses its indexed vertices"), RenderLOD.GetNumVertices() <= ReferencedVertices.Num());
				for (int32 Slot = 0; Slot < Sections.Num(); ++Slot)
				{
					TestEqual(TEXT("Tree LOD binds original slot"), RenderLOD.Sections[Slot].MaterialIndex, Slot);
					TestEqual(TEXT("Tree LOD draws each authored triangle once"), int32(RenderLOD.Sections[Slot].NumTriangles), Sections[Slot].Indices.Num()/3);
					if (Sections[Slot].Masked)
					{
						auto* Leaf = Mesh->GetMaterial(Slot);
						TestTrue(TEXT("Leaf cards visible on both sides"), Leaf->IsTwoSided());
						auto* Far = Dat->GetFarSceneryMaterial(Leaf);
						TestTrue(TEXT("Far fade preserves two-sided leaves"), Far && Far != Leaf && Far->IsTwoSided());
					}
				}
				if (Level)
					TestTrue(TEXT("Distant trees use fewer triangles"), Mesh->GetNumTriangles(Level) < Mesh->GetNumTriangles(Level-1));
			}
		}
		if (Capture && Id != 0x020003ee)
		{
			auto* Actor = World->SpawnActor<AStaticMeshActor>();
			auto* Component = Actor->GetStaticMeshComponent();
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetStaticMesh(Mesh);
			auto* Light = World->SpawnActor<ADirectionalLight>();
			Light->SetActorRotation(FRotator(-40,30,0));
			auto* CameraOwner = World->SpawnActor<AActor>();
			auto* Camera = NewObject<USceneCaptureComponent2D>(CameraOwner);
			CameraOwner->AddInstanceComponent(Camera);
			Camera->bCaptureEveryFrame = false;
			Camera->bCaptureOnMovement = false;
			Camera->RegisterComponent();
			Camera->ShowOnlyComponent(Component);
			Camera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			auto* Target = NewObject<UTextureRenderTarget2D>(CameraOwner);
			Target->ClearColor = FLinearColor(.07f,.10f,.15f);
			Target->InitCustomFormat(512,512,PF_B8G8R8A8,false);
			Camera->TextureTarget = Target;
			Camera->CaptureSource = SCS_FinalColorLDR;
			Camera->FOVAngle = 40.f;
			// Frame both captures against the same retail object and origin.
			const auto* Reference = Dat->GetOrCreateSetupStaticMesh(Id,100.f,true,0);
			const auto Bounds = Reference ? Reference->GetBounds() : Mesh->GetBounds();
			const FVector Focus = Bounds.Origin;
			const FVector Eye = Focus + FVector(-1,-.3,.05).GetSafeNormal() * Bounds.BoxExtent.GetMax() * 3.7;
			Camera->SetWorldLocationAndRotation(Eye,(Focus-Eye).Rotation());
#if WITH_EDITOR
			if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
#endif
			World->SendAllEndOfFrameUpdates();
			Camera->CaptureScene();
			FlushRenderingCommands();
			TArray<FColor> Pixels;
			if (TestTrue(TEXT("Capture outdoor scenery"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)))
				SavePreview(Id == 0x020005c9 ? TEXT("boulder.png") : Id == 0x020003df ? TEXT("stump.png") : TEXT("tree.png"),Pixels,512,512);
			CameraOwner->Destroy();
			Light->Destroy();
			Actor->Destroy();
		}
		AddInfo(FString::Printf(TEXT("Scenery %08x: %d triangles"), Id, Mesh->GetNumTriangles(0)));
	}
	if (HasBoulders)
	{
		auto* Toggle = CastChecked<UCheckBox>(Options->GetWidgetFromName(TEXT("LegionsBoulders")));
		const bool Initial = Toggle->IsChecked();
		constexpr uint32 SetupId = 0x020005c9;
		for (const bool On : {!Initial, Initial})
		{
			Toggle->SetIsChecked(On);
			Toggle->OnCheckStateChanged.Broadcast(On);
			Dat->ConsumeLandMeshReloadRequest();
			Dat->GetOrBuildSetupMeshShared(SetupId,100.f);
			auto* Mesh = Dat->GetOrCreateSetupStaticMesh(SetupId,100.f,true,0,false,false,true);
			if (!TestNotNull(TEXT("Live toggle resolves the rock in the same world"), Mesh)) return false;
			TestEqual(TEXT("Live toggle preserves the other artwork choices"), int32(ActiveEnvironment()->Enabled), (Expected & ~Boulders) | (On ? Boulders : 0));
			TestEqual(TEXT("Live rock uses the selected surface"), Mesh->GetMaterial(0) == Dat->GetImportedSceneryMaterial(0x400005be, false), On);
		}
	}
	return !HasAnyErrors();
}

// Opt-in census uses the streaming renderer's DAT placement rules, including slope,
// road and building exclusions and the actual randomized scene picked at each cell.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEDerethTreeSurvey, "ACE.Graphics.LegionsArtwork.DerethSurvey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEDerethTreeSurvey::RunTest(const FString&)
{
	if (!FParse::Param(FCommandLine::Get(),TEXT("DerethTreeSurvey"))) return true;
	FString Retail = TEXT("C:/Turbine/Asheron's Call");
	FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Retail);
	auto Source = PersonalFarSceneryPlacement::Open(Retail);
	FPersonalFarTerrainBake Bake;
	if (!Source || !PersonalFarTerrainBake::LoadOrBuild(Retail,Bake)) return false;
	TMap<uint32,FIntPoint> Counts;
	TArray<PersonalFarSceneryPlacement::FItem> Items;
	FString Towns, Selected;
	TArray<FString> SampleKeys;
	FParse::Value(FCommandLine::Get(),TEXT("TreeSurveyBlocks="),Selected,false);
	Selected.ParseIntoArray(SampleKeys,TEXT(","));
	TSet<uint32> Samples;
	for (const FString& Key : SampleKeys) Samples.Add(FParse::HexNumber(*Key));
	int32 Blocks=0;
	for (int32 X=0; X<Bake.LandblockDim; ++X) for (int32 Y=0; Y<Bake.LandblockDim; ++Y)
	{
		const uint32 Block = (uint32(X)<<24)|(uint32(Y)<<16);
		if (!PersonalFarSceneryPlacement::Place(Bake,*Source,Block,Items)) continue;
		++Blocks;
		for (const auto& Item : Items)
		{
			if (Samples.Contains(Block>>16))
				Towns.Appendf(TEXT("%04X %08X %d %.2f %.2f %.2f\n"),Block>>16,Item.SetupId,int32(Item.Kind),Item.OriginAc.X,Item.OriginAc.Y,Item.OriginAc.Z);
			if (Item.Kind == PersonalFarSceneryPlacement::EKind::Building) continue;
			auto& Count = Counts.FindOrAdd(Item.SetupId,FIntPoint::ZeroValue);
			if (Item.Kind == PersonalFarSceneryPlacement::EKind::Stab) ++Count.X; else ++Count.Y;
		}
	}
	const FString Dir = FPaths::ProjectSavedDir()/TEXT("Automation/DerethTrees");
	IFileManager::Get().MakeDirectory(*Dir,true);
	FString Census;
	Counts.KeySort([](uint32 A,uint32 B){return A<B;});
	for (const auto& Pair : Counts) Census.Appendf(TEXT("%08X %d %d\n"),Pair.Key,Pair.Value.X,Pair.Value.Y);
	TestTrue(TEXT("Write setup census"),FFileHelper::SaveStringToFile(Census,*(Dir/TEXT("setups.txt"))));
	if (!Samples.IsEmpty()) TestTrue(TEXT("Write town samples"),FFileHelper::SaveStringToFile(Towns,*(Dir/TEXT("towns.txt"))));
	AddInfo(FString::Printf(TEXT("Surveyed %d retail landblocks and %d distinct placed scenery IDs"),Blocks,Counts.Num()));
	return !HasAnyErrors();
}

// Requires an isolated user directory because this exercises the real importer and saved graphics controls.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEYaraqArtworkTest, "ACE.Graphics.LegionsArtwork.Yaraq",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEYaraqArtworkTest::RunTest(const FString&)
{
	using namespace ACELegionsSky;
	FString Source, UserDir;
	if (!FParse::Value(FCommandLine::Get(),TEXT("AC2SkyTestDat="),Source) ||
		!FParse::Value(FCommandLine::Get(),TEXT("UserDir="),UserDir) ||
		!FParse::Param(FCommandLine::Get(),TEXT("AC2YaraqCapture")))
	{
		AddInfo(TEXT("Supply -AC2YaraqCapture, -UserDir and -AC2SkyTestDat to render the Yaraq walk-through."));
		return true;
	}
	FString Error;
	if (!Import(Source,CachePath(),Error)) { AddError(Error); return false; }
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
	auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	auto* Game = NewObject<UGameInstance>(GEngine);
	World->SetGameInstance(Game);
	Context.OwningGameInstance = Game;
	Context.SetCurrentWorld(World);
	Game->OnWorldChanged(nullptr,World);
	Game->Init();
	ON_SCOPE_EXIT { Game->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	auto* Dat = Game->GetSubsystem<UACEDatSubsystem>();
	FString Retail = TEXT("C:/Turbine/Asheron's Call");
	FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Retail);
	if (!Dat || !TestTrue(TEXT("Load the Yaraq DAT"),Dat->LoadDatDirectory(Retail))) return false;
	auto* Options = CreateWidget<UACEVideoSettingsWidget>(World,UACEVideoSettingsWidget::StaticClass());
	if (!Options) return false;
	const auto Slate = Options->TakeWidget();
	auto Toggle = [&](const TCHAR* Name, bool Enabled)
	{
		auto* Check = Cast<UCheckBox>(Options->GetWidgetFromName(FName(Name)));
		if (!Check || !Check->GetIsEnabled()) { AddError(FString(TEXT("Missing control "))+Name); return; }
		Check->SetIsChecked(Enabled);
		Check->OnCheckStateChanged.Broadcast(Enabled);
	};
	Toggle(TEXT("LegionsGround"),false);
	Toggle(TEXT("LegionsGrass"),false);
	Toggle(TEXT("LegionsTrees"),false);
	Toggle(TEXT("LegionsGroundCover"),false);
	auto* Land = World->SpawnActor<AACELandblockActor>();
	const bool Upland = FParse::Param(FCommandLine::Get(),TEXT("AC2YaraqUpland"));
	const bool Shore = FParse::Param(FCommandLine::Get(),TEXT("AC2ArqisShore"));
	const uint32 Block = Shore ? 0x7b700000 : Upland ? 0x80650000 : 0x7D640000;
	auto DrawLand = [&](){
		Dat->ConsumeLandMeshReloadRequest();
		return Dat->GetOrBuildLandblockMesh(Block,100) && Land->LoadLandblock(Block,100,false,1,0);
	};
	if (!TestTrue(TEXT("Build the classic Yaraq landblock"),DrawLand())) return false;
	const auto* Terrain = Dat->FindLandblockMesh(Block);
	int32 GrassCorners=0,SandCorners=0;
	for (uint16 Word : Terrain->Terrain)
	{
		const int32 Type=(Word>>2)&31;
		GrassCorners+=Type==1;
		SandCorners+=Type==10;
	}
	TArray<FACEDatRegionSceneryItem> Scenery;
	Dat->CollectRegionScenery(Block,100,Scenery);
	int32 Trees = 0;
	for (const auto& Item : Scenery) Trees += IsMappedTree(Item.SetupId);
	if (const auto Info = Dat->GetLandblockInfo(Block))
		for (const auto& Item : Info->Objects) Trees += IsMappedTree(Item.Id);
	AddInfo(FString::Printf(TEXT("Yaraq %04X has %d grass and %d yellow sand corners, %d tree placements with a Legions counterpart"),Block>>16,GrassCorners,SandCorners,Trees));
	auto* Owner = World->SpawnActor<AActor>();
	auto* Sun = World->SpawnActor<ADirectionalLight>();
	Sun->SetActorRotation(FRotator(-48,22,0));
	// Static camera on the grass west of Yaraq's north/south road.
	FVector Feet=Land->GetActorLocation()+(Upland ? FVector(-3710,9927,0) : FVector(-3800,10500,0));
	float GroundZ=0;
	if (!Dat->SampleOutdoorGroundZ(Feet.X,Feet.Y,100,GroundZ)) return false;
	Feet.Z=GroundZ;
	auto* Camera=NewObject<USceneCaptureComponent2D>(Owner);
	Owner->AddInstanceComponent(Camera);
	Camera->bCaptureEveryFrame=false;
	Camera->bCaptureOnMovement=false;
	Camera->RegisterComponent();
	Camera->SetWorldLocationAndRotation(Feet+FVector(200,-140,135),FRotator(-19,145,0));
	Camera->FOVAngle=65;
	auto* Target=NewObject<UTextureRenderTarget2D>(Owner);
	Target->ClearColor=FLinearColor(.23f,.37f,.53f);
	Target->InitCustomFormat(960,540,PF_B8G8R8A8,false);
	Camera->TextureTarget=Target;
	Camera->CaptureSource=SCS_FinalColorLDR;
	Camera->ShowFlags.SetEyeAdaptation(false);
	auto Capture=[&](const TCHAR* Name)
	{
#if WITH_EDITOR
		if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
#endif
		World->SendAllEndOfFrameUpdates();
		Camera->CaptureScene();
		FlushRenderingCommands();
		TArray<FColor> Pixels;
		TestTrue(TEXT("Read the scene capture"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels));
		for (FColor& P : Pixels) P.A=255;
		TArray64<uint8> PNG;
		FImageUtils::PNGCompressImageArray(960,540,Pixels,PNG);
		const FString Path=FPaths::ProjectSavedDir()/TEXT("Automation/Yaraq")/Name;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
		TestTrue(TEXT("Write the Yaraq comparison"),FFileHelper::SaveArrayToFile(PNG,*Path));
		return Pixels;
	};
	auto Compare=[&](const TArray<FColor>& A,const TArray<FColor>& B)
	{
		int32 Changed=0;
		if (A.Num()==B.Num())
			for (int32 I=0; I<A.Num(); ++I)
				Changed+=FMath::Abs(int32(A[I].R)-B[I].R)+FMath::Abs(int32(A[I].G)-B[I].G)+FMath::Abs(int32(A[I].B)-B[I].B)>12;
		return Changed;
	};
	if (FParse::Param(FCommandLine::Get(), TEXT("AC2YaraqRoad")))
	{
		// The road follows x=72 m in 7D64 and branches east at y=96 m.
		// Frame a section of paving and both grass shoulders at the same distance.
		const float X = 72.f, Y = 109.f;
		if (!TestTrue(TEXT("Capture is on the Yaraq road"), FACELandblockMeshBuilder::IsOnRoad(*Terrain,X,Y))) return false;
		const int32 SW = 2*9+4, SE = SW+9;
		const auto* W = Terrain->Terrain;
		const uint32 Code = FACELandblockMeshBuilder::GetPalCode(W[SW]&3,W[SE]&3,W[SE+1]&3,W[SW+1]&3,
			(W[SW]>>2)&31,(W[SE]>>2)&31,(W[SE+1]>>2)&31,(W[SW+1]>>2)&31);
		const FVector Center = Land->GetActorLocation()+FVector(-X*100,Y*100,0);
		float Z = 0.f;
		if (!Dat->SampleOutdoorGroundZ(Center.X,Center.Y,100,Z)) return false;
		Camera->SetWorldLocationAndRotation(FVector(Center.X,Center.Y,Z+920), FRotator(-90,90,0));
		const auto Before = Capture(TEXT("road-ac1.png"));
		Toggle(TEXT("LegionsGround"),true);
		Toggle(TEXT("LegionsGrass"),true);
		Dat->ConsumeLandMeshReloadRequest();
		Land->Destroy();
		Land=World->SpawnActor<AACELandblockActor>();
		if (!DrawLand()) return false;
		const double Deadline = FPlatformTime::Seconds()+30;
		UTexture* Paving = nullptr;
		do
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			const auto* Mesh = Dat->FindLandblockMesh(Block);
			const auto* Section = Mesh ? Mesh->Sections.FindByPredicate([&](const auto& S){return S.PCode==Code;}) : nullptr;
			if (Section && Section->SharedBake && Section->SharedBake->IsLayerMask())
			{
				auto* Material = Dat->GetOrCreateLandMaterial(Code, Section->BakedPixels, Section->BakeWidth, Section->BakeHeight, Section->SharedBake);
				if (Material) Material->GetTextureParameterValue(TEXT("TerrainLayer4"),Paving);
			}
			if (!Paving) FPlatformProcess::Sleep(.02f);
		} while (!Paving && FPlatformTime::Seconds()<Deadline);
		if (!TestNotNull(TEXT("Live road material has a dedicated paving texture"), Paving)) return false;
		TestEqual(TEXT("GPU paving retains the installed top mip"), int32(Paving->GetSurfaceWidth()), ActiveEnvironment()->Terrain(32)->Width);
		const auto After = Capture(TEXT("road-ac2.png"));
		TestTrue(TEXT("Rendered Yaraq road view changes on import"), Compare(Before,After)>500);
		return !HasAnyErrors();
	}
	if (Shore)
	{
		auto* South=World->SpawnActor<AACELandblockActor>();
		if (!TestTrue(TEXT("Load southern half of Al-Arqis shore"),
			Dat->GetOrBuildLandblockMesh(0x7b6f0000,100) && South->LoadLandblock(0x7b6f0000,100,false,1,0))) return false;
		const FVector Beach=Land->GetActorLocation()+FVector(-12772.55,89.56,-45);
		auto Shoot=[&](const TCHAR* Close,const TCHAR* Above)
		{
			Camera->SetWorldLocationAndRotation(Beach+FVector(-260,0,210),FRotator(-16,0,0));
			Capture(Close);
			Camera->SetWorldLocationAndRotation(Beach+FVector(850,-89.56,1500),FRotator(-90,0,0));
			return Capture(Above);
		};
		const auto Classic = Shoot(TEXT("shore-classic.png"),TEXT("shore-classic-above.png"));
		Toggle(TEXT("LegionsGround"),true);
		// Recreate both blocks using the normal saved-on material path.
		Dat->ConsumeLandMeshReloadRequest();
		Land->Destroy(); South->Destroy();
		auto NewBlock=[&](uint32 Id)
		{
			auto* Actor=World->SpawnActor<AACELandblockActor>();
			auto* Data=Dat->GetOrBuildLandblockMesh(Id,100);
			return Data && Actor->LoadLandblock(Id,100,false,1,0);
		};
		if (!NewBlock(Block) || !NewBlock(0x7b6f0000)) return false;
		// The normal live refresh updates shared section textures asynchronously.
		const double Until=FPlatformTime::Seconds()+30;
		bool Ready=false;
		do
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			auto* Mesh=Dat->FindLandblockMesh(Block);
			Ready=Mesh!=nullptr;
			if (Mesh) for(const auto& Section : Mesh->Sections)
				if (ActiveEnvironment()->TerrainTileCells(Section.PCode)>1)
					Ready &= Section.SharedBake && (Section.SharedBake->CellsAcross>1 || Section.SharedBake->OverlaySurfaceId || Section.SharedBake->IsLayerMask());
			if (!Ready) FPlatformProcess::Sleep(.02f);
		} while (!Ready && FPlatformTime::Seconds()<Until);
		if (!TestTrue(TEXT("Imported Al-Arqis shoreline finished streaming"),Ready)) return false;
		const auto Imported = Shoot(TEXT("shore-legions.png"),TEXT("shore-legions-above.png"));
		if (!TestEqual(TEXT("Both shoreline captures are complete"), Imported.Num(), 960 * 540)
			|| !TestEqual(TEXT("Matching reference dimensions"), Classic.Num(), Imported.Num())) return false;
		int ChangedWater = 0, ChangedBeach = 0;
		for (int Y=0; Y<540; ++Y)
			for (int X=0; X<960; ++X)
			{
				const FColor A=Classic[Y*960+X], B=Imported[Y*960+X];
				if (FMath::Abs(int(A.R)-B.R)+FMath::Abs(int(A.G)-B.G)+FMath::Abs(int(A.B)-B.B)<=12) continue;
				// The two water tile boundaries intersect at (480,311). The beach enters at the right edge.
				ChangedWater += FMath::Abs(X-480)<=5 || (FMath::Abs(Y-311)<=5 && X<840);
				ChangedBeach += X>=840 && Y>=360;
			}
		TestTrue(TEXT("Both underwater tile edges retain classic detail"), ChangedWater < 20);
		TestTrue(TEXT("The adjacent beach uses imported sand"), ChangedBeach > 500);
		return !HasAnyErrors();
	}
	const bool RooftopOnly = FParse::Param(FCommandLine::Get(),TEXT("AC2RooftopTree"));
	if (RooftopOnly || FParse::Param(FCommandLine::Get(),TEXT("AC2DerethTowns")))
	{
		Target->TargetGamma = GEngine->GetDisplayGamma();
		Target->InitCustomFormat(960,540,PF_B8G8R8A8,true);
		auto* Sky = World->SpawnActor<AACESkyDomeActor>();
		Sky->DatSubsystem = Dat;
		if (!TestTrue(TEXT("Load town sky"),Sky->TryBuild())) return false;
		struct FView { const TCHAR* Name; uint32 Block; FVector Eye, LookAt; };
		// Survey positions are in AC1 landblock-local metres. Eye height is above the ground.
		const FView Views[] = {
			{TEXT("yaraq-rooftop"),0x7b610000, {189.6,169.2,4.8},{184.205,156.446,16.5}},
			{TEXT("yaraq-deadwood"),0x7d650000,{92,49,2.8},{101.42,62.09,18.5}},
			{TEXT("yaraq-palms"),0x7c6a0000,{160,137,2.4},{164,146,14.4}},
			{TEXT("samsur"),0x977b0000, {47,68,3.6},{72,104,6}},
			{TEXT("stonehold"),0x64d50000, {137,30,3.8},{111,52,84}},
			{TEXT("sawato"),0xc95b0000, {57,57,7},{104,49,28}},
			{TEXT("arwic"),0xc6a90000, {72,65,5},{88,95,49}},
			{TEXT("stonehold-pines"),0x64d50000, {116,7,5.5},{104,60,88}},
			{TEXT("sawato-overlook"),0xc95b0000, {65,56,19},{107,52,31}}
		};
		for (const FView& View : Views)
		{
			if (!Dat->GetOrBuildLandblockMesh(View.Block,100) || !Land->LoadLandblock(View.Block,100,true,1,0)) return false;
			for (const auto& Item : Land->PendingScenery)
			{
				const int32 Placement = Item.bRegionDesc || Item.bBuilding ? 0 : UACEDatSubsystem::ACEPlacementResting;
				Dat->GetOrBuildSetupMeshShared(Item.ModelId,100,Placement);
				Land->TrySpawnOneScenery(Dat,Item);
			}
			Land->PendingScenery.Reset();
			const bool Rooftop = View.Block == 0x7b610000;
			TArray<AACEEnvCellActor*> Rooms;
			if (Rooftop)
			{
				const auto Info = Dat->GetLandblockInfo(View.Block);
				if (!TestTrue(TEXT("Rooftop building has interior cells"),Info.IsValid() && Info->NumCells > 0)) return false;
				for (uint32 I=0; I<Info->NumCells; ++I)
				{
					const uint32 CellId = View.Block|(0x100+I);
					if (!TestNotNull(TEXT("Build rooftop room geometry"),Dat->GetOrBuildEnvCellMesh(CellId,100))) return false;
					auto* Room = World->SpawnActor<AACEEnvCellActor>();
					Rooms.Add(Room);
					if (!TestTrue(TEXT("Load rooftop building interior"),Room->LoadEnvCell(CellId,Land->GetActorLocation(),100))) return false;
					Room->SetEnvCellHiddenInGame(false);
				}
			}
			TMap<UInstancedStaticMeshComponent*,TArray<FTransform>> Placed;
			TMap<UInstancedStaticMeshComponent*,UStaticMesh*> Originals, Scrub;
			int32 Count = 0;
			for (const auto& Pair : Land->SceneryHisms)
			{
				if (!Pair.Value) continue;
				const uint32 Setup = uint32(Pair.Key>>32);
				if (Setup == 0x02000246 || Setup == 0x020004bb || Setup == 0x020004b2 || Setup == 0x020004bc || Setup == 0x020004c7)
					Scrub.Add(Pair.Value,Pair.Value->GetStaticMesh());
				if (!IsMappedTree(Setup)) continue;
				Originals.Add(Pair.Value,Pair.Value->GetStaticMesh());
				auto& Transforms = Placed.Add(Pair.Value);
				for (int32 I=0; I<Pair.Value->GetInstanceCount(); ++I)
					Pair.Value->GetInstanceTransform(I,Transforms.AddDefaulted_GetRef(),true);
				Count += Transforms.Num();
			}
			FVector Eye = Land->GetActorLocation()+FACEPosition::AceVectorToUnreal(View.Eye,100);
			float Height;
			if (!Dat->SampleOutdoorGroundZ(Eye.X,Eye.Y,100,Height)) return false;
			Eye.Z = Height + 100*View.Eye.Z;
			const FVector Focus = Land->GetActorLocation()+FACEPosition::AceVectorToUnreal(View.LookAt,100);
			Camera->SetWorldLocationAndRotation(Eye,(Focus-Eye).Rotation());
			Sky->SetActorLocation(Eye);
			Sky->UpdateSky(.5f,0,false);
			const FString Prefix(View.Name);
			const auto Classic = Capture(*(Prefix+TEXT("-ac1.png")));
			auto Doorway = [&](const TCHAR* Filename)
			{
				const FVector From = Land->GetActorLocation()+FACEPosition::AceVectorToUnreal({189.6,163.2,11.7},100);
				const FVector To = Land->GetActorLocation()+FACEPosition::AceVectorToUnreal({184.205,156.446,12.2},100);
				Camera->SetWorldLocationAndRotation(From,(To-From).Rotation());
				Capture(Filename);
				Camera->SetWorldLocationAndRotation(Eye,(Focus-Eye).Rotation());
			};
			if (Rooftop) Doorway(TEXT("yaraq-doorway-ac1.png"));
			Toggle(TEXT("LegionsTrees"),true);
			Dat->ConsumeLandMeshReloadRequest();
			if (View.Block == 0x64d50000) TestTrue(TEXT("Stonehold scrub is in the scene"),!Scrub.IsEmpty());
			for (const auto& Pair : Scrub)
				TestTrue(TEXT("Toggle retains the native scrub, deadwood and young palm meshes"),Pair.Key->GetStaticMesh()==Pair.Value);
			for (const auto& Pair : Placed)
			{
				TestTrue(TEXT("Live tree uses imported geometry"),Pair.Key->GetStaticMesh()!=Originals.FindChecked(Pair.Key));
				const FVector OldExtent=Originals.FindChecked(Pair.Key)->GetBounds().BoxExtent;
				const FVector NewExtent=Pair.Key->GetStaticMesh()->GetBounds().BoxExtent;
				TestTrue(TEXT("Canopy fits within its original footprint"),NewExtent.X <= OldExtent.X+.5 && NewExtent.Y <= OldExtent.Y+.5);
				TestEqual(TEXT("Tree count is stable"),Pair.Key->GetInstanceCount(),Pair.Value.Num());
				for (int32 I=0; I<Pair.Value.Num(); ++I)
				{
					FTransform T;
					TestTrue(TEXT("Tree keeps its original placement"),Pair.Key->GetInstanceTransform(I,T,true) && T.Equals(Pair.Value[I]));
				}
			}
			const int32 Diff=Compare(Classic,Capture(*(Prefix+TEXT("-legions.png"))));
			if (Rooftop)
			{
				bool Found = false;
				for (const auto& Pair : Land->SceneryHisms)
				{
					if (uint32(Pair.Key>>32) != 0x02000322 || !Originals.Contains(Pair.Value)) continue;
					const FVector PlantingPoint = Land->GetActorLocation()+FACEPosition::AceVectorToUnreal({184.205,156.446,14.605},100);
					for (const FTransform& Instance : Placed.FindChecked(Pair.Value))
					{
						if (!Instance.GetLocation().Equals(PlantingPoint,1.)) continue;
						const FBox Original = Originals.FindChecked(Pair.Value)->GetBoundingBox();
						const FBox Imported = Pair.Value->GetStaticMesh()->GetBoundingBox();
						TestTrue(TEXT("Rooftop root ends within the original AC1 base"),Imported.Min.Z >= Original.Min.Z - .01);
						AddInfo(FString::Printf(TEXT("Rooftop tree relative to planting point (cm): AC1 base %.2f, AC2 base %.2f, AC2 crown %.2f"),Original.Min.Z,Imported.Min.Z,Imported.Max.Z));
						const double Clearance = Imported.TransformBy(Instance).Min.Z - Rooms[0]->CellMesh->Bounds.GetBox().Max.Z;
						TestTrue(TEXT("AC2 tree clears the ceiling of room 7B610100"),Clearance > 0.);
						AddInfo(FString::Printf(TEXT("Rooftop tree clearance above room 7B610100: %.2f cm"),Clearance));
						Found = true;
					}
				}
				TestTrue(TEXT("Found the planted rooftop tree"),Found);
				Doorway(TEXT("yaraq-doorway-legions.png"));
			}
			Toggle(TEXT("LegionsTrees"),false);
			Dat->ConsumeLandMeshReloadRequest();
			const int32 Restored=Compare(Classic,Capture(*(Prefix+TEXT("-restored.png"))));
			TestTrue(TEXT("Town has mapped tree instances"),Count>0);
			if (View.Block == 0x7d650000 || View.Block == 0x7c6a0000)
				TestTrue(TEXT("Protected Yaraq scenery present in screenshot landblock"),!Scrub.IsEmpty());
			else TestTrue(TEXT("Imported tree artwork is visible"),Diff>1000);
			TestTrue(TEXT("Turning trees off restores the view"),Restored<100);
			AddInfo(FString::Printf(TEXT("%s: %d mapped instances, %d changed pixels, %d after restoration"),View.Name,Count,Diff,Restored));
			for (auto* Room : Rooms) Room->Destroy();
			if (RooftopOnly) return !HasAnyErrors();
		}
		TArray<uint32> Models=Dat->GetPortalDat()->GetFileIds(0x01);
		Models.Append(Dat->GetPortalDat()->GetFileIds(0x02));
		TMap<uint32,FBox> Bounds;
		for (uint32 Id : Models)
		{
			if (!IsMappedTree(Id)) continue;
			Dat->GetOrBuildSetupMeshShared(Id,100,0);
			auto* Mesh=Dat->GetOrCreateSetupStaticMesh(Id,100,true,0,false,false,true);
			if (!TestNotNull(*FString::Printf(TEXT("Original tree %08x builds"),Id),Mesh)) return false;
			Bounds.Add(Id,Mesh->GetBoundingBox());
		}
		Toggle(TEXT("LegionsTrees"),true);
		Dat->ConsumeLandMeshReloadRequest();
		for (const auto& Pair : Bounds)
		{
			const FTree* Tree=ActiveEnvironment()->FindTree(Pair.Key);
			auto* Mesh=Dat->GetOrCreateSetupStaticMesh(Pair.Key,100,true,0,false,false,true);
			const FString ContextText=FString::Printf(TEXT("Tree %08x"),Pair.Key);
			if (!TestNotNull(*(ContextText+TEXT(" decodes")),Tree) || !TestNotNull(*(ContextText+TEXT(" renders")),Mesh)) return false;
			if (!TestEqual(*(ContextText+TEXT(" material slots")),Mesh->GetStaticMaterials().Num(),Tree->Sections.Num())) continue;
			int32 Triangles=0;
			for (int32 I=0; I<Tree->Sections.Num(); ++I)
			{
				const auto& S=Tree->Sections[I];
				Triangles+=S.Indices.Num()/3*(S.Masked && !Mesh->GetMaterial(I)->IsTwoSided() ? 2 : 1);
			}
			TestEqual(*(ContextText+TEXT(" uses the native draw geometry")),Mesh->GetNumTriangles(0),Triangles);
			const FVector Extent = Mesh->GetBounds().BoxExtent;
			TestTrue(*(ContextText+TEXT(" stays inside the AC1 crown footprint")),Extent.X <= Pair.Value.GetExtent().X+.5 && Extent.Y <= Pair.Value.GetExtent().Y+.5);
			const FBox Imported = Mesh->GetBoundingBox();
			TestTrue(*(ContextText+TEXT(" stays within the AC1 root depth")),Imported.Min.Z >= Pair.Value.Min.Z-.02);
			const bool Planted = Pair.Value.Min.Z < 0 && Tree->Bounds.Min.Z < 0 && Pair.Value.Max.Z > 0 && Tree->Bounds.Max.Z > UE_SMALL_NUMBER;
			const double Scale = Planted ? Imported.Max.Z/Tree->Bounds.Max.Z : Extent.Z/Tree->Bounds.GetExtent().Z;
			const auto& LOD = Mesh->GetRenderData()->LODResources[0];
			const auto& Positions = LOD.VertexBuffers.PositionVertexBuffer;
			auto LongestEdge = [](int32 Count, auto Vertex)
			{
				double MaxSquared = 0.;
				for (int32 I=0; I+2<Count; I+=3)
					for (int32 J=0; J<3; ++J)
						MaxSquared=FMath::Max(MaxSquared,double((Vertex(I+J)-Vertex(I+(J+1)%3)).SizeSquared2D()));
				return FMath::Sqrt(MaxSquared);
			};
			const double MeshEdge=LongestEdge(LOD.IndexBuffer.GetNumIndices(),[&](int32 I){return Positions.VertexPosition(LOD.IndexBuffer.GetIndex(I));});
			double NativeEdge=0.;
			for (const auto& Section : Tree->Sections)
				NativeEdge=FMath::Max(NativeEdge,LongestEdge(Section.Indices.Num(),[&](int32 I){return Tree->Vertices[Section.Indices[I]];}));
			TestTrue(*(ContextText+TEXT(" preserves the horizontal crown scale")),FMath::IsNearlyEqual(MeshEdge,NativeEdge*Scale,1.));
		}
		Toggle(TEXT("LegionsTrees"),false);
		Dat->ConsumeLandMeshReloadRequest();
		AddInfo(FString::Printf(TEXT("Checked the draw geometry and dimensions of all %d mapped DAT setups"),Bounds.Num()));
		return !HasAnyErrors();
	}
	if (FParse::Param(FCommandLine::Get(),TEXT("AC2YaraqTrees")))
	{
		if (!TestEqual(TEXT("All nine town tree placements are mapped"),Trees,9)) return false;
		if (!Land->LoadLandblock(Block,100,true,1,0)) return false;
		for (const auto& Item : Land->PendingScenery)
		{
			const int32 Placement = Item.bRegionDesc || Item.bBuilding ? 0 : UACEDatSubsystem::ACEPlacementResting;
			Dat->GetOrBuildSetupMeshShared(Item.ModelId,100,Placement);
			if (!TestTrue(TEXT("Build town scenery from its placed setup"),Land->TrySpawnOneScenery(Dat,Item))) return false;
		}
		Land->PendingScenery.Reset();
		// Match the game viewport display gamma in a non-sRGB target so the LDR output
		// gets one display transform when saved.
		Target->TargetGamma = GEngine->GetDisplayGamma();
		Target->InitCustomFormat(960,540,PF_B8G8R8A8,true);
		const FVector Eye = Land->GetActorLocation() + FVector(-14500,8200,2000);
		const FVector Focus = Land->GetActorLocation() + FVector(-10600,10250,1670);
		Camera->SetWorldLocationAndRotation(Eye,(Focus-Eye).Rotation());
		auto* Sky = World->SpawnActor<AACESkyDomeActor>();
		Sky->SetActorLocation(Eye);
		Sky->DatSubsystem = Dat;
		if (!TestTrue(TEXT("Load the town sky from the retail DAT"),Sky->TryBuild())) return false;
		Sky->UpdateSky(.5f,0,false);
		TMap<UInstancedStaticMeshComponent*,UStaticMesh*> OriginalMeshes;
		int32 Instances=0;
		for (const auto& Pair : Land->SceneryHisms)
		{
			if (uint32(Pair.Key >> 32) != 0x02000322 || !Pair.Value) continue;
			OriginalMeshes.Add(Pair.Value,Pair.Value->GetStaticMesh());
			Instances += Pair.Value->GetInstanceCount();
		}
		if (!TestEqual(TEXT("Fixed and region trees are present in the rendered town"),Instances,9)) return false;
		const auto ClassicTrees = Capture(TEXT("08-yaraq-trees-ac1.png"));
		Toggle(TEXT("LegionsTrees"),true);
		Dat->ConsumeLandMeshReloadRequest();
		for (const auto& Pair : OriginalMeshes)
		{
			UStaticMesh* NewMesh = Pair.Key->GetStaticMesh();
			if (!TestTrue(TEXT("Live instance component switched to imported geometry"),NewMesh && NewMesh != Pair.Value)) return false;
			TestEqual(TEXT("Legions town tree uses its native bark and foliage atlas"),NewMesh->GetStaticMaterials().Num(),1);
			const UBodySetup* OldBody = Pair.Value->GetBodySetup();
			const UBodySetup* NewBody = NewMesh->GetBodySetup();
			TestEqual(TEXT("Town tree keeps its collision"),NewBody != nullptr,OldBody != nullptr);
			if (OldBody && NewBody)
				TestEqual(TEXT("Same number of authored simple collision elements"),
					NewBody->AggGeom.GetElementCount(),OldBody->AggGeom.GetElementCount());
		}
		const auto LegionsTrees = Capture(TEXT("09-yaraq-trees-legions.png"));
		const auto Art = ActiveEnvironment();
		for (uint32 Id : {0x02000257u,0x020003eau})
		{
			const FTree* ExpectedTree = Art->FindTree(Id);
			if (!TestNotNull(TEXT("Nearby tree decoded"),ExpectedTree)) return false;
			Dat->GetOrBuildSetupMeshShared(Id,100,0);
			UStaticMesh* Mesh = Dat->GetOrCreateSetupStaticMesh(Id,100,true,0,false,false,true);
			if (!Mesh || !TestEqual(TEXT("Nearby setup uses all Legions materials"),Mesh->GetStaticMaterials().Num(),ExpectedTree->Sections.Num())) return false;
			int32 ExpectedTriangles = 0;
			for (int32 I=0; I<ExpectedTree->Sections.Num(); ++I)
			{
				const auto& Section = ExpectedTree->Sections[I];
				const auto* Material = Mesh->GetMaterial(I);
				if (!TestNotNull(TEXT("Tree material resolves"),Material)) return false;
				TestEqual(TEXT("Leaf cutouts and opaque bark match their assigned slot"),
					Material->GetBlendMode()==BLEND_Masked,Section.Masked);
				ExpectedTriangles += Section.Indices.Num()/3 * (Section.Masked && !Material->IsTwoSided() ? 2 : 1);
			}
			TestEqual(TEXT("Nearby setup uses the imported draw mesh"),Mesh->GetNumTriangles(0),ExpectedTriangles);
		}
		const int32 Changed = Compare(ClassicTrees,LegionsTrees);
		TestTrue(TEXT("Replacement is visible from the same Yaraq camera"),Changed > 3000);
		Toggle(TEXT("LegionsTrees"),false);
		Dat->ConsumeLandMeshReloadRequest();
		const int32 Restored = Compare(ClassicTrees,Capture(TEXT("10-yaraq-trees-restored.png")));
		TestTrue(TEXT("Turning trees off restores the original view"),Restored < 100);
		AddInfo(FString::Printf(TEXT("Yaraq town: %d trees, %d changed pixels, %d after restoring AC1"),Instances,Changed,Restored));
		return !HasAnyErrors();
	}
	auto CaptureUplandFromAbove = [&](const TCHAR* Name)
	{
		const FTransform CloseView = Camera->GetComponentTransform();
		Camera->SetWorldLocationAndRotation(Feet+FVector(2100,-1600,1500),FRotator(-27,145,0));
		Capture(Name);
		Camera->SetWorldTransform(CloseView);
	};
	const auto Classic=Capture(TEXT("01-classic.png"));
	if (Upland) CaptureUplandFromAbove(TEXT("upland-distant-classic.png"));
	Toggle(TEXT("LegionsGround"),true);
	Toggle(TEXT("LegionsGrass"),true);
	Toggle(TEXT("LegionsTrees"),true);
	TMap<UMaterialInstanceDynamic*,UTexture*> OldTextures;
	for (int32 I=0; I<Land->TerrainMesh->GetNumSections(); ++I)
		if (auto* Mid=Cast<UMaterialInstanceDynamic>(Land->TerrainMesh->GetMaterial(I)))
		{
			UTexture* Texture=nullptr;
			if (Mid->GetTextureParameterValue(TEXT("ACETexture"),Texture)) OldTextures.Add(Mid,Texture);
		}
	if (!TestFalse(TEXT("Yaraq has textured mesh sections"),OldTextures.IsEmpty())) return false;
	Dat->ConsumeLandMeshReloadRequest();
	const double Deadline = FPlatformTime::Seconds()+45;
	while (!OldTextures.IsEmpty() && FPlatformTime::Seconds()<Deadline)
	{
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		for (auto It=OldTextures.CreateIterator();It;++It)
		{
			UTexture* Current=nullptr;
			if (It.Key()->GetTextureParameterValue(TEXT("ACETexture"),Current) && Current && Current!=It.Value()) It.RemoveCurrent();
		}
		if (!OldTextures.IsEmpty()) FPlatformProcess::Sleep(.01f);
	}
	TestTrue(TEXT("Existing Yaraq materials received the selected artwork"),OldTextures.IsEmpty());
	TestTrue(TEXT("Changing artwork retained the live terrain and collision"),Terrain==Dat->FindLandblockMesh(Block));
	const auto Imported=Capture(TEXT("02-terrain.png"));
	const int32 GroundChange=Compare(Classic,Imported);
	TestTrue(TEXT("New ground is visible from the same camera"),GroundChange>10000);
	if (Upland)
	{
		CaptureUplandFromAbove(TEXT("upland-distant-legions.png"));
		TMap<UMaterialInstanceDynamic*,float> Scales;
		for (int32 I=0; I<Land->TerrainMesh->GetNumSections(); ++I)
			if (auto* Mid=Cast<UMaterialInstanceDynamic>(Land->TerrainMesh->GetMaterial(I)))
			{
				float Scale=0.f;
				if (Mid->GetScalarParameterValue(TEXT("GroundDetailScale"),Scale) && Scale>0)
				{
					Scales.Add(Mid,Scale);
					Mid->SetScalarParameterValue(TEXT("GroundDetailScale"),0.f);
				}
			}
		const auto DiffuseOnly=Capture(TEXT("upland-native-diffuse.png"));
		CaptureUplandFromAbove(TEXT("upland-distant-diffuse.png"));
		const int32 DetailChange=Compare(Imported,DiffuseOnly);
		TestTrue(TEXT("Native detail visibly changes the close ground"),DetailChange>1000);
		AddInfo(FString::Printf(TEXT("Native 3 m detail changed %d pixels at the fixed upland camera"),DetailChange));
		for (const auto& Pair : Scales) Pair.Key->SetScalarParameterValue(TEXT("GroundDetailScale"),Pair.Value);
		return !HasAnyErrors();
	}
	Owner->SetActorHiddenInGame(true);
	// Both local scene selectors occur on Yaraq's grass texture. Look from the
	// open ground toward the meadow in the northwest corner of the same block.
	Feet=Land->GetActorLocation()+FVector(-800,16900,0);
	if (!Dat->SampleOutdoorGroundZ(Feet.X,Feet.Y,100,GroundZ)) return false;
	Feet.Z=GroundZ;
	Camera->SetWorldLocationAndRotation(Feet+FVector(-1300,-1700,340), FRotator(-9,52,0));
	// This meadow straddles a block corner. Stream all four terrain meshes
	// as the normal presenter would before testing scenery footprints.
	for (uint32 Id : {0x7C640000u,0x7C650000u,0x7D650000u})
	{
		auto* Neighbour=World->SpawnActor<AACELandblockActor>();
		if (!Dat->GetOrBuildLandblockMesh(Id,100) || !Neighbour->LoadLandblock(Id,100,false,1,0)) return false;
	}
	Target->TargetGamma=GEngine->GetDisplayGamma();
	Target->InitCustomFormat(960,540,PF_B8G8R8A8,true);
	auto* FieldSky=World->SpawnActor<AACESkyDomeActor>();
	FieldSky->DatSubsystem=Dat;
	if (!TestTrue(TEXT("Load the Yaraq day sky"),FieldSky->TryBuild())) return false;
	FieldSky->SetActorLocation(Camera->GetComponentLocation());
	FieldSky->UpdateSky(.5f,0,false);
	const auto Bare=Capture(TEXT("02-meadow-off.png"));
	Toggle(TEXT("LegionsGroundCover"),true);
	const auto Art=ActiveEnvironment();
	if (!TestTrue(TEXT("Native meadow rules and grass atlas loaded"),Art->Meadow.Rules.Num()==36 && Art->GrassCards.Width>0)) return false;
	auto* Presenter=NewObject<UACETerrainPresenterComponent>(Owner);
	Owner->AddInstanceComponent(Presenter); Presenter->RegisterComponent();
	Presenter->bHasKnownCell=true;
	Presenter->LastKnownCellId=Block|0xD;
	Presenter->Client=Game->GetSubsystem<UACEClientSubsystem>();
	FACEPosition ServerPosition;
	ServerPosition.CellId=Presenter->LastKnownCellId;
	ServerPosition.Location=FVector(38,105,GroundZ/100.);
	Presenter->Client->GetSession()->SetLocalPosition(ServerPosition);

	auto* Pawn=World->SpawnActor<APawn>();
	auto* Capsule=NewObject<UCapsuleComponent>(Pawn);
	Pawn->SetRootComponent(Capsule); Pawn->AddInstanceComponent(Capsule);
	Capsule->InitCapsuleSize(35,90); Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Capsule->RegisterComponent();
	auto* PC=World->SpawnActor<APlayerController>();
	World->AddController(PC); PC->Possess(Pawn);
	auto* Eye=World->SpawnActor<ACameraActor>();
	if (!PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();
		PC->PlayerCameraManager->InitializeFor(PC);
	}
	// The isolated world has no viewport/local player to advance the camera cache.
	PC->PlayerCameraManager->bUseClientSideCameraUpdates=false;
	PC->SetViewTarget(Eye);
	auto Walk=[&](FVector Foot,FVector View)
	{
		Pawn->SetActorLocation(Foot+FVector(0,0,90));
		Eye->SetActorLocation(View);
		PC->PlayerCameraManager->UpdateCamera(1.f/30);
		Presenter->TickComponent(1.f/30,LEVELTICK_All,nullptr);
	};
	const FVector CameraPos=Camera->GetComponentLocation();
	for (int I=0; I<180; ++I) Walk(Feet+FVector(0,0,500),CameraPos);
	if (!TestTrue(TEXT("Outdoor presenter activated grass"),Presenter->LegionsGrass.IsValid())) return false;
	auto& Cover=*Presenter->LegionsGrass;
	for (const FIntPoint Key : {FIntPoint(2415,1930),FIntPoint(2408,1930)})
	{
		FACELegionsGroundCover::FPatch Empty;
		TestTrue(TEXT("Resident bare ground evaluated"),Cover.BuildPatch(*Dat,*Owner,Key,100,Empty));
		TestEqual(TEXT("Beach and town clearing stay open"),Empty.TuftCount,0);
	}
	FACELegionsGroundCover::FPatch Duplicate;
	TestTrue(TEXT("Resident edge of a meadow vertex evaluated"),Cover.BuildPatch(*Dat,*Owner,FIntPoint(2401,1937),100,Duplicate));
	TestEqual(TEXT("One AC1 grass vertex does not repeat the AC2 scene across its footprint"),Duplicate.TuftCount,0);
	// Open grass around the player's reported 0x7D66001C has scenery variant zero.
	if (!Dat->GetOrBuildLandblockMesh(0x7D660000,100)) return false;
	int32 Occupied=0, Total=0;
	for (int X=4; X<=12; ++X) for (int Y=4; Y<=12; ++Y)
	{
		const FIntPoint Key(125*192/10+X,FMath::RoundToInt(102*192/10.)+Y);
		FACELegionsGroundCover::FPatch Sample;
		if (!TestTrue(TEXT("Reported meadow patch evaluated"),Cover.BuildPatch(*Dat,*Owner,Key,100,Sample))) return false;
		Occupied += Sample.TuftCount>0;
		++Total;
		if (auto* M=Sample.Mesh.Get()) M->DestroyComponent();
	}
	AddInfo(FString::Printf(TEXT("Open Yaraq field: %d of %d patch sites contain grass"),Occupied,Total));
	TestTrue(TEXT("Open field has distributed clumps and clearings"),Occupied>=Total/10 && Occupied<=Total/2);
	const auto Meadow=Capture(TEXT("03-grass.png"));
	TestTrue(TEXT("Meadow renders in the same view as open ground"),Compare(Bare,Meadow)>2000);
	FIntPoint ChosenKey=FIntPoint::ZeroValue; FVector Crossing=Feet; double Best=MAX_dbl;
	int Tufts=0;
	for (const auto& Pair : Cover.Patches)
	{
		Tufts+=Pair.Value.TuftCount;
		if (!Pair.Value.Mesh.IsValid()) continue;
		auto* Mesh=Pair.Value.Mesh.Get();
		const auto& Sec=*Mesh->GetProcMeshSection(0);
		for (const auto& V : Sec.ProcVertexBuffer)
		{
			// Find a tip inside the card. A walk between the two end columns used to miss it.
			if (V.UV1.X<.7 || V.UV1.Y<40) continue;
			const FVector P=Mesh->GetComponentTransform().TransformPosition(V.Position);
			const double Dist=FVector::DistSquared2D(P,Feet);
			if (Dist<Best) { Best=Dist; Crossing=P; ChosenKey=Pair.Key; }
		}
	}
	AddInfo(FString::Printf(TEXT("Meadow stream center %s, resident %d, tufts %d, closest blade %.2f m, pawn %s, view %s"),
		*Cover.StreamCenter.Get(FIntPoint::ZeroValue).ToString(),Cover.Patches.Num(),Tufts,FMath::Sqrt(Best)/100.,
		*Pawn->GetActorLocation().ToString(),*PC->PlayerCameraManager->GetCameraLocation().ToString()));
	if (!TestTrue(TEXT("Nearby authored meadow has interactive grass"), Best<2000*2000)) return false;
	if (!Dat->SampleOutdoorGroundZ(Crossing.X,Crossing.Y,100,GroundZ)) return false;
	Crossing.Z=GroundZ;
	auto PatchMesh=Cover.Patches.FindChecked(ChosenKey).Mesh;
	TestFalse(TEXT("Game mode owns a visible grass actor"),PatchMesh->GetOwner()->IsHidden());
	const FVector NearView=Crossing+FVector(-310,-180,170);
	Camera->SetWorldLocationAndRotation(NearView,(Crossing+FVector(0,0,55)-NearView).Rotation());
	const FVector Start=Crossing-FVector(300,0,0);
	for (int I=0; I<35; ++I) Walk(Start,NearView);
	const auto Upright=Capture(TEXT("04-before-step.png"));
	for (int I=1; I<=15; ++I) Walk(FMath::Lerp(Start,Crossing,I/15.),NearView);
	const auto Bent=Capture(TEXT("05-walking.png"));
	const int StepChange=Compare(Upright,Bent);
	TestTrue(TEXT("Possessed pawn parts visible grass during a normal walk with a stale server position"),StepChange>150);
	for (int I=0; I<75; ++I) Walk(Start,NearView);
	TestTrue(TEXT("Departed grass recovers at the same camera"),Compare(Upright,Capture(TEXT("06-recovered.png")))<100);

	// Keep the capture fixed to measure only the depth-writing grass coverage.
	Camera->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Camera->ShowOnlyComponent(PatchMesh.Get());
	const FVector PatchCenter=PatchMesh->Bounds.Origin;
	Camera->SetWorldLocationAndRotation(PatchCenter+FVector(0,0,1600),FRotator(-90,0,0));
	auto AtRange=[&](float Metres,const TCHAR* Name)
	{
		Walk(Start,PatchCenter+FVector(Metres*100,0,0));
		return Capture(Name);
	};
	const auto Near=AtRange(140,TEXT("07-range-full.png"));
	const auto Fading=AtRange(156,TEXT("08-range-fading.png"));
	const auto Far=AtRange(170,TEXT("09-range-hidden.png"));
	const int FullPixels=Compare(Near,Far),FadePixels=Compare(Fading,Far);
	TestTrue(TEXT("Grass coverage falls before unload"),FullPixels>1000 && FadePixels>0 && FadePixels<FullPixels*.9);
	TestTrue(TEXT("Hidden grass remains resident past the fade"),PatchMesh.IsValid());
	Walk(Start,Crossing+FVector(25000,0,0));
	TestFalse(TEXT("The invisible patch retires after passing the retention ring"),Cover.Patches.Contains(ChosenKey));
	for (int I=0; I<220 && !Cover.Patches.Contains(ChosenKey); ++I) Walk(Start,PatchCenter+FVector(17000,0,0));
	const auto* Rebuilt=Cover.Patches.Find(ChosenKey);
	if (!TestTrue(TEXT("Returning grass is prefetched outside draw range"),Rebuilt && Rebuilt->Mesh.IsValid())) return false;
	Camera->ClearShowOnlyComponents(); Camera->ShowOnlyComponent(Rebuilt->Mesh.Get());
	const auto Arriving=AtRange(140,TEXT("10-just-loaded.png"));
	for (int I=0; I<24; ++I) Walk(Start,PatchCenter+FVector(14000,0,0));
	const auto Mature=Capture(TEXT("11-settled.png"));
	TestTrue(TEXT("Newly streamed visible grass fills in gradually"),
		Compare(Arriving,Far)<FullPixels*.25 && Compare(Mature,Far)>FullPixels*.95);
	if (FParse::Param(FCommandLine::Get(),TEXT("AC2MeadowWalkClip")))
	{
		Camera->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
		const FVector SideView=Crossing+FVector(-280,-390,210);
		Camera->SetWorldLocationAndRotation(SideView,(Crossing+FVector(0,0,80)-SideView).Rotation());
		FieldSky->SetActorLocation(SideView);
		for (int I=0; I<160; ++I) Walk(Start,SideView);
		FACEWorldObject Player;
		Player.Guid=12345; Player.SetupId=0x02000001; Player.MotionTableId=0x09000001; Player.bIsPlayer=true;
		auto* Avatar=World->SpawnActor<AACEWorldEntityActor>();
		Avatar->InitializeFromObject(Player,100,false);
		if (!Avatar->Appearance->ApplyWorldObject(Player,100,false)) return false;
		Avatar->SetActorRotation(FRotator(0,-90,0));
		Avatar->Appearance->SetLocomotionInput(1,0,false);
		for (int Frame=0; Frame<=48; ++Frame)
		{
			FVector Foot=Crossing+FVector(-240+10*Frame,0,0);
			if (!Dat->SampleOutdoorGroundZ(Foot.X,Foot.Y,100,GroundZ)) return false;
			Foot.Z=GroundZ;
			Walk(Foot,SideView); Walk(Foot,SideView);
			Avatar->SetActorLocation(Foot);
			Avatar->Appearance->TickComponent(1.f/15,LEVELTICK_All,nullptr);
			Capture(*FString::Printf(TEXT("walk-%02d.png"),Frame));
		}
	}
	Toggle(TEXT("LegionsGroundCover"),false); Walk(Start,NearView);
	TestTrue(TEXT("Toggle removes grass meshes"),!Presenter->LegionsGrass.IsValid());
	// Render the reported open field through the normal terrain presenter from a fixed camera.
	const uint32 FieldBlock=0x7D660000;
	Toggle(TEXT("LegionsTrees"),true);
	Dat->ConsumeLandMeshReloadRequest();
	if (!Dat->GetOrBuildLandblockMesh(FieldBlock,100) || !Land->LoadLandblock(FieldBlock,100,true,1,0)) return false;
	for (const auto& Item : Land->PendingScenery)
	{
		const int32 Placement=Item.bRegionDesc || Item.bBuilding ? 0 : UACEDatSubsystem::ACEPlacementResting;
		Dat->GetOrBuildSetupMeshShared(Item.ModelId,100,Placement);
		if (!Land->TrySpawnOneScenery(Dat,Item)) return false;
	}
	Land->PendingScenery.Reset();
	ServerPosition.CellId=Presenter->LastKnownCellId=FieldBlock|0x1C;
	ServerPosition.Location=FVector(78.151073,79.819825,10.006);
	Presenter->Client->GetSession()->SetLocalPosition(ServerPosition);
	const FVector FieldFeet=ServerPosition.ToUnrealLocation(100.f);
	const FVector FieldEye=FieldFeet+FVector(610,-100,245);
	Camera->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	Camera->SetWorldLocationAndRotation(FieldEye,(FieldFeet+FVector(-1200,140,100)-FieldEye).Rotation());
	FieldSky->SetActorLocation(FieldEye);
	const auto OpenField=Capture(TEXT("field-grass-off.png"));
	Toggle(TEXT("LegionsGroundCover"),true);
	for (int I=0; I<190; ++I) Walk(FieldFeet,FieldEye);
	TestTrue(TEXT("The ground-cover toggle visibly populates the reported open field"),
		Compare(OpenField,Capture(TEXT("field-grass-on.png")))>2000);
	AddInfo(FString::Printf(TEXT("Yaraq: %d tufts, walk changed %d pixels; full/transition coverage %d/%d"),Tufts,StepChange,FullPixels,FadePixels));
	return !HasAnyErrors();
}
#endif
