#include "ACEWorldBakeFbxWriter.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeDatEnvironment.h"
#include "ACEWorldBakeDatImageTexture.h"
#include "ACEWorldBakeDatModel.h"
#include "ACEWorldBakeDatSurface.h"
#include "ACEWorldBakeFbxModel.h"
#include "ACEWorldBakePortalFile.h"
#include "Misc/DateTime.h"
#include "Modules/ModuleManager.h"

const bool bExportEnvironmentCollision = false; // disabled because its just a list of convex planes (2d) but ucx must be convex hulls (3d)

FString FbxTimeString(const FDateTime& DateTime)
{
	return FString::Printf(TEXT("\"%d-%02d-%02d %02d:%02d:%02d:%03d\""), DateTime.GetYear(), DateTime.GetMonth(), DateTime.GetDay(), DateTime.GetHour(), DateTime.GetMinute(), DateTime.GetSecond(), DateTime.GetMillisecond());
}

uint32 ImageColorIdFromSurface(uint32 SurfaceIndentifier)
{
	IACEWorldBakeDatToolsModule& ACEWorldBakeDatToolsModule = FModuleManager::LoadModuleChecked<IACEWorldBakeDatToolsModule>("ACEWorldBakeDatTools");
	FACEWorldBakePortalFile PortalFile(ACEWorldBakeDatToolsModule.GetDatFile(EACEWorldBakeDatFile::Portal));

	FACEWorldBakePortalSurface Surface;
	const bool SurfaceOk = PortalFile.GetResource(SurfaceIndentifier, Surface);
	if (SurfaceOk)
	{
		FACEWorldBakePortalImageTexture PortalTexture;
		const bool PortalTextureOk = PortalFile.GetResource(Surface.TextureID, PortalTexture);
		if (PortalTextureOk && 0 < PortalTexture.ImageColorIDs.Num())
		{
			return PortalTexture.ImageColorIDs[0];
		}
	}

	return SurfaceIndentifier;
}

FACEWorldBakeFbxContent::FACEWorldBakeFbxContent(int32 InDepth)
: FACEWorldBakeFbxContent(TEXT("Invalid: Invalid\r\n"), InDepth)
{
}

FACEWorldBakeFbxContent::FACEWorldBakeFbxContent(const FString& InContent, int32 InDepth)
: Content(InContent)
, Depth(InDepth)
, PrevDepth(FMath::Max(0, InDepth - 1))
, NextDepth(InDepth + 1)
{
	if (0 < Depth)
	{
		for (int32 i = 0; i < InDepth; ++i)
		{
			Indent += TEXT("\t");
		}

		Content.InsertAt(0, Indent);
	}
}

FACEWorldBakeFbxValue::FACEWorldBakeFbxValue(const FString& InKey, const FString& InValue, int32 InDepth)
: FACEWorldBakeFbxContent(FString::Printf(TEXT("%s: %s\r\n"), *InKey, *InValue), InDepth)
{
}

FACEWorldBakeFbxValue::FACEWorldBakeFbxValue(const FString& InKey, const int32& InValue, int32 InDepth)
: FACEWorldBakeFbxValue(InKey, FString::Printf(TEXT("%d"), InValue), InDepth)
{
}

FACEWorldBakeFbxValue::FACEWorldBakeFbxValue(const FString& InKey, const float& InValue, int32 InDepth)
: FACEWorldBakeFbxValue(InKey, FString::Printf(TEXT("%4.2f"), InValue), InDepth)
{
}

FACEWorldBakeFbxValue::FACEWorldBakeFbxValue(const FString& InKey, const TArray<FString>& InValue, int32 InDepth)
: FACEWorldBakeFbxValue(InKey, TEXT(""), InDepth)
{
	ArrayToString(InValue, Content);
}

FACEWorldBakeFbxValue::FACEWorldBakeFbxValue(const FString& InKey, const TArray<int32>& InValue, int32 InDepth)
: FACEWorldBakeFbxValue(InKey, TEXT(""), InDepth)
{
	ArrayToString(InValue, Content);
}

