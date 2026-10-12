#include "Mods/ACEPluginCosmetics.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACEScriptComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "ImageUtils.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Mods/ACEPluginSubsystem.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

UACEPluginCosmetics::UACEPluginCosmetics(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

bool UACEPluginCosmetics::IsBodySlot(const FString& Id)
{ return Id==TEXT("head")||Id==TEXT("shoulders")||Id==TEXT("waist")||Id==TEXT("back")||Id==TEXT("chest")||Id==TEXT("arms")||Id==TEXT("hands")||Id==TEXT("legs")||Id==TEXT("feet"); }

bool UACEPluginCosmetics::LocalFile(const FString& Path)
{
#if PLATFORM_WINDOWS
    if(Path.StartsWith(TEXT("\\\\")) || FPaths::IsRelative(Path)) return false;
    const FString Full=FPaths::ConvertRelativePathToFull(Path);
    if(Full.Len()<3 || Full[1]!=':' || GetDriveTypeW(*(Full.Left(3).Replace(TEXT("/"),TEXT("\\"))))==DRIVE_REMOTE) return false;
    FString Part=Full;
    while(Part.Len()>3)
    {
        const DWORD Attributes=GetFileAttributesW(*Part);
        if(Attributes!=INVALID_FILE_ATTRIBUTES && (Attributes&FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        Part=FPaths::GetPath(Part);
    }
    return true;
#else
    return false; // Companion-tool API 1 currently targets Windows x64.
#endif
}

UGameInstance* UACEPluginCosmetics::GetGameInstance() const { return Host?Host->GetGameInstance():nullptr; }
UWorld* UACEPluginCosmetics::GetWorld() const { return Host?Host->GetWorld():nullptr; }
void UACEPluginCosmetics::Shutdown()
{
    Disable();
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);Ticker.Reset();
}

void UACEPluginCosmetics::CancelTool()
{
    if(ToolProcess.IsValid())
    {
        FPlatformProcess::TerminateProc(ToolProcess,true);
        FPlatformProcess::CloseProc(ToolProcess);
        ToolProcess.Reset();
    }
    if(PipeRead || PipeWrite) FPlatformProcess::ClosePipe(PipeRead,PipeWrite);
    PipeRead=PipeWrite=nullptr;
    bEnableAfterTool=false; ToolOutput.Empty(); ToolCommand.Empty();
    PendingRequest.Reset();
}

void UACEPluginCosmetics::Tell(const FString& Message)
{
    Status=Message;
    UE_LOG(LogTemp,Display,TEXT("Local cosmetics [%s]: %s"),*PluginId,*Message);
    if(GEngine) GEngine->AddOnScreenDebugMessage(reinterpret_cast<uint64>(this),8.f,FColor::White,Message.Left(220));
}

void UACEPluginCosmetics::RunTool(const TArray<FString>& Arguments,bool bLoadAfter)
{
    if(ToolProcess.IsValid()) { Tell(TEXT("A local check is already running.")); return; }
    const FString Executable=HelperPath,Directory=FPaths::GetPath(HelperPath);
    if(!LocalFile(Executable) || !FPaths::FileExists(Executable))
    { Tell(TEXT("Bundled converter is missing or not on a supported local path. Reinstall this plugin.")); return; }
    FString Params;
    // Arguments are constructed by the compiled adapter, never a Lua command line.
    // No shell is involved. Reject quotes/control characters; double terminal
    // backslashes so Windows cannot swallow the closing argument quote.
    if(Arguments.IsEmpty() || (Arguments[0]!=TEXT("prepare-appearance") && Arguments[0]!=TEXT("scan") && Arguments[0]!=TEXT("verify-active") && Arguments[0]!=TEXT("convert-select") && Arguments[0]!=TEXT("scan-items") && Arguments[0]!=TEXT("catalog-items") && Arguments[0]!=TEXT("clear-slot") && Arguments[0]!=TEXT("apply-outfit"))) return;
    for(const FString& Arg:Arguments)
    {
        if(Arg.Len()>1024 || Arg.Contains(TEXT("\"")) || Arg.Contains(TEXT("\n")) || Arg.Contains(TEXT("\r"))) { Tell(TEXT("Invalid converter argument.")); return; }
        FString Quoted=Arg;
        for(int32 I=Arg.Len()-1; I>=0&&Arg[I]=='\\'; --I) Quoted+=TEXT("\\");
        if(!Params.IsEmpty()) Params+=TEXT(" ");
        Params+=TEXT("\"")+Quoted+TEXT("\"");
    }
    if(bLoadAfter && !bPaperdoll) Disable();
    if(!FPlatformProcess::CreatePipe(PipeRead,PipeWrite)) { Tell(TEXT("Could not create converter output pipe.")); return; }
    ToolOutput.Empty();
    ToolProcess=FPlatformProcess::CreateProc(*Executable,*Params,false,true,true,nullptr,0,*Directory,PipeWrite,nullptr,PipeWrite);
    if(!ToolProcess.IsValid())
    {
        FPlatformProcess::ClosePipe(PipeRead,PipeWrite); PipeRead=PipeWrite=nullptr;
        Tell(TEXT("Could not start the bundled local converter. Reinstall this plugin.")); return;
    }
    bEnableAfterTool=bLoadAfter;
    ToolCommand=Arguments[0];
    ToolStarted=FPlatformTime::Seconds();
    Tell(TEXT("Checking local source files..."));
}

void UACEPluginCosmetics::PollTool()
{
    if(!ToolProcess.IsValid()) return;
    ToolOutput+=FPlatformProcess::ReadPipe(PipeRead);
    if(ToolOutput.Len()>(ToolCommand==TEXT("scan-items")||ToolCommand==TEXT("catalog-items")?128*1024*1024:262144) || (ToolCommand!=TEXT("scan-items") && FPlatformTime::Seconds()-ToolStarted>120))
    {
        FPlatformProcess::TerminateProc(ToolProcess,true);
        bEnableAfterTool=false;
        ToolOutput=TEXT("Local check exceeded its size/time limit.");
    }
    if(FPlatformProcess::IsProcRunning(ToolProcess))
    {
        if(ToolCommand==TEXT("scan-items") && FPlatformTime::Seconds()>=NextProgressRead)
        {
            NextProgressRead=FPlatformTime::Seconds()+1;
            const FString Path=CacheRoot/TEXT("scan-progress.json");FString Json;TSharedPtr<FJsonObject> Progress;
            if(LocalFile(Path)&&IFileManager::Get().FileSize(*Path)<16384&&FFileHelper::LoadFileToString(Json,*Path)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Progress)&&Progress)
            {
                double Done=0,Total=0,Ready=0;
                if(Progress->TryGetNumberField(TEXT("attempted"),Done)&&Progress->TryGetNumberField(TEXT("total"),Total)&&Progress->TryGetNumberField(TEXT("ready"),Ready))
                    Status=FString::Printf(TEXT("Scanning all local models: %.0f / %.0f checked, %.0f ready. Cancel retains scan progress."),Done,Total,Ready);
            }
        }
        return;
    }
    ToolOutput+=FPlatformProcess::ReadPipe(PipeRead);
    int32 ExitCode=-1;
    FPlatformProcess::GetProcReturnCode(ToolProcess,&ExitCode);
    FPlatformProcess::CloseProc(ToolProcess); ToolProcess.Reset();
    FPlatformProcess::ClosePipe(PipeRead,PipeWrite); PipeRead=PipeWrite=nullptr;
    const bool LoadAfter=bEnableAfterTool;
    bEnableAfterTool=false;
    if(ExitCode==0 && ToolCommand==TEXT("scan")) UpdateInstallations(ToolOutput);
    else if(ExitCode==0 && (ToolCommand==TEXT("scan-items")||ToolCommand==TEXT("catalog-items"))) UpdateCatalog(ToolOutput);
    else if(ExitCode==0 && ToolCommand==TEXT("prepare-appearance"))
    {
        TSharedPtr<FJsonObject> Receipt;FString Id;FGuid Guid;
        if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ToolOutput),Receipt)&&Receipt&&Receipt->TryGetStringField(TEXT("id"),Id)&&FGuid::ParseExact(Id,EGuidFormats::Digits,Guid))
        {
            for(const auto& Item:Catalog)
            {
                FString Previous;Item->TryGetStringField(TEXT("cacheId"),Previous);
                if(Previous==PreviewCacheId){Item->SetStringField(TEXT("cacheId"),Id);Item->SetStringField(TEXT("reason"),TEXT("Local model preview ready. Apply outfit to wear it."));}
            }
            Tell(TEXT("Preview ready; draft outfit is unchanged."));
        }
        else Tell(TEXT("Invalid appearance preview response."));
    }
    else if(ExitCode==0 && LoadAfter)
    {
        if(!LoadVerified(ToolOutput)) { Tell(TEXT("Local cache could not be loaded; existing appearances retained. See the Output Log.")); }
    }
    else
    {
        TSharedPtr<FJsonObject> Result; FString Error;
        if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ToolOutput),Result)&&Result) Result->TryGetStringField(TEXT("error"),Error);
        Tell(!Error.IsEmpty()?Error:ToolOutput.IsEmpty()?TEXT("Local converter returned no result."):ToolOutput.Left(4096));
    }
}

