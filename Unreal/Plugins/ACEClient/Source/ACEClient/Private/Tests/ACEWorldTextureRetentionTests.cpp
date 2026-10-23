#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/Compression.h"
#include "Dat/ACESetupMeshBuilder.h"
#include "Dat/ACEWorldTextureMipProvider.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEWorldTextureRetentionTest,"ACE.Rendering.WorldTextureRetention",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEWorldTextureRetentionTest::RunTest(const FString&)
{
    FString Directory=TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Directory);
    FACEDatDatabase Portal,HighRes;
    if(!TestTrue(TEXT("Retail portal DAT loads"),Portal.Open(Directory/TEXT("client_portal.dat"))))return false;
    HighRes.Open(Directory/TEXT("client_highres.dat"));
    FACEDatTextureResolver Resolver(&Portal,&HighRes);
    FACESetupMeshBuilder Builder(&Portal,&HighRes,&Resolver);
    TSet<uint32> Surfaces;
    for(uint32 Setup:{0x02000001u,0x0200004Eu,0x020007DDu,0x0200124Au})
    {
        FACEBuiltSetupMesh Mesh;
        if(!TestTrue(TEXT("Real human/creature/prop mesh builds"),Builder.BuildSetupWithAppearance(Setup,{},100,Mesh)))return false;
        for(const auto& Part:Mesh.Parts)for(const auto& Section:Part.Sections)
            if(!Section.bCollisionOnly && Section.SurfaceId)Surfaces.Add(Section.SurfaceId);
    }
    auto Ordered=Surfaces.Array();Ordered.Sort();
    double FastSeconds=0,LegacySeconds=0;
    int64 RawBytes=0,FastBytes=0,LegacyBytes=0;
    int32 Textures=0,MipCount=0;
    auto CheckMips=[&](const TArray<TArray<FColor>>& Mips)
    {
        TStrongObjectPtr<UACEWorldTextureMipProvider> Provider(NewObject<UACEWorldTextureMipProvider>());
        const double Start=FPlatformTime::Seconds();
        for(const auto& Mip:Mips)Provider->AddMip(Mip);
        FastSeconds+=FPlatformTime::Seconds()-Start;
        FastBytes+=Provider->GetRetainedBytes();
        for(const auto& Mip:Mips)
        {
            const int32 Bytes=Mip.Num()*sizeof(FColor);
            const double LegacyStart=FPlatformTime::Seconds();
            int32 PackedBytes=FCompression::CompressMemoryBound(NAME_LZ4,Bytes);
            TArray<uint8> Packed;Packed.SetNumUninitialized(PackedBytes);
            const bool Compressed=FCompression::CompressMemory(NAME_LZ4,Packed.GetData(),PackedBytes,Mip.GetData(),Bytes);
            LegacySeconds+=FPlatformTime::Seconds()-LegacyStart;
            LegacyBytes+=Compressed?FMath::Min(PackedBytes,Bytes):Bytes;
            RawBytes+=Bytes;++MipCount;
        }
        // Full upload, low-detail tail, then full quality again. Exact equality
        // includes alpha; compression cannot degrade cutouts, dyes or mip colors.
        for(int32 First:{0,FMath::Min(2,Mips.Num()-1),0})
        {
            TArray<void*> Data;Data.Init(nullptr,Mips.Num()-First);
            TArray<int64> Sizes;Sizes.Init(0,Data.Num());
            if(TestTrue(TEXT("Retained mips can be re-uploaded"),Provider->GetInitialMipData(First,Data,Sizes,TEXT("WorldTextureRetention"))))
                for(int32 I=0;I<Data.Num();++I)
                {
                    const auto& Expected=Mips[I+First];
                    const int64 Bytes=Expected.Num()*sizeof(FColor);
                    TestEqual(TEXT("Retained mip byte count"),Sizes[I],Bytes);
                    TestTrue(TEXT("Every retained mip is lossless"),Data[I] && Sizes[I]==Bytes
                        && FMemory::Memcmp(Data[I],Expected.GetData(),Bytes)==0);
                }
            for(void* Mip:Data)FMemory::Free(Mip);
        }
    };
    for(uint32 Surface:Ordered)
    {
        FACEDatDecodedSurface Decoded;
        if(!Resolver.ResolveSurface(Surface,Decoded) || !Decoded.bHasPixels)continue;
        TArray<TArray<FColor>> Mips;Mips.Add(MoveTemp(Decoded.Pixels));
        int32 W=Decoded.Width,H=Decoded.Height;
        while(W>1 || H>1)
        {
            TArray<FColor> Next;FACEDatTextureResolver::BuildOpaqueMip(Mips.Last(),W,H,Next);
            Mips.Add(MoveTemp(Next));W=FMath::Max(1,W/2);H=FMath::Max(1,H/2);
        }
        CheckMips(Mips);++Textures;
    }
    TestTrue(TEXT("Benchmark contains real textured surfaces"),Textures>10);
    AddInfo(FString::Printf(TEXT("Retail texture retention: %d textures, %d mips, %.2f MiB raw; fast %.2f ms / %.2f MiB; legacy LZ4HC %.2f ms / %.2f MiB"),
        Textures,MipCount,RawBytes/1048576.,FastSeconds*1000,FastBytes/1048576.,LegacySeconds*1000,LegacyBytes/1048576.));
    // Small/incompressible data takes the raw fallback, including a 1x1 mip.
    TArray<TArray<FColor>> Noise;
    FRandomStream Random(12345);
    auto& Pixels=Noise.AddDefaulted_GetRef();
    for(int32 I=0;I<1024;++I)Pixels.Add(FColor(Random.GetUnsignedInt()));
    Noise.Add({FColor(12,34,56,78)});
    CheckMips(Noise);
    return !HasAnyErrors();
}
#endif
