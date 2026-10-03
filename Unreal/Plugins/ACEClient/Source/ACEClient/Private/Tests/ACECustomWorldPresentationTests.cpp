#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACECharacterCreation.h"
#include "ACEWorldEntityActor.h"
#include "ACEVisibleObjectPick.h"
#include "ACEScriptComponent.h"
#include "Components/BoxComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
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
    Object.PhysicsState=0;
    FTransform Actual;

    // Daralet's supplied Tou-Tou weenie (1050067): an object-looking Creature,
    // not a static prop. Its setup, clothing and motion all come from retail DAT.
    {
        FACEWorldObject Crystal=Object;
        Crystal.WeenieClassId=1050067; Crystal.Name=TEXT("Tou-Tou");
        Crystal.ItemType=ACEItemType::Creature; Crystal.SetupId=0x02001AC5;
        Crystal.MotionTableId=0x090001FC; Crystal.Scale=.75f;
        Crystal.UseRadius=2;
        Crystal.InitialMotionStyle=ACEMotion::StanceNonCombat;
        Crystal.InitialMotionCommand=ACEMotion::Ready;
        Crystal.PhysicsState=ACEPhysicsState::Gravity | ACEPhysicsState::Ethereal | ACEPhysicsState::ReportCollisions;
        Crystal.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck;
        Crystal.Appearance.PaletteBaseId=0x04000BEF;
        FACECharacterCreation Creation; FString Error;
        TestTrue(TEXT("Tou-Tou clothing decoder loads"),Creation.Load(*Dat->GetPortalDat(),Error));
        TestTrue(TEXT("Tou-Tou clothing resolves"),Creation.ApplyClothing(0x100007D8,Crystal.SetupId,2,0,Crystal.Appearance));
        auto* CrystalActor=World->SpawnActor<AACEWorldEntityActor>();
        CrystalActor->InitializeFromObject(Crystal,100,true);
        TestTrue(TEXT("Tou-Tou creation preserves the elevated server origin above nearby terrain"),CrystalActor->GetActorLocation().Equals(Authored,.01));
        CrystalActor->Appearance->GetPartCurrentTransform(0,Actual);
        TestEqual(TEXT("Tou-Tou has its authored idle lift before the first draw"),Actual.GetLocation().Z,50.);
        TArray<FTransform> Pose; int32 Count=0;
        for (int32 Frame=0;Frame<3;++Frame)
        {
            if(Frame) CrystalActor->Appearance->TickComponent(.5f,LEVELTICK_All,nullptr);
            CrystalActor->Appearance->GetPartCurrentTransform(0,Actual);
            TestTrue(TEXT("Retail DAT supplies Tou-Tou's rotating Ready pose"),Dat->EvaluateMotionCommand(
                Crystal.MotionTableId,ACEMotion::Ready,Frame*.5f,1,Pose,100,Count,nullptr,nullptr,ACEMotion::StanceNonCombat));
            TestTrue(TEXT("Object-looking NPC retains the complete DAT pose"),Count==1 && Actual.Equals(Pose[0],.001));
            TestTrue(TEXT("Crystal animation offset receives server scale exactly once"),
                FMath::IsNearlyEqual(CrystalActor->Appearance->GetPartMesh(0)->GetComponentLocation().Z-Authored.Z,37.5,.01));
            FBox Bounds; CrystalActor->Appearance->GetVisualWorldBounds(Bounds);
            const FVector Center=Bounds.GetCenter();
            TestEqual(TEXT("Visible Tou-Tou crystal can be clicked at its animated height"),ACEVisibleObjectPick::Trace(*World,
                Center-FVector(200,0,0),Center+FVector(200,0,0),nullptr),static_cast<AActor*>(CrystalActor));
        }
        CrystalActor->InitializeFromObject(Crystal,100,true);
        TestTrue(TEXT("Repeated object descriptions do not lower the crystal"),CrystalActor->GetActorLocation().Equals(Authored,.01));
        CrystalActor->Destroy();
        CrystalActor=World->SpawnActor<AACEWorldEntityActor>(); CrystalActor->InitializeFromObject(Crystal,100,true);
        TestTrue(TEXT("Recreating after relog preserves the same origin"),CrystalActor->GetActorLocation().Equals(Authored,.01));
        CrystalActor->Destroy();
    }
    Floor->Destroy();

    // Real setup 020014CA has a zero-height placement but a 2 m idle part
    // offset in 09000198/03000B5E. The weenie need not be a creature/lifestone.
    Object.SetupId=0x020014CA; Object.MotionTableId=0x09000198;
    Object.Scale=1; Object.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck;
    Entity=World->SpawnActor<AACEWorldEntityActor>(); Entity->InitializeFromObject(Object,100,true);
    TArray<FTransform> Idle; int32 IdleCount=0;
    TestTrue(TEXT("DAT supplies the custom prop's elevated idle"),Dat->EvaluateIdleMotion(Object.MotionTableId,0,1,Idle,100,IdleCount));
    TestTrue(TEXT("Custom prop is posed before first draw, above its origin"),IdleCount==1
        && Entity->Appearance->GetPartCurrentTransform(0,Actual) && Actual.Equals(Idle[0],.001)
        && Actual.GetLocation().Z>199);
    TestTrue(TEXT("Elevated custom prop retains the authoritative object origin"),Entity->GetActorLocation().Equals(Authored,.01));
    Entity->Destroy();

    // A Switch sends Twitch1. A quiet prop must wake up and evaluate its
    // authored action even when no creature/openable bits exist on the weenie.
    Object.SetupId=0x020004B5; Object.MotionTableId=0x0900006D; // retail Lever (WCID 285)
    Entity=World->SpawnActor<AACEWorldEntityActor>(); Entity->InitializeFromObject(Object,100,true);
    Entity->Appearance->GetPartCurrentTransform(1,Actual); // part 0 is the stationary base
    const FTransform BeforeAction=Actual;
    Entity->Appearance->SetComponentTickEnabled(false);
    FACEObjectMotionState Switch; Switch.ActionCommand=0x10000051; Switch.CurrentStyle=ACEMotion::StanceNonCombat;
    Entity->ApplyMotionState(Switch);
    TestTrue(TEXT("Server switch action wakes animation ticking"),Entity->Appearance->IsComponentTickEnabled());
    bool Moved=false;
    for(int32 Frame=0;Frame<60;++Frame)
    {
        if(Entity->Appearance->IsComponentTickEnabled()) Entity->Appearance->TickComponent(1.f/60,LEVELTICK_All,nullptr);
        Entity->Appearance->GetPartCurrentTransform(1,Actual);
        Moved |= !Actual.Equals(BeforeAction,.001);
    }
    TestTrue(TEXT("Server Twitch1 visibly moves the prop's actual DAT parts"),Moved);
    Entity->Destroy();

    // Transparent/UnHide must never make a ghost more opaque than the
    // server's original translucency. This applies during death motions too.
    Object.SetupId=0x02000001; Object.MotionTableId=0x09000001;
    Object.ItemType=ACEItemType::Creature; Object.Translucency=.6f;
    Entity=World->SpawnActor<AACEWorldEntityActor>(); Entity->InitializeFromObject(Object,100,true);
    auto Opacity=[&]()
    {
        auto* Part=Cast<UMeshComponent>(Entity->Appearance->GetPartMesh(0));
        float Value=-1; if(Part && Part->GetMaterial(0)) Part->GetMaterial(0)->GetScalarParameterValue(TEXT("OpacityMul"),Value);
        return Value;
    };
    if (auto* Part=Cast<UMeshComponent>(Entity->Appearance->GetPartMesh(0)))
        AddInfo(FString::Printf(TEXT("Ghost fixture part=%s materials=%d first=%s opacity=%.4f"),*Part->GetClass()->GetName(),
            Part->GetNumMaterials(),*GetNameSafe(Part->GetMaterial(0)),Opacity()));
    TestEqual(TEXT("Creature starts with server opacity"),Opacity(),.4f);
    FACEObjectMotionState Death; Death.ActionCommand=ACEMotion::Dead;
    Entity->ApplyMotionState(Death);
    FACEDatAnimationHook Fade; Fade.Type=EACEAnimationHookType::Transparent; Fade.Start=Fade.End=.9f;
    Entity->ScriptComponent->DispatchAnimationHook(Fade);
    TestEqual(TEXT("Temporary fade can reduce ghost opacity"),Opacity(),.1f);
    Fade.Start=Fade.End=0;
    Entity->ScriptComponent->DispatchAnimationHook(Fade);
    TestEqual(TEXT("Death/unhide retains original ghost opacity"),Opacity(),.4f);
    Entity->ScriptComponent->StopAllEffects();
    TestEqual(TEXT("Effect cleanup retains original ghost opacity"),Opacity(),.4f);
    Entity->Destroy(); Object.ItemType=ACEItemType::Misc; Object.Translucency=0; Object.MotionTableId=0;
    for(uint32 Surface : {0x08000590u,0x08000618u,0x0800061Du})
    {
        auto* AuthoredMaterial=Dat->GetOrCreateResolvedMaterial(Surface,0,FACEObjDesc(),0);
        TestTrue(TEXT("Corpse/default object material retains small authored DAT translucency"),
            AuthoredMaterial && AuthoredMaterial->GetBlendMode()==BLEND_Translucent);
        auto* FadedMaterial=Dat->GetOrCreateResolvedMaterial(Surface,0,FACEObjDesc(),.6f);
        UTexture* AuthoredTexture=nullptr; UTexture* FadedTexture=nullptr;
        if(AuthoredMaterial) AuthoredMaterial->GetTextureParameterValue(TEXT("Texture"),AuthoredTexture);
        if(FadedMaterial) FadedMaterial->GetTextureParameterValue(TEXT("Texture"),FadedTexture);
        TestTrue(TEXT("Default and network-faded body both retain their authored texture"),AuthoredTexture && FadedTexture);
    }

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
    Object.ObjectDescriptionFlags=0; Entity->InitializeFromObject(Object,100,false);

    auto* Window=World->SpawnActor<AACEEnvCellActor>();
    Window->SetActorLocation(Authored-FVector(100,0,0));
    auto Quad=[](UProceduralMeshComponent* Mesh,int32 Section,float LowY,float HighY)
    {
        const TArray<FVector> Vertices{{0,LowY,-100},{0,HighY,-100},{0,HighY,200},{0,LowY,200}};
        Mesh->CreateMeshSection(Section,Vertices,{0,1,2,0,2,3},{},{},{},{},true);
    };
    // Physics has one solid wall; DrawingBSP has a window aperture.
    Quad(Window->CellCollisionMesh,0,-200,200);
    Window->CellCollisionMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Window->CellCollisionMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    Window->CellCollisionMesh->SetCollisionResponseToChannel(ECC_Camera,ECR_Block);
    Window->CellCollisionMesh->bUseComplexAsSimpleCollision=true;
    Quad(Window->CellMesh,0,-200,-40); Quad(Window->CellMesh,1,40,200);
    Window->CellMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestEqual(TEXT("NPC is selectable through a drawn window despite solid movement BSP"),
        ACEVisibleObjectPick::Trace(*World,Start,End,nullptr),static_cast<AActor*>(Entity));
    Quad(Window->CellMesh,2,-40,40);
    TestNull(TEXT("A visible wall still blocks picking the NPC behind it"),ACEVisibleObjectPick::Trace(*World,Start,End,nullptr));
    return true;
}
#endif
