#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Dat/ACESetupMeshBuilder.h"
#include "Dat/ACEDatCursor.h"
#include "Dat/ACEDatMotionPlayer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailStreamingCostTest, "ACE.RetailParity.StreamingCost",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailStreamingCostTest::RunTest(const FString& Parameters)
{
    FACEDatDatabase Portal, HighRes;
    if (!TestTrue(TEXT("Open portal DAT for appearance benchmark"), Portal.Open(TEXT("C:/Turbine/Asheron's Call/client_portal.dat")))) return false;
    HighRes.Open(TEXT("C:/Turbine/Asheron's Call/client_highres.dat"));
    FACEDatTextureResolver Textures(&Portal,&HighRes);
    FACESetupMeshBuilder Builder(&Portal,&HighRes,&Textures);
    TArray<uint8> Blob;
    if (!Portal.ReadFile(0x0200004E,Blob)) return false;
    FACEDatCursor Cursor(Blob); FACEDatSetupModel Setup;
    if (!ACEDatUnpack::UnpackSetupModel(Cursor,Setup) || Setup.Parts.IsEmpty()) return false;
    // A real human with an ObjDesc part override exercises the uncached dye/skin path.
    FACEObjDesc Appearance;
    auto& Change=Appearance.AnimPartChanges.AddDefaulted_GetRef();
    Change.PartIndex=0; Change.PartId=Setup.Parts[0];
    FACEBuiltSetupMesh Cold;
    for (int32 Pass=0; Pass<2; ++Pass)
    {
        const double Start=FPlatformTime::Seconds(); const uint64 Before=Textures.GetSurfaceResolveCount();
        FACEBuiltSetupMesh Built;
        TestTrue(TEXT("Build human appearance"),Builder.BuildSetupWithAppearance(0x0200004E,Appearance,100.f,Built));
        const double Ms=(FPlatformTime::Seconds()-Start)*1000;
        const uint64 Requests=Textures.GetSurfaceResolveCount()-Before;
        int32 DrawSections=0,Triangles=0;
        for (const auto& Part : Built.Parts) for (const auto& Section : Part.Sections)
            if (!Section.bCollisionOnly) { ++DrawSections; Triangles+=Section.Triangles.Num()/3; }
        AddInfo(FString::Printf(TEXT("Human appearance %s: %.2f ms, %llu surface resolves, %d draw sections, %d triangles"),
            Pass==0?TEXT("cold"):TEXT("warm"),Ms,Requests,DrawSections,Triangles));
        // A material is shared by a polygon subset in retail; decode once per part/surface.
        TestTrue(TEXT("Texture work scales with surfaces, not polygons"),Requests<=uint64(DrawSections+Setup.Parts.Num()));
        if (Pass==0) Cold=MoveTemp(Built);
        else
        {
            TestEqual(TEXT("Warm appearance retains all parts"),Built.Parts.Num(),Cold.Parts.Num());
            for (int32 P=0; P<FMath::Min(Built.Parts.Num(),Cold.Parts.Num()); ++P)
            {
                TestEqual(TEXT("Warm appearance retains sections"),Built.Parts[P].Sections.Num(),Cold.Parts[P].Sections.Num());
                for (int32 S=0; S<FMath::Min(Built.Parts[P].Sections.Num(),Cold.Parts[P].Sections.Num()); ++S)
                {
                    const auto& A=Built.Parts[P].Sections[S]; const auto& B=Cold.Parts[P].Sections[S];
                    TestTrue(TEXT("Geometry, UVs and decoded colors are unchanged"),A.Vertices==B.Vertices && A.Triangles==B.Triangles && A.UVs==B.UVs && A.VertexColors==B.VertexColors);
                }
            }
        }
    }
    // Remote head colors are independent of how many clothing overrides arrive.
    TestTrue(TEXT("Human setup includes head part"),Setup.Parts.IsValidIndex(16));
    if (Setup.Parts.IsValidIndex(16) && Portal.ReadFile(Setup.Parts[16],Blob))
    {
        FACEDatCursor GfxCursor(Blob); FACEDatGfxObj Head;
        TestTrue(TEXT("Decode retail human head"),ACEDatUnpack::UnpackGfxObj(GfxCursor,Head));
        int32 ColoredSurfaces=0;
        for (uint32 SurfaceId : Head.Surfaces)
        {
            if (!Portal.ReadFile(SurfaceId,Blob)) continue;
            FACEDatCursor SurfaceCursor(Blob); FACEDatSurface Surface;
            if (!ACEDatUnpack::UnpackSurface(SurfaceCursor,Surface)) continue;
            uint32 OriginalPalette=Surface.OrigPaletteId;
            if (!OriginalPalette && Portal.ReadFile(Surface.OrigTextureId,Blob))
            {
                FACEDatCursor StCursor(Blob); FACEDatSurfaceTexture ST;
                if (ACEDatUnpack::UnpackSurfaceTexture(StCursor,ST))
                    for (uint32 TextureId : ST.Textures)
                        if (Portal.ReadFile(TextureId,Blob))
                        {
                            FACEDatCursor TexCursor(Blob); FACEDatTexture Tex;
                            if (ACEDatUnpack::UnpackTexture(TexCursor,Tex) && Tex.DefaultPaletteId)
                                OriginalPalette=Tex.DefaultPaletteId;
                        }
            }
            if (!OriginalPalette) continue;
            FACEObjDesc Simple; Simple.PaletteBaseId=OriginalPalette;
            auto& Sub=Simple.SubPalettes.AddDefaulted_GetRef();
            Sub.SubPaletteId=0x0400008E; // Existing 2048-entry retail palette, distinct from human 0400007E.
            Sub.Offset=0; Sub.NumColors=256;
            FACEDatDecodedSurface Original,Colored,Crowded;
            Textures.ResolveSurface(SurfaceId,Original);
            TestTrue(TEXT("Head subpalette resolves"),Textures.ResolveSurfaceWithAppearance(SurfaceId,16,&Simple,Colored));
            FACEObjDesc Equipped=Simple;
            for(int32 Part=0; Part<Setup.Parts.Num(); ++Part)
            {
                auto& Override=Equipped.AnimPartChanges.AddDefaulted_GetRef();
                Override.PartIndex=Part; Override.PartId=Setup.Parts[Part];
            }
            TestTrue(TEXT("Clothed remote head subpalette resolves"),Textures.ResolveSurfaceWithAppearance(SurfaceId,16,&Equipped,Crowded));
            TestTrue(TEXT("Clothing overrides cannot suppress head palette remaps"),Colored.Pixels==Crowded.Pixels);
            if (Original.Pixels!=Colored.Pixels) ++ColoredSurfaces;
        }
        TestTrue(TEXT("Fixture actually changes indexed head pixels"),ColoredSurfaces>0);
    }
    FACEDatMotionPlayer Player(&Portal);
    for (int32 Pass=0; Pass<100; ++Pass)
    {
        for (uint32 Table : {0x09000025u,0x09000007u})
        {
            TestTrue(TEXT("Switch between cached creature motion tables"),Player.SetMotionTable(Table));
            TArray<FTransform> Pose; int32 Animated=0;
            TestTrue(TEXT("Cached creature animation still evaluates"),Player.EvaluateIdle(Pass/60.f,20,Pose,100,Animated));
        }
    }
    TestEqual(TEXT("Alternating creatures retain two tables, not 200 reparses"),Player.GetCachedMotionTableCount(),2);
    FACEDatAnimData Hold; Hold.AnimId=0x03000771; Hold.LowFrame=19; Hold.HighFrame=-1; Hold.Framerate=0;
    TArray<FTransform> AtStart; int32 Count=0; bool Finished=false;
    TestTrue(TEXT("ATOYOT zero-rate DAT pose evaluates"),Player.EvaluateAnimData(Hold,0,20,AtStart,100,Count,false,&Finished));
    for (float Time : {0.25f, 2.f, 60.f})
    {
        TArray<FTransform> Later; int32 LaterCount=0;
        Player.EvaluateAnimData(Hold,Time,20,Later,100,LaterCount,false,&Finished);
        TestFalse(TEXT("Zero-rate pose never completes by elapsed time"),Finished);
        for(int32 I=0; I<Count; ++I)
            TestTrue(TEXT("ATOYOT stays on authored frame 19"),AtStart[I].Equals(Later[I],.001));
    }
    FACEObjectMotionState Move;
    Move.MoveToFlags=1; Move.MoveToSpeed=0.5f; Move.MoveToRunRate=4.f;
    Move.UpdateMoveToGait(20.f);
    TestFalse(TEXT("Walk-only wander is not promoted to run by a high RunRate"),Move.bRunning);
    TestEqual(TEXT("Walk animation keeps the server's speed"),Move.AnimPlayRate,0.5f);
    Move.MoveToFlags=3; Move.MoveToDistance=1.f; Move.MoveToWalkRunThreshold=2.f;
    Move.UpdateMoveToGait(20.f); TestTrue(TEXT("Distant walk/run approach runs"),Move.bRunning);
    Move.UpdateMoveToGait(2.f); TestFalse(TEXT("Approach switches to walk near its stopping distance"),Move.bRunning);
    Move.MoveToFlags=0x10; Move.UpdateMoveToGait(2.f); TestTrue(TEXT("Forced charge still runs"),Move.bRunning);
    return true;
}
#endif
