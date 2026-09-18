#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEDatTextureResolver.h"
#include "Dat/ACECellTransit.h"
#include "Dat/ACEOutdoorPortalPlan.h"
#include "Dat/ACEEnvCellMeshBuilder.h"
#include "Dat/ACEPortalViewMask.h"
#include "Engine/GameInstance.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEEntranceParticleTest, "ACE.RetailParity.EntranceAndParticleAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEEntranceParticleTest::RunTest(const FString& Parameters)
{
    auto* Dat=NewObject<UACEDatSubsystem>(NewObject<UGameInstance>());
    if (!Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))) return false;
    FACEDatTextureResolver& Resolver=*Dat->GetTextureResolver();
    for(uint32 Id : {0x080012FAu,0x08000498u,0x08000AACu})
    {
        FACEDatDecodedSurface S;
        TestTrue(TEXT("Particle surface decodes"),Resolver.ResolveSurface(Id,S));
        TestTrue(TEXT("Image particle contains texture pixels, never a solid fallback"), S.bHasPixels && !S.bIsSolid);
        TestEqual(TEXT("Complete image coverage decoded"),S.Pixels.Num(),S.Width*S.Height);
        int32 Transparent=0,White=0;
        for(const FColor& C:S.Pixels) { Transparent+=C.A<16; White+=C.R>240 && C.G>240 && C.B>240; }
        AddInfo(FString::Printf(TEXT("Surface %08X: %dx%d clip=%d add=%d transparent=%d white=%d pixels=%d"),
            Id,S.Width,S.Height,S.bClipMap,S.bAdditive,Transparent,White,S.Pixels.Num()));
        if (Id==0x080012FAu)
        {
            int32 Faded=0,Opaque=0,Colored=0;
            for(const auto& Pixel:S.Pixels)
            { Faded+=Pixel.A<32; Opaque+=Pixel.A>240; Colored+=Pixel.R+Pixel.G+Pixel.B>0; }
            TestTrue(TEXT("Destroyed portal shadow retains its alpha gradient"),Faded>0 && Opaque>0);
            TestEqual(TEXT("Destroyed portal shadow remains authored black"),Colored,0);
            TestFalse(TEXT("Luminous black shadow is not additive"),S.bAdditive);
        }
        if (auto* Material=Dat->GetOrCreateParticleMaterial(Id))
        {
            TestEqual(TEXT("Particle blend follows surface flags"),Material->GetBlendMode(),S.bAdditive?BLEND_Additive:BLEND_Translucent);
            // Front/back visibility and brightness are rendered in ParticleTiming.
            // IsTwoSided alone is insufficient: the mesh already has back faces.
            UTexture* Texture=nullptr;
            TestTrue(TEXT("Particle material receives its decoded image"),Material->GetTextureParameterValue(TEXT("ACETexture"),Texture));
            if(auto* Image=Cast<UTexture2D>(Texture))
            { TestEqual(TEXT("Particle upload width"),Image->GetSizeX(),S.Width); TestEqual(TEXT("Particle upload height"),Image->GetSizeY(),S.Height); }
        }
        else AddError(TEXT("Particle material creation failed"));
        if(S.Pixels.Num())
        {
            TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(S.Width,S.Height,S.Pixels,Png);
            FFileHelper::SaveArrayToFile(Png,*FPaths::Combine(FPaths::ProjectSavedDir(),FString::Printf(TEXT("Automation/Surface_%08X.png"),Id)));
        }
    }
    for (const auto Pair : {TPair<uint32,uint32>(0x01001111u,2u),{0x01003DE3u,1u},{0x010016C8u,1u}})
    {
        const auto* Shape=Dat->GetOrBuildSetupMesh(Pair.Key,100);
        if(TestTrue(TEXT("Reported particle geometry loads"),Shape && !Shape->Parts.IsEmpty()))
            TestEqual(TEXT("Particle facing follows its retail degrade mode"),Shape->Parts[0].DrawMode,Pair.Value);
    }
    for(uint32 Id=0x7D640100;Id<0x7D640174;++Id) Dat->GetOrBuildEnvCellMesh(Id,100);
    const FVector Origin=FACEPosition::AceVectorToUnreal(FVector(125*192,100*192,0),100);
    int32 WrongCell=0,MissingView=0;
    for(uint32 Id : {0x7D640150u,0x7D640151u,0x7D640152u})
    {
        const auto* Mesh=Dat->FindEnvCellMesh(Id,100);
        if(!TestNotNull(TEXT("Pedestal entrance cell"),Mesh))continue;
        const FTransform X=Mesh->GetCellLocalToLandblock(100)*FTransform(Origin);
        AddInfo(FString::Printf(TEXT("Cell %08X bsp=%d portals=%d"),Id,Mesh->CellBspNodes.Num(),Mesh->CellPortals.Num()));
        for(int I=0;I<Mesh->CellPortals.Num();++I)
        {
            const auto& V=Mesh->PortalApertureLocalVerts[I]; if(V.Num()<3)continue;
            FVector C=FVector::ZeroVector; for(const auto& P:V) C+=P; C/=V.Num();
            const FVector N=X.TransformVectorNoScale(Mesh->PortalApertureLocalNormals[I]);
            const FVector Door=X.TransformPosition(C);
            AddInfo(FString::Printf(TEXT("Portal %d dest=%04X center=%s normal=%s"),I,Mesh->CellPortals[I].OtherCellId,*Door.ToString(),*N.ToString()));
            if(!Mesh->CellPortals[I].IsOutsidePortal())continue;
            if(Id==0x7D640151u)
            {
                int32 Mismatches=0;
                for (float XOffset : {-150.f,0.f,150.f})
                for (float YOffset : {-450.f,-150.f,0.f,150.f,450.f})
                for (float ZOffset : {-120.f,0.f,120.f,300.f})
                for (uint32 Hint : {0x7D640014u,0x7D640151u})
                {
                    const FVector Eye=Door+FVector(XOffset,YOffset,ZOffset);
                    uint32 Resolved=0;
                    ACECellTransit::ResolveViewerCellId(*Dat,Hint,Door-FVector(0,0,100),Eye,100,Resolved);
                    if (ACECellTransit::IsIndoorCell(Resolved) && !Dat->IsPointInsideEnvCell(Resolved,Eye,100))
                    {
                        ++Mismatches;
                        if(Mismatches<=10) AddInfo(FString::Printf(TEXT("Wrong endpoint cell=%08X hint=%08X offset=%s"),Resolved,Hint,*FVector(XOffset,YOffset,ZOffset).ToString()));
                    }
                }
                TestEqual(TEXT("Orbiting entrance camera is never assigned a room not containing its eye"),Mismatches,0);
            }
            for(float Distance : {-30.f,-2.f,-.1f,0.f,.1f,2.f,30.f,150.f})
            {
                FVector Eye=Door+N*Distance; uint32 Resolved=0;
                ACECellTransit::ResolveViewerCellId(*Dat,Id,Door-N*60,Eye,100,Resolved);
                if(Distance>0 && ACECellTransit::IsIndoorCell(Resolved)) ++WrongCell;
                TSet<int32> Draw; bool Outside=false;
                TArray<ACEOutdoorPortalPlan::FAdmittedAperture> Exits;
                ACEOutdoorPortalPlan::CollectIndoorPViewCells(*Dat,Resolved,Eye,{},100,Draw,Outside,128,&Exits);
                if(Distance<0 && (!Outside || !FACEPortalViewMask::Build(Eye,Exits).Contains(Door+N*500))) ++MissingView;
                AddInfo(FString::Printf(TEXT("Distance %.2f resolved=%08X outside=%d exits=%d"),Distance,Resolved,Outside,Exits.Num()));
            }
        }
    }
    TestEqual(TEXT("Camera outside the entrance stays in landscape"),WrongCell,0);
    TestEqual(TEXT("Camera just inside retains the landscape through the entrance"),MissingView,0);
    return true;
}
#endif