bool UACEPluginCosmetics::LoadSingle(const FString& Json)
{
    TSharedPtr<FJsonObject> Object;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Object) || !Object) return false;
    FString Directory,Id,VerifiedSlot,VerifiedBuildInfo;
    if(!Object->TryGetStringField(TEXT("directory"),Directory) || !Object->TryGetStringField(TEXT("id"),Id)
        || !Object->TryGetStringField(TEXT("slot"),VerifiedSlot) || !Object->TryGetStringField(TEXT("sourceStamp"),VerifiedBuildInfo)) return false;
    FGuid Guid;
    const bool Body=IsBodySlot(VerifiedSlot);
    if(Id.Len()!=32 || !FGuid::ParseExact(Id,EGuidFormats::Digits,Guid) || (!Body&&VerifiedSlot!=TEXT("weapon") && VerifiedSlot!=TEXT("shield") && VerifiedSlot!=TEXT("offhand"))) return false;
    FString Expected=FPaths::Combine(CacheRoot,TEXT("cache"),Id);
    FPaths::NormalizeDirectoryName(Expected); FPaths::NormalizeDirectoryName(Directory);
    if(!Expected.Equals(Directory,ESearchCase::IgnoreCase) || !LocalFile(Directory) || !LocalFile(VerifiedBuildInfo)
        || Object->GetIntegerField(TEXT("schema"))!=1 || IFileManager::Get().FileSize(*VerifiedBuildInfo)>1000000) return false;
    const FString MeshFile=FPaths::Combine(Directory,TEXT("mesh.acwm"));
    const int64 Size=IFileManager::Get().FileSize(*MeshFile);
    if(!LocalFile(MeshFile) || Size<16 || Size>32*1024*1024) return false;
    TArray<uint8> Bytes;
    if(!FFileHelper::LoadFileToArray(Bytes,*MeshFile)) return false;
    FMemoryReader Reader(Bytes,true);
    uint32 Magic=0; int32 Version=0,VertexCount=0,SectionCount=0;
    Reader<<Magic<<Version<<VertexCount<<SectionCount;
    if(Magic!=0x4d574341 || (!Body?Version!=1:(Version<2||Version>5)) || VertexCount<1 || VertexCount>100000 || SectionCount<1 || SectionCount>64
        || Reader.TotalSize()-Reader.Tell()<static_cast<int64>(VertexCount)*32) return false;
    Vertices.SetNum(VertexCount); Normals.SetNum(VertexCount); UVs.SetNum(VertexCount);
    for(int32 I=0;I<VertexCount;++I)
    {
        float F[8];
        for(float& Value:F) { Reader<<Value; if(!FMath::IsFinite(Value) || FMath::Abs(Value)>1e7f) return false; }
        Vertices[I]=FVector(F[0],F[1],F[2]); Normals[I]=FVector(F[3],F[4],F[5]); UVs[I]=FVector2D(F[6],F[7]);
    }
    SkinBindings.Reset();SkinWeights.Reset();
    if(Version>=3)
    {
        int32 BindCount=0;Reader<<BindCount;
        if(BindCount<1||BindCount>4096||Reader.TotalSize()-Reader.Tell()<int64(BindCount)*(Version>=5?52:28)+int64(VertexCount)*32)return false;
        SkinBindings.SetNum(BindCount);
        for(auto& B:SkinBindings)
        {
            Reader<<B.Part;float F[6];for(float& V:F){Reader<<V;if(!FMath::IsFinite(V)||FMath::Abs(V)>10000)return false;}
            if(B.Part<0||B.Part>16)return false;
            B.Pivot=FVector(F[0],F[1],F[2]);B.End=FVector(F[3],F[4],F[5]);
            if((B.End-B.Pivot).IsNearlyZero())return false;
            if(Version>=5)
            {
                float R[6];for(float& V:R){Reader<<V;if(!FMath::IsFinite(V)||FMath::Abs(V)>10000)return false;}
                B.ReferenceMin=FVector(R[0],R[1],R[2]);B.ReferenceMax=FVector(R[3],R[4],R[5]);
                if(B.ReferenceMin.X>B.ReferenceMax.X||B.ReferenceMin.Y>B.ReferenceMax.Y||B.ReferenceMin.Z>B.ReferenceMax.Z)return false;
            }
        }
        SkinWeights.SetNum(VertexCount*4);
        for(int32 V=0;V<VertexCount;++V)
        {
            float Total=0;
            for(int32 I=0;I<4;++I){auto& W=SkinWeights[V*4+I];Reader<<W.Bone<<W.Weight;
                if(W.Bone<0||W.Bone>=BindCount||!FMath::IsFinite(W.Weight)||W.Weight<0||W.Weight>1)return false;Total+=W.Weight;}
            if(!FMath::IsNearlyEqual(Total,1.f,.02f))return false;
            for(int32 I=0;I<4;++I)SkinWeights[V*4+I].Weight/=Total;
        }
    }
    Sections.SetNum(SectionCount); Textures.SetNum(SectionCount);
    int64 TexturePixels=0;
    for(int32 S=0;S<SectionCount;++S)
    {
        if(Body)
        {
            if(Reader.TotalSize()-Reader.Tell()<4)return false;
            int32& Part=Sections[S].BodyPart;Reader<<Part;
            const bool ValidPart=VerifiedSlot==TEXT("head")?Part==16:
                VerifiedSlot==TEXT("shoulders")?Part==10||Part==13:
                VerifiedSlot==TEXT("waist")?Part==0:VerifiedSlot==TEXT("back")?Part==9:
                VerifiedSlot==TEXT("chest")?Part==0||Part==9:
                VerifiedSlot==TEXT("arms")?Part==10||Part==11||Part==13||Part==14:
                VerifiedSlot==TEXT("hands")?Part==12||Part==15:
                VerifiedSlot==TEXT("legs")?Part>=0&&Part<=8:
                VerifiedSlot==TEXT("feet")?Part==3||Part==4||Part==7||Part==8:false;
            if(Version>=3?(Part!=-1&&(Version<4||!ValidPart)):!ValidPart)return false;
        }
        if(Reader.TotalSize()-Reader.Tell()<8) return false;
        int32 Count=0; Reader<<Sections[S].Blend<<Count;
        if(Sections[S].Blend<0 || Sections[S].Blend>1 || Count<3 || Count>300000 || Count%3!=0
            || Reader.TotalSize()-Reader.Tell()<static_cast<int64>(Count)*4) return false;
        Sections[S].Indices.SetNum(Count);
        for(int32& Index:Sections[S].Indices) { Reader<<Index; if(Index<0 || Index>=VertexCount) return false; }
        const FString TextureFile=FPaths::Combine(Directory,FString::Printf(TEXT("texture-%d.png"),S));
        const int64 TextureSize=IFileManager::Get().FileSize(*TextureFile);
        TArray<uint8> Png;
        if(!LocalFile(TextureFile)||TextureSize<33||TextureSize>80*1024*1024||!FFileHelper::LoadFileToArray(Png,*TextureFile)) return false;
        const uint8 Signature[]={137,80,78,71,13,10,26,10};
        if(FMemory::Memcmp(Png.GetData(),Signature,8)!=0||FMemory::Memcmp(Png.GetData()+12,"IHDR",4)!=0) return false;
        auto BE=[&](int32 I){return (uint32(Png[I])<<24)|(uint32(Png[I+1])<<16)|(uint32(Png[I+2])<<8)|Png[I+3];};
        const uint32 Width=BE(16),Height=BE(20);
        if(!Width||!Height||Width>4096||Height>4096) return false;
        TexturePixels+=int64(Width)*Height;
        if(TexturePixels>32*1024*1024) return false;
        Textures[S]=FImageUtils::ImportBufferAsTexture2D(Png);
        if(!Textures[S]||Textures[S]->GetSizeX()!=Width||Textures[S]->GetSizeY()!=Height) return false;
        Textures[S]->AddressX=TA_Wrap; Textures[S]->AddressY=TA_Wrap;
    }
    if(Reader.IsError() || Reader.Tell()!=Reader.TotalSize()) return false;
    BuildInfo=VerifiedBuildInfo;
    if(!FFileHelper::LoadFileToString(BuildInfoSnapshot,*BuildInfo)) return false;
    Slot=VerifiedSlot;
    // Converters supply their own mount convention. The shared API assumes no source axes.
    const TSharedPtr<FJsonObject>* Mount=nullptr;
    Fit=FTransform::Identity;
    if(Object->TryGetObjectField(TEXT("fit"),Mount))
    {
        double V[7]={0,0,0,0,0,0,1};const TCHAR* Keys[]={TEXT("x"),TEXT("y"),TEXT("z"),TEXT("pitch"),TEXT("yaw"),TEXT("roll"),TEXT("scale")};
        for(int32 I=0;I<7;++I) if(!(*Mount)->TryGetNumberField(Keys[I],V[I])||!FMath::IsFinite(V[I])) return false;
        if(FMath::Abs(V[0])>500||FMath::Abs(V[1])>500||FMath::Abs(V[2])>500||V[6]<.01||V[6]>10) return false;
        Fit=FTransform(FRotator(V[3],V[4],V[5]),FVector(V[0],V[1],V[2]),FVector(V[6]));
    }
    bEnabled=true; NextSearch=0; NextInstallCheck=0;
    Tell(Body?TEXT("Local armor verified. Waiting for your local human character."):TEXT("Local cosmetic verified. Waiting for a matching equipped item on your character."));
    return true;
}

