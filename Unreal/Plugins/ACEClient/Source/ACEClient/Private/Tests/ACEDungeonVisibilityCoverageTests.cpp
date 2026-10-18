#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEOutdoorPortalPlan.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Engine/GameInstance.h"
#include "Misc/CommandLine.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEDungeonVisibilityCoverageTest,"ACE.Rendering.DungeonVisibilityCoverage",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FACEDungeonVisibilityCoverageTest::RunTest(const FString&)
{
    auto* Dat=NewObject<UACEDatSubsystem>(NewObject<UGameInstance>());
    FString Directory=TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Directory);
    if(!TestTrue(TEXT("Load dungeon DAT content"),Dat->LoadDatDirectory(Directory)))return false;
    struct FRoom {uint32 Id;FBox Bounds{ForceInit};TArray<FVector> Triangles;};
    int32 Samples=0,Missed=0;double WorstMs=0;
    for(uint32 Block:{0x01E30000u,0x01430000u})
    {
        const auto Info=Dat->GetLandblockInfo(Block);if(!Info)return false;
        const FVector Origin=FACEPosition::AceVectorToUnreal(FVector((Block>>24)*192,((Block>>16)&255)*192,0),100);
        TArray<FRoom> Rooms;TArray<TPair<uint32,FVector>> Eyes;
        for(uint32 I=0;I<Info->NumCells;++I)
        {
            const uint32 Id=Block| (0x100+I);const auto* Mesh=Dat->GetOrBuildEnvCellMesh(Id,100);if(!Mesh)continue;
            const FTransform Frame=Mesh->GetCellLocalToLandblock(100)*FTransform(Origin);
            FRoom Room;Room.Id=Id;
            for(const auto& Section:Mesh->Sections)
            {
                if(Section.bFullyTransparent||Section.bClipMap||Section.bCollisionOnly)continue;
                for(int32 Index:Section.Triangles)
                {
                    const FVector P=Frame.TransformPosition(Section.Vertices[Index]);Room.Triangles.Add(P);Room.Bounds+=P;
                }
            }
            if(!Room.Triangles.IsEmpty())Rooms.Add(MoveTemp(Room));
            // Several heights and turns through the reported hall neighborhood,
            // including the wasp stairwell. Samples must be inside an authored cell.
            const FVector Center=(Mesh->LocalBoundsMin+Mesh->LocalBoundsMax)*.5;
            const FVector WorldCenter=Frame.TransformPosition(Center);
            const FVector AC=FACEPosition::AceVectorToUnreal(WorldCenter-Origin,.01f);
            const bool Area=Block==0x01E30000u?(AC.X>=70&&AC.X<=125&&AC.Y>=-90&&AC.Y<=10)
                :(Id>=0x01430150u&&Id<=0x01430171u);
            if(!Area)continue;
            for(double Height:{70.,170.})
            {
                const FVector Eye=Frame.TransformPosition(FVector(Center.X,Center.Y,Mesh->LocalBoundsMin.Z+Height));
                if(Dat->IsPointInsideEnvCell(Id,Eye,100))Eyes.Add({Id,Eye});
            }
            // Exercise the near-doorway cases as well as room centers. Only
            // accept points on this side of the authored volume, including
            // views closer to the aperture than the ordinary near clip plane.
            for(int32 Portal=0;Portal<Mesh->PortalApertureLocalVerts.Num();++Portal)
            {
                const auto& Vertices=Mesh->PortalApertureLocalVerts[Portal];
                if(Vertices.IsEmpty()||!Mesh->PortalApertureLocalNormals.IsValidIndex(Portal))continue;
                FVector Mid=FVector::ZeroVector;
                for(const auto& Vertex:Vertices)Mid+=Vertex;
                Mid/=Vertices.Num();
                for(double Offset:{2.,20.,80.})
                {
                    const FVector Eye=Frame.TransformPosition(Mid-Mesh->PortalApertureLocalNormals[Portal]*Offset);
                    if(Dat->IsPointInsideEnvCell(Id,Eye,100,0))Eyes.Add({Id,Eye});
                }
            }
        }
        for(const auto& Sample:Eyes)
        for(int32 Yaw=0;Yaw<360;Yaw+=30)
        {
            const FQuat Rotation=FRotator(0,Yaw,0).Quaternion();const FVector Eye=Sample.Value;
            FConvexVolume Frustum;
            for(const FVector& N:{FVector(-1,1,0),FVector(-1,-1,0),FVector(-.6,0,1),FVector(-.6,0,-1),FVector(-1,0,0)})
                Frustum.Planes.Add(FPlane(Eye,Rotation.RotateVector(N.GetSafeNormal())));
            Frustum.Init();TSet<int32> Visible;bool Outside=false;const double Begin=FPlatformTime::Seconds();
            ACEOutdoorPortalPlan::CollectIndoorPViewCells(*Dat,Sample.Key,Eye,Frustum,100,Visible,Outside,128);
            WorstMs=FMath::Max(WorstMs,(FPlatformTime::Seconds()-Begin)*1000);
            for(double Side:{-.7,0.,.7})for(double Up:{-.35,0.,.35})
            {
                const FVector End=Eye+Rotation.RotateVector(FVector(1,Side,Up).GetSafeNormal())*16000;
                // A dungeon has intentionally open-backed cells. Unrelated rooms
                // behind that empty space are not a visibility reference. Walk
                // exact ray/door intersections independently of frustum clipping.
                TMap<uint32,double> RayCells{{Sample.Key,0}};
                TArray<TPair<uint32,double>> Queue{{Sample.Key,0}};
                auto ApertureHit=[&](const FACEBuiltEnvCellMesh& Mesh,int32 Portal,double& Distance)
                {
                    if(!Mesh.PortalApertureLocalVerts.IsValidIndex(Portal))return false;
                    const auto& V=Mesh.PortalApertureLocalVerts[Portal];
                    const FTransform Frame=Mesh.GetCellLocalToLandblock(100)*FTransform(Origin);
                    for(int32 I=1;I+1<V.Num();++I)
                    {
                        FVector Hit,N;
                        if(FMath::SegmentTriangleIntersection(Eye,End,Frame.TransformPosition(V[0]),Frame.TransformPosition(V[I]),Frame.TransformPosition(V[I+1]),Hit,N))
                        {Distance=FVector::Dist(Eye,Hit);return true;}
                    }
                    return false;
                };
                for(int32 Q=0;Q<Queue.Num();++Q)
                {
                    const auto Entry=Queue[Q];const auto* Mesh=Dat->FindEnvCellMesh(Entry.Key,100);if(!Mesh)continue;
                    const FTransform Frame=Mesh->GetCellLocalToLandblock(100)*FTransform(Origin);
                    for(int32 I=0;I<Mesh->CellPortals.Num();++I)
                    {
                        const auto& Portal=Mesh->CellPortals[I];
                        if(Portal.IsOutsidePortal()||Portal.OtherCellId<0x100||!Mesh->PortalApertureLocalNormals.IsValidIndex(I))continue;
                        if(FVector::DotProduct(Frame.TransformVectorNoScale(Mesh->PortalApertureLocalNormals[I]),End-Eye)<=0)continue;
                        double Distance=0;if(!ApertureHit(*Mesh,I,Distance)||Distance+0.01<Entry.Value)continue;
                        const uint32 Dest=Block|Portal.OtherCellId;
                        if(!Portal.IsExactMatch()&&Portal.OtherPortalId!=0xFFFF)
                        {
                            const auto* Other=Dat->FindEnvCellMesh(Dest,100);double OtherDistance=0;
                            if(!Other||!ApertureHit(*Other,Portal.OtherPortalId,OtherDistance))continue;
                            Distance=FMath::Max(Distance,OtherDistance);
                        }
                        if(const auto* Existing=RayCells.Find(Dest);Existing&&*Existing<=Distance)continue;
                        RayCells.Add(Dest,Distance);Queue.Add({Dest,Distance});
                    }
                }
                double AllDistance=DBL_MAX,VisibleDistance=DBL_MAX;uint32 FirstCell=0;
                for(const auto& Room:Rooms)
                {
                    if(!FMath::LineBoxIntersection(Room.Bounds,Eye,End,End-Eye))continue;
                    for(int32 I=0;I+2<Room.Triangles.Num();I+=3)
                    {
                        FVector Hit,Normal;
                        if(!FMath::SegmentTriangleIntersection(Eye,End,Room.Triangles[I],Room.Triangles[I+1],Room.Triangles[I+2],Hit,Normal))continue;
                        const double Distance=FVector::Dist(Eye,Hit);
                        if(Distance<1)continue;
                        if(const auto* EntryDistance=RayCells.Find(Room.Id);EntryDistance&&Distance+0.02>=*EntryDistance&&Distance<AllDistance)
                        {AllDistance=Distance;FirstCell=Room.Id;}
                        if(Visible.Contains(int32(Room.Id)))VisibleDistance=FMath::Min(VisibleDistance,Distance);
                    }
                }
                ++Samples;
                // Coplanar faces can belong to either adjoining cell. Compare the
                // nearest surface distance, not a particular duplicate's identity.
                if(AllDistance!=DBL_MAX&&VisibleDistance>AllDistance+2)
                {
                    if(++Missed<=10)AddInfo(FString::Printf(TEXT("Hidden surface: eye cell=%08X eye=%s yaw=%d ray=%.2f/%.2f wall=%08X at=%.1f visible=%.1f cells=%d"),
                        Sample.Key,*FACEPosition::AceVectorToUnreal(Eye-Origin,.01f).ToString(),Yaw,Side,Up,FirstCell,AllDistance,VisibleDistance==DBL_MAX?-1:VisibleDistance,Visible.Num()));
                }
            }
        }
    }
    AddInfo(FString::Printf(TEXT("Dungeon visibility rays=%d missing=%d worst traversal=%.3f ms"),Samples,Missed,WorstMs));
    TestTrue(TEXT("Reported hall and stairwell viewpoints are exercised"),Samples>1000);
    TestEqual(TEXT("Portal culling retains the first opaque surface down each view ray"),Missed,0);
    return !HasAnyErrors();
}
#endif
