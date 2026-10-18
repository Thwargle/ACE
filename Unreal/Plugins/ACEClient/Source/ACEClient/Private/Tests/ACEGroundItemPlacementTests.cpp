#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldPresenterComponent.h"
#include "ACEWorldEntityActor.h"
#include "Dat/ACEDatCursor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEGroundItemPlacementTest, "ACE.RetailParity.GroundItemPlacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEGroundItemPlacementTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); Context.OwningGameInstance=GI;
    GI->OnWorldChanged(nullptr,World); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if(!TestTrue(TEXT("Retail DAT opens"),Dat && Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")))) return false;
    Dat->SetLightweightStreaming(false);
    auto Session=GI->GetSubsystem<UACEClientSubsystem>()->GetSession();
    auto* Owner=World->SpawnActor<AActor>();
    auto* Presenter=NewObject<UACEWorldPresenterComponent>(Owner);
    Owner->AddInstanceComponent(Presenter); Presenter->RegisterComponent(); Owner->DispatchBeginPlay();
    auto Drain=[&](){ Presenter->DrainPendingSpawns(); Presenter->DrainPendingDatAppearance(); };
    auto Position=[&](int32 Guid,uint16 Sequence,int32 Placement,bool HasPlacement=true,bool Airborne=false,uint16 Instance=1,int32 Cut=0)
    {
        FACEBinaryWriter W; W.WriteUInt32(Guid);
        W.WriteUInt32(0x70 | (HasPlacement?2:0) | (Airborne?1:4));
        W.WriteUInt32(0x016C0101); W.WriteFloat(50); W.WriteFloat(50); W.WriteFloat(Airborne?2.f:.005f); W.WriteFloat(1);
        if(Airborne){ W.WriteFloat(.8f); W.WriteFloat(0); W.WriteFloat(.55f); }
        if(HasPlacement) W.WriteUInt32(Placement);
        W.WriteUInt16(Instance); W.WriteUInt16(Sequence); W.WriteUInt16(0); W.WriteUInt16(0);
        FACEBinaryReader R(W.GetData().GetData(),W.GetData().Num()-Cut); Session->HandleUpdatePosition(R);
    };
    auto Release=[&](int32 Guid){ FACEBinaryWriter W; W.WriteUInt32(Guid); FACEBinaryReader R(W.GetData()); Session->HandleInventoryPutObjIn3D(R); };
    struct FFixture { const TCHAR* Name; uint32 Setup; int32 Type; };
    const FFixture Fixtures[]={{TEXT("Round Shield"),0x02000162,ACEItemType::Armor},
        {TEXT("Bracelet"),0x020000FB,ACEItemType::Jewelry},{TEXT("Ring"),0x02000103,ACEItemType::Jewelry},
        {TEXT("Wand"),0x0200017C,ACEItemType::Caster},{TEXT("Arrow"),0x02000124,ACEItemType::MissileWeapon}};
    int32 Guid=0x71004000;
    for(const auto& Fixture:Fixtures)
    {
        AddInfo(Fixture.Name);
        TArray<uint8> Bytes; FACEDatSetupModel Setup;
        if(!Dat->GetPortalDat()->ReadFile(Fixture.Setup,Bytes)) return false;
        FACEDatCursor Cursor(Bytes);
        if(!TestTrue(TEXT("Real item setup decodes"),ACEDatUnpack::UnpackSetupModel(Cursor,Setup))) return false;
        const auto* Frames=Setup.PlacementFrames.Find(101);
        if(!TestTrue(TEXT("Fixture has an authored resting pose"),Frames && Frames->Num()==1)) return false;
        const auto& Frame=(*Frames)[0]; const auto Q=Frame.GetRotation();
        const FTransform Resting(FACEPosition::AceQuatToUnreal(Q.W,FVector(Q.X,Q.Y,Q.Z)),
            FACEPosition::AceVectorToUnreal(FVector(Frame.GetTranslation()),100),FVector::OneVector);
        TestFalse(TEXT("Resting differs from raw/default pose"),Resting.Equals(FTransform::Identity,.001));
        if(Fixture.Setup==0x02000162)
            TestTrue(TEXT("Shield resting frame turns it 90 degrees"),FMath::IsNearlyEqual(Resting.GetRotation().GetAngle(),double(PI/2),.001));

        FACEWorldObject Item; Item.Guid=++Guid; Item.Name=Fixture.Name; Item.SetupId=Fixture.Setup; Item.ItemType=Fixture.Type;
        Item.bHasPosition=true; Item.Position.CellId=0x016C0101; Item.Position.Location=FVector(50,50,.005);
        Item.bHasPhysicsTimestamps=true; Item.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]=1;
        Session->WorldObjects.Add(Item.Guid,Item); Presenter->SpawnOrUpdateEntity(Item,true);
        auto* Entity=Presenter->FindEntityActor(Item.Guid);
        if(!TestNotNull(TEXT("Item has a rendered actor"),Entity)) return false;
        auto* App=Entity->Appearance.Get(); auto* Mesh=Cast<UProceduralMeshComponent>(App->GetPartMesh(0));
        if(!TestNotNull(TEXT("Item has a real DAT mesh"),Mesh)) return false;
        auto* Section=Mesh->GetProcMeshSection(0); auto* Material=Mesh->GetMaterial(0);
        auto CheckPose=[&](const FTransform& Expected,const TCHAR* Label)
        { FTransform Pose; TestTrue(Label,App->GetPartCurrentTransform(0,Pose) && Pose.Equals(Expected,.002)); };
        Position(Item.Guid,1,101);
        TestEqual(TEXT("PositionPack retains the server's resting placement"),Session->WorldObjects[Item.Guid].PlacementId,101);
        CheckPose(Resting,TEXT("Existing world mesh uses retail resting part frame immediately"));
        TestTrue(TEXT("Placement-only change retains mesh sections and materials"),
            App->GetPartMesh(0)==Mesh && Mesh->GetProcMeshSection(0)==Section && Mesh->GetMaterial(0)==Material);
        const uint64 Revision=App->GetAppearanceRevision();
        Position(Item.Guid,2,101);
        TestEqual(TEXT("Repeated resting positions do not rebuild appearance"),App->GetAppearanceRevision(),Revision);
        Position(Item.Guid,1,0); Position(Item.Guid,99,0,true,false,0); Position(Item.Guid,3,0,true,false,1,1);
        CheckPose(Resting,TEXT("Stale, wrong-incarnation and truncated packets cannot replace placement"));
        TestEqual(TEXT("Rejected packets leave snapshot placement intact"),Session->WorldObjects[Item.Guid].PlacementId,101);
        Position(Item.Guid,3,0,false);
        CheckPose(FTransform::Identity,TEXT("Omitted placement means zero, as in retail PositionPack::UnPack"));
        Position(Item.Guid,4,101); Position(Item.Guid,5,0);
        CheckPose(FTransform::Identity,TEXT("Explicit zero is respected, not forced to resting"));
        Position(Item.Guid,6,9999);
        CheckPose(FTransform::Identity,TEXT("Unavailable placement falls back to the setup's frame zero"));

        // A drop can reach SmartboxQueue before UIQueue. A pending spawn also
        // needs the new placement, not only its updated world coordinates.
        Presenter->DestroySpawnedGuid(Item.Guid);
        Item.Guid=++Guid; Session->WorldObjects.Add(Item.Guid,Item); Presenter->HandleObjectCreated(Item);
        Position(Item.Guid,1,101); Drain();
        Entity=Presenter->FindEntityActor(Item.Guid); App=Entity?Entity->Appearance:nullptr;
        TestNotNull(TEXT("Deferred item appears"),App);
        if(App) CheckPose(Resting,TEXT("Deferred spawn uses latest placement"));
        Presenter->DestroySpawnedGuid(Item.Guid);
        for(bool PositionFirst:{false,true})
        {
            Item.Guid=++Guid; Item.ContainerId=123; Item.bHasPosition=false;
            Session->WorldObjects.Add(Item.Guid,Item);
            if(!PositionFirst) Release(Item.Guid);
            Position(Item.Guid,1,101);
            if(PositionFirst) Release(Item.Guid);
            Drain(); Entity=Presenter->FindEntityActor(Item.Guid); App=Entity?Entity->Appearance:nullptr;
            TestNotNull(TEXT("Drop appears in either queue order"),App);
            if(App) CheckPose(Resting,TEXT("Inventory drop uses retail pose in either queue order"));
            Presenter->DestroySpawnedGuid(Item.Guid);
        }
        Item.ContainerId=0; Item.bHasPosition=true; Item.Guid=++Guid;
        Session->WorldObjects.Add(Item.Guid,Item); Presenter->SpawnOrUpdateEntity(Item,true);
        Entity=Presenter->FindEntityActor(Item.Guid); App=Entity->Appearance;
        Position(Item.Guid,1,101,true,true);
        CheckPose(Resting,TEXT("VR release retains the server-selected pose"));
        const auto& Released=Session->WorldObjects[Item.Guid];
        TestTrue(TEXT("VR release retains hand height and airborne velocity"),
            Released.Position.Location.Z==2 && !Released.Position.bIsGrounded && Released.Position.bHasVelocity
            && Released.Position.Velocity.Equals(FVector(.8,0,.55),.001));
        Presenter->DestroySpawnedGuid(Item.Guid);
    }

    FACEWorldObject Held; Held.Guid=++Guid; Held.SetupId=0x0200017C; Held.ItemType=ACEItemType::Caster;
    Held.ParentGuid=123; Held.ParentLocation=1; Held.PlacementId=1;
    Held.bHasPhysicsTimestamps=true; Held.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]=1;
    Session->WorldObjects.Add(Held.Guid,Held); Presenter->SpawnOrUpdateEntity(Held,true);
    auto* HeldEntity=Presenter->FindEntityActor(Held.Guid);
    if(!TestNotNull(TEXT("Held item actor exists"),HeldEntity)) return false;
    Position(Held.Guid,1,101);
    TestEqual(TEXT("Accepted world position unparents the physics snapshot"),Session->WorldObjects[Held.Guid].ParentGuid,0);
    TestEqual(TEXT("Held render actor is detached without waiting for another descriptor"),HeldEntity->GetParentGuid(),0);
    FTransform DroppedPose;
    HeldEntity->Appearance->GetPartCurrentTransform(0,DroppedPose);
    const auto RestingWand=Dat->GetOrBuildSetupMeshShared(Held.SetupId,100,101);
    TestTrue(TEXT("Detached item immediately uses its resting pose"),RestingWand && DroppedPose.Equals(RestingWand->Parts[0].BindTransform,.002));
    Presenter->DestroySpawnedGuid(Held.Guid);

    FACEWorldObject Animated; Animated.Guid=++Guid; Animated.SetupId=0x02001632; // Floating Candle
    Animated.ItemType=ACEItemType::Misc; Animated.bHasPosition=true;
    Animated.Position.CellId=0x016C0101; Animated.Position.Location=FVector(50,50,2);
    Animated.bHasPhysicsTimestamps=true; Animated.PhysicsTimestamps[ACEPhysicsTimeStamp::Instance]=1;
    Session->WorldObjects.Add(Animated.Guid,Animated); Presenter->SpawnOrUpdateEntity(Animated,true);
    auto* AnimatedEntity=Presenter->FindEntityActor(Animated.Guid);
    if(!TestNotNull(TEXT("Animated prop actor exists"),AnimatedEntity)) return false;
    auto* AnimatedApp=AnimatedEntity->Appearance.Get();
    TestTrue(TEXT("Animated prop is running its authored motion"),AnimatedApp->bPlayIdleMotion);
    AnimatedApp->TickComponent(.2f,LEVELTICK_All,nullptr);
    FTransform Before,After; AnimatedApp->GetPartCurrentTransform(0,Before);
    const uint64 AnimatedRevision=AnimatedApp->GetAppearanceRevision();
    Position(Animated.Guid,1,101);
    AnimatedApp->GetPartCurrentTransform(0,After);
    TestTrue(TEXT("Position placement does not replace an active animation frame"),Before.Equals(After,.001));
    TestEqual(TEXT("Position correction does not restart animated prop geometry or effects"),AnimatedApp->GetAppearanceRevision(),AnimatedRevision);
    Presenter->DestroySpawnedGuid(Animated.Guid);
    // Retail set_description clears a setup default MTable when the server
    // supplies zero. All authored human placements must remain static then.
    TArray<uint8> HumanBytes;FACEDatSetupModel HumanSetup;
    Dat->GetPortalDat()->ReadFile(0x02000001,HumanBytes);FACEDatCursor HumanCursor(HumanBytes);
    if(!TestTrue(TEXT("Human decoration setup loads"),ACEDatUnpack::UnpackSetupModel(HumanCursor,HumanSetup)))return false;
    FACEWorldObject Decoration;Decoration.Guid=++Guid;Decoration.SetupId=0x02000001;Decoration.ItemType=ACEItemType::Misc;
    Decoration.bHasPosition=true;Decoration.Position=Animated.Position;Decoration.bHasServerPhysicsDescription=true;
    Decoration.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck;
    for(const auto& Placement:HumanSetup.PlacementFrames)
    {
        Decoration.PlacementId=Placement.Key;Session->WorldObjects.Add(Decoration.Guid,Decoration);
        Presenter->SpawnOrUpdateEntity(Decoration,true);auto* DecoEntity=Presenter->FindEntityActor(Decoration.Guid);
        if(!TestNotNull(TEXT("Decorative body actor exists"),DecoEntity))return false;
        auto* DecoApp=DecoEntity->Appearance.Get();DecoApp->TickComponent(.5f,LEVELTICK_All,nullptr);
        DecoApp->ApplyWorldObject(Decoration,100,true);DecoApp->TickComponent(.5f,LEVELTICK_All,nullptr);
        TestFalse(TEXT("No server animation table means no standing idle"),DecoApp->bPlayIdleMotion);
        for(int32 Part=0;Part<Placement.Value.Num();++Part)
        {
            const auto& Frame=Placement.Value[Part];const auto Q=Frame.GetRotation();
            const FTransform Expected(FACEPosition::AceQuatToUnreal(Q.W,FVector(Q.X,Q.Y,Q.Z)),
                FACEPosition::AceVectorToUnreal(FVector(Frame.GetTranslation()),100),FVector::OneVector);
            FTransform Actual;
            TestTrue(*FString::Printf(TEXT("Human placement %u part %d survives ticks and refresh"),Placement.Key,Part),
                DecoApp->GetPartCurrentTransform(Part,Actual)&&Actual.Equals(Expected,.002));
        }
    }
    Presenter->DestroySpawnedGuid(Decoration.Guid);
    return true;
}
#endif
