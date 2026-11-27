#include "Sky/ACELegionsGroundCover.h"
#include "Sky/ACELegionsEnvironment.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
	// CLandscapeDefs: 160 m blocks / 8 cells / 2 subdivisions. AC1 cells
	// remain 24 m. Use continuous world metres so block edges never restart the grid.
	constexpr int32 PatchMetres = 10;
	// AC2's frill range is 160 m with an eight-metre alpha fade. The extra
	// rings cover the authored offsets and give the streamer time to build ahead.
	constexpr int32 LoadRadius = 19, KeepRadius = 21;
	constexpr uint32 SceneSeed = 0x923b1u;

	double SceneRandom(uint32 X, uint32 Y)
	{
		return (((Y*X*X*0x019a266fu - 0x4a6506dbu)*Y - X*0x6b3c5f11u - 0x39b625fbu) >> 16) / 65536.;
	}
	double GroupFrequency(uint32 X, uint32 Y, uint32 Seed)
	{
		const uint32 K = Seed + 0x01191e67u;
		return (((0x62433d7du - K*X*X*Y*Y*0x2bd38901u)*K - Y*0x4a6506dbu - X*0x6b3c5f11u) >> 16) / 65536.;
	}
	double PlacementRandom(uint32 X, uint32 Y, uint32 K)
	{
		return ((K*0x62433d7du - (X*Y*Y*K*K*0x2bd38901u + 0x6b3c5f11u)*X - Y*0x4a6506dbu) >> 16) / 65536.;
	}

	enum class EGround { Unloaded, Excluded, Meadow };
	struct FGroundSampler
	{
		UACEDatSubsystem& Dat;
		struct FBlock { const FACEBuiltLandblockMesh* Land = nullptr; uint64 Buildings = 0; bool HaveBuildings = false; };
		TMap<uint32,FBlock,TInlineSetAllocator<4>> Blocks;
		bool MissingLand = false;
		EGround Sample(double X, double Y, float& Z, FVector& N, bool SceneAnchor = false)
		{
			if (X < 0 || Y < 0 || X >= 256*192 || Y >= 256*192) return EGround::Excluded;
			const int32 BX = FMath::FloorToInt(X/192.), BY = FMath::FloorToInt(Y/192.);
			const uint32 Id = (uint32(BX)<<24) | (uint32(BY)<<16);
			auto* Block = Blocks.Find(Id);
			if (!Block)
			{
				Block = &Blocks.Add(Id);
				Block->Land = Dat.FindLandblockMesh(Id);
			}
			const auto* Land = Block->Land;
			if (!Land || !Land->bHasHeights || !Land->bHasTerrain) { MissingLand = true; return EGround::Unloaded; }
			const double LX = X-BX*192., LY = Y-BY*192.;
			const int32 VX = FMath::RoundToInt(LX/24.), VY = FMath::RoundToInt(LY/24.);
			const uint16 Info = Land->Terrain[VX*9 + VY];
			const uint32 Type = (Info>>2)&31;
			// AC1's open grassland, lush grass and patchy grass use the Legions
			// meadow family. Respect authored town clearings (scene 15), roads and water.
			const uint32 LocalScene = Info >> 11;
			if ((Type!=1 && Type!=3 && Type!=9) || LocalScene==15 || FACELandblockMeshBuilder::IsOnRoad(*Land,LX,LY) ||
				FACELandblockMeshBuilder::GetWaterDepthAc(*Land,LX,LY)>0) return EGround::Excluded;
			if (SceneAnchor && LocalScene == 1)
			{
				// Each authored AC1 vertex contributes one 10 m AC2 scene. Filling
				// its whole 24 m footprint repeats the same meadow up to nine times.
				const int32 GX = FMath::RoundToInt(((BX*8+VX)*24.)/PatchMetres);
				const int32 GY = FMath::RoundToInt(((BY*8+VY)*24.)/PatchMetres);
				if (X!=GX*PatchMetres || Y!=GY*PatchMetres) return EGround::Excluded;
			}
			// Open AC1 fields commonly use scene zero throughout a landblock. Scatter
			// individual 10 m frill scenes in these fields, leaving gaps between clumps.
			else if (SceneAnchor && GroupFrequency(FMath::RoundToInt(X/PatchMetres),
				FMath::RoundToInt(Y/PatchMetres),0x8c79b451u) >= .26)
				return EGround::Excluded;
			if (!Block->HaveBuildings)
			{
				Block->HaveBuildings = true;
				if (const auto BlockInfo = Dat.GetLandblockInfo(Id))
					for (const auto& Building : BlockInfo->Buildings)
					{
						const int32 I=FMath::FloorToInt(Building.Origin.X/24), J=FMath::FloorToInt(Building.Origin.Y/24);
						if (uint32(I)<8 && uint32(J)<8) Block->Buildings |= uint64(1)<<(I*8+J);
					}
			}
			if (Block->Buildings & (uint64(1)<<(FMath::FloorToInt(LX/24)*8+FMath::FloorToInt(LY/24)))) return EGround::Excluded;
			return FACELandblockMeshBuilder::SampleHeightAc(*Land,LX,LY,Z,&N) ? EGround::Meadow : EGround::Excluded;
		}
	};
}

