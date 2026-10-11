#include "Mods/ACEPluginCosmetics.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACECharacterCreation.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"

void UACEPluginCosmetics::HideOriginal(UPrimitiveComponent* Part)
{
    if(!Part)return;
    FHiddenPart Saved{Part,static_cast<bool>(Part->bRenderInMainPass),static_cast<bool>(Part->CastShadow)};
    // Overlapping slots must all remember the original state, not another
    // cosmetic's suppression. Any order of outfit teardown then restores it.
    if(auto* Root=Cast<UACEPluginCosmetics>(GetOuter()))
        for(const auto& Pair:Root->SlotRenderers)
            for(const auto& Other:Pair.Value->HiddenParts)
                if(Other.Part==Part){Saved.MainPass=Other.MainPass;Saved.Shadow=Other.Shadow;}
    HiddenParts.Add(Saved);Part->SetRenderInMainPass(false);Part->SetCastShadow(false);
}

bool UACEPluginCosmetics::ReplaceCoveredParts(APawn* Pawn)
{
    auto* App=Pawn->FindComponentByClass<UACECharacterAppearanceComponent>();
    auto* Dat=GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
    if(!App||!Dat||!Dat->GetPortalDat())return false;
    TArray<int32> Covered,Accessories;
    if(Slot==TEXT("head")){Covered={16};Accessories={21,22};}
    else if(Slot==TEXT("shoulders"))Covered={10,13};
    else if(Slot==TEXT("chest"))Covered={0,9};
    else if(Slot==TEXT("arms"))Covered={10,11,13,14};
    else if(Slot==TEXT("hands")){Covered={11,12,14,15};Accessories={27,28};}
    else if(Slot==TEXT("legs"))Covered={1,2,5,6};
    else if(Slot==TEXT("feet"))Covered={2,3,4,6,7,8};
    else if(Slot==TEXT("back"))Accessories={29,30,31,32,33};
    // A buckle is an attachment, not a replacement of the entire pelvis.
    if(Covered.IsEmpty()&&Accessories.IsEmpty())return true;
    FACECharacterCreation Creation;FString Error;
    if(!Creation.Load(*Dat->GetPortalDat(),Error))return false;
    Creation.SelectSex(App->GetSetupId()==0x0200004e?2:1);
    for(auto& Gear:Creation.Selection.GearStyle)Gear=MAX_uint32;
    if(const auto* Sex=Creation.Sex())
        for(int32 I=0;I<Sex->Hair.Num();++I)if(Sex->Hair[I].Bald){Creation.Selection.HairStyle=I;break;}
    FACEWorldObject Bare,Player;
    if(!Creation.BuildAppearance(Bare))return false;
    if(auto* Client=GetGameInstance()->GetSubsystem<UACEClientSubsystem>();Client&&Client->GetWorldObject(Client->GetPlayerGuid(),Player))
    {
        // Preserve the wearer's skin palette and facial texture changes. The
        // geometry comes from the unarmored, bald human rather than the helmet.
        Bare.Appearance.PaletteBaseId=Player.Appearance.PaletteBaseId;
        Bare.Appearance.SubPalettes=Player.Appearance.SubPalettes;
        for(const auto& Change:Player.Appearance.TextureChanges)
            if(Change.PartIndex==16)Bare.Appearance.TextureChanges.Add(Change);
    }
    TArray<UProceduralMeshComponent*> Meshes;Meshes.SetNumZeroed(App->GetPartCount());
    for(int32 Index:Covered)
    {
        auto* Parent=App->GetPartMesh(Index);if(!Parent)return false;
        auto* Mesh=NewObject<UProceduralMeshComponent>(Pawn,NAME_None,RF_Transient);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);Mesh->SetupAttachment(Parent);Mesh->RegisterComponent();
        BareParts.Add(Mesh);Meshes[Index]=Mesh;
        // Only one bare underlay per part, even when gloves overlap sleeves or
        // boots overlap leggings. Sibling renderers keep their own replacement
        // ready so teardown/rebuild never depends on attachment order.
        if(auto* Root=Cast<UACEPluginCosmetics>(GetOuter()))
            for(const auto& Pair:Root->SlotRenderers)if(Pair.Value!=this)
                for(auto Other:Pair.Value->BareParts)
                    if(Other&&Other->GetAttachParent()==Parent&&Other->bRenderInMainPass)
                    {Mesh->SetRenderInMainPass(false);Mesh->SetCastShadow(false);}
    }
    TArray<FTransform> Binds;uint32 Motion=0;
    if(!Meshes.IsEmpty()&&!Dat->ApplySetupParts(Meshes,App->GetSetupId(),Bare.Appearance,100,Binds,Motion,0,false))return false;
    for(int32 Index:Covered)
    {
        Meshes[Index]->SetRelativeTransform(FTransform::Identity);
        HideOriginal(Cast<UPrimitiveComponent>(App->GetPartMesh(Index)));
    }
    for(int32 Index:Accessories)HideOriginal(Cast<UPrimitiveComponent>(App->GetPartMesh(Index)));
    return true;
}