FACEWorldBakeFbxValue::FACEWorldBakeFbxValue(const FString& InKey, const TArray<float>& InValue, int32 InDepth)
: FACEWorldBakeFbxValue(InKey, TEXT(""), InDepth)
{
	ArrayToString(InValue, Content);
}

FACEWorldBakeFbxSection::FACEWorldBakeFbxSection(const FString& InSectionTag, int32 InDepth)
: FACEWorldBakeFbxContent(FString::Printf(TEXT("%s: {\r\n"), *InSectionTag), InDepth)
{
}

FACEWorldBakeFbxSection::FACEWorldBakeFbxSection(const FString& InSectionTag, const FString& InSectionName, int32 InDepth)
: FACEWorldBakeFbxContent(FString::Printf(TEXT("%s: \"%s\" {\r\n"), *InSectionTag, *InSectionName), InDepth)
{
}

FACEWorldBakeFbxSection::FACEWorldBakeFbxSection(const FString& InSectionTag, const int32& InSectionId, int32 InDepth)
: FACEWorldBakeFbxContent(FString::Printf(TEXT("%s: %d {\r\n"), *InSectionTag, InSectionId), InDepth)
{
}

FACEWorldBakeFbxSection::operator FString() const
{
	FString OutContent = Content;
	AppendBody(OutContent);
	AppendFooter(OutContent);
	return OutContent;
}

void FACEWorldBakeFbxSection::AppendFooter(FString& OutContent) const
{
	OutContent.Append(FString(FString::Printf(TEXT("%s}\r\n"), *Indent)));
}

struct FACEWorldBakeFbxCreationTimeStamp : public FACEWorldBakeFbxSection
{
	FACEWorldBakeFbxCreationTimeStamp(const FDateTime& InUtcTime)
	: FACEWorldBakeFbxSection(TEXT("CreationTimeStamp"), 1)
	, UtcTime(InUtcTime)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Version"), 1000, NextDepth)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Year"), UtcTime.GetYear(), NextDepth)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Month"), UtcTime.GetMonth(), NextDepth)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Day"), UtcTime.GetDay(), NextDepth)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Hour"), UtcTime.GetHour(), NextDepth)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Minute"), UtcTime.GetMinute(), NextDepth)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Second"), UtcTime.GetSecond(), NextDepth)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Millisecond"), UtcTime.GetMillisecond(), NextDepth)));
	}

	FDateTime UtcTime;
};

struct FACEWorldBakeFbxSectionSingleValue : public FACEWorldBakeFbxSection
{
	FACEWorldBakeFbxSectionSingleValue(const FString& InSectionTag, const FString& InKey, const FString& InValue)
	: FACEWorldBakeFbxSection(InSectionTag, 1)
	, SimpleKey(InKey)
	, SimpleValue(InValue)
	{
	}

	FACEWorldBakeFbxSectionSingleValue(const FString& InSectionTag, const FString& InSectionName, const FString& InKey, const FString& InValue)
	: FACEWorldBakeFbxSection(InSectionTag, InSectionName, 1)
	, SimpleKey(InKey)
	, SimpleValue(InValue)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		OutContent.Append(FString(FACEWorldBakeFbxValue(SimpleKey, SimpleValue, NextDepth)));
	}

	FString SimpleKey;
	FString SimpleValue;
};

struct FACEWorldBakeFbxHeaderExtension : public FACEWorldBakeFbxSection
{
	FACEWorldBakeFbxHeaderExtension(const FDateTime& InUtcTime)
	: FACEWorldBakeFbxSection(TEXT("FBXHeaderExtension"), 0)
	, UtcTime(InUtcTime)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("FBXHeaderVersion"), 1003, NextDepth)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("FBXVersion"), 6100, NextDepth)));
		OutContent.Append(FString(FACEWorldBakeFbxCreationTimeStamp(UtcTime)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Creator"), TEXT("\"ACEWorldBake/DatTools\""), NextDepth)));
		OutContent.Append(FString(FACEWorldBakeFbxSectionSingleValue(TEXT("OtherFlags"), TEXT("FlagPLE"), TEXT("0"))));
	}

	FDateTime UtcTime;
};