void UACEPluginCosmetics::RestoreOriginal()
{
    if(auto* Scripts=HiddenItemParticles.Get())Scripts->SetCosmeticParticlesHidden(this,false);
    HiddenItemParticles.Reset();
    auto* Root=Cast<UACEPluginCosmetics>(GetOuter());
    for(const FHiddenPart& Hidden:HiddenParts)
        if(auto* Part=Hidden.Part.Get())
        {
            bool OtherOwner=false;
            if(Root)for(const auto& Pair:Root->SlotRenderers)if(Pair.Value!=this)
                OtherOwner|=Pair.Value->HiddenParts.ContainsByPredicate([Part](const FHiddenPart& Other){return Other.Part==Part;});
            if(!OtherOwner){Part->SetRenderInMainPass(Hidden.MainPass);Part->SetCastShadow(Hidden.Shadow);}
        }
    HiddenParts.Empty();
    for(auto Part:BareParts)if(Part)
    {
        if(Root&&Part->bRenderInMainPass)
        {
            UPrimitiveComponent* Successor=nullptr;
            for(const auto& Pair:Root->SlotRenderers)if(Pair.Value!=this)
                for(auto Other:Pair.Value->BareParts)if(Other&&Other->GetAttachParent()==Part->GetAttachParent())Successor=Other;
            if(Successor){Successor->SetRenderInMainPass(true);Successor->SetCastShadow(true);}
        }
        Part->DestroyComponent();
    }
    BareParts.Empty();LastSkinPose.Empty();
    FittedVertices.Empty();FittedNormals.Empty();FittedBodyParts.Empty();
    SkinnedVertices.Empty();SkinnedNormals.Empty();
    for(auto Part:BodyCosmetics)if(Part){if(Part==Cosmetic)Cosmetic=nullptr;Part->DestroyComponent();}
    BodyCosmetics.Empty();BodyPawn.Reset();
    if(Cosmetic) { Cosmetic->DestroyComponent(); Cosmetic=nullptr; }
    ReplacedItem.Reset(); AppearanceRevision=0;
}

