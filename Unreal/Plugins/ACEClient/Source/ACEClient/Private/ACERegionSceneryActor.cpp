#include "ACERegionSceneryActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEDatSubsystem.h"
#include "ACEScriptComponent.h"
#include "ACETypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

AACERegionSceneryActor::AACERegionSceneryActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Appearance = CreateDefaultSubobject<UACECharacterAppearanceComponent>(TEXT("Appearance"));
	Appearance->bUseWorldLighting = true;
	Appearance->bHideOwnerMeshes = true;

	ScriptComponent = CreateDefaultSubobject<UACEScriptComponent>(TEXT("ScriptComponent"));
}

bool AACERegionSceneryActor::InitializeFromSetup(int32 SetupId, float Scale, float InWorldScale, bool bEnableCollision)
{
	WorldScale = InWorldScale;
	if (!Appearance || SetupId == 0)
	{
		return false;
	}

	FACEWorldObject Obj;
	Obj.SetupId = SetupId;
	Obj.Scale = FMath::Clamp(Scale, 0.05f, 16.f);
	Obj.DefaultAnimationId = 0;
	// ApplyWorldObject fills DefaultAnimation from Setup when zero and enables DefaultAnimLoop
	// for non-creature props — same path as lifestones / animated scenery in retail.
	const bool bMeshOk = Appearance->ApplyWorldObject(Obj, WorldScale, bEnableCollision);

	UACEDatSubsystem* Dat = nullptr;
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			Dat = GI->GetSubsystem<UACEDatSubsystem>();
		}
	}
	uint32 DefaultScript = 0, ScriptTableId = 0, SoundTableId = 0;
	uint32 DefaultAnim = 0;
	float IgnoredStep = 0.f, IgnoredH = 0.f, IgnoredR = 0.f;
	if (Dat)
	{
		Dat->TryGetSetupRuntimeMetadata(static_cast<uint32>(SetupId), DefaultScript, ScriptTableId, SoundTableId);
		Dat->TryGetSetupPhysics(static_cast<uint32>(SetupId), IgnoredStep, IgnoredH, IgnoredR, DefaultAnim);
	}
	const bool bPhysicsScript = DefaultScript != 0 && (DefaultScript & 0xFF000000u) == 0x33000000u;
	// Particle-only Setups (Font of Jojii 0x0200058E) fail the mesh cook but still need
	// DefaultScript emitters. Retry when DAT is still cold (no mesh and no script/anim yet).
	if (!bMeshOk && !bPhysicsScript && DefaultAnim == 0)
	{
		return false;
	}

	// Birds: DefaultAnimation frame 0 SetOmega → continuous yaw; part offsets create the orbit.
	if (Appearance)
	{
		Appearance->SetComponentTickEnabled(true);
	}
	if (ScriptComponent)
	{
		ScriptComponent->InitializeFromObject(Obj, WorldScale);
		ScriptComponent->NotifyAppearanceReady(/*bIsDoor*/ false);
		ScriptComponent->SetComponentTickEnabled(true);
	}
	return true;
}
