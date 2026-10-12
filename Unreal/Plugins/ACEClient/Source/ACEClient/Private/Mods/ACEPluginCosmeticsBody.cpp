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
    // A textured waist garment replaces the pelvis; a rigid buckle does not.
    else if(Slot==TEXT("waist")&&!SkinBindings.IsEmpty())Covered={0};
    // A robe or sleeved tunic can cover more parts than its nominal UI slot.
    // Replace only parts weighted by exported clothing vertices, not every
    // bone in the source skeleton (which also contains face/hair bones).
    if(!SkinBindings.IsEmpty()&&(Slot==TEXT("chest")||Slot==TEXT("legs")))for(const auto& W:SkinWeights)if(W.Weight>.05f&&SkinBindings.IsValidIndex(W.Bone))
    {
        const int32 Part=SkinBindings[W.Bone].Part;
        // Small seam weights and atlas-border triangles must not strip an
        // unrelated slot (e.g. gloves removing a shirt or pants removing chest).
        if(Part==0||Part==1||Part==2||Part==5||Part==6||
            (Slot==TEXT("chest")&&(Part==9||Part==10||Part==11||Part==13||Part==14)))Covered.AddUnique(Part);
    }
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
    TArray<int32> References=Covered;
    for(const auto& W:SkinWeights)if(W.Weight>0&&SkinBindings.IsValidIndex(W.Bone))
        References.AddUnique(SkinBindings[W.Bone].Part);
    for(int32 Index:References)
    {
        auto* Parent=App->GetPartMesh(Index);if(!Parent)return false;
        auto* Mesh=NewObject<UProceduralMeshComponent>(Pawn,NAME_None,RF_Transient);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);Mesh->SetupAttachment(Parent);Mesh->RegisterComponent();
        BareParts.Add(Mesh);Meshes[Index]=Mesh;
        // Seams can be influenced by adjacent parts without replacing their
        // clothing. They still need the same anatomical reference fit.
        if(!Covered.Contains(Index)){Mesh->SetVisibility(false);Mesh->SetCastShadow(false);}
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
    for(int32 Index:References)
    {
        Meshes[Index]->SetRelativeTransform(FTransform::Identity);
        // ApplySetupParts enables populated meshes; restore reference-only
        // visibility after it has finished creating their sections.
        if(!Covered.Contains(Index)){Meshes[Index]->SetVisibility(false);Meshes[Index]->SetCastShadow(false);}
    }
    for(int32 Index:Covered)
    {
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
    if(!ReplaceCoveredParts(Pawn)){RestoreOriginal();return false;}
    // Fit to bare anatomy, never to the currently equipped robe/helmet or to
    // decorative armor bounds. Compute once when the appearance changes.
    auto Frame=[](const FVector& Axis,const FVector& Origin)
    {
        const FVector Side=FVector::CrossProduct(FMath::Abs(Axis.Y)>.95?FVector::UpVector:FVector::RightVector,Axis).GetSafeNormal();
        return FMatrix(FPlane(Side,0),FPlane(FVector::CrossProduct(Axis,Side),0),FPlane(Axis,0),FPlane(Origin,1));
    };
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
        B.ToPart=(FTransform(Rotation,Anchor-Rotation.RotateVector(B.Pivot*Scale),FVector(Scale))*Bind.Inverse()).ToMatrixWithScale();
        const FVector SourceSize=B.ReferenceMax-B.ReferenceMin;
        if(SourceSize.GetMin()>1.)
        {
            const FMatrix SourceFrame=Frame(SourceDirection.GetSafeNormal(),B.Pivot);
            const FMatrix TargetFrame=Frame(TargetDirection.GetSafeNormal(),Anchor);
            FBox TargetBounds(ForceInit);
            for(auto Bare:BareParts)if(Bare)
            {
                int32 ReferencePart=B.Part;
                if((B.Part==3||B.Part==7)&&Bare->GetAttachParent()==App->GetPartMesh(B.Part+1))ReferencePart=B.Part+1;
                if(Bare->GetAttachParent()!=App->GetPartMesh(ReferencePart))continue;
                FTransform ReferenceBind;if(!App->GetPartBindTransform(ReferencePart,ReferenceBind))continue;
                const FMatrix ReferenceFrame=ReferenceBind.ToMatrixWithScale()*TargetFrame.Inverse();
                for(int32 S=0;S<Bare->GetNumSections();++S)if(const auto* Section=Bare->GetProcMeshSection(S))
                    for(const auto& V:Section->ProcVertexBuffer)TargetBounds+=FVector(ReferenceFrame.TransformPosition(V.Position));
            }
            if(TargetBounds.IsValid&&TargetBounds.GetSize().GetMin()>1.)
            {
                FVector Proportion=TargetBounds.GetSize()/SourceSize;
                for(int32 I=0;I<3;++I)Proportion[I]=FMath::Clamp(Proportion[I],.25,2.5);
                // A small radial allowance keeps thin cloth outside the skin.
                Proportion.X*=1.025;Proportion.Y*=1.025;
                FVector SourceCenter=(B.ReferenceMin+B.ReferenceMax)*.5,TargetCenter=TargetBounds.GetCenter();
                // Joint anchors define segment length; reference body bounds
                // only change girth. Preserve cuffs/necklines relative to joints.
                if(B.Part!=0&&B.Part!=9&&B.Part!=3&&B.Part!=7&&B.Part!=12&&B.Part!=15)
                {Proportion.Z=Scale;TargetCenter.Z=SourceCenter.Z*Scale;}
                else if(B.Part!=0&&B.Part!=9)Proportion.Z*=1.06;
                const FMatrix Resize=FTransform(FQuat::Identity,FVector::ZeroVector,Proportion).ToMatrixWithScale();
                B.ToPart=SourceFrame.Inverse()*FTranslationMatrix(-SourceCenter)*Resize
                    *FTranslationMatrix(TargetCenter)*TargetFrame*Bind.ToMatrixWithScale().Inverse();
            }
        }
        B.ToPartNormal=B.ToPart.Inverse().GetTransposed();
    }
    // The two games have different muscle/foot shapes, even after joint and
    // girth fitting. Project penetrating garment vertices out to the bare AC
    // surface in bind space once; preserve decorations already outside it.
    // This never runs per frame and never changes collision or the AC body.
    struct FBodyTriangle { FVector A,B,C,N; };
    TMap<int32,TArray<FBodyTriangle>> BodyTriangles;
    TArray<FMatrix> BindMatrices;BindMatrices.SetNum(17);
    for(int32 Part=0;Part<17;++Part)
    {
        FTransform Bind;if(!App->GetPartBindTransform(Part,Bind))return false;
        BindMatrices[Part]=Bind.ToMatrixWithScale();
        for(auto Bare:BareParts)if(Bare&&Bare->GetAttachParent()==App->GetPartMesh(Part))
            for(int32 S=0;S<Bare->GetNumSections();++S)if(const auto* Section=Bare->GetProcMeshSection(S))
                for(int32 T=0;T+2<Section->ProcIndexBuffer.Num();T+=3)
                {
                    auto Point=[&](int32 I){return Bind.TransformPosition(FVector(Section->ProcVertexBuffer[Section->ProcIndexBuffer[T+I]].Position));};
                    FVector Normal=FVector::ZeroVector;
                    for(int32 I=0;I<3;++I)Normal+=FVector(Section->ProcVertexBuffer[Section->ProcIndexBuffer[T+I]].Normal);
                    BodyTriangles.FindOrAdd(Part).Add({Point(0),Point(1),Point(2),Bind.TransformVectorNoScale(Normal).GetSafeNormal()});
                }
    }
    TSet<int32> RigidVertices;
    for(const auto& Section:Sections)if(Section.BodyPart>=0)for(int32 V:Section.Indices)RigidVertices.Add(V);
    FittedVertices=Vertices;
    FittedNormals=Normals;FittedBodyParts.Init(INDEX_NONE,Vertices.Num());
    for(int32 V=0;V<Vertices.Num();++V)
    {
        if(RigidVertices.Contains(V))continue;
        int32 Dominant=0;FMatrix Blended(ForceInit);
        for(int32 I=0;I<4;++I)
        {
            const auto& W=SkinWeights[V*4+I];if(W.Weight<=0)continue;
            if(W.Weight>SkinWeights[V*4+Dominant].Weight)Dominant=I;
            const auto& B=SkinBindings[W.Bone];Blended=Blended+(B.ToPart*BindMatrices[B.Part])*W.Weight;
        }
        const auto& B=SkinBindings[SkinWeights[V*4+Dominant].Bone];
        if((B.ReferenceMax-B.ReferenceMin).GetMin()<=1.)continue;
        FVector P=FVector(Blended.TransformPosition(Vertices[V]));
        const FMatrix Mounted=B.ToPart*BindMatrices[B.Part];
        const FVector Axis=FVector(Mounted.TransformVector(B.End-B.Pivot)).GetSafeNormal();
        const FVector Center=FVector(Mounted.TransformPosition(B.Pivot));
        const FVector Origin=Center+Axis*FVector::DotProduct(P-Center,Axis);
        const FVector Direction=(P-Origin).GetSafeNormal();const double Radius=(P-Origin).Size();
        double Surface=0;int32 FittedPart=B.Part;
        TSet<int32> Parts;Parts.Add(B.Part);
        if(B.Part==3||B.Part==7)Parts.Add(B.Part+1);
        for(int32 I=0;I<4;++I)if(SkinWeights[V*4+I].Weight>.05)Parts.Add(SkinBindings[SkinWeights[V*4+I].Bone].Part);
        if(B.Part!=3&&B.Part!=7)for(int32 Part:Parts)
            if(const auto* Triangles=BodyTriangles.Find(Part))for(const auto& T:*Triangles)
            {
                const FVector E1=T.B-T.A,E2=T.C-T.A,H=FVector::CrossProduct(Direction,E2);
                const double D=FVector::DotProduct(E1,H);if(FMath::Abs(D)<1.e-8)continue;
                const FVector S=Origin-T.A;const double U=FVector::DotProduct(S,H)/D;if(U<0||U>1)continue;
                const FVector Q=FVector::CrossProduct(S,E1);const double W=FVector::DotProduct(Direction,Q)/D;if(W<0||U+W>1)continue;
                const double Distance=FVector::DotProduct(E2,Q)/D;
                if(Distance>Surface)Surface=Distance;
            }
        // Boots bridge the independently moving foot/toe seam. Reserve enough
        // clearance for the facets between surface samples as that seam bends.
        const double Clearance=B.Part==3||B.Part==7?2.:B.Part==0||B.Part==9?1.5:1.;
        if(Surface>0&&Radius<Surface+Clearance)
            P=Origin+Direction*(Surface+Clearance);
        if(B.Part==3||B.Part==7)
        {
            // The ankle axis sits ABOVE the forefoot. A radial ray through
            // it can hit both the instep and sole and move the boot top below
            // the foot. Fit to the closest outward surface instead.
            P=FVector(Blended.TransformPosition(Vertices[V]));
            double Nearest=TNumericLimits<double>::Max();FVector Contact=FVector::ZeroVector,Normal=FVector::ZeroVector;
            for(int32 Part:{B.Part,B.Part+1})if(const auto* Triangles=BodyTriangles.Find(Part))for(const auto& T:*Triangles)
            {
                const FVector Q=FMath::ClosestPointOnTriangleToPoint(P,T.A,T.B,T.C);
                const double Distance=(P-Q).SizeSquared();
                if(Distance<Nearest){Nearest=Distance;Contact=Q;Normal=T.N;FittedPart=Part;}
            }
            if(Nearest<TNumericLimits<double>::Max()&&FVector::DotProduct(P-Contact,Normal)<Clearance)
                P=Contact+Normal*Clearance;
        }
        // AC animates segmented parts rather than the source game's smooth
        // skeleton. Rebind fitted surfaces to those parts so a waist/cuff does
        // not slide back through the skin when a combat pose bends the joint.
        FittedBodyParts[V]=FittedPart;
        FittedVertices[V]=FVector(BindMatrices[FittedPart].Inverse().TransformPosition(P));
        FittedNormals[V]=FVector((Blended*BindMatrices[FittedPart].Inverse()).Inverse().GetTransposed().TransformVector(Normals[V])).GetSafeNormal();
    }
    // Adjacent-part references are only needed during fitting, never retained
    // as extra rendered geometry or per-frame work.
    for(int32 I=BareParts.Num()-1;I>=0;--I)if(!BareParts[I]->IsVisible())
    {BareParts[I]->DestroyComponent();BareParts.RemoveAt(I);}
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
    TArray<FMatrix> Transforms,NormalTransforms;Transforms.Reserve(SkinBindings.Num());NormalTransforms.Reserve(SkinBindings.Num());
    TArray<FMatrix,TInlineAllocator<17>> PoseMatrices,PoseNormals;
    for(const auto& P:Pose){const FMatrix M=P.ToMatrixWithScale();PoseMatrices.Add(M);PoseNormals.Add(M.Inverse().GetTransposed());}
    for(const auto& B:SkinBindings)
    {
        Transforms.Add(B.ToPart*PoseMatrices[B.Part]);NormalTransforms.Add(B.ToPartNormal*PoseNormals[B.Part]);
    }
    for(int32 V=0;V<Vertices.Num();++V)
    {
        if(FittedBodyParts[V]!=INDEX_NONE)
        {
            const int32 Part=FittedBodyParts[V];
            SkinnedVertices[V]=FVector(PoseMatrices[Part].TransformPosition(FittedVertices[V]));
            SkinnedNormals[V]=FVector(PoseNormals[Part].TransformVector(FittedNormals[V])).GetSafeNormal();
            continue;
        }
        FVector P=FVector::ZeroVector,N=FVector::ZeroVector;
        for(int32 I=0;I<4;++I)
        {
            const auto& W=SkinWeights[V*4+I];if(W.Weight<=0)continue;
            P+=FVector(Transforms[W.Bone].TransformPosition(FittedVertices[V]))*W.Weight;
            N+=FVector(NormalTransforms[W.Bone].TransformVector(Normals[V]))*W.Weight;
        }
        SkinnedVertices[V]=P;SkinnedNormals[V]=N.GetSafeNormal();
    }
    // Composite clothing can carry rigid buckles/attachments. These vertices
    // are authored in their AC part's local frame, not the shared human rig.
    for(const auto& Section:Sections)if(Section.BodyPart>=0)
        for(int32 V:Section.Indices)
        {
            SkinnedVertices[V]=Pose[Section.BodyPart].TransformPosition(Vertices[V]);
            SkinnedNormals[V]=Pose[Section.BodyPart].TransformVectorNoScale(Normals[V]).GetSafeNormal();
        }
    for(int32 S=0;S<Sections.Num();++S)
        Cosmetic->UpdateMeshSection(S,SkinnedVertices,SkinnedNormals,UVs,TArray<FColor>(),TArray<FProcMeshTangent>());
}
