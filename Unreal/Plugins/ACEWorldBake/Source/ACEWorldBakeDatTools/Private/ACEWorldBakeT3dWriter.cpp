#include "ACEWorldBakeT3dWriter.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeCellFile.h"
#include "ACEWorldBakePortalFile.h"
#include "ACEWorldBakeDatEnvironment.h"
#include "ACEWorldBakeDatModel.h"
#include "ACEWorldBakeDatScene.h"
#include "ACEWorldBakeDatSetup.h"
#include "ACEWorldBakeDatRegion.h"
#include "LandscapeDataAccess.h"
#include "Misc/DateTime.h"
#include "Modules/ModuleManager.h"

const float kStructureExEnvironmentZBump = 0.001f; // 1mm

static FVector3f ToUnrealLocation(const FACEWorldBakeResourceLocation& Location) { return FVector3f(Location.OffsetX, -Location.OffsetY, Location.OffsetZ) * 100.0f; }

FACEWorldBakeT3dContent::FACEWorldBakeT3dContent(int32 InDepth)
: FACEWorldBakeT3dContent(TEXT("Invalid: Invalid\r\n"), InDepth)
{
}

FACEWorldBakeT3dContent::FACEWorldBakeT3dContent(const FString& InContent, int32 InTabDepth)
: Content(InContent)
, TabDepth(InTabDepth)
, PrevDepth(FMath::Max(0, InTabDepth - 1))
, NextDepth(InTabDepth + 1)
{
	if (0 < TabDepth)
	{
		for (int32 i = 0; i < InTabDepth; ++i)
		{
			Indent += TEXT("\t");
		}

		Content.InsertAt(0, Indent);
	}
}

FACEWorldBakeT3dSection::FACEWorldBakeT3dSection(const FString& InSectionTag, const FString& InSectionValue, int32 InTabDepth)
: FACEWorldBakeT3dContent(FString::Printf(TEXT("Begin %s%s\r\n"), *InSectionTag, *InSectionValue), InTabDepth)
, SectionTag(InSectionTag)
{
}

FACEWorldBakeT3dSection::operator FString() const
{
	FString OutContent = Content;
	AppendBody(OutContent);
	AppendFooter(OutContent);
	return OutContent;
}

void FACEWorldBakeT3dSection::AppendFooter(FString& OutContent) const
{
	OutContent.Append(FString(FString::Printf(TEXT("%sEnd %s\r\n"), *Indent, *SectionTag)));
}

struct FACEWorldBakeT3dSceneComponent : public FACEWorldBakeT3dSection
{
public:
	FACEWorldBakeT3dSceneComponent(const FACEWorldBakeScaledResourceLocation& InLocation, const FString& InIdentifier, const FString& InAttachParentType, const FString& InAttachParent, bool InUseAttachParentBounds, int32 InTabDepth)
	: FACEWorldBakeT3dSection(TEXT("Object"), FString::Printf(TEXT(" Name=\"%s\""), *InIdentifier), InTabDepth)
	, Location(InLocation)
	, AttachParentType(InAttachParentType)
	, AttachParent(InAttachParent)
	, bUseAttachParentBounds(InUseAttachParentBounds)
	{
	}

	FACEWorldBakeT3dSceneComponent(const FACEWorldBakeScaledResourceLocation& InLocation, int32 InIdentifier, int32 InIndex, int32 InDepth, const FString& InAttachParentType, const FString& InAttachParent, bool InUseAttachParentBounds, const FString& InIdentifierPrefix = TEXT(""))
	: FACEWorldBakeT3dSection(TEXT("Object"), FString::Printf(TEXT(" Name=\"%s%d_0x%08X\""), *InIdentifierPrefix, InIndex, InIdentifier), InDepth)
	, Location(InLocation)
	, AttachParentType(InAttachParentType)
	, AttachParent(InAttachParent)
	, bUseAttachParentBounds(InUseAttachParentBounds)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		const FVector3f UnrealLocation = ToUnrealLocation(Location);
		FQuat4f UnrealQuat = FQuat4f(-Location.HeadingA, Location.HeadingB, -Location.HeadingC, Location.HeadingW);
		UnrealQuat.Normalize();

		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("AttachParent=%s'\"%s\"'\r\n"), *AttachParentType, *AttachParent), NextDepth)));

		if (bUseAttachParentBounds)
		{
			OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("bUseAttachParentBound=True\r\n"), NextDepth)));
		}

		if (!UnrealLocation.IsNearlyZero())
		{
			OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("RelativeLocation=(X=%1.6f,Y=%1.6f,Z=%1.6f)\r\n"), UnrealLocation.X, UnrealLocation.Y, UnrealLocation.Z), NextDepth)));
		}

		if (!UnrealQuat.Rotator().IsNearlyZero())
		{
			OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("RelativeRotation=(Pitch=%1.6f,Yaw=%1.6f,Roll=%1.6f)\r\n"), UnrealQuat.Rotator().Pitch, UnrealQuat.Rotator().Yaw, UnrealQuat.Rotator().Roll), NextDepth)));
		}

		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("RelativeScale3D=(X=%1.6f,Y=%1.6f,Z=%1.6f)\r\n"), Location.Scale.X, Location.Scale.Y, Location.Scale.Z), NextDepth)));

		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("CreationMethod=Instance\r\n"), NextDepth)));
	}

	FACEWorldBakeScaledResourceLocation Location;
	FString AttachParentType;
	FString AttachParent;
	bool bUseAttachParentBounds;
};

struct FACEWorldBakeT3dLightComponent : public FACEWorldBakeT3dSceneComponent
{
public:
	FACEWorldBakeT3dLightComponent(const FACEWorldBakeSetupLight& InLight, const FACEWorldBakeResourceLocation& InLocation, int32 InIndex, int32 InTabDepth, const FString& InAttachParentType, const FString& InAttachParent)
	: FACEWorldBakeT3dSceneComponent(FACEWorldBakeScaledResourceLocation(InLocation, FVector3f::OneVector), FString::Printf(TEXT("Light_%03d"), InIndex), InAttachParentType, InAttachParent, false, InTabDepth)
	, Light(InLight)
	, AttachParent(InAttachParent)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("AttenuationRadius=%1.6f\r\n"), Light.Falloff * 100.0f), NextDepth)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("Intensity=%1.6f\r\n"), Light.Intensity), NextDepth)));
		const uint8* const pColor = reinterpret_cast<const uint8*>(&Light.Color);
		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("LightColor=(B=%d,G=%d,R=%d,A=%d)\r\n"), pColor[0], pColor[1], pColor[2], pColor[3]), NextDepth)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("CastShadows=False\r\n"), NextDepth)));
		FACEWorldBakeT3dSceneComponent::AppendBody(OutContent);
	}

	FACEWorldBakeSetupLight Light;
	FString AttachParent;
};

struct FACEWorldBakeT3dLandblockStaticObject : public FACEWorldBakeT3dSceneComponent
{
public:
	FACEWorldBakeT3dLandblockStaticObject(const FACEWorldBakeScaledResourceInfo& InStaticObject, int32 InIndex, int32 InTabDepth, const FString& InAttachParentType, const FString& InAttachParent, bool InUseAttachParentBounds, bool InOccluder, const FString& InIdentifierPrefix = TEXT(""), const TArray<uint32>& InOverrideMaterials = {})
	: FACEWorldBakeT3dSceneComponent(FACEWorldBakeScaledResourceLocation(InStaticObject.Location, InStaticObject.Scale), FString::Printf(TEXT("%s%d_0x%08X"), *InIdentifierPrefix, InIndex, InStaticObject.ResourceId), InAttachParentType, InAttachParent, InUseAttachParentBounds, InTabDepth)
	, StaticObject(InStaticObject)
	, AttachParent(InAttachParent)
	, OverrideMaterials(InOverrideMaterials)
	, bOccluder(InOccluder)
	{
	}

	FACEWorldBakeT3dLandblockStaticObject(const FACEWorldBakeScaledResourceInfo& InStaticObject, int32 InIndex, int32 InSubIndex, int32 InTabDepth, const FString& InAttachParentType, const FString& InAttachParent, bool InUseAttachParentBounds, bool InOccluder, const FString& InIdentifierPrefix = TEXT(""), const TArray<uint32>& InOverrideMaterials = {})
	: FACEWorldBakeT3dSceneComponent(FACEWorldBakeScaledResourceLocation(InStaticObject.Location, InStaticObject.Scale), FString::Printf(TEXT("%s%d_0x%08X_%d"), *InIdentifierPrefix, InIndex, InStaticObject.ResourceId, InSubIndex), InAttachParentType, InAttachParent, InUseAttachParentBounds, InTabDepth)
	, StaticObject(InStaticObject)
	, AttachParent(InAttachParent)
	, OverrideMaterials(InOverrideMaterials)
	, bOccluder(InOccluder)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		FString ResourcePath;
		if (EACEWorldBakeResource_Native::kEnvironment == (StaticObject.ResourceId & EACEWorldBakeResource_Native::kMask))
		{
			ResourcePath = TEXT("Environments");
		}
		else
		{
			ResourcePath = TEXT("Models");
		}

		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("StaticMesh=StaticMesh'\"/Game/%s/0x%08X.0x%08X\"'\r\n"), *ResourcePath, StaticObject.ResourceId, StaticObject.ResourceId), NextDepth)));

		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("StaticMeshImportVersion=1\r\n"), NextDepth)));

		for (int32 OverrideMaterialIndex = 0; OverrideMaterialIndex < OverrideMaterials.Num(); ++OverrideMaterialIndex)
		{
			const uint32 SurfaceId = EACEWorldBakeResource_Native::kSurface | OverrideMaterials[OverrideMaterialIndex];
			OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("OverrideMaterials(%d)=MaterialInstanceConstant'\"/Game/Surfaces/0x%08X.0x%08X\"'\r\n"), OverrideMaterialIndex, SurfaceId, SurfaceId), NextDepth)));
		}

		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("bGenerateOverlapEvents=False\r\n"), NextDepth)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("bUseAsOccluder=%s\r\n"), bOccluder ? TEXT("True") : TEXT("False")), NextDepth)));

		FACEWorldBakeT3dSceneComponent::AppendBody(OutContent);
	}

	FACEWorldBakeResourceInfo StaticObject;
	FString AttachParent;
	TArray<uint32> OverrideMaterials;
	bool bOccluder;
};