void FACELegionsGroundCover::Clear()
{
	for (auto& Pair : Patches)
		if (auto* Mesh = Pair.Value.Mesh.Get()) Mesh->DestroyComponent();
	Patches.Reset();
	if (AActor* Actor = MeshOwner.Get()) Actor->Destroy();
	MeshOwner.Reset();
	PreviousFeet.Reset();
	StreamCenter.Reset();
	Pending.Reset();
	Unloaded.Reset();
	Material.Reset();
	Elapsed = 0;
	NextRetry = 0;
}

bool FACELegionsGroundCover::BuildPatch(UACEDatSubsystem& Dat, AActor& Owner, FIntPoint Key, float Scale, FPatch& Out)
{
	const double X = Key.X*double(PatchMetres), Y = Key.Y*double(PatchMetres);
	float GroundZ;
	FVector N;
	FGroundSampler Ground{Dat};
	const EGround Anchor = Ground.Sample(X,Y,GroundZ,N,true);
	if (Anchor != EGround::Meadow) return Anchor != EGround::Unloaded;
	const auto Art = ACELegionsSky::ActiveEnvironment();
	const auto& Meadow = Art->Meadow;
	if (Meadow.Indices.IsEmpty() || Meadow.Indices.Num()%6) return false;
	uint32 Seed = SceneSeed;
	if (SceneRandom(Key.X,Key.Y)*(Meadow.DirectWeight+Meadow.NestedWeight) >= Meadow.DirectWeight)
	{
		Seed *= Meadow.NestedScene*SceneSeed;
		if (GroupFrequency(Key.X,Key.Y,Seed) >= Meadow.NestedFrequency) return true;
	}
	Seed = (Meadow.DirectScene*Seed*SceneSeed + 0x03804309u)*Meadow.FrillScene*SceneSeed;
	const FVector Origin(-X*Scale,Y*Scale,0);
	TArray<FVector> Verts;
	TArray<FVector2D> Weights;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<int32> Indices;
	for (const auto& Rule : Meadow.Rules)
	{
		const uint32 Child = Seed + Rule.ChildIndex*0x2d41bf19u;
		if (GroupFrequency(Key.X,Key.Y,Child) >= Rule.Frequency) continue;
		auto Random = [&](uint32 Offset) { return PlacementRandom(Key.X,Key.Y,Child+Offset); };
		const double RX = Random(0x28d0642d);
		const FVector Offset = Rule.MinOffset + (Rule.MaxOffset-Rule.MinOffset)*FVector(RX,Random(0x62ade4f),Random(0x71d36d));
		// AC1 leaves a gap between vertex scenes. Taper the outer tufts of
		// each isolated scene so that gap follows the grass, not a square grid.
		const double RelativeRadius = Offset.SizeSquared2D() / FMath::Square(PatchMetres*.5);
		const double Density = .75 * (1.-FMath::SmoothStep(.05,1.45,RelativeRadius));
		if (Random(0x8590ce3u) >= Density) continue;
		const double CX = X+Offset.X, CY = Y+Offset.Y;
		auto Accept = [&](double SX, double SY, double MinZ, double MaxZ, float& H, FVector& Normal)
		{
			return Ground.Sample(SX,SY,H,Normal)==EGround::Meadow && Normal.Z >= MinZ && Normal.Z <= MaxZ;
		};
		if (!Accept(CX,CY,Rule.MinNormalZ,Rule.MaxNormalZ,GroundZ,N)) continue;
		const FVector Size = FMath::Lerp(Rule.MinScale,Rule.MaxScale,Random(0x515e71));
		bool Fits = true;
		if (Rule.CheckRadius > 0)
			for (int32 I=0; I<8 && Fits; ++I)
			{
				const double Angle = I*UE_PI/4, R = Rule.CheckRadius*Size.GetAbsMax();
				float H; FVector Normal;
				Fits = Accept(CX+R*FMath::Cos(Angle),CY+R*FMath::Sin(Angle),Rule.MinCheckZ,Rule.MaxCheckZ,H,Normal);
			}
		if (!Fits) continue;
		const FQuat Yaw(FVector::UpVector,-FMath::DegreesToRadians(FMath::Lerp(double(Rule.MinRotation),double(Rule.MaxRotation),RX)));
		const FQuat Rotation = FRotationMatrix::MakeFromYZ(FVector(0,N.Z,-N.Y),N).ToQuat()*Yaw;
		const int32 First = Verts.Num();
		const double Height = Meadow.Bounds.GetSize().Z;
		for (const FVector& V : Meadow.Vertices)
		{
			const FVector Base = Rotation.RotateVector(FVector(V.X,V.Y,Meadow.Bounds.Min.Z)*Size);
			const FVector Tip = Rotation.RotateVector(V*Size);
			float H; FVector Normal;
			if (Ground.Sample(CX+Base.X,CY+Base.Y,H,Normal)!=EGround::Meadow) { Fits=false; break; }
			Verts.Emplace(-(Offset.X+Tip.X)*Scale,(Offset.Y+Tip.Y)*Scale,(H+Tip.Z-Base.Z)*Scale);
			Normals.Emplace(-Normal.X,Normal.Y,Normal.Z);
			Weights.Emplace((V.Z-Meadow.Bounds.Min.Z)/Height, Height*Size.Z*Scale);
		}
		if (!Fits)
		{
			Verts.SetNum(First,EAllowShrinking::No);
			Weights.SetNum(First,EAllowShrinking::No);
			Normals.SetNum(First,EAllowShrinking::No);
			continue;
		}
		++Out.TuftCount;
		UVs.Append(Meadow.UVs);
		for (int32 Index : Meadow.Indices) Indices.Add(First+Index);
	}
	if (Ground.MissingLand) return false;
	if (!Out.TuftCount) return true;
	auto* Mat = Dat.GetImportedSceneryMaterial(0x40000008);
	if (!Mat) return false;
	Material = Mat;
	// The DAT has ten crossed cards, each represented by two wide triangles.
	// Give the original faces vertical strips so a foot crossing the middle can
	// part the blades. Interpolate the authored UVs and keep the original diagonal.
	TArray<int32> DrawIndices;
	for (int Face=0; Face<Indices.Num(); Face+=6)
	{
		// Paired triangles must divide the shared diagonal at identical intervals.
		double Width=0;
		for (int I=0; I<6; I+=3)
			for (int E=0; E<3; ++E)
				Width=FMath::Max(Width,FVector::Dist2D(Verts[Indices[Face+I+E]],Verts[Indices[Face+I+(E+1)%3]]));
		const int Cuts=FMath::Clamp(FMath::CeilToInt(Width/(.65*Scale)),1,24);
		for (int I=0; I<6; I+=3)
		{
			const int32* Tri=Indices.GetData()+Face+I;
			int Upright=0;
			for (int E=1; E<3; ++E)
				if (FVector::DistSquared2D(Verts[Tri[E]],Verts[Tri[(E+1)%3]]) <
					FVector::DistSquared2D(Verts[Tri[Upright]],Verts[Tri[(Upright+1)%3]])) Upright=E;
			const int A=Tri[(Upright+2)%3], B=Tri[Upright], C=Tri[(Upright+1)%3];
			auto EdgePoint=[&](int End, double T)
			{
				const FVector V=FMath::Lerp(Verts[A],Verts[End],T);
				const FVector Nrm=FMath::Lerp(Normals[A],Normals[End],T).GetSafeNormal();
				const FVector2D UV=FMath::Lerp(UVs[A],UVs[End],T), W=FMath::Lerp(Weights[A],Weights[End],T);
				Normals.Add(Nrm); UVs.Add(UV); Weights.Add(W);
				return Verts.Add(V);
			};
			int PrevL=A,PrevR=A;
			for (int Step=1; Step<=Cuts; ++Step)
			{
				const double T=double(Step)/Cuts;
				const int L=Step==Cuts ? B : EdgePoint(B,T), R=Step==Cuts ? C : EdgePoint(C,T);
				DrawIndices.Append({PrevL,L,R});
				if (Step>1) DrawIndices.Append({PrevL,R,PrevR});
				PrevL=L; PrevR=R;
			}
		}
	}
	// The terrain presenter belongs to AGameModeBase (a hidden AInfo). A
	// registered primitive on that actor stays hidden even with a valid mesh.
	if (!MeshOwner.IsValid())
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.OverrideLevel = Owner.GetLevel();
		Params.Owner = &Owner;
		MeshOwner = Owner.GetWorld()->SpawnActor<AActor>(AActor::StaticClass(),FTransform::Identity,Params);
		if (!MeshOwner.IsValid()) return false;
		MeshOwner->SetActorEnableCollision(false);
	}
	auto* Mesh = NewObject<UProceduralMeshComponent>(MeshOwner.Get());
	MeshOwner->AddInstanceComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
	Mesh->bUseAsOccluder = false;
	Mesh->SetWorldLocation(Origin);
	Mesh->SetCustomPrimitiveDataFloat(0,Elapsed);
	Mesh->SetBoundsScale(1.25f);
	Mesh->RegisterComponent();
	Mesh->CreateMeshSection_LinearColor(0,Verts,DrawIndices,Normals,UVs,Weights,{},{},{},{},false);
	Mesh->SetMaterial(0,Mat);
	Out.Mesh = Mesh;
	return true;
}

