#include "Mods/ACEPluginTerrain.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "UI/ACERadarVisuals.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool ACEPluginTerrain::Parse(const TSharedPtr<FJsonObject>& J, FACEPluginTerrain& Out, FString& Error)
{
    FACEPluginTerrain S;
    auto Bool=[&](const TCHAR* K,bool& V){return !J->HasField(K)||J->TryGetBoolField(K,V);};
    auto Number=[&](const TCHAR* K,float& V,float Min,float Max)
    {
        if(!J->HasField(K))return true;
        double N=0;if(!J->TryGetNumberField(K,N)||!FMath::IsFinite(N)||N<Min||N>Max)return false;
        V=N;return true;
    };
    auto Color=[&](const TCHAR* K,FLinearColor& C)
    {
        if(!J->HasField(K))return true;
        const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
        if(!J->TryGetArrayField(K,A)||A->Num()!=3)return false;
        for(int32 I=0;I<3;++I){double V;if(!(*A)[I]->TryGetNumber(V)||!FMath::IsFinite(V)||V<0||V>1)return false;C.Component(I)=V;}
        return true;
    };
    if(!Bool(TEXT("slopes"),S.Slopes)||!Bool(TEXT("water"),S.Water)||!Bool(TEXT("radar"),S.Radar)||!Bool(TEXT("dungeon"),S.Dungeon)
        ||!Number(TEXT("opacity"),S.Opacity,0,.8f)||!Number(TEXT("range"),S.Range,24,192)||!Number(TEXT("brightness"),S.Brightness,0,1)
        ||!Color(TEXT("slope_color"),S.SlopeColor)||!Color(TEXT("water_color"),S.WaterColor)
        ||!Color(TEXT("radar_color"),S.RadarColor)||!Color(TEXT("light_color"),S.LightColor))
    {Error=TEXT("Invalid terrain overlay: booleans, opacity 0..0.8, range 24..192, brightness/RGB 0..1 required");return false;}
    Out=S;return true;
}

FLinearColor ACEPluginTerrain::Shade(float NormalZ,float DepthAc,uint8 BlockWaterType,const FACEPluginTerrain& S)
{
    // Retail rejects slopes below floor_z and entirely-water landblocks.
    // Flooded cells in a coastal block are wadeable and must stay uncolored.
    FLinearColor C=FLinearColor::Transparent;
    if(S.Slopes && NormalZ<WalkableZ)
    {
        const float T=FMath::Clamp((WalkableZ-NormalZ)/WalkableZ,0.f,1.f);
        C=FMath::Lerp(FLinearColor(1,.8f,.04f),S.SlopeColor,T);C.A=S.Opacity;
    }
    if(S.Water && BlockWaterType==2)
    {C=S.WaterColor;C.A=S.Opacity;}
    return C;
}

void UACEPluginSubsystem::ClearPluginTerrain()
{
    if(TerrainOverlayActor)TerrainOverlayActor->Destroy();
    TerrainOverlayActor=nullptr;TerrainOverlayMesh=nullptr;TerrainRadarMesh=nullptr;TerrainDungeonLight=nullptr;NextTerrainBuild=0;TerrainGeometryValid=false;RadarGeometryValid=false;
}