struct FACEWorldBakeT3dLandblockStaticObjectInstanced : public FACEWorldBakeT3dSceneComponent
{
public:
	FACEWorldBakeT3dLandblockStaticObjectInstanced(uint32 InResourceId, int32 InIndex, int32 InTabDepth, const FString& InAttachParentType, const FString& InAttachParent, bool InUseAttachParentBounds, bool InOccluder, const FString& InIdentifierPrefix = TEXT(""), const TArray<uint32>& InOverrideMaterials = {})
	: FACEWorldBakeT3dSceneComponent(FACEWorldBakeScaledResourceLocation(), FString::Printf(TEXT("%s%d_0x%08X_Instanced"), *InIdentifierPrefix, InIndex, InResourceId), InAttachParentType, InAttachParent, InUseAttachParentBounds, InTabDepth)
	, ResourceId(InResourceId)
	, Index(InIndex)
	, OverrideMaterials(InOverrideMaterials)
	, bOccluder(InOccluder)
	{
	}

	void AddInstance(const FACEWorldBakeScaledResourceLocation& InstanceLocation)
	{
		Instances.Add(InstanceLocation);
	}

	void AppendPerInstanceData(FString& OutContent) const
	{
		for (int32 InstanceIndex = 0; InstanceIndex < Instances.Num(); ++InstanceIndex)
		{
			const FACEWorldBakeScaledResourceLocation& InstanceLocation = Instances[InstanceIndex];
			const FVector3f UnrealLocation = ToUnrealLocation(InstanceLocation);
			FQuat4f UnrealQuat = FQuat4f(-InstanceLocation.HeadingA, InstanceLocation.HeadingB, -InstanceLocation.HeadingC, InstanceLocation.HeadingW);
			UnrealQuat.Normalize();

			const FVector3f InstanceScale = Location.Scale * InstanceLocation.Scale;
			const FString XPlane = FString::Printf(TEXT("(W=%1.6f,X=%1.6f,Y=%1.6f,Z=%1.6f)"), 0.0f, UnrealQuat.GetAxisX().X * InstanceScale.X, UnrealQuat.GetAxisX().Y, UnrealQuat.GetAxisX().Z);
			const FString YPlane = FString::Printf(TEXT("(W=%1.6f,X=%1.6f,Y=%1.6f,Z=%1.6f)"), 0.0f, UnrealQuat.GetAxisY().X, UnrealQuat.GetAxisY().Y * InstanceScale.Y, UnrealQuat.GetAxisY().Z);
			const FString ZPlane = FString::Printf(TEXT("(W=%1.6f,X=%1.6f,Y=%1.6f,Z=%1.6f)"), 0.0f, UnrealQuat.GetAxisZ().X, UnrealQuat.GetAxisZ().Y, UnrealQuat.GetAxisZ().Z * InstanceScale.Z);
			const FString WPlane = FString::Printf(TEXT("(W=%1.6f,X=%1.6f,Y=%1.6f,Z=%1.6f)"), 1.0f, UnrealLocation.X, UnrealLocation.Y, UnrealLocation.Z);
			OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("PerInstanceSMData(%d)=(Transform=(XPlane=%s,YPlane=%s,ZPlane=%s,WPlane=%s))\r\n"), InstanceIndex, *XPlane, *YPlane, *ZPlane, *WPlane), NextDepth)));
		}
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		FString ResourcePath;
		if (EACEWorldBakeResource_Native::kEnvironment == (ResourceId & EACEWorldBakeResource_Native::kMask))
		{
			ResourcePath = TEXT("Environments");
		}
		else
		{
			ResourcePath = TEXT("Models");
		}

		// @avoid UE 5.4 check(InstancingRandomSeed !=0)
		// InstancingRandomSeed is typically set via ALightWeightInstanceStaticMeshManager::SetInstancedStaticMeshParams() to FMath::Rand<int32>()
		// for now nothing is intentionally randomized and instancing is intentionally identical
		const int32 InstancedStaticMeshRandomSeed = 1;
		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("InstancingRandomSeed=%d\r\n"), InstancedStaticMeshRandomSeed), NextDepth)));

		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("StaticMesh=StaticMesh'\"/Game/%s/0x%08X.0x%08X\"'\r\n"), *ResourcePath, ResourceId, ResourceId), NextDepth)));

		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("StaticMeshImportVersion=1\r\n"), NextDepth)));

		for (int32 OverrideMaterialIndex = 0; OverrideMaterialIndex < OverrideMaterials.Num(); ++OverrideMaterialIndex)
		{
			const uint32 SurfaceId = EACEWorldBakeResource_Native::kSurface | OverrideMaterials[OverrideMaterialIndex];
			OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("OverrideMaterials(%d)=MaterialInstanceConstant'\"/Game/Surfaces/0x%08X.0x%08X\"'\r\n"), OverrideMaterialIndex, SurfaceId, SurfaceId), NextDepth)));
		}

		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("bGenerateOverlapEvents=False\r\n"), NextDepth)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("bUseAsOccluder=%s\r\n"), bOccluder ? TEXT("True") : TEXT("False")), NextDepth)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("bHasPerInstanceHitProxies=True\r\n"), NextDepth)));

		FACEWorldBakeT3dSceneComponent::AppendBody(OutContent);
	}

	uint32 ResourceId;
	int32 Index;
	TArray<uint32> OverrideMaterials;
	TArray<FACEWorldBakeScaledResourceLocation> Instances;
	bool bOccluder;
};

void AppendResourceHeader(const FACEWorldBakePortalFile& PortalFile, const FACEWorldBakeResourceInfo& ResourceInfo, const FString& NamePrefix, int32 ObjectIndex, int32 NextDepth, FString& OutContent)
{
	switch (ResourceInfo.ResourceId & EACEWorldBakeResource_Native::kMask)
	{
		case EACEWorldBakeResource_Native::kEnvironment:
		case EACEWorldBakeResource_Native::kModel:
			OutContent.Append(FString(FACEWorldBakeT3dSection(TEXT("Object"), FString::Printf(TEXT(" Class=/Script/Engine.StaticMeshComponent Name=\"%s%d_0x%08X\""), *NamePrefix, ObjectIndex, ResourceInfo.ResourceId), NextDepth + 2)));
			break;

		case EACEWorldBakeResource_Native::kSetup:
		{
			FACEWorldBakePortalSetup SetupObject;
			if (PortalFile.GetResource(ResourceInfo.ResourceId, SetupObject))
			{
				for (int32 ModelResourceIndex = 0; ModelResourceIndex < SetupObject.ModelResourceIds.Num(); ++ModelResourceIndex)
				{
					const int32 ModelResourceId = SetupObject.ModelResourceIds[ModelResourceIndex];
					OutContent.Append(FString(FACEWorldBakeT3dSection(TEXT("Object"), FString::Printf(TEXT(" Class=/Script/Engine.StaticMeshComponent Name=\"%s%d_0x%08X_%d\""), *NamePrefix, ObjectIndex, ModelResourceId, ModelResourceIndex), NextDepth + 2)));
				}

				for (int32 LightIndex = 0; LightIndex < SetupObject.Lights.Num(); ++LightIndex)
				{
					OutContent.Append(FString(FACEWorldBakeT3dSection(TEXT("Object"), FString::Printf(TEXT(" Class=/Script/Engine.PointLightComponent Name=\"Light_%03d\""), ObjectIndex + LightIndex), NextDepth + 2)));
				}
			}
		}
		break;

		case EACEWorldBakeResource_Native::kScene:
		{
			FACEWorldBakePortalScene Scene;
			if (PortalFile.GetResource(ResourceInfo.ResourceId, Scene))
			{
				for (int32 SceneObjectIndex = 0; SceneObjectIndex < Scene.Objects.Num(); ++SceneObjectIndex)
				{
					const FACEWorldBakePortalSceneObject& SceneObject = Scene.Objects[SceneObjectIndex];
					AppendResourceHeader(PortalFile, SceneObject.ResourceInfo, NamePrefix, ObjectIndex, NextDepth, OutContent);
				}
			}
		}
		break;
	}
}