bool UACEPluginCosmetics::AttachSkinnedBody(APawn* Pawn)
{
    auto* App=Pawn?Pawn->FindComponentByClass<UACECharacterAppearanceComponent>():nullptr;
    if(!App||!App->HasAppearance()||(App->GetSetupId()!=0x02000001&&App->GetSetupId()!=0x0200004e))return false;
    auto* Dat=GetGameInstance()->GetSubsystem<UACEDatSubsystem>();if(!Dat)return false;
    for(auto& B:SkinBindings)
    {
        FTransform Bind;if(!App->GetPartBindTransform(B.Part,Bind))return false;
        FVector Anchor=Bind.GetLocation();
        if(B.Part==11||B.Part==14)Anchor=Bind.TransformPosition(FVector(0,0,14));
        const int32 Child=B.Part==10?11:B.Part==13?14:B.Part==11?12:B.Part==14?15:
            B.Part==1?2:B.Part==5?6:B.Part==2?3:B.Part==6?7:INDEX_NONE;
        FVector TargetDirection=B.Part==3||B.Part==7?FVector(0,20,0):
            B.Part==12||B.Part==15?FVector(0,0,-20):FVector(0,0,25);
        FTransform End;
        if(Child!=INDEX_NONE&&App->GetPartBindTransform(Child,End))
        {
            FVector EndPoint=End.GetLocation();
            if(Child==11||Child==14)EndPoint=End.TransformPosition(FVector(0,0,14));
            TargetDirection=EndPoint-Anchor;
        }
        const FVector SourceDirection=B.End-B.Pivot;
        const float Scale=Child!=INDEX_NONE?FMath::Clamp(TargetDirection.Size()/FMath::Max(1.,SourceDirection.Size()),.4,2.):1.f;
        const FQuat Rotation=FQuat::FindBetweenNormals(SourceDirection.GetSafeNormal(),TargetDirection.GetSafeNormal());
        B.ToPart=FTransform(Rotation,Anchor-Rotation.RotateVector(B.Pivot*Scale),FVector(Scale))*Bind.Inverse();
    }
    Cosmetic=NewObject<UProceduralMeshComponent>(Pawn,NAME_None,RF_Transient);
    Cosmetic->SetCollisionEnabled(ECollisionEnabled::NoCollision);Cosmetic->SetGenerateOverlapEvents(false);
    Cosmetic->SetCanEverAffectNavigation(false);Cosmetic->SetupAttachment(App->GetMeshRoot());
    Cosmetic->SetRelativeTransform(Fit);Cosmetic->RegisterComponent();BodyCosmetics.Add(Cosmetic);
    BodyPawn=Pawn;SkinnedVertices=Vertices;SkinnedNormals=Normals;
    for(int32 S=0;S<Sections.Num();++S)
    {
        auto* Base=Sections[S].Blend==1?Dat->EnsureAceUnlitMaskedMaterialBase():Dat->EnsureAceUnlitTexturedMaterialBase();
        if(!Base){RestoreOriginal();return false;}
        auto* Material=UMaterialInstanceDynamic::Create(Base,Cosmetic);
        Material->SetTextureParameterValue(TEXT("ACETexture"),Textures[S]);Material->SetTextureParameterValue(TEXT("Texture"),Textures[S]);
        Cosmetic->CreateMeshSection(S,Vertices,Sections[S].Indices,Normals,UVs,TArray<FColor>(),TArray<FProcMeshTangent>(),false);Cosmetic->SetMaterial(S,Material);
    }
    if(!ReplaceCoveredParts(Pawn)){RestoreOriginal();return false;}
    AppearanceRevision=App->GetAppearanceRevision();UpdateSkin();return true;
}

void UACEPluginCosmetics::UpdateSkin()
{
    auto* App=BodyPawn.IsValid()?BodyPawn->FindComponentByClass<UACECharacterAppearanceComponent>():nullptr;
    if(!App||!Cosmetic||SkinBindings.IsEmpty())return;
    TArray<FTransform,TInlineAllocator<17>> Pose;Pose.SetNum(17);bool Changed=LastSkinPose.Num()!=17;
    for(int32 I=0;I<17;++I)
    {if(!App->GetPartCurrentTransform(I,Pose[I]))return;Changed|=!LastSkinPose.IsValidIndex(I)||!LastSkinPose[I].Equals(Pose[I],.0001);}
    if(!Changed)return;
    LastSkinPose.Reset();LastSkinPose.Append(Pose.GetData(),17);
    TArray<FTransform> Transforms;Transforms.Reserve(SkinBindings.Num());
    for(const auto& B:SkinBindings)Transforms.Add(B.ToPart*Pose[B.Part]);
    for(int32 V=0;V<Vertices.Num();++V)
    {
        FVector P=FVector::ZeroVector,N=FVector::ZeroVector;
        for(int32 I=0;I<4;++I)
        {
            const auto& W=SkinWeights[V*4+I];if(W.Weight<=0)continue;
            P+=Transforms[W.Bone].TransformPosition(Vertices[V])*W.Weight;
            N+=Transforms[W.Bone].TransformVectorNoScale(Normals[V])*W.Weight;
        }
        SkinnedVertices[V]=P;SkinnedNormals[V]=N.GetSafeNormal();
    }
    for(int32 S=0;S<Sections.Num();++S)
        Cosmetic->UpdateMeshSection(S,SkinnedVertices,SkinnedNormals,UVs,TArray<FColor>(),TArray<FProcMeshTangent>());
}