void FACELegionsGroundCover::Update(UACEDatSubsystem& Dat,AActor& Owner,FVector Feet,float BodyRadius,float BodyHeight,
	FVector Viewer,float Scale,float DeltaTime)
{
	const auto Art = ACELegionsSky::ActiveEnvironment();
	if (Art->ContentCrc != ContentCrc || Art->GrassCards.Mips.IsEmpty() || Scale<=0)
	{
		Clear();
		ContentCrc = Art->ContentCrc;
		if (Art->GrassCards.Mips.IsEmpty() || Scale<=0) return;
	}
	Elapsed += DeltaTime;
	const double Span = PatchMetres*Scale;
	const FIntPoint Center(FMath::RoundToInt(-Viewer.X/Span),FMath::RoundToInt(Viewer.Y/Span));
	if (!StreamCenter.IsSet() || Center!=StreamCenter.GetValue())
	{
		StreamCenter=Center;
		for (auto It=Patches.CreateIterator(); It; ++It)
			if ((It.Key()-Center).SizeSquared()>KeepRadius*KeepRadius)
			{
				if (auto* Mesh=It.Value().Mesh.Get()) Mesh->DestroyComponent();
				It.RemoveCurrent();
			}
		Pending.Reset(); Unloaded.Reset();
		for (int X=-LoadRadius; X<=LoadRadius; ++X)
			for (int Y=-LoadRadius; Y<=LoadRadius; ++Y)
			{
				const FIntPoint D(X,Y), Key=Center+D;
				if (D.SizeSquared()<=LoadRadius*LoadRadius && !Patches.Contains(Key)) Pending.Add(Key);
			}
		Pending.Sort([Center](FIntPoint A,FIntPoint B){return (A-Center).SizeSquared()>(B-Center).SizeSquared();});
	}
	if (Pending.IsEmpty() && Elapsed>=NextRetry)
	{
		Swap(Pending,Unloaded);
		NextRetry=Elapsed+.5;
	}
	const double Deadline=FPlatformTime::Seconds()+.0015;
	for (int Attempts=0; Attempts<40 && !Pending.IsEmpty(); ++Attempts)
	{
		const FIntPoint Key=Pending.Pop(EAllowShrinking::No);
		FPatch Patch;
		if (BuildPatch(Dat,Owner,Key,Scale,Patch)) Patches.Add(Key,MoveTemp(Patch));
		else Unloaded.Add(Key);
		if (FPlatformTime::Seconds()>=Deadline) break;
	}

	if (!PreviousFeet.IsSet() || FVector::DistSquared(Feet,*PreviousFeet)>FMath::Square(5.*Scale)) Trail=Feet;
	else Trail=FMath::Lerp(Trail,Feet,1.-FMath::Exp(-8.*DeltaTime));
	PreviousFeet=Feet;
	if (auto* Mat=Material.Get())
	{
		Mat->SetVectorParameterValue(TEXT("GrassContact"),FLinearColor(Feet.X,Feet.Y,Feet.Z,BodyRadius+.75f*Scale));
		Mat->SetVectorParameterValue(TEXT("GrassTrail"),FLinearColor(Trail.X,Trail.Y,Trail.Z,BodyHeight));
		Mat->SetVectorParameterValue(TEXT("GrassView"),FLinearColor(Viewer.X,Viewer.Y,Elapsed,Scale));
	}
}
