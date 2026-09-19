#pragma once
#include "CoreMinimal.h"

class UWorld;

// Scene inventory, not GPU draw-call counts: culling, stereo and shadow passes
// determine which of these sections are actually submitted by the renderer.
struct FACERenderAuditGroup
{
	int32 Actors = 0, Primitives = 0, MainPrimitives = 0, Sections = 0;
	int32 TranslucentSections = 0, ShadowSections = 0, Instances = 0;
};

struct ACECLIENT_API FACERenderAudit
{
	TMap<FString, FACERenderAuditGroup> Groups;
	TArray<FString> DuplicateIdentities;
	TArray<FString> OverlappingStaticInstances;
	TArray<FString> Captures;
	TMap<FString, int32> LandscapeTextureFormats;
	int64 LandscapeTextureBytes = 0;
	int32 DuplicateIdentityCount = 0, OverlappingStaticInstanceCount = 0;
	int32 ParticleEmitters = 0, DegradedEmitters = 0, ActiveParticles = 0, ParticleLights = 0;
	static FACERenderAudit Collect(UWorld* World);
	void Log() const;
};
