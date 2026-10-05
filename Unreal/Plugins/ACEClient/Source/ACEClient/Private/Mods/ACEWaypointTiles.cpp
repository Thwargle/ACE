#include "ACEWaypointTiles.h"
#include "Dat/ACEDatDatabase.h"
#include "Dat/ACEDatTextureResolver.h"
#include "Dat/ACELandblockMeshBuilder.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"

struct FACEWaypointTiles::FWorker
{
    FCriticalSection Mutex;
    bool Pending=false,Ready=false,Opened=false,Failed=false;
    FIntVector Key;
    int32 Resolution=256;
    TArray<FColor> Result;
    FACEDatDatabase Portal,Cell;
    TUniquePtr<FACEDatTextureResolver> Resolver;
    TUniquePtr<FACELandblockMeshBuilder> Builder;
    TArray<float> Heights;
    TMap<uint64,TArray<FColor>> Blends;
    int64 BlendBytes=0;
    void Build(const FString& Folder,FIntVector Tile,int32 Size)
    {
        if(!Opened&&!Failed)
        {
            Opened=Portal.Open(Folder/TEXT("client_portal.dat"))&&Cell.Open(Folder/TEXT("client_cell_1.dat"));Failed=!Opened;
            if(Opened){Resolver=MakeUnique<FACEDatTextureResolver>(&Portal,nullptr);Builder=MakeUnique<FACELandblockMeshBuilder>(&Portal,&Cell,Resolver.Get());
                if(!Builder->EnsureTexMerge()){Failed=true;Opened=false;}
                TArray<uint8> Bytes;if(Portal.ReadFile(0x13000000,Bytes)){FACEDatCursor Cur(Bytes);ACEDatUnpack::UnpackRegionLandHeightTable(Cur,Heights);}}
        }
        TArray<FColor> Pixels;
        if(Opened)
        {
            Pixels.Init(FColor(18,37,53),Size*Size);
            const int32 PerBlock=Size/Tile.Z;
            for(int BX=0;BX<Tile.Z;++BX)for(int BY=0;BY<Tile.Z;++BY)
            {
                TArray<uint8> Bytes;FACEDatCellLandblock Land;
                if(!Cell.ReadFile(uint32((Tile.X+BX)<<24)|uint32((Tile.Y+BY)<<16)|0xffff,Bytes))continue;
                FACEDatCursor Cur(Bytes);if(!ACEDatUnpack::UnpackCellLandblock(Cur,Land))continue;
                for(int CX=0;CX<8;++CX)for(int CY=0;CY<8;++CY)
                {
                    const int Indices[]={CX*9+CY,(CX+1)*9+CY,(CX+1)*9+CY+1,CX*9+CY+1};
                    const uint16 A=Land.Terrain[Indices[0]],B=Land.Terrain[Indices[1]],C=Land.Terrain[Indices[2]],D=Land.Terrain[Indices[3]];
                    const uint32 Code=FACELandblockMeshBuilder::GetPalCode(A&3,B&3,C&3,D&3,(A>>2)&31,(B>>2)&31,(C>>2)&31,(D>>2)&31);
                    const int CellPixels=PerBlock/8;const uint64 BlendKey=uint64(Code)|(uint64(CellPixels)<<32);
                    auto* Blend=Blends.Find(BlendKey);
                    if(!Blend){TArray<FColor> Color;int32 W=0,H=0;if(!Builder->BakePCode(Code,Color,W,H)||W!=H||W<CellPixels)continue;
                        while(W>CellPixels){TArray<FColor> Smaller;FACEDatTextureResolver::BuildOpaqueMip(Color,W,H,Smaller);Color=MoveTemp(Smaller);W=FMath::Max(1,W/2);H=W;}
                        // Small atlas cells used to flush every 256 patterns,
                        // repeatedly rebaking the same full-resolution terrain sources.
                        // Budget actual pixel storage so overview mips can reuse
                        // thousands of tiny blends without unbounded residency.
                        const int64 PixelBytes=int64(Color.Num())*sizeof(FColor);
                        if(Blends.Num()>=8192||BlendBytes+PixelBytes>16*1024*1024){Blends.Reset();BlendBytes=0;}
                        BlendBytes+=PixelBytes;Blend=&Blends.Add(BlendKey,MoveTemp(Color));}
                    float Shade=1;
                    if(Heights.Num()==256){const float DX=(Heights[Land.Height[Indices[1]]]-Heights[Land.Height[Indices[0]]])/24.f;
                        const float DY=(Heights[Land.Height[Indices[3]]]-Heights[Land.Height[Indices[0]]])/24.f;
                        Shade=FMath::Clamp(.85f+.25f*FVector(-DX,-DY,1).GetSafeNormal().Dot(FVector(-.4f,.4f,.82f)),.5f,1.12f);}
                    for(int X=0;X<CellPixels;++X)for(int Y=0;Y<CellPixels;++Y)
                    {
                        const auto Color=(*Blend)[(CellPixels-1-Y)*CellPixels+(CellPixels-1-X)];
                        Pixels[(Size-1-(BY*PerBlock+CY*CellPixels+Y))*Size+BX*PerBlock+CX*CellPixels+X]=FColor(uint8(FMath::Min(255.f,Color.R*Shade)),uint8(FMath::Min(255.f,Color.G*Shade)),uint8(FMath::Min(255.f,Color.B*Shade)),255);
                    }
                }
            }
        }
        FScopeLock Lock(&Mutex);Result=MoveTemp(Pixels);Key=Tile;Resolution=Size;Ready=true;
    }
};
FACEWaypointTiles::FACEWaypointTiles()=default;
FACEWaypointTiles::~FACEWaypointTiles()=default;
FBox2D FACEWaypointTiles::Bounds(FIntVector K)
{return FBox2D(FVector2D(K.X*.8-101.95,K.Y*.8-101.95),FVector2D((K.X+K.Z)*.8-101.95,(K.Y+K.Z)*.8-101.95));}
FIntPoint FACEWaypointTiles::Detail(double Scale)
{
    int32 Span=32,Resolution=256;
    while(Span>1&&Span*.8*Scale>Resolution)Span/=2;
    while(Resolution<1024&&Span*.8*Scale>Resolution)Resolution*=2;
    return {Span,Resolution};
}
void FACEWaypointTiles::Update(const FString& Directory,const FBox2D& Visible,double Scale)
{
    ++Frame;
    bool Pending=false;
    if(Source!=Directory||!Worker){Source=Directory;Worker=MakeShared<FWorker,ESPMode::ThreadSafe>();Tiles.Reset();}
    {
        FScopeLock Lock(&Worker->Mutex);
        if(Worker->Ready)
        {
            if(Worker->Result.Num()==Worker->Resolution*Worker->Resolution)
            {
                auto Tile=MakeShared<FTile>();Tile->Key=Worker->Key;Tile->Used=Frame;Tile->Resolution=Worker->Resolution;
                Tile->Texture.Reset(FACEDatTextureResolver::CreateTransientRgbaWithMips(Tile->Resolution,Tile->Resolution,Worker->Result,false,TA_Clamp,TA_Clamp,false,false,0,TF_Trilinear,false));
                Tile->Brush.SetResourceObject(Tile->Texture.Get());Tile->Brush.DrawAs=ESlateBrushDrawType::Image;Tiles.Add(Tile);
                // Painter order changes only when residency changes, not every frame.
                Tiles.Sort([](const auto& A,const auto& B){return A->Key.Z==B->Key.Z?A->Resolution<B->Resolution:A->Key.Z>B->Key.Z;});
            }
            Worker->Ready=false;Worker->Pending=false;Worker->Result.Reset();
        }
        Pending=Worker->Pending;
        if(Worker->Failed||Scale<2||!Visible.Intersect(Bounds({0,0,256}))){Loading=false;return;}
    }
    const auto Lod=Detail(Scale);const int Span=Lod.X,Resolution=Lod.Y;
    const int MinX=FMath::Clamp(FMath::FloorToInt((Visible.Min.X+101.95)/.8/Span)*Span,0,256-Span);
    const int MinY=FMath::Clamp(FMath::FloorToInt((Visible.Min.Y+101.95)/.8/Span)*Span,0,256-Span);
    const int MaxX=FMath::Clamp(FMath::FloorToInt((Visible.Max.X+101.95)/.8/Span)*Span,0,256-Span);
    const int MaxY=FMath::Clamp(FMath::FloorToInt((Visible.Max.Y+101.95)/.8/Span)*Span,0,256-Span);
    FIntVector Next(-1,-1,Span);double Nearest=MAX_dbl;
    for(int X=MinX;X<=MaxX;X+=Span)for(int Y=MinY;Y<=MaxY;Y+=Span)
    {
        const FIntVector Key(X,Y,Span);bool Found=false;
        for(auto& Tile:Tiles)if(Tile->Key==Key&&Tile->Resolution==Resolution){Tile->Used=Frame;Found=true;break;}
        const double Distance=(Bounds(Key).GetCenter()-Visible.GetCenter()).SizeSquared();
        if(!Found&&Distance<Nearest){Nearest=Distance;Next=Key;}
    }
    int64 Pixels=0;for(const auto& Tile:Tiles)Pixels+=int64(Tile->Resolution)*Tile->Resolution;
    // At most 32 MiB RGBA base levels (~43 MiB with mips), including high-DPI tiles.
    while(Tiles.Num()>64||Pixels>8*1024*1024){int Oldest=0;for(int I=1;I<Tiles.Num();++I)if(Tiles[I]->Used<Tiles[Oldest]->Used)Oldest=I;Pixels-=int64(Tiles[Oldest]->Resolution)*Tiles[Oldest]->Resolution;Tiles.RemoveAt(Oldest);}
    Loading=Next.X>=0;
    if(Loading&&!Pending){FScopeLock Lock(&Worker->Mutex);Worker->Pending=true;Async(EAsyncExecution::ThreadPool,[W=Worker,Directory,Next,Resolution](){W->Build(Directory,Next,Resolution);});}
}
