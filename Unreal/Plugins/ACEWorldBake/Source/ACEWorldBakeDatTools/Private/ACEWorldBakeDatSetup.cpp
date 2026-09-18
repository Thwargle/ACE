#include "ACEWorldBakeDatSetup.h"
#include "ACEWorldBakeDatTools.h"
#include "ACEWorldBakeByteReader.h"
#include "ACEWorldBakeDatFile.h"

enum SetupFlag : int32
{
	kHasParentIndex = 0x1,
	kHasDefaultScale = 0x2,
	kAllowFreeHeading = 0x4,
	kHasPhysicsBSP = 0x8
};

namespace AnimationHookType
{
	enum Type : int32
	{
		kNoOp = 0x0000,
		kSound = 0x0001,
		kSoundTable = 0x0002,
		kAttack = 0x0003,
		kAnimDone = 0x0004,
		kReplaceObject = 0x0005,
		kEthereal = 0x0006,
		kTransparentPart = 0x0007,
		kLuminous = 0x0008,
		kLuminousPart = 0x0009,
		kDiffuse = 0x000a,
		kDiffusePart = 0x000b,
		kScale = 0x000c,
		kCreateParticle = 0x000d,
		kDestroyParticle = 0x000e,
		kStopParticle = 0x000f,
		kNoDraw = 0x0010,
		kDefaultScript = 0x0011,
		kDefaultScriptPart = 0x0012,
		kCallPES = 0x0013,
		kTransparent = 0x0014,
		kSoundTweaked = 0x0015,
		kSetOmega = 0x0016,
		kTextureVelocity = 0x0017,
		kTextureVelocityPart = 0x0018,
		kSetLight = 0x0019,
		kCreateBlockingParticle = 0x001a
	};
}

FACEWorldBakeSetupLight::FACEWorldBakeSetupLight()
: LightIndex(INDEX_NONE)
, Color(0)
, Intensity(0.0f)
, Falloff(0.0f)
, ConeAngle(0.0f)
{

}

FACEWorldBakePortalSetup::FACEWorldBakePortalSetup()
: ResourceId(INDEX_NONE)
, Flags(0)
, LightCount(0)
, DefaultAnimId(INDEX_NONE)
, DefaultPhysScriptId(INDEX_NONE)
, DefaultMotionTableId(INDEX_NONE)
, DefaultSoundTableId(INDEX_NONE)
, DefaultPhysScriptTableId(INDEX_NONE)
{
}

