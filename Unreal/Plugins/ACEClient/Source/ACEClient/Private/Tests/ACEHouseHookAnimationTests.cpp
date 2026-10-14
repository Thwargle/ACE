#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEWorldPresenterComponent.h"
#include "ACEWorldEntityActor.h"
#include "Dat/ACEDatCursor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEHouseHookAnimationTest,"ACE.RetailParity.HouseHookAnimation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEHouseHookAnimationTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); Context.OwningGameInstance=GI;
    GI->OnWorldChanged(nullptr,World); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    FString Directory=TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Directory);
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if(!TestTrue(TEXT("Retail DAT opens"),Dat && Dat->LoadDatDirectory(Directory))) return false;

    // WCID 34089 Floating Candle. The hook keeps its own WCID and container
    // descriptor while ACE swaps the displayed Setup to the item's 02001632.
    TArray<uint8> Bytes; FACEDatSetupModel Setup;
    if(!Dat->GetPortalDat()->ReadFile(0x02001632,Bytes)) return false;
    FACEDatCursor SetupCursor(Bytes);
    if(!TestTrue(TEXT("Floating Candle setup decodes"),ACEDatUnpack::UnpackSetupModel(SetupCursor,Setup))) return false;
    FACEDatAnimation Animation;
    if(!Dat->GetPortalDat()->ReadFile(Setup.DefaultAnimation,Bytes)) return false;
    FACEDatCursor AnimationCursor(Bytes);
    if(!TestTrue(TEXT("Floating Candle default animation decodes"),ACEDatUnpack::UnpackAnimation(AnimationCursor,Animation))) return false;
    AddInfo(FString::Printf(TEXT("Floating Candle: animation=%08X, motion=%08X, parts=%d, frames=%d"),
        Setup.DefaultAnimation,Setup.DefaultMotionTable,Animation.NumParts,Animation.NumFrames));
    TestTrue(TEXT("Candle contains a complete animated cycle"),Animation.NumFrames>1);

    auto* Owner=World->SpawnActor<AActor>();
    auto* Presenter=NewObject<UACEWorldPresenterComponent>(Owner);
    Owner->AddInstanceComponent(Presenter); Presenter->RegisterComponent(); Owner->DispatchBeginPlay();
    auto Session=GI->GetSubsystem<UACEClientSubsystem>()->GetSession();
    FACEWorldObject Hook;
    Hook.Guid=0x71002001; Hook.ItemType=ACEItemType::Container; Hook.bIsHouseHook=true;
    Hook.ObjectDescriptionFlags=ACEObjectDescFlag::Openable | ACEObjectDescFlag::Stuck;
    Hook.bHasPosition=true; Hook.Position.CellId=0x016C0101; Hook.Position.Location=FVector(50,50,1);
    Hook.PlacementId=103;
    // No item/area-specific exceptions: all five hook kinds, deferred appearance,
    // duplicate descriptions and replacement of contents use the same pipeline.
    for(int32 Wcid : {9686,11697,11698,12678,12679})
    {
        Presenter->ClearSpawned();
        Hook.WeenieClassId=Wcid; Hook.SetupId=0x02001632; Hook.MotionTableId=0;
        Hook.InitialMotionCommand=0; Hook.Scale=.5f;
        Session->WorldObjects.Add(Hook.Guid,Hook);
        Presenter->SpawnOrUpdateEntity(Hook,false); Presenter->DrainPendingDatAppearance();
        auto* Actor=Presenter->FindEntityActor(Hook.Guid);
        if(!TestNotNull(TEXT("Hook actor is created"),Actor)) return false;
        TestFalse(TEXT("Hook actor does not infer chest motion from container flags"),Actor->UsesOnOffMotion());
        auto* Appearance=Actor->Appearance.Get();
        TestTrue(TEXT("Hooked candle ticks its default animation"),Appearance->HasAppearance() && Appearance->IsComponentTickEnabled());
        TArray<FTransform> Expected; int32 Count=0; float Time=0;
        bool Moved=false; FTransform First;
        Appearance->GetPartCurrentTransform(0,First);
        for(uint32 Frame=0; Frame<=Animation.NumFrames*2; ++Frame)
        {
            if(Frame)
            {
                Time+=1.f/30.f;
                if(Appearance->IsComponentTickEnabled()) Appearance->TickComponent(1.f/30.f,LEVELTICK_All,nullptr);
            }
            if(Frame==Animation.NumFrames/2)
            {
                Presenter->SpawnOrUpdateEntity(Hook,false); Presenter->DrainPendingDatAppearance();
            }
            Dat->EvaluateAnimationLoop(Setup.DefaultAnimation,Time,Animation.NumParts,Expected,100,Count);
            for(int32 Part=0; Part<Count; ++Part)
            {
                FTransform Actual; Appearance->GetPartCurrentTransform(Part,Actual);
                if(!TestTrue(TEXT("Every candle frame, including repeat and refresh, matches DAT"),Actual.Equals(Expected[Part],.005))) return false;
                if(Part==0) Moved |= !Actual.Equals(First,.01);
            }
        }
        TestTrue(TEXT("Candle visibly moves instead of freezing on its hook placement"),Moved);

        // A motion-table decoration must also keep its complete default idle.
        // This stock setup has a rotating/elevated Ready pose, unlike the candle.
        Hook.SetupId=0x02001AC5; Hook.MotionTableId=0x090001FC; Hook.Scale=1;
        Session->WorldObjects.Add(Hook.Guid,Hook);
        Presenter->SpawnOrUpdateEntity(Hook,false); Presenter->DrainPendingDatAppearance();
        Time=0;
        for(int32 Frame=0; Frame<120; ++Frame)
        {
            if(Frame) { Time+=1.f/30.f; Appearance->TickComponent(1.f/30.f,LEVELTICK_All,nullptr); }
            Dat->EvaluateIdleMotion(Hook.MotionTableId,Time,1,Expected,100,Count);
            FTransform Actual; Appearance->GetPartCurrentTransform(0,Actual);
            if(!TestTrue(TEXT("Hooked motion-table decoration retains DAT idle"),Count==1 && Actual.Equals(Expected[0],.005))) return false;
        }
        // Removing the item restores an empty hook, without retaining its motion.
        Hook.SetupId=0x02000A8E; Hook.MotionTableId=0; Hook.PlacementId=0;
        Session->WorldObjects.Add(Hook.Guid,Hook);
        Presenter->SpawnOrUpdateEntity(Hook,false); Presenter->DrainPendingDatAppearance();
        TestFalse(TEXT("Empty hook stops the previous decoration animation"),Appearance->IsComponentTickEnabled());
        Hook.PlacementId=103;
    }
    Presenter->ClearSpawned(); Owner->Destroy(); Session->WorldObjects.Remove(Hook.Guid);
    return true;
}
#endif
