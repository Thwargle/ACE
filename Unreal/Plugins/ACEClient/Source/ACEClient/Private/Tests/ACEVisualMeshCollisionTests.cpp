#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ProceduralMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Interfaces/Interface_CollisionDataProvider.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVisualMeshCollisionTest, "ACE.Rendering.VisualMeshCollision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEVisualMeshCollisionTest::RunTest(const FString&)
{
    auto* Mesh=NewObject<UProceduralMeshComponent>();
    const TArray<FVector> V={FVector(0,0,0),FVector(100,0,0),FVector(0,100,0)};
    const TArray<int32> I={0,1,2};
    auto Section=[&](int32 Index,bool Collide) { Mesh->CreateMeshSection(Index,V,I,{}, {}, {}, {},Collide); };
    const FGuid Empty=Mesh->GetBodySetup()->BodySetupGuid;
    for(int S=0;S<64;++S) Section(S,false);
    TestEqual(TEXT("Visual clothing/LOD sections do not recook a physics body"),Mesh->GetBodySetup()->BodySetupGuid,Empty);
    Mesh->ClearMeshSection(8); Mesh->ClearAllMeshSections();
    TestEqual(TEXT("Clearing visual sections does not rebuild physics"),Mesh->GetBodySetup()->BodySetupGuid,Empty);
    Section(0,true);
    const FGuid Solid=Mesh->GetBodySetup()->BodySetupGuid;
    TestTrue(TEXT("Adding physical geometry rebuilds collision"),Solid!=Empty);
    Section(1,false); Mesh->ClearMeshSection(1);
    TestEqual(TEXT("A visual addition preserves existing physical geometry"),Mesh->GetBodySetup()->BodySetupGuid,Solid);
    FTriMeshCollisionData Data; Mesh->GetPhysicsTriMeshData(&Data,false);
    TestEqual(TEXT("Visual changes retain the physical triangle"),Data.Indices.Num(),1);
    Section(0,false);
    TestTrue(TEXT("Replacing a solid section with visual geometry removes its old collision"),Mesh->GetBodySetup()->BodySetupGuid!=Solid);
    TestFalse(TEXT("Replaced geometry no longer collides"),Mesh->ContainsPhysicsTriMeshData(false));
    Section(0,true); const FGuid Rebuilt=Mesh->GetBodySetup()->BodySetupGuid; Mesh->ClearMeshSection(0);
    TestTrue(TEXT("Clearing a solid section rebuilds physics"),Mesh->GetBodySetup()->BodySetupGuid!=Rebuilt);
    Section(0,true); const FGuid All=Mesh->GetBodySetup()->BodySetupGuid; Mesh->ClearAllMeshSections();
    TestTrue(TEXT("Clearing all solid sections rebuilds physics"),Mesh->GetBodySetup()->BodySetupGuid!=All);
    TestFalse(TEXT("No stale collision remains after clearing"),Mesh->ContainsPhysicsTriMeshData(false));
    return true;
}
#endif