void AppendResourceBody(const FACEWorldBakePortalFile& PortalFile, const FACEWorldBakeScaledResourceInfo& ResourceInfo, const FString& NamePrefix, int32 ObjectIndex, int32 NextDepth, FString& OutContent, const TArray<uint32>& EnvironmentSurfaceIds = {})
{
	const bool bInParentsBounds = (FMath::Abs(ResourceInfo.Location.OffsetX) < 96.0f) && (FMath::Abs(ResourceInfo.Location.OffsetY) < 96.0f);

	switch (ResourceInfo.ResourceId & EACEWorldBakeResource_Native::kMask)
	{
		case EACEWorldBakeResource_Native::kEnvironment:
		{
			OutContent.Append(FString(FACEWorldBakeT3dLandblockStaticObject(ResourceInfo, ObjectIndex, NextDepth + 2, TEXT("SceneComponent"), TEXT("DefaultSceneRoot"), bInParentsBounds, true, NamePrefix, EnvironmentSurfaceIds)));
		}
		break;

		case EACEWorldBakeResource_Native::kModel:
		{
			OutContent.Append(FString(FACEWorldBakeT3dLandblockStaticObject(ResourceInfo, ObjectIndex, NextDepth + 2, TEXT("SceneComponent"), TEXT("DefaultSceneRoot"), bInParentsBounds, false, NamePrefix)));
		}
		break;

		case EACEWorldBakeResource_Native::kSetup:
		{
			FACEWorldBakePortalSetup SetupObject;
			if (PortalFile.GetResource(ResourceInfo.ResourceId, SetupObject))
			{
				const TArray<FACEWorldBakeResourceLocation>* const ModelLocations = SetupObject.PlacementFrames.Contains(0x65) ? SetupObject.PlacementFrames.Find(0x65) : SetupObject.PlacementFrames.Find(0);
				for (int32 ModelResourceIndex = 0; ModelResourceIndex < SetupObject.ModelResourceIds.Num(); ++ModelResourceIndex)
				{
					const int32 ModelResourceId = SetupObject.ModelResourceIds[ModelResourceIndex];

					FACEWorldBakeResourceLocation ModelLocation;
					if (ModelLocations)
					{
						ModelLocation = (*ModelLocations)[(ModelResourceIndex < ModelLocations->Num()) ? ModelResourceIndex : 0];
					}
					else
					{
						ModelLocation.HeadingA = ModelLocation.HeadingB = ModelLocation.HeadingC = ModelLocation.HeadingW = ModelLocation.OffsetX = ModelLocation.OffsetY = ModelLocation.OffsetZ = 0.0f;
					}

					ModelLocation = SumResourceLocation(ModelLocation, ResourceInfo.Location);

					// todo: is SetupObject.Flags used for anything? i.e. scaling/placement frame/etc?

					const FVector3f ModelScale = SetupObject.ModelScales.IsValidIndex(ModelResourceIndex) ? SetupObject.ModelScales[ModelResourceIndex] : FVector3f::OneVector;
					const FACEWorldBakeScaledResourceInfo SubResourceInfo(ModelResourceId, ModelLocation, ModelScale);
					const bool bSubResourceInParentsBounds = (FMath::Abs(SubResourceInfo.Location.OffsetX) < 96.0f) && (FMath::Abs(SubResourceInfo.Location.OffsetY) < 96.0f);
					OutContent.Append(FString(FACEWorldBakeT3dLandblockStaticObject(SubResourceInfo, ObjectIndex, ModelResourceIndex, NextDepth + 2, TEXT("SceneComponent"), TEXT("DefaultSceneRoot"), bSubResourceInParentsBounds, false, NamePrefix)));
				}

				for (int32 LightIndex = 0; LightIndex < SetupObject.Lights.Num(); ++LightIndex)
				{
					FACEWorldBakeResourceLocation LightLocation = SumResourceLocation(SetupObject.Lights[LightIndex].Location, ResourceInfo.Location);

					FACEWorldBakeT3dLightComponent LightComponent(SetupObject.Lights[LightIndex], LightLocation, ObjectIndex + LightIndex, NextDepth + 2, TEXT("SceneComponent"), TEXT("DefaultSceneRoot"));
					OutContent.Append(FString(LightComponent));
				}
			}
		}
		break;

		case EACEWorldBakeResource_Native::kScene:
		{
			FACEWorldBakePortalScene Scene;
			if (PortalFile.GetResource(ResourceInfo.ResourceId, Scene))
			{
				for (int32 SceneObjectIndex = 0; SceneObjectIndex < Scene.Objects.Num(); ++SceneObjectIndex)
				{
					const FACEWorldBakePortalSceneObject& SceneObject = Scene.Objects[SceneObjectIndex];
					AppendResourceBody(PortalFile, SceneObject.ResourceInfo, NamePrefix, ObjectIndex, NextDepth, OutContent);
				}
			}
		}
		break;
	}
}

int32 AppendResourceFooter(const FACEWorldBakePortalFile& PortalFile, const FACEWorldBakeResourceInfo& ResourceInfo, const FString& NamePrefix, int32 ObjectIndex, int32 ResourceIndex, int32 NextDepth, FString& OutContent)
{
	switch (ResourceInfo.ResourceId & EACEWorldBakeResource_Native::kMask)
	{
		case EACEWorldBakeResource_Native::kEnvironment:
		case EACEWorldBakeResource_Native::kModel:
			OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("InstanceComponents(%d)=StaticMeshComponent'\"%s%d_0x%08X\"'\r\n"), ResourceIndex++, *NamePrefix, ObjectIndex, ResourceInfo.ResourceId), NextDepth + 2)));
			break;

		case EACEWorldBakeResource_Native::kSetup:
		{
			FACEWorldBakePortalSetup SetupObject;
			if (PortalFile.GetResource(ResourceInfo.ResourceId, SetupObject))
			{
				for (int32 ModelResourceIndex = 0; ModelResourceIndex < SetupObject.ModelResourceIds.Num(); ++ModelResourceIndex)
				{
					const int32 ModelResourceId = SetupObject.ModelResourceIds[ModelResourceIndex];
					OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("InstanceComponents(%d)=StaticMeshComponent'\"%s%d_0x%08X_%d\"'\r\n"), ResourceIndex++, *NamePrefix, ObjectIndex, ModelResourceId, ModelResourceIndex), NextDepth + 2)));
				}

				for (int32 LightIndex = 0; LightIndex < SetupObject.Lights.Num(); ++LightIndex)
				{
					OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("InstanceComponents(%d)=PointLightComponent'\"Light_%03d\"'\r\n"), ResourceIndex++, ObjectIndex + LightIndex), NextDepth + 2)));
				}
			}
		}
		break;

		case EACEWorldBakeResource_Native::kScene:
		{
			FACEWorldBakePortalScene Scene;
			if (PortalFile.GetResource(ResourceInfo.ResourceId, Scene))
			{
				for (int32 SceneObjectIndex = 0; SceneObjectIndex < Scene.Objects.Num(); ++SceneObjectIndex)
				{
					const FACEWorldBakePortalSceneObject& SceneObject = Scene.Objects[SceneObjectIndex];
					AppendResourceFooter(PortalFile, SceneObject.ResourceInfo, NamePrefix, ObjectIndex, ResourceIndex, NextDepth, OutContent);
				}
			}
		}
		break;
	}

	return ResourceIndex;
}

struct FACEWorldBakeT3dScene : public FACEWorldBakeT3dSection
{
public:
	FACEWorldBakeT3dScene(const FACEWorldBakePortalScene& InScene)
		: FACEWorldBakeT3dSection(TEXT("Map"), TEXT(""), 0)
		, Scene(InScene)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		const FString NamePrefix = TEXT("");

		IACEWorldBakeDatToolsModule& ACEWorldBakeDatToolsModule = FModuleManager::LoadModuleChecked<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools");
		FACEWorldBakePortalFile PortalFile(ACEWorldBakeDatToolsModule.GetDatFile(EACEWorldBakeDatFile::Portal));

		FACEWorldBakeT3dSection LevelSection(TEXT("Level"), TEXT(""), NextDepth);
		OutContent.Append(FString(LevelSection.GetContent()));

		FACEWorldBakeT3dSection ActorSection(TEXT("Actor"), FString::Printf(TEXT(" Class=/Script/Engine.Actor Name=0x%08X Archetype=/Script/Engine.Actor'/Script/Engine.Default__Actor'"), Scene.ResourceId), NextDepth + 1);
		OutContent.Append(FString(ActorSection.GetContent()));

		// resource headers
		OutContent.Append(FString(FACEWorldBakeT3dSection(TEXT("Object"), FString::Printf(TEXT(" Class=/Script/Engine.SceneComponent Name=\"DefaultSceneRoot\"")), NextDepth + 2)));

		for (int32 ObjectIndex = 0; ObjectIndex < Scene.Objects.Num(); ++ObjectIndex)
		{
			const FACEWorldBakeResourceInfo& StaticObject = Scene.Objects[ObjectIndex].ResourceInfo;
			AppendResourceHeader(PortalFile, StaticObject, NamePrefix, ObjectIndex, NextDepth, OutContent);
		}

		// resource body
		for (int32 ObjectIndex = 0; ObjectIndex < Scene.Objects.Num(); ++ObjectIndex)
		{
			const FACEWorldBakeResourceInfo& StaticObject = Scene.Objects[ObjectIndex].ResourceInfo;
			AppendResourceBody(PortalFile, StaticObject, NamePrefix, ObjectIndex, NextDepth, OutContent);
		}

		FACEWorldBakeT3dSection SceneRootSection(TEXT("Object"), TEXT(" Name=\"DefaultSceneRoot\""), NextDepth + 2);
		OutContent.Append(FString(SceneRootSection.GetContent()));
		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("RelativeLocation=(X=%1.6f,Y=%1.6f,Z=%1.6f)\r\n"), 0.0f, 0.0f, 0.0f), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("bVisualizeComponent=True\r\n"), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("CreationMethod=Instance\r\n"), NextDepth + 3)));
		SceneRootSection.AppendFooter(OutContent);

		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("RootComponent=SceneComponent'\"DefaultSceneRoot\"'\r\n"), NextDepth + 2)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("ActorLabel=\"0x%08X\"\r\n"), Scene.ResourceId), NextDepth + 2)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("InstanceComponents(0)=SceneComponent'\"DefaultSceneRoot\"'\r\n"), NextDepth + 2)));

		int32 ResourceIndex = 1;

		// resource footers
		for (int32 ObjectIndex = 0; ObjectIndex < Scene.Objects.Num(); ++ObjectIndex)
		{
			const FACEWorldBakeResourceInfo& StaticObject = Scene.Objects[ObjectIndex].ResourceInfo;
			ResourceIndex = AppendResourceFooter(PortalFile, StaticObject, NamePrefix, ObjectIndex, ResourceIndex, NextDepth, OutContent);
		}

		ActorSection.AppendFooter(OutContent);
		LevelSection.AppendFooter(OutContent);

		OutContent.Append(FString(FACEWorldBakeT3dSection(TEXT("Surface"), TEXT(""), TabDepth)));
	}

	FACEWorldBakePortalScene Scene;
};

struct FACEWorldBakeT3dSetup : public FACEWorldBakeT3dSection
{
public:
	FACEWorldBakeT3dSetup(const FACEWorldBakePortalSetup& InSetup)
		: FACEWorldBakeT3dSection(TEXT("Map"), TEXT(""), 0)
		, Setup(InSetup)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		const FString NamePrefix = TEXT("");

		IACEWorldBakeDatToolsModule& ACEWorldBakeDatToolsModule = FModuleManager::LoadModuleChecked<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools");
		FACEWorldBakePortalFile PortalFile(ACEWorldBakeDatToolsModule.GetDatFile(EACEWorldBakeDatFile::Portal));

