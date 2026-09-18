#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACERenderAudit.h"
#include "ACEWorldEntityActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERenderAuditTest, "ACE.Rendering.SceneAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACERenderAuditTest::RunTest(const FString& Parameters)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* A = World->SpawnActor<AACEWorldEntityActor>(); A->ACEGuid = 123;
	auto* B = World->SpawnActor<AACEWorldEntityActor>(); B->ACEGuid = 123;
	TestEqual(TEXT("Audit detects repeated network identity even when one copy is hidden"),
		FACERenderAudit::Collect(World).DuplicateIdentityCount, 1);
	B->Destroy();
	TestEqual(TEXT("Destroyed portal residue is not counted as a live duplicate"),
		FACERenderAudit::Collect(World).DuplicateIdentityCount, 0);
	auto* Owner = World->SpawnActor<AActor>();
	auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Fixture geometry loads"), Cube)) return false;
	auto* Batch = NewObject<UInstancedStaticMeshComponent>(Owner);
	Batch->SetStaticMesh(Cube); Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision); Batch->RegisterComponent();
	Batch->AddInstance(FTransform::Identity);
	Batch->AddInstance(FTransform(FVector(200, 0, 0)));
	TestEqual(TEXT("Instancing distinct placements is not duplicate rendering"),
		FACERenderAudit::Collect(World).OverlappingStaticInstanceCount, 0);
	auto* Copy = NewObject<UStaticMeshComponent>(Owner);
	Copy->SetStaticMesh(Cube); Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision); Copy->RegisterComponent();
	TestEqual(TEXT("Audit detects a second mesh at an existing instance's placement"),
		FACERenderAudit::Collect(World).OverlappingStaticInstanceCount, 1);
	Copy->SetVisibility(false);
	TestEqual(TEXT("Hidden collision geometry is not a duplicate main-pass draw"),
		FACERenderAudit::Collect(World).OverlappingStaticInstanceCount, 0);
	Copy->SetVisibility(true); Copy->SetVisibleInSceneCaptureOnly(true);
	TestEqual(TEXT("Preview-only models are excluded from world overlap counts"),
		FACERenderAudit::Collect(World).OverlappingStaticInstanceCount, 0);
	return true;
}
#endif
