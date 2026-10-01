#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACEVisibleObjectPick.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECustomWorldPresentationTest,"ACE.RetailParity.CustomWorldPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACECustomWorldPresentationTest::RunTest(const FString&)
{
    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* GI = NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); Context.OwningGameInstance=GI;
    GI->OnWorldChanged(nullptr,World); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
    if (!TestTrue(TEXT("Retail DAT opens"),Dat && Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    FACEWorldObject Object;
    Object.Guid=0x71001001; Object.WeenieClassId=900001; Object.ItemType=ACEItemType::Misc;
    Object.bHasPosition=true; Object.Position.CellId=0x016C0101; Object.Position.Location=FVector(50,50,1.5);
    const FVector Authored=Object.Position.ToUnrealLocation(100);
    auto* Floor=World->SpawnActor<AActor>();
    auto* Box=NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(1000,1000,20)); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
    Floor->SetActorLocation(Authored-FVector(0,0,170));
    auto* Entity=World->SpawnActor<AACEWorldEntityActor>();
    Entity->InitializeFromObject(Object,100,false);
    TestTrue(TEXT("Gravity-free custom object keeps its elevated server origin"),Entity->GetActorLocation().Equals(Authored,.01));
    Object.PhysicsState=ACEPhysicsState::Gravity;
    Entity->Destroy(); Entity=World->SpawnActor<AACEWorldEntityActor>(); Entity->InitializeFromObject(Object,100,false);
    TestTrue(TEXT("Ordinary gravity loot still seats on its floor"),Entity->GetActorLocation().Z < Authored.Z-100);
    Entity->Destroy();
    Floor->Destroy();

    // The actual blade setup has distinct default/resting frames. Use it as
    // arbitrary custom scenery to catch replacement of server frame zero.
    Object.SetupId=0x0200087D; Object.PhysicsState=0; Object.PlacementId=0;
    Object.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck;
    Entity=World->SpawnActor<AACEWorldEntityActor>(); Entity->InitializeFromObject(Object,100,true);
    auto* Appearance=Entity->Appearance.Get();
    const auto* Default=Dat->GetOrBuildSetupMesh(Object.SetupId,100,0);
    const auto* Resting=Dat->GetOrBuildSetupMesh(Object.SetupId,100,101);
    TestTrue(TEXT("Fixture has genuinely different authored placements"),Default && Resting && !Default->Parts.IsEmpty()
        && !Resting->Parts.IsEmpty() && !Default->Parts[0].BindTransform.Equals(Resting->Parts[0].BindTransform,.001));
    FTransform Actual;
    TestTrue(TEXT("Custom world props use exact server default placement"),Default && Appearance->GetPartCurrentTransform(0,Actual)
        && Actual.Equals(Default->Parts[0].BindTransform,.001));
    const uint64 Revision=Appearance->GetAppearanceRevision();
    FBox Bounds; Appearance->GetVisualWorldBounds(Bounds);
    for (float Scale : {.1f,8.f,1.f})
    {
        Object.Scale=Scale; Entity->InitializeFromObject(Object,100,true);
        FBox Scaled; Appearance->GetVisualWorldBounds(Scaled);
        TestTrue(TEXT("Server scale is applied once to every custom part"),Scaled.GetSize().Equals(Bounds.GetSize()*Scale,.05));
        TestEqual(TEXT("Scale-only custom update keeps cached geometry"),Appearance->GetAppearanceRevision(),Revision);
    }
    Entity->Destroy();

    // UIHidden excludes picking while retaining visual geometry and physics.
    Object.SetupId=0; Object.Scale=1; Object.ObjectDescriptionFlags=0;
    Entity=World->SpawnActor<AACEWorldEntityActor>(); Entity->InitializeFromObject(Object,100,false);
    const FVector Start=Authored+FVector(-200,0,25), End=Authored+FVector(200,0,25);
    TestEqual(TEXT("Visible custom fallback can be selected"),ACEVisibleObjectPick::Trace(*World,Start,End,nullptr),static_cast<AActor*>(Entity));
    Object.ObjectDescriptionFlags=ACEObjectDescFlag::UiHidden; Entity->InitializeFromObject(Object,100,false);
    TestNull(TEXT("UIHidden pedestal is never a world click target"),ACEVisibleObjectPick::Trace(*World,Start,End,nullptr));
    TestFalse(TEXT("UIHidden retains the rendered object"),Entity->IsHidden());
    TestTrue(TEXT("UIHidden retains physical collision"),Entity->GetActorEnableCollision());
    return true;
}
#endif