void UACEPluginCosmetics::Disable()
{
    CancelTool();
    for(auto& Pair:SlotRenderers) Pair.Value->Disable();
    SlotRenderers.Empty();
    bEnabled=false; bEnableAfterTool=false;
    RestoreOriginal();
    Vertices.Empty(); Normals.Empty(); UVs.Empty(); Sections.Empty(); Textures.Empty();
    Tell(TEXT("Local cosmetics disabled; original AC appearance restored."));
}

void UACEPluginCosmetics::SetFit(const FVector& Offset,const FRotator& Rotation,float Scale)
{
    Fit=FTransform(Rotation,Offset,FVector(Scale));
    if(Cosmetic) Cosmetic->SetRelativeTransform(Fit);
    for(auto Part:BodyCosmetics)if(Part)Part->SetRelativeTransform(Fit);
    Tell(TEXT("Cosmetic fit updated for this session."));
}

bool UACEPluginCosmetics::Attach(AACEWorldEntityActor* Item)
{
    RestoreOriginal();
    if(!Item || !Item->Appearance || !Item->Appearance->HasAppearance()) return false;
    UACEDatSubsystem* Dat=GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
    if(!Dat) return false;
    Cosmetic=NewObject<UProceduralMeshComponent>(Item,NAME_None,RF_Transient);
    Cosmetic->SetIsReplicated(false);
    Cosmetic->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Cosmetic->SetGenerateOverlapEvents(false);
    Cosmetic->SetCanEverAffectNavigation(false);
    Cosmetic->SetupAttachment(Item->GetRootComponent());
    Cosmetic->SetRelativeTransform(Fit);
    Cosmetic->RegisterComponent();
    for(int32 S=0;S<Sections.Num();++S)
    {
        UMaterialInterface* Parent=Sections[S].Blend==1?Dat->EnsureAceUnlitMaskedMaterialBase():Dat->EnsureAceUnlitTexturedMaterialBase();
        if(!Parent) { RestoreOriginal(); return false; }
        auto* Material=UMaterialInstanceDynamic::Create(Parent,Cosmetic);
        Material->SetTextureParameterValue(TEXT("ACETexture"),Textures[S]);
        Material->SetTextureParameterValue(TEXT("Texture"),Textures[S]);
        Cosmetic->CreateMeshSection(S,Vertices,Sections[S].Indices,Normals,UVs,TArray<FColor>(),TArray<FProcMeshTangent>(),false);
        Cosmetic->SetMaterial(S,Material);
    }
    // Preserve original collision/selection geometry and only suppress its rendering.
    for(int32 I=0;I<Item->Appearance->GetPartCount();++I)
        if(auto* Part=Cast<UPrimitiveComponent>(Item->Appearance->GetPartMesh(I)))
        {
            HiddenParts.Add({Part,static_cast<bool>(Part->bRenderInMainPass),static_cast<bool>(Part->CastShadow)});
            Part->SetRenderInMainPass(false); Part->SetCastShadow(false);
        }
    ReplacedItem=Item; AppearanceRevision=Item->Appearance->GetAppearanceRevision();
    HiddenItemParticles=Item->ScriptComponent;
    if(auto* Scripts=HiddenItemParticles.Get())Scripts->SetCosmeticParticlesHidden(this,true);
    Tell(TEXT("Local cosmetic active. Disable this plugin to restore AC appearance."));
    return true;
}