struct FACEWorldBakeFbxHeaderDefinitions : public FACEWorldBakeFbxSection
{
	FACEWorldBakeFbxHeaderDefinitions(const int32 InModelCount, const int32 InCollisionCount, const int32 InMaterialCount)
	: FACEWorldBakeFbxSection(TEXT("Definitions"), 0)
	, ModelCount(InModelCount)
	, CollisionCount(InCollisionCount)
	, MaterialCount(InMaterialCount)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Version"), 100, NextDepth)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Count"), ModelCount + CollisionCount + MaterialCount, NextDepth)));
		OutContent.Append(FString(FACEWorldBakeFbxSectionSingleValue(TEXT("ObjectType"), TEXT("Model"), TEXT("Count"), FString::Printf(TEXT("%d"), ModelCount + CollisionCount))));
		OutContent.Append(FString(FACEWorldBakeFbxSectionSingleValue(TEXT("ObjectType"), TEXT("Geometry"), TEXT("Count"), FString::Printf(TEXT("%d"), 1))));
		OutContent.Append(FString(FACEWorldBakeFbxSectionSingleValue(TEXT("ObjectType"), TEXT("Material"), TEXT("Count"), FString::Printf(TEXT("%d"), MaterialCount))));
		OutContent.Append(FString(FACEWorldBakeFbxSectionSingleValue(TEXT("ObjectType"), TEXT("GlobalSettings"), TEXT("Count"), FString::Printf(TEXT("%d"), 1))));
	}

	int32 ModelCount;
	int32 CollisionCount;
	int32 MaterialCount;
	FDateTime UtcTime;
};

struct FACEWorldBakeFbxMaterial : public FACEWorldBakeFbxSection
{
	FACEWorldBakeFbxMaterial(const uint32 InSurfaceID, const int32 InSurfaceIndex)
	: FACEWorldBakeFbxSection(TEXT("Material"), FString::Printf(TEXT("Material::0x%08X"), InSurfaceID), 1)
	, SurfaceID(InSurfaceID)
	, SurfaceIndex(InSurfaceIndex)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Version"), 102, NextDepth)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("ShadingModel"), TEXT("phong"), NextDepth)));
		OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("MultiLayer"), TEXT("0"), NextDepth)));
	}

	int32 SurfaceID;
	int32 SurfaceIndex;
};

struct FACEWorldBakeFbxParts : public FACEWorldBakeFbxSection
{
	FACEWorldBakeFbxParts(const FACEWorldBakePortalEnvironment& InEnvironment, EACEWorldBakePortalEnvironmentPartSurface::Type InSurfaceType, const TArray<uint32>& InSurfaceIDs)
	: FACEWorldBakeFbxSection(TEXT("Objects"), 0)
	, Environment(InEnvironment)
	, SurfaceType(InSurfaceType)
	, SurfaceIDs(InSurfaceIDs)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		const bool bForPortals = EACEWorldBakePortalEnvironmentPartSurface::kPortals == SurfaceType;

		TArray<uint32> CollidablePartIndices;
		for (uint32 PartIndex = 0; PartIndex < Environment.NumParts; ++PartIndex)
		{
			const FACEWorldBakePortalEnvironmentPart& Part = Environment.Parts[PartIndex];
			for (uint16 SurfaceIndex : Part.GetSurfaceIndices(SurfaceType))
			{
				const FString SurfaceName = FString::Printf(TEXT("Part%d_Surface%d"), PartIndex, SurfaceIndex);
				FACEWorldBakeFbxModel Model(Part.TriangleFans, Part.Vertices, SurfaceIndex, true, bForPortals, bForPortals, SurfaceName);
				OutContent.Append(FString(Model));
			}

			if (Part.HitTriangleFans.Num() > 0)
			{
				CollidablePartIndices.Add(PartIndex);
			}
		}