void UACEPluginSubsystem::UpdatePluginTerrain()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ACEPluginTerrain);
    auto* C=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();auto* W=GetWorld();
    auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    const FACEClientPlugin* Owner=nullptr;
    if(W && C->GetSessionState()==EACESessionState::InWorld && (!PC||!PC->IsWorldTransitionActive()))
        for(const auto& P:Plugins)if(P->Enabled&&P->Running&&P->Terrain.Active()&&P->HasPresentation()&&P->Permissions.Contains(TEXT("terrain"))
            && P->Player==C->GetPlayerGuid()&&P->Server==C->GetServerName()&&P->VMSession.Pin()==C->GetSession())
        {Owner=P.Get();break;} // One shared surface; plugins cannot multiply overdraw.
    if(!Owner){if(TerrainOverlayActor)ClearPluginTerrain();return;}
    if(TerrainOverlayActor && TerrainOverlayActor->GetWorld()!=W)ClearPluginTerrain();
    auto* Dat=GetGameInstance()->GetSubsystem<UACEDatSubsystem>();const auto& S=Owner->Terrain;
    if(!TerrainOverlayActor)
    {
        TerrainOverlayActor=W->SpawnActor<AActor>();if(!TerrainOverlayActor)return;
        auto MakeMesh=[&]()
        {
            auto* M=NewObject<UProceduralMeshComponent>(TerrainOverlayActor);TerrainOverlayActor->AddInstanceComponent(M);
            M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->SetCastShadow(false);M->SetCanEverAffectNavigation(false);
            M->SetMaterial(0,Dat->GetTerrainOverlayMaterial());M->RegisterComponentWithWorld(W);return M;
        };
        TerrainOverlayMesh=MakeMesh();TerrainOverlayActor->SetRootComponent(TerrainOverlayMesh);TerrainRadarMesh=MakeMesh();
        TerrainDungeonLight=NewObject<UPostProcessComponent>(TerrainOverlayActor);TerrainOverlayActor->AddInstanceComponent(TerrainDungeonLight);
        TerrainDungeonLight->bUnbound=true;TerrainDungeonLight->Priority=50;TerrainDungeonLight->RegisterComponentWithWorld(W);
    }
    FACEPosition Pose=C->GetPlayerPosition();if(PC)PC->TryGetLocallyPredictedPosition(Pose);
    const FVector Origin=Pose.ToUnrealLocation();const bool Indoors=(uint32(Pose.CellId)&65535)>=256;
    // Transform is updated every frame, independently of Lua and mesh rebuilds.
    TerrainRadarMesh->SetWorldLocation(Origin);
    TerrainRadarMesh->SetVisibility(S.Radar);
    TerrainOverlayMesh->SetVisibility(!Indoors&&(S.Slopes||S.Water));
    TerrainDungeonLight->BlendWeight=Indoors&&S.Dungeon?1.f:0.f;
    auto& Post=TerrainDungeonLight->Settings;
    Post.bOverride_ColorGamma=Post.bOverride_ColorGain=true;
    Post.ColorGamma=FVector4(1+S.Brightness*.6f,1+S.Brightness*.6f,1+S.Brightness*.6f,1);
    Post.ColorGain=FVector4(1+S.LightColor.R*S.Brightness,1+S.LightColor.G*S.Brightness,1+S.LightColor.B*S.Brightness,1);
    const double Now=FPlatformTime::Seconds();if(Now<NextTerrainBuild)return;NextTerrainBuild=Now+.5;
    struct FMesh
    {
        TArray<FVector> V,N;TArray<int32> I;TArray<FLinearColor> C;
        void Tri(FVector A,FVector B,FVector D,FLinearColor Color)
        {const int32 Base=V.Num();V.Append({A,B,D});I.Append({Base,Base+1,Base+2});for(int J=0;J<3;++J){N.Add(FVector::UpVector);C.Add(Color);}}
        void Apply(UProceduralMeshComponent* M)
        {if(V.IsEmpty())M->ClearAllMeshSections();else M->CreateMeshSection_LinearColor(0,V,I,N,{},C,{},false);}
    };
    // Retain native geometry across callbacks. Replacing these sections every
    // half-second needlessly rebuilt render proxies even while standing still.
    const uint64 Revision=Dat->GetLandblockMeshRevision();
    const bool RebuildTerrain=!TerrainGeometryValid || TerrainBuiltIndoors!=Indoors
        || TerrainBuiltRevision!=Revision || !TerrainBuiltSettings.SameSurface(S)
        || FVector::DistSquared2D(TerrainBuiltOrigin,Origin)>FMath::Square(600.);
    const bool RebuildRadar=!RadarGeometryValid || RadarBuiltIndoors!=Indoors
        || TerrainBuiltSettings.Radar!=S.Radar || TerrainBuiltSettings.Opacity!=S.Opacity
        || TerrainBuiltSettings.RadarColor!=S.RadarColor;
    if(!RebuildTerrain && !RebuildRadar)return;
    FMesh Terrain,Radar;
    if(RebuildTerrain&&!Indoors&&(S.Slopes||S.Water))
    {
        const int32 BX=FMath::FloorToInt(-Origin.X/19200.),BY=FMath::FloorToInt(Origin.Y/19200.);
        // Memory-only lookups, nine blocks max. Never load terrain on behalf
        // of a visual aid, raycast the world, or send geometry through Lua.
        for(int32 X=FMath::Max(0,BX-1);X<=FMath::Min(255,BX+1);++X)for(int32 Y=FMath::Max(0,BY-1);Y<=FMath::Min(255,BY+1);++Y)
        {
            const auto* Land=Dat->FindLandblockMesh((uint32(X)<<24)|(uint32(Y)<<16));if(!Land)continue;
            const FVector Base(-X*19200.,Y*19200.,2.);
            for(const auto& Section:Land->Sections)for(int32 T=0;T+2<Section.Triangles.Num();T+=3)
            {
                const FVector A=Section.Vertices[Section.Triangles[T]],B=Section.Vertices[Section.Triangles[T+1]],D=Section.Vertices[Section.Triangles[T+2]];
                const FVector Center=(A+B+D)/3.;if(FVector::DistSquared2D(Base+Center,Origin)>FMath::Square(S.Range*100.))continue;
                const float Nz=FMath::Abs(FVector::CrossProduct(B-A,D-A).GetSafeNormal().Z);
                const float Depth=FACELandblockMeshBuilder::GetWaterDepthAc(*Land,-Center.X/100.,Center.Y/100.);
                const FLinearColor Color=ACEPluginTerrain::Shade(Nz,Depth,Land->BlockWaterType,S);
                if(Color.A>.001f)Terrain.Tri(A+Base,B+Base,D+Base,Color);
            }
        }
    }
    if(RebuildTerrain)
    {
        Terrain.Apply(TerrainOverlayMesh);TerrainGeometryValid=true;
        TerrainBuiltOrigin=Origin;TerrainBuiltRevision=Revision;TerrainBuiltIndoors=Indoors;++TerrainGeometryBuilds;
    }
    if(RebuildRadar&&S.Radar)
    {
        const double Radius=ACERadarVisuals::RangeAc(uint32(Pose.CellId))*100.;FLinearColor Color=S.RadarColor;Color.A=S.Opacity*.5f;
        for(int32 I=0;I<96;++I)
        {
            const double A=I*2*PI/96.,B=(I+1)*2*PI/96.;
            const FVector P(Radius*FMath::Cos(A),Radius*FMath::Sin(A),-12000),Q(Radius*FMath::Cos(B),Radius*FMath::Sin(B),-12000),Up(0,0,24000);
            Radar.Tri(P,Q,Q+Up,Color);Radar.Tri(P,Q+Up,P+Up,Color);
        }
    }
    if(RebuildRadar){Radar.Apply(TerrainRadarMesh);RadarGeometryValid=true;RadarBuiltIndoors=Indoors;++RadarGeometryBuilds;}
    TerrainBuiltSettings=S;
}