		FBox3f SetupBounds = FBox3f::BuildAABB(FVector3f::ZeroVector, FVector3f(10.0f));

		// resource headers
		FACEWorldBakeT3dSection LevelSection(TEXT("Level"), TEXT(""), NextDepth);
		OutContent.Append(FString(LevelSection.GetContent()));

		FACEWorldBakeT3dSection ActorSection(TEXT("Actor"), FString::Printf(TEXT(" Class=/Script/Engine.Actor Name=0x%08X Archetype=/Script/Engine.Actor'/Script/Engine.Default__Actor'"), Setup.ResourceId), NextDepth + 1);
		OutContent.Append(FString(ActorSection.GetContent()));

		FACEWorldBakeT3dSection BoundsBoxRootSection(TEXT("Object"), TEXT(" Class=/Script/Engine.BoxComponent Name=\"DefaultSceneRoot\""), NextDepth + 2);
		OutContent.Append(FString(BoundsBoxRootSection.GetContent()));
		OutContent.Append(FString(FACEWorldBakeT3dSection(TEXT("Object"), TEXT(" Class=/Script/Engine.BodySetup Name=\"BodySetup_0\""), NextDepth + 3)));
		BoundsBoxRootSection.AppendFooter(OutContent);

		for (int32 ModelResourceIndex = 0; ModelResourceIndex < Setup.ModelResourceIds.Num(); ++ModelResourceIndex)
		{
			FACEWorldBakeResourceInfo ModelResourceInfo;
			ModelResourceInfo.ResourceId = Setup.ModelResourceIds[ModelResourceIndex];
			AppendResourceHeader(PortalFile, ModelResourceInfo, NamePrefix, ModelResourceIndex, NextDepth, OutContent);
		}

		// resource body
		{
			const TArray<FACEWorldBakeResourceLocation>* const ModelLocations = Setup.PlacementFrames.Contains(0x65) ? Setup.PlacementFrames.Find(0x65) : Setup.PlacementFrames.Find(0);
			for (int32 ModelResourceIndex = 0; ModelResourceIndex < Setup.ModelResourceIds.Num(); ++ModelResourceIndex)
			{
				const int32 ModelResourceId = Setup.ModelResourceIds[ModelResourceIndex];

				FACEWorldBakeResourceLocation ModelLocation;
				if (ModelLocations)
				{
					ModelLocation = (*ModelLocations)[(ModelResourceIndex < ModelLocations->Num()) ? ModelResourceIndex : 0];
				}
				else
				{
					ModelLocation.HeadingA = ModelLocation.HeadingB = ModelLocation.HeadingC = ModelLocation.HeadingW = ModelLocation.OffsetX = ModelLocation.OffsetY = ModelLocation.OffsetZ = 0.0f;
				}

				// todo: is SetupObject.Flags used for anything? i.e. scaling/placement frame/etc?

				const FVector3f ModelScale = Setup.ModelScales.IsValidIndex(ModelResourceIndex) ? Setup.ModelScales[ModelResourceIndex] : FVector3f::OneVector;
				const FACEWorldBakeScaledResourceInfo ResourceInfo(ModelResourceId, ModelLocation, ModelScale);
				FACEWorldBakePortalModel ResourceModel;
				if (PortalFile.GetResource(ModelResourceId, ResourceModel))
				{
					for (const FACEWorldBakePortalModelVertex& ModelVertex : ResourceModel.Vertices)
					{
						FACEWorldBakeResourceLocation VertexLocation;
						VertexLocation.OffsetX = ModelVertex.Position.X;
						VertexLocation.OffsetY = ModelVertex.Position.Y;
						VertexLocation.OffsetZ = ModelVertex.Position.Z;
						FACEWorldBakeResourceLocation ModelVertexLocation = SumResourceLocation(ModelLocation, VertexLocation);

						const FVector3f ModelVertexLocationVector = FVector3f(ModelVertexLocation.OffsetX, ModelVertexLocation.OffsetY, ModelVertexLocation.OffsetZ);
						SetupBounds = SetupBounds.ExpandBy(ModelVertexLocationVector);
					}
				}

				AppendResourceBody(PortalFile, ResourceInfo, NamePrefix, ModelResourceIndex, NextDepth, OutContent);
			}
		}

		FACEWorldBakeT3dSection SceneRootSection(TEXT("Object"), TEXT(" Name=\"DefaultSceneRoot\""), NextDepth + 2);
		OutContent.Append(FString(SceneRootSection.GetContent()));

		FACEWorldBakeT3dSection SceneRootBodySetupSection(TEXT("Object"), TEXT(" Class=/Script/Engine.BodySetup Name=\"BodySetup_0\""), NextDepth + 3);
		OutContent.Append(FString(SceneRootBodySetupSection.GetContent()));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("AggGeom=(BoxElems=((TM=(XPlane=(W=0.000000,X=0.000000,Y=0.000000,Z=-0.000000),YPlane=(W=0.000000,X=0.000000,Y=0.000000,Z=0.000000),ZPlane=(W=0.000000,X=0.000000,Y=0.000000,Z=-0.000000),WPlane=(W=0.000000,X=-76802848.000000,Y=0.000000,Z=0.000000)),X=19200.000000,Y=19200.000000,Z=25600.000000)))\r\n"), NextDepth + 4)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("CollisionTraceFlag=CTF_UseSimpleAsComplex\r\n"), NextDepth + 4)));
		SceneRootBodySetupSection.AppendFooter(OutContent);

		// todo: SetupBounds is wrong and is would need to be centered to be correct
		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("BoxExtent=(X=%1.6f,Y=%1.6f,Z=%1.6f)\r\n"), SetupBounds.GetExtent().X, SetupBounds.GetExtent().Y, SetupBounds.GetExtent().Z), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("bGenerateOverlapEvents=False\r\n"), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("CanCharacterStepUpOn=ECB_No\r\n"), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("BodyInstance=(ObjectType=ECC_WorldStatic,CollisionProfileName=\"NoCollision\",CollisionResponses=(ResponseArray=((Channel=\"Visibility\",Response=ECR_Ignore),(Channel=\"Camera\",Response=ECR_Ignore))),CollisionEnabled=NoCollision,MaxAngularVelocity=3599.999756)\r\n"), NextDepth + 3)));

		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("RelativeLocation=(X=%1.6f,Y=%1.6f,Z=%1.6f)\r\n"), 0.0f, 0.0f, 0.0f), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("bVisualizeComponent=True\r\n"), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("CreationMethod=Instance\r\n"), NextDepth + 3)));
		SceneRootSection.AppendFooter(OutContent);

		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("RootComponent=SceneComponent'\"DefaultSceneRoot\"'\r\n"), NextDepth + 2)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("ActorLabel=\"0x%08X\"\r\n"), Setup.ResourceId), NextDepth + 2)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("InstanceComponents(0)=SceneComponent'\"DefaultSceneRoot\"'\r\n"), NextDepth + 2)));

		int32 ResourceIndex = 1;
		// resource footers
		for (int32 ModelResourceIndex = 0; ModelResourceIndex < Setup.ModelResourceIds.Num(); ++ModelResourceIndex)
		{
			FACEWorldBakeResourceInfo ModelResourceInfo;
			ModelResourceInfo.ResourceId = Setup.ModelResourceIds[ModelResourceIndex];
			ResourceIndex = AppendResourceFooter(PortalFile, ModelResourceInfo, NamePrefix, ModelResourceIndex, ResourceIndex, NextDepth, OutContent);
		}

		ActorSection.AppendFooter(OutContent);
		LevelSection.AppendFooter(OutContent);

		OutContent.Append(FString(FACEWorldBakeT3dSection(TEXT("Surface"), TEXT(""), TabDepth)));
	}

	FACEWorldBakePortalSetup Setup;
};

double prng(uint32 CellX, uint32 CellY, uint32 Seed)
{
	uint32 Ival = 0x6C1AC587 * CellY - 0x421BE3BD * CellX - Seed * (0x5111BFEF * CellY * CellX + 0x70892FB7);
	return static_cast<double>(Ival) * 2.3283064e-10;
}