		for (uint32 PartIndex = 0; PartIndex < Environment.NumParts; ++PartIndex)
		{
			const FACEWorldBakePortalEnvironmentPart& Part = Environment.Parts[PartIndex];
			for (uint16 SurfaceIndex : Part.GetSurfaceIndices(SurfaceType))
			{
				FACEWorldBakeFbxMaterial SurfaceMaterial(SurfaceIDs[SurfaceIndex], SurfaceIndex);
				OutContent.Append(FString(SurfaceMaterial));
			}
		}

		if (bExportEnvironmentCollision && (CollidablePartIndices.Num() > 0))
		{
			FACEWorldBakeFbxModel UCXModel({}, {}, 0, false, true, false, TEXT("UCX_Part0_Surface0"));

			for (uint32 PartIndex : CollidablePartIndices)
			{
				const int32 FirstVertexIndex = UCXModel.Vertices.Num();

				const FACEWorldBakePortalEnvironmentPart& Part = Environment.Parts[PartIndex];
				UCXModel.Vertices.Append(Part.Vertices);
				for (FACEWorldBakePortalModelVertex& PartVertex : UCXModel.Vertices)
				{
					PartVertex.Index += FirstVertexIndex;
				}

				for (const FACEWorldBakePortalModelTriangleFan& PartFan : Part.HitTriangleFans)
				{
					const int32 FanIndex = UCXModel.Fans.Add(PartFan);
					UCXModel.Fans[FanIndex].FanIndex = FanIndex;
					for (uint16& OldVertexIndex : UCXModel.Fans[FanIndex].VertexIndices)
					{
						OldVertexIndex += FirstVertexIndex;
					}
				}
			}

			OutContent.Append(FString(UCXModel));
		}
	}

	FACEWorldBakePortalEnvironment Environment;
	EACEWorldBakePortalEnvironmentPartSurface::Type SurfaceType;
	TArray<uint32> SurfaceIDs;
};

struct FACEWorldBakeFbxObjects : public FACEWorldBakeFbxSection
{
	FACEWorldBakeFbxObjects(const FACEWorldBakePortalModel& InModel)
	: FACEWorldBakeFbxSection(TEXT("Objects"), 0)
	, PortalModel(InModel)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		for (int32 SurfaceIndex = 0; SurfaceIndex < PortalModel.SurfaceCount; ++SurfaceIndex)
		{
			const FString SurfaceName = FString::Printf(TEXT("Surface%d"), SurfaceIndex);
			FACEWorldBakeFbxModel Model(PortalModel.RenderFans, PortalModel.Vertices, SurfaceIndex, true, false, false, SurfaceName);
			OutContent.Append(FString(Model));
		}

		if ((PortalModel.NumCollisionFans > 0) && (PortalModel.PortalNodes.Num() == 0))
		{
			const FString SurfaceCollisionName = TEXT("UCX_Surface0");
			FACEWorldBakeFbxModel ModelCollision(PortalModel.CollisionFans, PortalModel.Vertices, 0, false, true, false, SurfaceCollisionName);
			OutContent.Append(FString(ModelCollision));
		}

		for (int32 SurfaceIndex = 0; SurfaceIndex < PortalModel.SurfaceCount; ++SurfaceIndex)
		{
			FACEWorldBakeFbxMaterial SurfaceMaterial(PortalModel.SurfaceIDs[SurfaceIndex], SurfaceIndex);
			OutContent.Append(FString(SurfaceMaterial));
		}
	}

	FACEWorldBakePortalModel PortalModel;
};

struct FACEWorldBakeFbxRelations : public FACEWorldBakeFbxSection
{
	FACEWorldBakeFbxRelations(const int32 InSurfaceCount, const int32 InCollisionCount, const bool InMaterial)
	: FACEWorldBakeFbxSection(TEXT("Relations"), 0)
	, SurfaceCount(InSurfaceCount)
	, CollisionCount(InCollisionCount)
	, Material(InMaterial)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		for (int32 SurfaceIndex = 0; SurfaceIndex < SurfaceCount; ++SurfaceIndex)
		{
			OutContent.Append(FString(FACEWorldBakeFbxSection(TEXT("Model"), FString::Printf(TEXT("Model::Surface%d\", \"Mesh"), SurfaceIndex), NextDepth)));
		}

