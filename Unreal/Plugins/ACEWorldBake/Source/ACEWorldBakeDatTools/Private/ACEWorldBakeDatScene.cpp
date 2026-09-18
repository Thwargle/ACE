#include "ACEWorldBakeDatScene.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeByteReader.h"
#include "ACEWorldBakeDatFile.h"

const float kCellSize = 24.0f;

FACEWorldBakePortalScene::FACEWorldBakePortalScene()
{
}

FACEWorldBakePortalScene::FACEWorldBakePortalScene(FACEWorldBakeByteReader& Reader)
: ResourceId(Reader.ReadUint32())
, NumObects(Reader.ReadUint32())
{
	Objects.AddUninitialized(NumObects);

	for (uint32 ObjectIndex = 0; ObjectIndex < NumObects; ++ObjectIndex)
	{
		FACEWorldBakePortalSceneObject& SceneObject = Objects[ObjectIndex];

		SceneObject.ResourceInfo = Reader.ReadValue<FACEWorldBakeResourceInfo>();
		check(SceneObject.ResourceInfo.ResourceId == 0 ||
			(SceneObject.ResourceInfo.ResourceId & 0xFF000000) == static_cast<uint32_t>(EACEWorldBakeResource_Native::kModel) ||
			(SceneObject.ResourceInfo.ResourceId & 0xFF000000) == static_cast<uint32_t>(EACEWorldBakeResource_Native::kSetup));

		check(SceneObject.ResourceInfo.Location.OffsetX >= -kCellSize && SceneObject.ResourceInfo.Location.OffsetX <= kCellSize);
		check(SceneObject.ResourceInfo.Location.OffsetY >= -kCellSize && SceneObject.ResourceInfo.Location.OffsetY <= kCellSize);

		SceneObject.Frequency = Reader.ReadFloat();
		check(SceneObject.Frequency >= 0.0 && SceneObject.Frequency <= 1.0);

		SceneObject.DisplaceX = Reader.ReadFloat();
		check(SceneObject.DisplaceX >= -kCellSize && SceneObject.DisplaceX <= kCellSize);

		SceneObject.DisplaceY = Reader.ReadFloat();
		check(SceneObject.DisplaceY >= -kCellSize && SceneObject.DisplaceY <= kCellSize);

		SceneObject.MinScale = Reader.ReadFloat();
		check(SceneObject.MinScale >= 0.0f);

		SceneObject.MaxScale = Reader.ReadFloat();
		check(SceneObject.MaxScale >= 0.0f);
		check(SceneObject.MinScale <= SceneObject.MaxScale);

		SceneObject.MaxRotation = Reader.ReadFloat();
		check(SceneObject.MaxRotation >= 0.0 && SceneObject.MaxRotation <= 360.0);

		SceneObject.MinSlope = Reader.ReadFloat();
		SceneObject.MaxSlope = Reader.ReadFloat();

		SceneObject.IntAlign = Reader.ReadUint32();
		check(SceneObject.IntAlign == 0 || SceneObject.IntAlign == 1);

		SceneObject.IntOrient = Reader.ReadUint32();
		check(SceneObject.IntOrient == 0 || SceneObject.IntOrient == 1);

		SceneObject.IntIsWeenieObj = Reader.ReadUint32();
		check(SceneObject.IntIsWeenieObj == 0 || SceneObject.IntIsWeenieObj == 1);
	}

	check(0 == Reader.BytesLeft());
}