FACEWorldBakePortalSetup::FACEWorldBakePortalSetup(FACEWorldBakeByteReader& Reader)
: ResourceId(Reader.ReadUint32())
, Flags(Reader.ReadUint32())
, LightCount(0)
, DefaultAnimId(INDEX_NONE)
, DefaultPhysScriptId(INDEX_NONE)
, DefaultMotionTableId(INDEX_NONE)
, DefaultSoundTableId(INDEX_NONE)
, DefaultPhysScriptTableId(INDEX_NONE)
{
	int32 NumModels = Reader.ReadInt32();
	for (int32 ModelIndex = 0; ModelIndex < NumModels; ++ModelIndex)
	{
		ModelResourceIds.Add(Reader.ReadInt32());
	}

	if (Flags & kHasParentIndex)
	{
		for (int32 ModelIndex = 0; ModelIndex < NumModels; ++ModelIndex)
		{
			ModelParents.Add(Reader.ReadInt32());
		}
	}

	if (Flags & kHasDefaultScale)
	{
		for (int32 ModelIndex = 0; ModelIndex < NumModels; ++ModelIndex)
		{
			ModelScales.Add(Reader.ReadValue<FVector3f>());
		}
	}

	int32 NumHoldingLocations = Reader.ReadInt32();
	for (int32 HoldingLocationIndex = 0; HoldingLocationIndex < NumHoldingLocations; ++HoldingLocationIndex)
	{
		const int32 Key = Reader.ReadInt32();
		HoldingLocations.Add(Key, Reader.ReadValue<FACEWorldBakeResourceInfo>());
	}

	int32 NumConnectionPoints = Reader.ReadInt32();
	check(0 == NumConnectionPoints);
	for (int32 ConnectionPointIndex = 0; ConnectionPointIndex < NumConnectionPoints; ++ConnectionPointIndex)
	{
		const int32 Key = Reader.ReadInt32();
		ConnectionPoints.Add(Key, Reader.ReadValue<FACEWorldBakeResourceInfo>());
	}

	int32 NumPlacementFrames = Reader.ReadInt32();
	for (int32 PlacementFramesIndex = 0; PlacementFramesIndex < NumPlacementFrames; ++PlacementFramesIndex)
	{
		int32 Key = Reader.ReadInt32();
		OrderedPlacementKeys.Add(Key);

		TArray<FACEWorldBakeResourceLocation>* ResourceLocations = PlacementFrames.Find(Key);
		if (nullptr == ResourceLocations)
		{
			ResourceLocations = &PlacementFrames.Add(Key, TArray<FACEWorldBakeResourceLocation>());
		}
		check(ResourceLocations);

		for (int32 ModelIndex = 0; ModelIndex < NumModels; ++ModelIndex)
		{
			ResourceLocations->Add(Reader.ReadValue<FACEWorldBakeResourceLocation>());
		}

		int32 NumHooks = Reader.ReadInt32();
		for (int32 HookIndex = 0; HookIndex < NumHooks; ++HookIndex)
		{
			AnimationHookType::Type HookType = Reader.ReadValue<AnimationHookType::Type>();
			int32 HookDir = Reader.ReadInt32();

			switch (HookType)
			{
				case AnimationHookType::kSound:
				{
					int32 SoundId =  Reader.ReadInt32();
				}
				break;

				case AnimationHookType::kSoundTable:
				{
					int32 SoundType =  Reader.ReadInt32();
				}
				break;

				case AnimationHookType::kAttack:
				{
					int32 PartIndex =  Reader.ReadInt32();
					float LeftX = Reader.ReadFloat();
					float LeftY = Reader.ReadFloat();
					float RightX = Reader.ReadFloat();
					float RightY = Reader.ReadFloat();
					float Radius = Reader.ReadFloat();
					float Height = Reader.ReadFloat();
				}
				break;

				case AnimationHookType::kReplaceObject:
				{
					int16 PartIndex = Reader.ReadInt16();
					int16 PartId = Reader.ReadInt16();
				}
				break;

				case AnimationHookType::kEthereal:
				{
					bool Ethereal = (Reader.ReadInt32() != 0);
				}
				break;

				case AnimationHookType::kTransparentPart:
				{
					int32 PartIndex = Reader.ReadInt32();
					float Start = Reader.ReadFloat();
					float End = Reader.ReadFloat();
					float Time = Reader.ReadFloat();
				}
				break;

				case AnimationHookType::kScale:
				{
					float End = Reader.ReadFloat();
					float Time = Reader.ReadFloat();
				}
				break;

				case AnimationHookType::kCreateParticle:
				{
					int32 EmitterInfoId =  Reader.ReadInt32();
					int32 PartIndex =  Reader.ReadInt32();
					FACEWorldBakeResourceLocation PartLocation = Reader.ReadValue<FACEWorldBakeResourceLocation>();
					int32 EmitterId =  Reader.ReadInt32();
				}
				break;

				case AnimationHookType::kDestroyParticle:
				{
					int32 emitterId = Reader.ReadInt32();
				}
				break;

				case AnimationHookType::kStopParticle:
				{
					int32 EmitterId = Reader.ReadInt32();
				}
				break;

				case AnimationHookType::kNoDraw:
				{
					bool NoDraw = (Reader.ReadInt32() != 0);
				}
				break;

				case AnimationHookType::kDefaultScript:
				{
				}
				break;

				case AnimationHookType::kCallPES:
				{
					int32 PesId =  Reader.ReadInt32();
					float Pause = Reader.ReadFloat();
				}
				break;

				case AnimationHookType::kTransparent:
				{
					float Start = Reader.ReadFloat();
					float End = Reader.ReadFloat();
					float Time = Reader.ReadFloat();
				}
				break;

				case AnimationHookType::kSoundTweaked:
				{
					int32 SoundId = Reader.ReadInt32();
					float Priority = Reader.ReadFloat();
					float Probability = Reader.ReadFloat();
					float Volume = Reader.ReadFloat();
				}
				break;

				case AnimationHookType::kSetOmega:
				{
					FVector3f Axis = Reader.ReadValue<FVector3f>();
				}
				break;

				case AnimationHookType::kTextureVelocity:
				{
					float uSpeed = Reader.ReadFloat();
					float vSpeed = Reader.ReadFloat();
				}
				break;

				case AnimationHookType::kSetLight:
				{
					bool LightsOn = (Reader.ReadInt32() != 0);
				}
				break;

				case AnimationHookType::kCreateBlockingParticle:
				{
					int32 EmitterInfoId =  Reader.ReadInt32();
					int32 PartIndex =  Reader.ReadInt32();
					FACEWorldBakeResourceLocation PartLocation = Reader.ReadValue<FACEWorldBakeResourceLocation>();
					int32 EmitterId =  Reader.ReadInt32();
				}
			}
		}
	}

	const int32 NumCapsules = Reader.ReadInt32();
	for (int32 CapsuleIndex = 0; CapsuleIndex < NumCapsules; CapsuleIndex++)
	{
		const FVector3f Center = Reader.ReadValue<FVector3f>();
		const float Radius = Reader.ReadFloat();
		const float Height = Reader.ReadFloat();
	}

	const int32 NumSpheres = Reader.ReadInt32();
	for (int32 SphereIndex = 0; SphereIndex < NumSpheres; SphereIndex++)
	{
		const FVector3f Center = Reader.ReadValue<FVector3f>();
		const float Radius = Reader.ReadFloat();
	}

	const float Height = Reader.ReadFloat();
	const float Radius = Reader.ReadFloat();
	const float StepUpHeight = Reader.ReadFloat();
	const float StepDownHeight = Reader.ReadFloat();

	const FVector3f SortingCenter = Reader.ReadValue<FVector3f>();
	const float SortingRadius = Reader.ReadFloat();

	const FVector3f SelectionCenter = Reader.ReadValue<FVector3f>();
	const float SelectionRadius = Reader.ReadFloat();

	LightCount = Reader.ReadInt32();
	for (int32 LightIndex = 0; LightIndex < LightCount; LightIndex++)
	{
		FACEWorldBakeSetupLight Light;
		Light.LightIndex = Reader.ReadInt32();
		check(Light.LightIndex == LightIndex);
		Light.Location = Reader.ReadValue<FACEWorldBakeResourceLocation>();
		Light.Color = Reader.ReadInt32();
		Light.Intensity = Reader.ReadFloat();
		Light.Falloff = Reader.ReadFloat();
		Light.ConeAngle = Reader.ReadFloat();
		Lights.Add(Light);
	}

	DefaultAnimId = Reader.ReadInt32();

	DefaultPhysScriptId = Reader.ReadInt32();

	DefaultMotionTableId = Reader.ReadInt32();

	DefaultSoundTableId = Reader.ReadInt32();

	DefaultPhysScriptTableId = Reader.ReadInt32();

	check(0 == Reader.BytesLeft());
}
