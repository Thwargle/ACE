#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "ACEEnvCellActor.h"
#include "ProceduralMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEStaticCellVisibilityTest,"ACE.Rendering.StaticCellVisibility",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEStaticCellVisibilityTest::RunTest(const FString&)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    ON_SCOPE_EXIT {World->DestroyWorld(false);};
    auto* Cell=World->SpawnActor<AACEEnvCellActor>();
    Cell->SetActorLocation(FVector(1000,0,0));
    auto* Root=Cell->GetStaticSceneryRoot();
    if(!TestNotNull(TEXT("Scenery has an independent visibility owner"),Root))return false;
    auto* Stair=NewObject<UProceduralMeshComponent>(Cell->StaticSceneryActor);
    Stair->SetupAttachment(Root);Stair->RegisterComponent();
    const TArray<FVector> Vertices={{-1100,-100,0},{0,-100,0},{0,100,200},{-1100,100,200}};
    Stair->CreateMeshSection(0,Vertices,{0,1,2,0,2,3},{},{},{},{},false);
    Cell->StaticObjectMeshes.Add(Stair);
    TestEqual(TEXT("Scenery uses the same world frame as its cell"),Stair->GetComponentLocation(),Cell->GetActorLocation());
    Cell->SetEnvCellHiddenInGame(true);
    const FBox View(FVector(-200,-200,-100),FVector(200,200,300));
    TestFalse(TEXT("The object's origin is outside the view"),View.IsInside(Stair->GetComponentLocation()));
    Cell->RefreshStaticVisibility([&](const FBox& Bounds){return View.Intersect(Bounds);});
    TestTrue(TEXT("Origin room remains hidden"),Cell->IsHidden());
    TestFalse(TEXT("Large scenery is drawn while any of its bounds remains visible"),Cell->StaticSceneryActor->IsHidden());
    TestTrue(TEXT("Visible stair component retained"),Stair->IsVisible());
    Cell->RefreshStaticVisibility([](const FBox&){return false;});
    TestTrue(TEXT("Scenery disappears when no occupied cell is visible"),Cell->StaticSceneryActor->IsHidden());
    Cell->SetEnvCellHiddenInGame(false);
    Cell->RefreshStaticVisibility([](const FBox&){return false;});
    TestTrue(TEXT("Revealing the origin room restores component visibility"),Stair->IsVisible());
    TestFalse(TEXT("Revealing the origin room restores owner visibility"),Cell->StaticSceneryActor->IsHidden());
    Cell->ApplyStabPawnCollision(false);
    TestFalse(TEXT("Hidden-cell collision residency also reaches the separate scenery owner"),Cell->StaticSceneryActor->GetActorEnableCollision());
    Cell->ApplyStabPawnCollision(true);
    TestTrue(TEXT("Returning to a cell restores scenery collision"),Cell->StaticSceneryActor->GetActorEnableCollision());
    const TWeakObjectPtr<AActor> Owner=Cell->StaticSceneryActor;
    Cell->ClearStaticObjects();
    TestTrue(TEXT("Clearing the room destroys its scenery owner"),!Owner.IsValid()||Owner->IsActorBeingDestroyed());
    return true;
}
#endif