bool UACEPluginCosmetics::AttachBody(APawn* Pawn)
{
    RestoreOriginal();
    if(!SkinBindings.IsEmpty())return AttachSkinnedBody(Pawn);
    auto* App=Pawn?Pawn->FindComponentByClass<UACECharacterAppearanceComponent>():nullptr;
    if(!IsBodySlot(Slot)||!App||!App->HasAppearance())return false;
    // Part indices are the AC human setup contract, not arbitrary creature bones.
    const uint32 Setup=static_cast<uint32>(App->GetSetupId());
    if(Setup!=0x02000001 && Setup!=0x0200004e)return false;
    auto* Dat=GetGameInstance()->GetSubsystem<UACEDatSubsystem>();if(!Dat)return false;
    TMap<int32,UProceduralMeshComponent*> Parts;
    for(int32 S=0;S<Sections.Num();++S)
    {
        const int32 Index=Sections[S].BodyPart;
        auto* Parent=App->GetPartMesh(Index);if(!Parent){RestoreOriginal();return false;}
        auto*& Mesh=Parts.FindOrAdd(Index);
        if(!Mesh)
        {
            Mesh=NewObject<UProceduralMeshComponent>(Pawn,NAME_None,RF_Transient);
            BodyCosmetics.Add(Mesh);if(!Cosmetic)Cosmetic=Mesh;
            Mesh->SetIsReplicated(false);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->SetGenerateOverlapEvents(false);Mesh->SetCanEverAffectNavigation(false);
            Mesh->SetupAttachment(Parent);Mesh->SetRelativeTransform(Fit);Mesh->RegisterComponent();
        }
        auto* Base=Sections[S].Blend==1?Dat->EnsureAceUnlitMaskedMaterialBase():Dat->EnsureAceUnlitTexturedMaterialBase();
        if(!Base){RestoreOriginal();return false;}
        auto* Material=UMaterialInstanceDynamic::Create(Base,Mesh);
        Material->SetTextureParameterValue(TEXT("ACETexture"),Textures[S]);Material->SetTextureParameterValue(TEXT("Texture"),Textures[S]);
        Mesh->CreateMeshSection(S,Vertices,Sections[S].Indices,Normals,UVs,TArray<FColor>(),TArray<FProcMeshTangent>(),false);Mesh->SetMaterial(S,Material);
    }
    if(!ReplaceCoveredParts(Pawn)){RestoreOriginal();return false;}
    BodyPawn=Pawn;AppearanceRevision=App->GetAppearanceRevision();
    Tell(TEXT("Local armor overlay active. It follows your character's animated body parts."));
    return true;
}