		for (int32 CollisionIndex = 0; CollisionIndex < CollisionCount; ++CollisionIndex)
		{
			OutContent.Append(FString(FACEWorldBakeFbxSection(TEXT("Model"), FString::Printf(TEXT("Model::UCX_Surface%d\", \"Mesh"), CollisionIndex), NextDepth)));
		}

		if (Material)
		{
			OutContent.Append(FString(FACEWorldBakeFbxSection(TEXT("Material"), TEXT("Material::cube_pruned_mat\", \""), NextDepth)));
		}
	}

	int32 SurfaceCount;
	int32 CollisionCount;
	bool Material;
};

struct FACEWorldBakeFbxConnections : public FACEWorldBakeFbxSection
{
	FACEWorldBakeFbxConnections(const TArray<uint32>& InSurfaceIDs, const int32 InCollisionCount)
	: FACEWorldBakeFbxSection(TEXT("Connections"), 0)
	, SurfaceIDs(InSurfaceIDs)
	, CollisionCount(InCollisionCount)
	{
	}

	virtual void AppendBody(FString& OutContent) const override
	{
		for (int32 SurfaceIndex = 0; SurfaceIndex < SurfaceIDs.Num(); ++SurfaceIndex)
		{
			OutContent.Append(FString(FACEWorldBakeFbxSection(TEXT("Connect"), FString::Printf(TEXT("OO\", \"Model::Surface%d\", \"Model::Scene"), SurfaceIndex), NextDepth)));
			OutContent.Append(FString(FACEWorldBakeFbxSection(TEXT("Connect"), FString::Printf(TEXT("OO\", \"Material::0x%08X\", \"Model::Surface%d"), SurfaceIDs[SurfaceIndex], SurfaceIndex), NextDepth)));
		}

		for (int32 CollisionIndex = 0; CollisionIndex < CollisionCount; ++CollisionIndex)
		{
			OutContent.Append(FString(FACEWorldBakeFbxSection(TEXT("Connect"), FString::Printf(TEXT("OO\", \"Model::UCX_Surface%d\", \"Model::Scene"), CollisionIndex), NextDepth)));
		}
	}

	TArray<uint32> SurfaceIDs;
	int32 CollisionCount;
};