TScaledResourceInfoArray GetLandblockSceneResources(const FACEWorldBakePortalRegion& Region, uint32 CellX, uint32 CellY)
{
	TScaledResourceInfoArray SceneResources;

	IACEWorldBakeDatToolsModule& ACEWorldBakeDatToolsModule = FModuleManager::LoadModuleChecked<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools");
	FACEWorldBakePortalFile PortalFile(ACEWorldBakeDatToolsModule.GetDatFile(EACEWorldBakeDatFile::Portal));
	FACEWorldBakeCellFile CellFile(ACEWorldBakeDatToolsModule.GetDatFile(EACEWorldBakeDatFile::Cell));

	const FACEWorldBakeCellLandblock* const LandBlock = CellFile.GetLandblock(CellX, CellY);
	if (LandBlock)
	{
		FACEWorldBakeCellLandblockObject lbi;
		CellFile.GetLandblockObject(CellX, CellY, lbi);

		for (uint32 TopographyY = 0; TopographyY < FACEWorldBakeCellLandblock::kDimension; ++TopographyY)
		{
			for (uint32 TopographyX = 0; TopographyX < FACEWorldBakeCellLandblock::kDimension; ++TopographyX)
			{
				const uint8 RegionTerrain = LandBlock->GetRegionTerrain(TopographyX, TopographyY);
				check(RegionTerrain < static_cast<uint32>(Region.TerrainTypes.Num()));
				const uint8 RegionTerrainScene = LandBlock->GetRegionScene(TopographyX, TopographyY);
				check(RegionTerrainScene < static_cast<uint32>(Region.TerrainTypes[RegionTerrain].TerrainSceneTypes.Num()));
				const uint32 SceneType = Region.TerrainTypes[RegionTerrain].TerrainSceneTypes[RegionTerrainScene];
				check(SceneType < static_cast<uint32>(Region.SceneTypes.Num()));

				const uint32 SceneIdCount = Region.SceneTypes[SceneType].SceneIds.Num();
				if (0 < SceneIdCount)
				{
					// TODO: use global var/const for lblock_side (8)
					uint32 SceneX = CellX * 8 + TopographyX;
					uint32 SceneY = CellY * 8 + TopographyY;

					uint32 SceneNum = static_cast<uint32>(prng(SceneX, SceneY, 0x00002bf9) * static_cast<double>(SceneIdCount));

					if (SceneNum >= SceneIdCount)
					{
						SceneNum = 0;
					}

					const uint32 SceneID = Region.SceneTypes[SceneType].SceneIds[SceneNum];
					FACEWorldBakePortalScene Scene;
					const bool ValidScene = PortalFile.GetResource(SceneID, Scene);
					check(ValidScene);

					for (int32 i = 0; i < Scene.Objects.Num(); i++)
					{
						const FACEWorldBakePortalSceneObject& PortalSceneObject = Scene.Objects[i];
						if (PortalSceneObject.IntIsWeenieObj != 0)
						{
							//PortalSceneObject.ResourceInfo.ResourceId is invalid?
							continue;
						}

						if (!(prng(SceneX, SceneY, 0x00005b67/* + i*/) < PortalSceneObject.Frequency))
						{
							// hidden
							continue;
						}

						FVector3f CellPos = FVector3f(PortalSceneObject.ResourceInfo.Location.OffsetX, PortalSceneObject.ResourceInfo.Location.OffsetY, PortalSceneObject.ResourceInfo.Location.OffsetZ);

						if (PortalSceneObject.DisplaceX > 0.0f)
						{
							CellPos.X += static_cast<float>(prng(SceneX, SceneY, 0x0000b2cd + i) * PortalSceneObject.DisplaceX);
						}

						if (PortalSceneObject.DisplaceY > 0.0f)
						{
							CellPos.Y += static_cast<float>(prng(SceneX, SceneY, 0x00011c0f + i) * PortalSceneObject.DisplaceY);
						}

						FVector3f TempPos = CellPos;

						double sceneRot = prng(SceneX, SceneY, 0x0000e7eb);

						if (sceneRot >= 0.75)
						{
							CellPos.X = TempPos.Y;
							CellPos.Y = -TempPos.X;
						}
						else if (sceneRot >= 0.5)
						{
							CellPos.X = -TempPos.X;
							CellPos.Y = -TempPos.Y;
						}
						else if (sceneRot >= 0.25)
						{
							CellPos.X = -TempPos.Y;
							CellPos.Y = TempPos.X;
						}
						else
						{
							CellPos.X = TempPos.Y;
							CellPos.Y = TempPos.X;
						}

						FVector3f BlockPos = CellPos + FVector3f(TopographyX * 24.0f, TopographyY * 24.0f, 0.0);

						if (BlockPos.X < 0.0 || BlockPos.X >= 192.0f || BlockPos.Y < 0.0 || BlockPos.Y >= 192.0f)
						{
							continue;
						}

						if (LandBlock->OnRoad(BlockPos, Region))
							continue;

						if (lbi.StaticObjectExCount > 0)
						{
							uint32 objcellid = ((uint32)(floor(BlockPos.X / 24.0f)) << 3) | (uint32)(floor(BlockPos.Y / 24.0f));

							bool found = false;
							for (int32 bi = 0; bi < lbi.StaticObjectExCount; bi++)
							{
								const FACEWorldBakeResourceLocation& loc = lbi.StaticObjectsEx[bi].Location;
								uint32 bldcellid = ((uint32)(floor(loc.OffsetX / 24.0f)) << 3) | (uint32)(floor(loc.OffsetY / 24.0f));

								if (objcellid == bldcellid)
								{
									found = true;
									break;
								}
							}

							if (found) continue;
						}

						const FPlane4f TrianglePlane = LandBlock->GetTrianglePlane(BlockPos.X, BlockPos.Y, Region.HeightValues);

						float Dot = FVector3f::DotProduct(FVector3f::UpVector, TrianglePlane);
						float Radians = FMath::Acos(Dot);
						float Measurement = TrianglePlane.Z;
						if (Measurement < PortalSceneObject.MinSlope || Measurement > PortalSceneObject.MaxSlope)
						{
							continue;
						}

						BlockPos.Z += LandBlock->GetHeight(BlockPos.X, BlockPos.Y, Region.HeightValues);

						const float Scale = (PortalSceneObject.MinScale == PortalSceneObject.MaxScale) ? PortalSceneObject.MaxScale : static_cast<float>(PortalSceneObject.MinScale * FMath::Pow(PortalSceneObject.MaxScale / PortalSceneObject.MinScale, prng(SceneX, SceneY, 0x00007f51 + i)));

						const float RandYaw = FMath::DegreesToRadians(static_cast<float>(prng(SceneX, SceneY, 0x0000f697 + i)) * PortalSceneObject.MaxRotation * 0.0174533f);

						FACEWorldBakeScaledResourceInfo ResourceInfo;
						ResourceInfo.ResourceId = PortalSceneObject.ResourceInfo.ResourceId;
						ResourceInfo.Location.OffsetX = BlockPos.X;
						ResourceInfo.Location.OffsetY = BlockPos.Y;
						ResourceInfo.Location.OffsetZ = BlockPos.Z;
						ResourceInfo.Location.HeadingA = 0.0f;
						ResourceInfo.Location.HeadingB = 0.0f;
						ResourceInfo.Location.HeadingC = 1.0f;
						ResourceInfo.Location.HeadingW = RandYaw;
						ResourceInfo.Scale = FVector3f(Scale);
						SceneResources.Add(ResourceInfo);
					}
				}
			}
		}
	}

	return SceneResources;
}

FBox3f GetLandblockBounds(const FACEWorldBakePortalRegion& Region, uint32 CellX, uint32 CellY)
{
	TArray<FACEWorldBakeResourceInfo> SceneResources;

	IACEWorldBakeDatToolsModule& ACEWorldBakeDatToolsModule = FModuleManager::LoadModuleChecked<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools");
	FACEWorldBakePortalFile PortalFile(ACEWorldBakeDatToolsModule.GetDatFile(EACEWorldBakeDatFile::Portal));
	FACEWorldBakeCellFile CellFile(ACEWorldBakeDatToolsModule.GetDatFile(EACEWorldBakeDatFile::Cell));

	float High = 50.0f;
	float Low = -50.0f;

	const FACEWorldBakeCellLandblock* const LandBlock = CellFile.GetLandblock(CellX, CellY);
	if (LandBlock)
	{
		for (uint32 TopographyY = 0; TopographyY < FACEWorldBakeCellLandblock::kDimension; ++TopographyY)
		{
			for (uint32 TopographyX = 0; TopographyX < FACEWorldBakeCellLandblock::kDimension; ++TopographyX)
			{
				const float RegionTopographyHeight = 128.0f * LandBlock->GetHeightAt(TopographyX, TopographyY, Region.HeightValues);
				if (RegionTopographyHeight < Low)
				{
					Low = RegionTopographyHeight;
				}

				if (High < RegionTopographyHeight)
				{
					High = RegionTopographyHeight;
				}
			}
		}
	}

	const float MidPoint = Low + (FMath::Abs(Low) + FMath::Abs(High) * 0.5f);

	return FBox3f::BuildAABB(FVector3f(0.0f, 0.0f, /*MidPoint*/0.0f), FVector3f(9600.0f, 9600.0f, High));
}

void MapResources(const TScaledResourceInfoArray& Resources, TScaledResourceLocationsMap& InOutResourceMap)
{
	IACEWorldBakeDatToolsModule& ACEWorldBakeDatToolsModule = FModuleManager::LoadModuleChecked<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools");
	FACEWorldBakePortalFile PortalFile(ACEWorldBakeDatToolsModule.GetDatFile(EACEWorldBakeDatFile::Portal));

	for (const FACEWorldBakeScaledResourceInfo& Resource : Resources)
	{
		if (EACEWorldBakeResource_Native::kSetup == (Resource.ResourceId & EACEWorldBakeResource_Native::kMask))
		{
			FACEWorldBakePortalSetup SetupObject;
			if (PortalFile.GetResource(Resource.ResourceId, SetupObject))
			{
				const TArray<FACEWorldBakeResourceLocation>* const ModelLocations = SetupObject.PlacementFrames.Contains(0x65) ? SetupObject.PlacementFrames.Find(0x65) : SetupObject.PlacementFrames.Find(0);
				for (int32 ModelResourceIndex = 0; ModelResourceIndex < SetupObject.ModelResourceIds.Num(); ++ModelResourceIndex)
				{
					const int32 ModelResourceId = SetupObject.ModelResourceIds[ModelResourceIndex];

					FACEWorldBakeResourceLocation ModelLocation;
					if (ModelLocations)
					{
						ModelLocation = (*ModelLocations)[(ModelResourceIndex < ModelLocations->Num()) ? ModelResourceIndex : 0];
					}
					else
					{
						ModelLocation.HeadingA = ModelLocation.HeadingB = ModelLocation.HeadingC = ModelLocation.HeadingW = ModelLocation.OffsetX = ModelLocation.OffsetY = ModelLocation.OffsetZ = 0.0f;
					}

					ModelLocation = SumResourceLocation(ModelLocation, Resource.Location);

					if (!InOutResourceMap.Contains(ModelResourceId))
					{
						InOutResourceMap.Add(ModelResourceId, MakeShareable(new TArray<FACEWorldBakeScaledResourceLocation>()));
					}

					const FVector3f ModelScale = SetupObject.ModelScales.IsValidIndex(ModelResourceIndex) ? SetupObject.ModelScales[ModelResourceIndex] : FVector3f::OneVector;
					InOutResourceMap[ModelResourceId]->Add(FACEWorldBakeScaledResourceLocation(ModelLocation, ModelScale));
				}
			}
		}
		else
		{
			check(EACEWorldBakeResource_Native::kModel == (Resource.ResourceId & EACEWorldBakeResource_Native::kMask));
			if (!InOutResourceMap.Contains(Resource.ResourceId))
			{
				InOutResourceMap.Add(Resource.ResourceId, MakeShareable(new TArray<FACEWorldBakeScaledResourceLocation>()));
			}

			InOutResourceMap[Resource.ResourceId]->Add(FACEWorldBakeScaledResourceLocation(Resource.Location, FVector3f::OneVector));
		}
	}
}