bool UACEPluginCosmetics::Tick(float Dt)
{
    PollTool();
    if(bPaperdoll)
    {
        bool Any=false;for(auto& Pair:SlotRenderers){Pair.Value->Tick(Dt);Any|=Pair.Value->bEnabled;}
        if(bEnabled&&!Any)Tell(TEXT("Source installation changed or was removed. Recheck your saved outfit."));
        bEnabled=Any;return true;
    }
    if(!bEnabled) return true;
    const double Now=FPlatformTime::Seconds();
    if(Now>=NextInstallCheck)
    {
        NextInstallCheck=Now+5;
        FString Current;
        if(!FFileHelper::LoadFileToString(Current,*BuildInfo) || Current!=BuildInfoSnapshot)
        { Disable(); Tell(TEXT("Source installation changed or was removed. Reconvert and reload.")); return true; }
    }
    UACEClientSubsystem* Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();
    auto* Controller=GetGameInstance()->GetFirstLocalPlayerController();
    APawn* Pawn=Controller?Controller->GetPawn():nullptr;
    if(!Client || Client->GetSessionState()!=EACESessionState::InWorld || !Pawn)
    { RestoreOriginal(); PlayerGuid=0; return true; }
    if(PlayerGuid!=Client->GetPlayerGuid()) { RestoreOriginal(); PlayerGuid=Client->GetPlayerGuid(); }
    if(!SkinBindings.IsEmpty())UpdateSkin();
    if(Now<NextSearch) return true;
    NextSearch=Now+0.2;
    if(IsBodySlot(Slot))
    {
        auto* App=Pawn->FindComponentByClass<UACECharacterAppearanceComponent>();
        if(!App||!App->HasAppearance()){RestoreOriginal();return true;}
        if(BodyPawn.Get()!=Pawn||!IsValid(Cosmetic)||AppearanceRevision!=App->GetAppearanceRevision())AttachBody(Pawn);
        return true;
    }
    AACEWorldEntityActor* Match=nullptr;
    for(TActorIterator<AACEWorldEntityActor> It(GetWorld());It;++It)
    {
        // Both network ownership and actual attachment must point to the possessed local pawn.
        if(MatchesLocalItem(*It,Pawn,PlayerGuid)) { Match=*It; break; }
    }
    if(!Match) { RestoreOriginal(); return true; }
    if(ReplacedItem.Get()!=Match || !Cosmetic || AppearanceRevision!=Match->Appearance->GetAppearanceRevision()) Attach(Match);
    return true;
}