static FString ExportText(const FACEWorldBakePortalEnvironment& Environment, EACEWorldBakePortalEnvironmentPartSurface::Type SurfaceType)
{
	const FDateTime UtcNow = FDateTime::UtcNow();
	const FString UtcNowStr = FbxTimeString(UtcNow);
	const bool bForPortals = EACEWorldBakePortalEnvironmentPartSurface::kPortals == SurfaceType;

	uint32 SurfaceCount = FMath::Max(Environment.GetPartSurfaceIndices(false).Num(), 1);
	TArray<uint32> TempSurfaceIds;
	for (uint32 TempSurfaceId = 0; TempSurfaceId < SurfaceCount; ++TempSurfaceId)
	{
		TempSurfaceIds.Add(bForPortals ? 0x08FFFFFF : TempSurfaceId);
	}

	int32 ModelCount = 0;
	int32 CollisionCount = 0;
	int32 PortalCount = 0;
	for (const FACEWorldBakePortalEnvironmentPart& Part : Environment.Parts)
	{
		if (Part.TriangleFans.Num() > 0)
		{
			ModelCount++;
		}

		if (Part.HitTriangleFans.Num() > 0)
		{
			CollisionCount++;
		}

		if (Part.Portals.Num() > 0)
		{
			PortalCount++;
		}
	}

	FString OutContent;
	OutContent.Append(FString(FACEWorldBakeFbxContent(TEXT("; FBX 6.1.0 project file\r\n"), 0)));
	OutContent.Append(FString(FACEWorldBakeFbxHeaderExtension(UtcNow)));
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("CreationTime"), UtcNowStr, 0)));
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Creator"), TEXT("\"ACEWorldBake/DatTools\""), 0)));
	OutContent.Append(FString(FACEWorldBakeFbxHeaderDefinitions(bForPortals ? PortalCount : ModelCount, CollisionCount, SurfaceCount)));
	OutContent.Append(FString(FACEWorldBakeFbxParts(Environment, SurfaceType, TempSurfaceIds)));

	FACEWorldBakeFbxSection Connections(TEXT("Connections"), 0);
	OutContent.Append(FString(Connections.GetContent()));

	TArray<uint32> CollidablePartIndices;
	for (uint32 PartIndex = 0; PartIndex < Environment.NumParts; ++PartIndex)
	{
		const FACEWorldBakePortalEnvironmentPart& Part = Environment.Parts[PartIndex];

		for (uint16 SurfaceIndex : Part.GetSurfaceIndices(SurfaceType))
		{
			const FString SurfaceName = FString::Printf(TEXT("Part%d_Surface%d"), PartIndex, SurfaceIndex);
			OutContent.Append(FString(FACEWorldBakeFbxSection(TEXT("Connect"), FString::Printf(TEXT("OO\", \"Model::%s\", \"Model::Scene"), *SurfaceName), 1)));
			OutContent.Append(FString(FACEWorldBakeFbxSection(TEXT("Connect"), FString::Printf(TEXT("OO\", \"Material::0x%08X\", \"Model::%s"), TempSurfaceIds[SurfaceIndex], *SurfaceName), 1)));
		}

		if (Part.HitTriangleFans.Num() > 0)
		{
			CollidablePartIndices.Add(PartIndex);
		}
	}

	if (bExportEnvironmentCollision && (CollidablePartIndices.Num() > 0))
	{
		OutContent.Append(FString(FACEWorldBakeFbxSection(TEXT("Connect"), TEXT("OO\", \"Model::UCX_Part0_Surface0\", \"Model::Scene"), 1)));
	}

	Connections.AppendFooter(OutContent);

	return OutContent;
}

FString FACEWorldBakeFbxWriter::ExportText(const FACEWorldBakePortalEnvironment& Environment)
{
	return ::ExportText(Environment, EACEWorldBakePortalEnvironmentPartSurface::kTriangles);
}

FString FACEWorldBakeFbxWriter::ExportPortalsText(const FACEWorldBakePortalEnvironment& Environment)
{
	return ::ExportText(Environment, EACEWorldBakePortalEnvironmentPartSurface::kPortals);
}

FString FACEWorldBakeFbxWriter::ExportText(const FACEWorldBakePortalModel& Model)
{
	const FDateTime UtcNow = FDateTime::UtcNow();
	const FString UtcNowStr = FbxTimeString(UtcNow);
	const int32 CollisionCount = ((Model.NumCollisionFans > 0) && (Model.PortalNodes.Num() == 0)) ? 1 : 0;

	FString OutContent;
	OutContent.Append(FString(FACEWorldBakeFbxContent(TEXT("; FBX 6.1.0 project file\r\n"), 0)));
	OutContent.Append(FString(FACEWorldBakeFbxHeaderExtension(UtcNow)));
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("CreationTime"), UtcNowStr, 0)));
	OutContent.Append(FString(FACEWorldBakeFbxValue(TEXT("Creator"), TEXT("\"ACEWorldBake/DatTools\""), 0)));
	OutContent.Append(FString(FACEWorldBakeFbxHeaderDefinitions(Model.SurfaceCount, CollisionCount, Model.SurfaceCount)));
	OutContent.Append(FString(FACEWorldBakeFbxObjects(Model)));
	OutContent.Append(FString(FACEWorldBakeFbxRelations(Model.SurfaceCount, CollisionCount, false)));
	OutContent.Append(FString(FACEWorldBakeFbxConnections(Model.SurfaceIDs, CollisionCount)));

	return OutContent;
}