struct FACEWorldBakeT3dMap : public FACEWorldBakeT3dSection
{
public:
	FACEWorldBakeT3dMap(const int32 InCellX, const int32 InCellY, const FACEWorldBakeCellLandblockObject& InLandblockInfos)
	: FACEWorldBakeT3dSection(TEXT("Map"), TEXT(""), 0)
	, LandblockInfos(InLandblockInfos)
	, CellX(InCellX)
	, CellY(InCellY)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		const FString NamePrefixEnvironment = TEXT("ev");
		const FString NamePrefixStatic = TEXT("sa");
		const FString NamePrefixStaticEx = TEXT("se");
		const FString NamePrefixStructure = TEXT("su");
		const FString NamePrefixScene = TEXT("sc");

		IACEWorldBakeDatToolsModule& ACEWorldBakeDatToolsModule = FModuleManager::LoadModuleChecked<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools");
		FACEWorldBakePortalFile PortalFile(ACEWorldBakeDatToolsModule.GetDatFile(EACEWorldBakeDatFile::Portal));
		FACEWorldBakeCellFile CellFile(ACEWorldBakeDatToolsModule.GetDatFile(EACEWorldBakeDatFile::Cell));

		FACEWorldBakePortalRegion PortalRegion;
		const bool ValidRegion = PortalFile.GetResource(EACEWorldBakeResource_Native::kRegion, PortalRegion);
		check(ValidRegion);

		TArray<FACEWorldBakeCellStructure> Structures;
		if (0 < LandblockInfos.StructureCount)
		{
			CellFile.GetLandblockStructures(LandblockInfos, Structures);
		}

		FBox3f Bounds = GetLandblockBounds(PortalRegion, CellX, CellY);

		FACEWorldBakeT3dSection LevelSection(TEXT("Level"), TEXT(""), NextDepth);
		OutContent.Append(FString(LevelSection.GetContent()));

		FACEWorldBakeT3dSection ActorSection(TEXT("Actor"), FString::Printf(TEXT(" Class=/Script/Engine.Actor Name=Landblock_%03d_%03d Archetype=/Script/Engine.Actor'/Script/Engine.Default__Actor'"), CellX, CellY), NextDepth + 1);
		OutContent.Append(FString(ActorSection.GetContent()));

		TScaledResourceLocationsMap StructureMap;
		for (const FACEWorldBakeCellStructure& Structure : Structures)
		{
			TScaledResourceInfoArray ScaledStaticObjects;
			Algo::Transform(Structure.StaticObjects, ScaledStaticObjects, [](const FACEWorldBakeResourceInfo& StaticObjectEs)
			{
				return FACEWorldBakeScaledResourceInfo(StaticObjectEs, FVector3f::OneVector);
			});

			MapResources(ScaledStaticObjects, StructureMap);
		}

		TArray<uint32> StructureMapKeys;
		const int32 NumStructureKeys = StructureMap.GetKeys(StructureMapKeys);

		// resource headers
		FACEWorldBakeT3dSection BoundsBoxRootSection(TEXT("Object"), TEXT(" Class=/Script/Engine.BoxComponent Name=\"DefaultSceneRoot\""), NextDepth + 2);
		OutContent.Append(FString(BoundsBoxRootSection.GetContent()));
		OutContent.Append(FString(FACEWorldBakeT3dSection(TEXT("Object"), TEXT(" Class=/Script/Engine.BodySetup Name=\"BodySetup_0\""), NextDepth + 3)));
		BoundsBoxRootSection.AppendFooter(OutContent);

		{
			for (int32 StaticObjectIndex = 0; StaticObjectIndex < LandblockInfos.StaticObjectCount; ++StaticObjectIndex)
			{
				const FACEWorldBakeResourceInfo& StaticObject = LandblockInfos.StaticObjects[StaticObjectIndex];
				AppendResourceHeader(PortalFile, StaticObject, NamePrefixStatic, StaticObjectIndex, NextDepth, OutContent);
				Bounds += ToUnrealLocation(StaticObject.Location);
			}

			for (int32 StaticObjectIndexEx = 0; StaticObjectIndexEx < LandblockInfos.StaticObjectExCount; ++StaticObjectIndexEx)
			{
				const FACEWorldBakeResourceInfo& StaticObjectEx = LandblockInfos.StaticObjectsEx[StaticObjectIndexEx];
				AppendResourceHeader(PortalFile, StaticObjectEx, NamePrefixStaticEx, StaticObjectIndexEx, NextDepth, OutContent);
				Bounds += ToUnrealLocation(StaticObjectEx.Location);
			}

			int32 StructureObjectIndex = 0;
			for (uint32 Key : StructureMapKeys)
			{
				if (1 < StructureMap[Key]->Num())
				{
					FACEWorldBakeT3dSection InstancedMeshSection(TEXT("Object"), FString::Printf(TEXT(" Class=/Script/Engine.InstancedStaticMeshComponent Name=\"%s%d_0x%08X_Instanced\""), *NamePrefixStructure, StructureObjectIndex, Key), NextDepth + 2);
					OutContent.Append(FString(InstancedMeshSection.GetContent()));
					InstancedMeshSection.AppendBody(OutContent);
					InstancedMeshSection.AppendFooter(OutContent);
				}
				else
				{
					FACEWorldBakeScaledResourceInfo StaticObject(Key, (*StructureMap[Key])[0]);
					AppendResourceHeader(PortalFile, StaticObject, NamePrefixStructure, StructureObjectIndex, NextDepth, OutContent);
				}

				StructureObjectIndex++;
			}

			TMap<uint32, TSet<FVector3f>> EnvironmentMap;

			for (int32 StructureIndex = 0; StructureIndex < Structures.Num(); ++StructureIndex)
			{
				const FACEWorldBakeCellStructure& Structure = Structures[StructureIndex];
				TSet<FVector3f>* Locations = EnvironmentMap.Find(Structure.EnvironmentId);
				const FVector3f Location(Structure.Location.OffsetX, -Structure.Location.OffsetY, Structure.Location.OffsetZ);
				if (0 != Structure.EnvironmentId && (!Locations || !Locations->Contains(Location)))
				{
					FACEWorldBakeResourceInfo StructureEnvironment;
					StructureEnvironment.Location = Structure.Location;
					StructureEnvironment.ResourceId = EACEWorldBakeResource_Native::kEnvironment | Structure.EnvironmentId;
					AppendResourceHeader(PortalFile, StructureEnvironment, NamePrefixEnvironment, StructureIndex, NextDepth, OutContent);

					if (Locations)
					{
						Locations->Add(Location);
					}
					else
					{
						TSet<FVector3f> NewLocations;
						NewLocations.Add(Location);
						EnvironmentMap.Add(Structure.EnvironmentId, NewLocations);
					}

					Bounds += ToUnrealLocation(StructureEnvironment.Location);
				}
			}
		}

		// resource bodies
		{
			for (int32 StaticObjectIndex = 0; StaticObjectIndex < LandblockInfos.StaticObjectCount; ++StaticObjectIndex)
			{
				FACEWorldBakeResourceInfo StaticObject = LandblockInfos.StaticObjects[StaticObjectIndex];
				StaticObject.Location.OffsetX -= 96.0f;
				StaticObject.Location.OffsetY -= 96.0f;
				AppendResourceBody(PortalFile, StaticObject, NamePrefixStatic, StaticObjectIndex, NextDepth, OutContent);
			}

			for (int32 StaticObjectIndexEx = 0; StaticObjectIndexEx < LandblockInfos.StaticObjectExCount; ++StaticObjectIndexEx)
			{
				FACEWorldBakeResourceInfo StaticObjectEx = LandblockInfos.StaticObjectsEx[StaticObjectIndexEx];
				StaticObjectEx.Location.OffsetX -= 96.0f;
				StaticObjectEx.Location.OffsetY -= 96.0f;
				StaticObjectEx.Location.OffsetZ += kStructureExEnvironmentZBump;
				AppendResourceBody(PortalFile, StaticObjectEx, NamePrefixStaticEx, StaticObjectIndexEx, NextDepth, OutContent);
			}

			int32 StructureObjectIndex = 0;
			for (uint32 Key : StructureMapKeys)
			{
				const TSharedPtr<TArray<FACEWorldBakeScaledResourceLocation>>& Locations = StructureMap[Key];
				if (1 < StructureMap[Key]->Num())
				{
					FACEWorldBakeT3dLandblockStaticObjectInstanced InstancedObject(Key, StructureObjectIndex, NextDepth + 2, TEXT("BoxComponent"), TEXT("DefaultSceneRoot"), false, false, NamePrefixStructure);

					for (int32 InstanceIndex = 0; InstanceIndex < Locations->Num(); ++InstanceIndex)
					{
						FACEWorldBakeScaledResourceLocation ObjectLocation = (*Locations)[InstanceIndex];
						ObjectLocation.OffsetX -= 96.0f;
						ObjectLocation.OffsetY -= 96.0f;
						ObjectLocation.OffsetZ += kStructureExEnvironmentZBump;
						InstancedObject.AddInstance(ObjectLocation);
					}

					OutContent.Append(FString(InstancedObject.GetContent()));
					InstancedObject.AppendPerInstanceData(OutContent);
					InstancedObject.AppendBody(OutContent);
					InstancedObject.AppendFooter(OutContent);
				}
				else
				{
					FACEWorldBakeResourceInfo StaticObjectEs(Key, (*Locations)[0]);
					StaticObjectEs.Location.OffsetX -= 96.0f;
					StaticObjectEs.Location.OffsetY -= 96.0f;
					StaticObjectEs.Location.OffsetZ += kStructureExEnvironmentZBump;
					AppendResourceBody(PortalFile, StaticObjectEs, NamePrefixStructure, StructureObjectIndex, NextDepth, OutContent);
				}

				StructureObjectIndex++;
			}

			TMap<uint32, TSet<FVector3f>> EnvironmentMap;

			for (int32 StructureIndex = 0; StructureIndex < Structures.Num(); ++StructureIndex)
			{
				const FACEWorldBakeCellStructure& Structure = Structures[StructureIndex];
				TSet<FVector3f>* Locations = EnvironmentMap.Find(Structure.EnvironmentId);
				const FVector3f Location(Structure.Location.OffsetX, -Structure.Location.OffsetY, Structure.Location.OffsetZ);
				if (0 != Structure.EnvironmentId && (!Locations || !Locations->Contains(Location)))
				{
					FACEWorldBakeResourceInfo StructureEnvironment;
					StructureEnvironment.Location = Structure.Location;
					StructureEnvironment.Location.OffsetX -= 96.0f;
					StructureEnvironment.Location.OffsetY -= 96.0f;
					StructureEnvironment.Location.OffsetZ += kStructureExEnvironmentZBump;
					StructureEnvironment.ResourceId = EACEWorldBakeResource_Native::kEnvironment | Structure.EnvironmentId;

					TArray<uint32> SurfaceIds;

					FACEWorldBakePortalEnvironment Environment;
					const bool EnvironmentOk = PortalFile.GetResource(StructureEnvironment.ResourceId, Environment);
					if (EnvironmentOk)
					{
						for (uint16 SurfaceIndex : Environment.GetPartSurfaceIndices(true))
						{
							check(SurfaceIndex < static_cast<uint32>(Structure.SurfaceIDs.Num()));
							SurfaceIds.Add(Structure.SurfaceIDs[SurfaceIndex]);
						}
					}

					AppendResourceBody(PortalFile, StructureEnvironment, NamePrefixEnvironment, StructureIndex, NextDepth, OutContent, SurfaceIds);

					if (Locations)
					{
						Locations->Add(Location);
					}
					else
					{
						TSet<FVector3f> NewLocations;
						NewLocations.Add(Location);
						EnvironmentMap.Add(Structure.EnvironmentId, NewLocations);
					}
				}
			}
		}