bool UACEPluginCosmetics::MatchesLocalItem(const AACEWorldEntityActor* Item,const APawn* Pawn,int32 Guid) const
{
    if(!Item || !Pawn || Guid==0 || Item->GetAttachParentActor()!=Pawn
        || (Item->GetWielderId()!=Guid && Item->GetParentGuid()!=Guid)) return false;
    const bool bSlot=Slot==TEXT("shield")?Item->ParentLocation==3:(Slot==TEXT("offhand")?Item->ParentLocation==8:Item->ParentLocation==1);
    return bSlot && Item->Appearance && Item->Appearance->HasAppearance();
}

UACEPluginCosmetics* UACEPluginCosmetics::RendererForSlot(const FString& Id) const
{
    const auto* Found=SlotRenderers.Find(Id);
    if(!Found && Id==TEXT("offhand")) Found=SlotRenderers.Find(TEXT("shield"));
    return Found?Found->Get():nullptr;
}

bool UACEPluginCosmetics::LoadVerified(const FString& Json)
{
    if(!bPaperdoll) { if(!LoadSingle(Json))return false;Host->ClaimLocalCosmetic(PluginId);return true; }
    TSharedPtr<FJsonObject> Object;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Object)||!Object)return false;
    TArray<TSharedPtr<FJsonValue>> Single;const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(!Object->TryGetArrayField(TEXT("appearances"),Values)) { Single.Add(MakeShared<FJsonValueObject>(Object));Values=&Single; }
    if(Values->Num()>12 || (Values->IsEmpty()&&ToolCommand!=TEXT("clear-slot")&&ToolCommand!=TEXT("apply-outfit")&&ToolCommand!=TEXT("verify-active")))return false;
    TMap<FString,TObjectPtr<UACEPluginCosmetics>> Pending;
    for(const auto& Value:*Values)
    {
        auto O=Value->AsObject();if(!O)return false;
        FString Encoded;FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&Encoded));
        auto* Renderer=NewObject<UACEPluginCosmetics>(this);Renderer->Host=Host;Renderer->PluginId=PluginId;Renderer->CacheRoot=CacheRoot;
        if(!Renderer->LoadSingle(Encoded)||Pending.Contains(Renderer->Slot))return false;
        Pending.Add(Renderer->Slot,Renderer);
    }
    if(Pending.Contains(TEXT("offhand"))&&Pending.Contains(TEXT("shield")))return false;
    Host->ClaimLocalCosmetic(PluginId);
    for(auto& Pair:SlotRenderers) Pair.Value->Disable();
    SlotRenderers=MoveTemp(Pending);bEnabled=!SlotRenderers.IsEmpty();
    SlotSelections.Reset();
    // Restore icons for saved/manual conversions as well as dropdown choices.
    // A verified receipt can remain usable even if it is absent from this catalog.
    for(const auto& Value:*Values)
    {
        const auto O=Value->AsObject();const FString Id=O->GetStringField(TEXT("id")),Target=O->GetStringField(TEXT("slot"));
        const FString UiSlot=Target==TEXT("shield")?TEXT("offhand"):Target;
        FString CatalogId,AppearanceName;O->TryGetStringField(TEXT("catalogId"),CatalogId);O->TryGetStringField(TEXT("appearanceName"),AppearanceName);
        if(CatalogId.Len()>256)CatalogId.Empty();if(AppearanceName.Len()>256)AppearanceName.Empty();
        TSharedPtr<FJsonObject> Selection;
        for(const auto& Entry:Catalog)
        {
            FString Cached,EntryId;Entry->TryGetStringField(TEXT("cacheId"),Cached);Entry->TryGetStringField(TEXT("id"),EntryId);
            if(Cached==Id||(!CatalogId.IsEmpty()&&EntryId==CatalogId)){Selection=Entry;Selection->SetStringField(TEXT("cacheId"),Id);break;}
        }
        if(!Selection)
        {
            Selection=MakeShared<FJsonObject>();Selection->SetStringField(TEXT("id"),CatalogId.IsEmpty()?Id:CatalogId);Selection->SetStringField(TEXT("cacheId"),Id);
            Selection->SetStringField(TEXT("name"),AppearanceName.IsEmpty()?TEXT("Saved ")+Target+TEXT(" appearance"):AppearanceName);Selection->SetStringField(TEXT("sourceSlot"),Target);
            Selection->SetBoolField(TEXT("supported"),true);Selection->SetArrayField(TEXT("targets"),{MakeShared<FJsonValueString>(Target)});
            Selection->SetStringField(TEXT("reason"),TEXT("Verified saved appearance. Choose a catalog item to replace it."));
        }
        SlotSelections.Add(UiSlot,Selection);
    }
    SelectSlot(SelectedSlot);
    Object->TryGetStringField(TEXT("slot"),Slot);
    if(auto* Renderer=RendererForSlot(Slot))Fit=Renderer->Fit;
    Tell(FString::Printf(TEXT("%d local appearance(s) verified."),SlotRenderers.Num()));
    return true;
}