		FACEWorldBakeT3dSection SceneRootSection(TEXT("Object"), TEXT(" Name=\"DefaultSceneRoot\""), NextDepth + 2);
		OutContent.Append(FString(SceneRootSection.GetContent()));

		FACEWorldBakeT3dSection SceneRootBodySetupSection(TEXT("Object"), TEXT(" Class=/Script/Engine.BodySetup Name=\"BodySetup_0\""), NextDepth + 3);
		OutContent.Append(FString(SceneRootBodySetupSection.GetContent()));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("AggGeom=(BoxElems=((TM=(XPlane=(W=0.000000,X=0.000000,Y=0.000000,Z=-0.000000),YPlane=(W=0.000000,X=0.000000,Y=0.000000,Z=0.000000),ZPlane=(W=0.000000,X=0.000000,Y=0.000000,Z=-0.000000),WPlane=(W=0.000000,X=-76802848.000000,Y=0.000000,Z=0.000000)),X=19200.000000,Y=19200.000000,Z=25600.000000)))\r\n"), NextDepth + 4)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("CollisionTraceFlag=CTF_UseSimpleAsComplex\r\n"), NextDepth + 4)));
		SceneRootBodySetupSection.AppendFooter(OutContent);

		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("BoxExtent=(X=9600.000000,Y=9600.000000,Z=%1.6f)\r\n"), Bounds.GetExtent().Z), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("bGenerateOverlapEvents=False\r\n"), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("CanCharacterStepUpOn=ECB_No\r\n"), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("BodyInstance=(ObjectType=ECC_WorldStatic,CollisionProfileName=\"NoCollision\",CollisionResponses=(ResponseArray=((Channel=\"Visibility\",Response=ECR_Ignore),(Channel=\"Camera\",Response=ECR_Ignore))),CollisionEnabled=NoCollision,MaxAngularVelocity=3599.999756)\r\n"), NextDepth + 3)));

		const float LandblockX = static_cast<float>(CellX - 127) * 19200.0f;
		const float LandblockY = static_cast<float>(127 - CellY) * 19200.0f;
		const float LandblockZ = 0.0f;
		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("RelativeLocation=(X=%1.6f,Y=%1.6f,Z=%1.6f)\r\n"), LandblockX, LandblockY, LandblockZ), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("bVisualizeComponent=True\r\n"), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("CreationMethod=Instance\r\n"), NextDepth + 3)));
		SceneRootSection.AppendFooter(OutContent);

		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("RootComponent=SceneComponent'\"DefaultSceneRoot\"'\r\n"), NextDepth + 2)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("ActorLabel=\"Landblock_%03d_%03d\"\r\n"), CellX, CellY), NextDepth + 2)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("InstanceComponents(0)=SceneComponent'\"DefaultSceneRoot\"'\r\n"), NextDepth + 2)));

		int32 ResourceIndex = 1;
		// resource footers
		{
			for (int32 StaticObjectIndex = 0; StaticObjectIndex < LandblockInfos.StaticObjectCount; ++StaticObjectIndex)
			{
				const FACEWorldBakeResourceInfo& StaticObject = LandblockInfos.StaticObjects[StaticObjectIndex];
				ResourceIndex = AppendResourceFooter(PortalFile, StaticObject, NamePrefixStatic, StaticObjectIndex, ResourceIndex, NextDepth, OutContent);
			}

			for (int32 StaticObjectIndexEx = 0; StaticObjectIndexEx < LandblockInfos.StaticObjectExCount; ++StaticObjectIndexEx)
			{
				const FACEWorldBakeResourceInfo& StaticObjectEx = LandblockInfos.StaticObjectsEx[StaticObjectIndexEx];
				ResourceIndex = AppendResourceFooter(PortalFile, StaticObjectEx, NamePrefixStaticEx, StaticObjectIndexEx, ResourceIndex, NextDepth, OutContent);
			}

			int32 StructureObjectIndex = 0;
			for (uint32 Key : StructureMapKeys)
			{
				if (1 < StructureMap[Key]->Num())
				{
					OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("InstanceComponents(%d)=InstancedStaticMeshComponent'\"%s%d_0x%08X_Instanced\"'\r\n"), ResourceIndex++, *NamePrefixStructure, StructureObjectIndex, Key), NextDepth + 2)));
				}
				else
				{
					FACEWorldBakeScaledResourceInfo StaticObject(Key, (*StructureMap[Key])[0]);
					ResourceIndex = AppendResourceFooter(PortalFile, StaticObject, NamePrefixStructure, StructureObjectIndex, ResourceIndex, NextDepth, OutContent);
				}

				StructureObjectIndex++;
			}

			TMap<uint32, TSet<FVector3f>> EnvironmentMap;

			for (int32 StructureIndex = 0; StructureIndex < Structures.Num(); ++StructureIndex)
			{
				const FACEWorldBakeCellStructure& Structure = Structures[StructureIndex];
				TSet<FVector3f>* Locations = EnvironmentMap.Find(Structure.EnvironmentId);
				const FVector3f Location(Structure.Location.OffsetX, -Structure.Location.OffsetY, Structure.Location.OffsetZ);
				if (0 != Structure.EnvironmentId && (!Locations || !Locations->Contains(Location)))
				{
					FACEWorldBakeResourceInfo StructureEnvironment;
					StructureEnvironment.Location = Structure.Location;
					StructureEnvironment.ResourceId = EACEWorldBakeResource_Native::kEnvironment | Structure.EnvironmentId;
					ResourceIndex = AppendResourceFooter(PortalFile, StructureEnvironment, NamePrefixEnvironment, StructureIndex, ResourceIndex, NextDepth, OutContent);

					if (Locations)
					{
						Locations->Add(Location);
					}
					else
					{
						TSet<FVector3f> NewLocations;
						NewLocations.Add(Location);
						EnvironmentMap.Add(Structure.EnvironmentId, NewLocations);
					}
				}
			}
		}

		ActorSection.AppendFooter(OutContent);
		LevelSection.AppendFooter(OutContent);

		OutContent.Append(FString(FACEWorldBakeT3dSection(TEXT("Surface"), TEXT(""), TabDepth)));
	}

	FACEWorldBakeCellLandblockObject LandblockInfos;
	int32 CellX;
	int32 CellY;
};

struct FACEWorldBakeT3dMapScene : public FACEWorldBakeT3dSection
{
public:
	FACEWorldBakeT3dMapScene(const int32 InStartX, const int32 InStartY, const int32 InCountX, const int32 InCountY)
	: FACEWorldBakeT3dSection(TEXT("Map"), TEXT(""), 0)
	, StartX(InStartX)
	, StartY(InStartY)
	, CountX(InCountX)
	, CountY(InCountY)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		const FString NamePrefixScene = TEXT("sc");

		IACEWorldBakeDatToolsModule& ACEWorldBakeDatToolsModule = FModuleManager::LoadModuleChecked<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools");
		FACEWorldBakePortalFile PortalFile(ACEWorldBakeDatToolsModule.GetDatFile(EACEWorldBakeDatFile::Portal));
		FACEWorldBakeCellFile CellFile(ACEWorldBakeDatToolsModule.GetDatFile(EACEWorldBakeDatFile::Cell));

		FACEWorldBakePortalRegion PortalRegion;
		const bool ValidRegion = PortalFile.GetResource(EACEWorldBakeResource_Native::kRegion, PortalRegion);
		check(ValidRegion);

		TScaledResourceLocationsMap SceneMap;

		FBox3f MapBounds;
		float LargestZ = 0.0f;

		const float MidIndexX = StartX + static_cast<float>(CountX) / 2.0f;
		const float MidIndexY = StartY + static_cast<float>(CountY) / 2.0f;

		for (int32 CellY = StartY; CellY < (StartY + CountY); ++CellY)
		{
			for (int32 CellX = StartX; CellX < (StartX + CountX); ++CellX)
			{
				TScaledResourceInfoArray SceneResources = GetLandblockSceneResources(PortalRegion, CellX, CellY);
				// move from center of cell, to relative to center of grid
				const float OffsetX = (CellX - MidIndexX) * 192.0f;
				const float OffsetY = (CellY - MidIndexY) * 192.0f;
				for (FACEWorldBakeScaledResourceInfo& ResourceInfo : SceneResources)
				{
					ResourceInfo.Location.OffsetX += OffsetX;
					ResourceInfo.Location.OffsetY += OffsetY;

					if (LargestZ < FMath::Abs(ResourceInfo.Location.OffsetZ))
					{
						LargestZ = FMath::Abs(ResourceInfo.Location.OffsetZ);
					}
				}

				MapResources(SceneResources, SceneMap);

				const FBox3f CellBounds = GetLandblockBounds(PortalRegion, CellX, CellY);
				if (MapBounds.IsValid)
				{
					MapBounds += CellBounds;
				}
				else
				{
					MapBounds = CellBounds;
				}
			}
		}

		FACEWorldBakeT3dSection LevelSection(TEXT("Level"), TEXT(""), NextDepth);
		OutContent.Append(FString(LevelSection.GetContent()));

		FACEWorldBakeT3dSection ActorSection(TEXT("Actor"), FString::Printf(TEXT(" Class=/Script/Engine.Actor Name=Landblock_%03d_%03d_%03d_%03d Archetype=/Script/Engine.Actor'/Script/Engine.Default__Actor'"), StartX, StartY, StartX + CountX, StartY + CountY), NextDepth + 1);
		OutContent.Append(FString(ActorSection.GetContent()));

		TArray<uint32> SceneMapKeys;
		const int32 NumSceneKeys = SceneMap.GetKeys(SceneMapKeys);

		// resource headers
		FACEWorldBakeT3dSection BoundsBoxRootSection(TEXT("Object"), TEXT(" Class=/Script/Engine.BoxComponent Name=\"DefaultSceneRoot\""), NextDepth + 2);
		OutContent.Append(FString(BoundsBoxRootSection.GetContent()));
		OutContent.Append(FString(FACEWorldBakeT3dSection(TEXT("Object"), TEXT(" Class=/Script/Engine.BodySetup Name=\"BodySetup_0\""), NextDepth + 3)));
		BoundsBoxRootSection.AppendFooter(OutContent);

		{
			int32 SceneObjectIndex = 0;
			for (uint32 Key : SceneMapKeys)
			{
				if (1 < SceneMap[Key]->Num())
				{
					FACEWorldBakeT3dSection InstancedMeshSection(TEXT("Object"), FString::Printf(TEXT(" Class=/Script/Engine.InstancedStaticMeshComponent Name=\"%s%d_0x%08X_Instanced\""), *NamePrefixScene, SceneObjectIndex, Key), NextDepth + 2);
					OutContent.Append(FString(InstancedMeshSection.GetContent()));
					InstancedMeshSection.AppendBody(OutContent);
					InstancedMeshSection.AppendFooter(OutContent);
				}
				else
				{
					FACEWorldBakeScaledResourceInfo StaticObject(Key, (*SceneMap[Key])[0]);
					AppendResourceHeader(PortalFile, StaticObject, NamePrefixScene, SceneObjectIndex, NextDepth, OutContent);
				}

				SceneObjectIndex++;
			}
		}

		// resource bodies
		{
			int32 SceneObjectIndex = 0;
			for (uint32 Key : SceneMapKeys)
			{
				const TSharedPtr<TArray<FACEWorldBakeScaledResourceLocation>>& Locations = SceneMap[Key];
				if (1 < SceneMap[Key]->Num())
				{
					FACEWorldBakeT3dLandblockStaticObjectInstanced InstancedObject(Key, SceneObjectIndex, NextDepth + 2, TEXT("BoxComponent"), TEXT("DefaultSceneRoot"), false, false, NamePrefixScene);

					for (int32 InstanceIndex = 0; InstanceIndex < Locations->Num(); ++InstanceIndex)
					{
						FACEWorldBakeScaledResourceLocation ObjectLocation = (*Locations)[InstanceIndex];
						ObjectLocation.OffsetX -= 96.0f;
						ObjectLocation.OffsetY -= 96.0f;
						InstancedObject.AddInstance(ObjectLocation);
					}

					OutContent.Append(FString(InstancedObject.GetContent()));
					InstancedObject.AppendPerInstanceData(OutContent);
					InstancedObject.AppendBody(OutContent);
					InstancedObject.AppendFooter(OutContent);
				}
				else
				{
					FACEWorldBakeScaledResourceLocation ObjectLocation = (*Locations)[0];
					ObjectLocation.OffsetX -= 96.0f;
					ObjectLocation.OffsetY -= 96.0f;
					FACEWorldBakeScaledResourceInfo StaticObject(Key, ObjectLocation);
					AppendResourceBody(PortalFile, StaticObject, NamePrefixScene, SceneObjectIndex, NextDepth, OutContent);;
				}

				SceneObjectIndex++;
			}
		}

		const float WidthX = static_cast<float>(CountX) * 19200.0f;
		const float WidthY = static_cast<float>(CountY) * 19200.0f;

		FACEWorldBakeT3dSection SceneRootSection(TEXT("Object"), TEXT(" Name=\"DefaultSceneRoot\""), NextDepth + 2);
		OutContent.Append(FString(SceneRootSection.GetContent()));

		FACEWorldBakeT3dSection SceneRootBodySetupSection(TEXT("Object"), TEXT(" Class=/Script/Engine.BodySetup Name=\"BodySetup_0\""), NextDepth + 3);
		OutContent.Append(FString(SceneRootBodySetupSection.GetContent()));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("AggGeom=(BoxElems=((TM=(XPlane=(W=0.000000,X=0.000000,Y=0.000000,Z=-0.000000),YPlane=(W=0.000000,X=0.000000,Y=0.000000,Z=0.000000),ZPlane=(W=0.000000,X=0.000000,Y=0.000000,Z=-0.000000),WPlane=(W=0.000000,X=-76802848.000000,Y=0.000000,Z=0.000000)),X=19200.000000,Y=19200.000000,Z=25600.000000)))\r\n"), NextDepth + 4)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("CollisionTraceFlag=CTF_UseSimpleAsComplex\r\n"), NextDepth + 4)));
		SceneRootBodySetupSection.AppendFooter(OutContent);

		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("BoxExtent=(X=%1.6f,Y=%1.6f,Z=%1.6f)\r\n"), WidthX * 0.5f, WidthY * 0.5f, LargestZ * 100.0f), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("bGenerateOverlapEvents=False\r\n"), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("CanCharacterStepUpOn=ECB_No\r\n"), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("BodyInstance=(ObjectType=ECC_WorldStatic,CollisionProfileName=\"NoCollision\",CollisionResponses=(ResponseArray=((Channel=\"Visibility\",Response=ECR_Ignore),(Channel=\"Camera\",Response=ECR_Ignore))),CollisionEnabled=NoCollision,MaxAngularVelocity=3599.999756)\r\n"), NextDepth + 3)));

		const float LandblockX = static_cast<float>(StartX - 127) * 19200.0f + WidthX * 0.5f;
		const float LandblockY = static_cast<float>(127 - StartY) * 19200.0f - WidthY * 0.5f;
		const float LandblockZ = 0.0f;
		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("RelativeLocation=(X=%1.6f,Y=%1.6f,Z=%1.6f)\r\n"), LandblockX, LandblockY, LandblockZ), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("bVisualizeComponent=True\r\n"), NextDepth + 3)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("CreationMethod=Instance\r\n"), NextDepth + 3)));
		SceneRootSection.AppendFooter(OutContent);

		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("RootComponent=SceneComponent'\"DefaultSceneRoot\"'\r\n"), NextDepth + 2)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("ActorLabel=\"Landblock_%03d_%03d_%03d_%03d\"\r\n"), StartX, StartY, StartX + CountX, StartY + CountY), NextDepth + 2)));
		OutContent.Append(FString(FACEWorldBakeT3dContent(TEXT("InstanceComponents(0)=SceneComponent'\"DefaultSceneRoot\"'\r\n"), NextDepth + 2)));

		int32 ResourceIndex = 1;
		// resource footers
		{
			int32 SceneObjectIndex = 0;
			for (uint32 Key : SceneMapKeys)
			{
				if (1 < SceneMap[Key]->Num())
				{
					OutContent.Append(FString(FACEWorldBakeT3dContent(FString::Printf(TEXT("InstanceComponents(%d)=InstancedStaticMeshComponent'\"%s%d_0x%08X_Instanced\"'\r\n"), ResourceIndex++, *NamePrefixScene, SceneObjectIndex, Key), NextDepth + 2)));
				}
				else
				{
					FACEWorldBakeResourceInfo StaticObject(Key, (*SceneMap[Key])[0]);
					ResourceIndex = AppendResourceFooter(PortalFile, StaticObject, NamePrefixScene, SceneObjectIndex, ResourceIndex, NextDepth, OutContent);
				}

				SceneObjectIndex++;
			}
		}

		ActorSection.AppendFooter(OutContent);
		LevelSection.AppendFooter(OutContent);

		OutContent.Append(FString(FACEWorldBakeT3dSection(TEXT("Surface"), TEXT(""), TabDepth)));
	}

	int32 StartX;
	int32 StartY;
	int32 CountX;
	int32 CountY;
};

FString FACEWorldBakeT3dWriter::ExportText(const int32 CellX, const int32 CellY, const FACEWorldBakeCellLandblockObject& LandblockInfos)
{
	FString OutContent;
	OutContent.Append(FString(FACEWorldBakeT3dMap(CellX, CellY, LandblockInfos)));

	return OutContent;
}

FString FACEWorldBakeT3dWriter::ExportLandblockScenesText(const int32 StartX, const int32 StartY, const int32 CountX, const int32 CountY)
{
	FString OutContent;
	OutContent.Append(FString(FACEWorldBakeT3dMapScene(StartX, StartY, CountX, CountY)));

	return OutContent;
}

FString FACEWorldBakeT3dWriter::ExportText(const FACEWorldBakePortalScene& Scene)
{
	FString OutContent;
	OutContent.Append(FString(FACEWorldBakeT3dScene(Scene)));

	return OutContent;
}

FString FACEWorldBakeT3dWriter::ExportText(const FACEWorldBakePortalSetup& Setup)
{
	FString OutContent;
	OutContent.Append(FString(FACEWorldBakeT3dSetup(Setup)));

	return OutContent;
}
