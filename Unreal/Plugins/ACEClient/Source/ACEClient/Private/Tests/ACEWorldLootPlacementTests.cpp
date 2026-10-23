#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACEInputBindings.h"
#include "ACESession.h"
#include "ACELandblockActor.h"
#include "ACEEnvCellActor.h"
#include "ACEWorldEntityActor.h"
#include "ACEVisibleObjectPick.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Camera/PlayerCameraManager.h"
#include "ACEHoverTooltipWidget.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEWorldLootPlacementTest,"ACE.RetailParity.WorldLootPlacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEWorldLootPlacementTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine);World->SetGameInstance(GI);Context.OwningGameInstance=GI;
    GI->OnWorldChanged(nullptr,World);GI->Init();
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);};
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    FString DatDirectory=TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),DatDirectory);
    if(!TestTrue(TEXT("Retail DAT opens"),Dat&&Dat->LoadDatDirectory(DatDirectory)))return false;
    Dat->GetOrBuildLandblockMesh(0xD6990000,100);
    auto* Land=World->SpawnActor<AACELandblockActor>();
    if(!TestTrue(TEXT("Asheron's Castle geometry loads"),Land->LoadLandblock(0xD6990000,100)))return false;
    const double Deadline=FPlatformTime::Seconds()+20;
    while(!Land->IsSceneryComplete()&&FPlatformTime::Seconds()<Deadline)
    {++GFrameCounter;Land->Tick(.016f);FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);FPlatformProcess::Sleep(.001f);}
    TestTrue(TEXT("Castle furniture is ready for selection tests"),Land->IsSceneryComplete());

    auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto Session=Client->GetSession();
    // Inspect real reliable action payloads without connecting to an account/server.
    auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    auto* Receiver=Sockets->CreateSocket(NAME_DGram,TEXT("Tabletop loot receiver"),false);
    const auto Address=Sockets->CreateInternetAddr();bool ValidAddress=false;
    Address->SetIp(TEXT("127.0.0.1"),ValidAddress);Address->SetPort(0);
    ON_SCOPE_EXIT {if(Receiver){Receiver->Close();Sockets->DestroySocket(Receiver);}
        if(Session->SocketC2S){Session->SocketC2S->Close();Sockets->DestroySocket(Session->SocketC2S);Session->SocketC2S=nullptr;}};
    if(!TestTrue(TEXT("Isolated pickup transport binds"),Receiver&&Receiver->Bind(*Address)))return false;
    Receiver->GetAddress(*Address);
    Session->SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("Tabletop loot sender"),false);
    Session->ServerC2SAddr=Address;Session->IssacClient=MakeUnique<FACEIsaac>(123u);
    auto CountAction=[&](uint32 Action)
    {
        int32 Count=0;
        for(const auto& Packet:Session->CachedC2SPackets)
        {FACEBinaryReader R(Packet.Value.Payload);R.Skip(16);if(R.CanRead(12)&&R.ReadUInt32()==ACEOpcode::GameAction)
            {R.ReadUInt32();if(R.ReadUInt32()==Action)++Count;}}
        return Count;
    };
    auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));World->AddController(PC);
    if(!PC->PlayerCameraManager){PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();PC->PlayerCameraManager->InitializeFor(PC);}
    auto* Pawn=World->SpawnActor<APawn>();auto* Root=NewObject<UCapsuleComponent>(Pawn);Root->InitCapsuleSize(48,88);
    Pawn->SetRootComponent(Root);Pawn->AddInstanceComponent(Root);Root->RegisterComponent();PC->Possess(Pawn);
    Pawn->SetActorEnableCollision(false);
    PC->PlayerInput=NewObject<UPlayerInput>(PC);PC->SetupInputComponent();PC->bRetailCursorInstalled=true;
    PC->HoverTooltipWidget=CreateWidget<UACEHoverTooltipWidget>(GI,UACEHoverTooltipWidget::StaticClass());
    PC->bEnterWorldLoading=false;PC->bWorldRevealActive=false;
    Session->OnMotionUpdate.AddUObject(PC,&AACEPlayerController::HandleMotionUpdate);
    Session->OnUseDone.AddUObject(PC,&AACEPlayerController::HandleUseDone);
    FACEWorldObject Self;Self.Guid=12345;Self.SetupId=0x02000001;Self.bIsPlayer=true;Self.bHasPosition=true;
    Self.ItemsCapacity=120;Self.ContainersCapacity=7;
    Self.Position.CellId=0xD6990006;Self.Position.Location=FVector(6.408918,143.564311,374.005);
    Session->PlayerGuid=Self.Guid;Session->State=EACESessionState::InWorld;Session->WorldObjects.Add(Self.Guid,Self);
    PC->ApplyPlayerCapsuleFromSetup(Self.SetupId);

    // Retail Use opens a dropped pack; explicit pickup collects the same pack.
    // Capacity-only descriptors must work too, without relying on Openable.
    Session->PlayerPosition=Self.Position;PC->PredictedPose=Self.Position;PC->bHavePredictedPose=true;
    for(bool Openable:{false,true})
    {
        FACEWorldObject Pack;Pack.Guid=0x71002900+int32(Openable);Pack.Name=TEXT("Dropped pack");
        Pack.ItemType=ACEItemType::Container;Pack.ItemsCapacity=24;Pack.ItemUseable=32;
        Pack.ObjectDescriptionFlags=Openable?ACEObjectDescFlag::Openable:0;
        Pack.bHasPosition=true;Pack.Position=Self.Position;Pack.Position.Location.Y+=1;
        FACEWorldObject Contents;Contents.Guid=Pack.Guid+10;Contents.ContainerId=Pack.Guid;
        Contents.ItemType=ACEItemType::Gem;Contents.Name=TEXT("Pack contents");
        Session->WorldObjects.Add(Pack.Guid,Pack);Session->WorldObjects.Add(Contents.Guid,Contents);
        Client->SelectObject(Pack.Guid);Session->CachedC2SPackets.Reset();
        PC->InteractWithSelectedObject();
        TestEqual(TEXT("Use opens a portable ground pack instead of picking it up"),CountAction(ACEGameAction::Use),1);
        TestEqual(TEXT("Opening a pack never sends an inventory transfer"),CountAction(ACEGameAction::PutItemInContainer),0);
        TestEqual(TEXT("Opening leaves the pack in the world"),Session->WorldObjects[Pack.Guid].ContainerId,0);
        FACEBinaryWriter View;View.WriteUInt32(Pack.Guid);View.WriteUInt32(1);
        View.WriteUInt32(Contents.Guid);View.WriteUInt32(0);
        FACEBinaryReader ViewReader(View.GetData());Session->HandleViewContents(ViewReader);
        FACEBinaryWriter Done;Done.WriteUInt32(0);FACEBinaryReader DoneReader(Done.GetData());Session->HandleUseDone(DoneReader);
        TestEqual(TEXT("Server contents opens the ground pack's external inventory"),Session->OpenExternalContainerGuid,Pack.Guid);
        TestEqual(TEXT("Opened pack exposes its contents"),Session->ContainerContents.FindChecked(Pack.Guid).Num(),1);
        PC->PickupSelectedObject();
        TestEqual(TEXT("Explicit pickup collects an open ground pack"),CountAction(ACEGameAction::PutItemInContainer),1);
        TestEqual(TEXT("Pickup does not re-use or close the pack by toggling Use"),CountAction(ACEGameAction::Use),1);
        PC->PickupSelectedObject();
        TestEqual(TEXT("Repeated pickup waits for the inventory acknowledgment"),CountAction(ACEGameAction::PutItemInContainer),1);
        FACEBinaryWriter Close;Close.WriteUInt32(Pack.Guid);FACEBinaryReader CloseReader(Close.GetData());Session->HandleCloseGroundContainer(CloseReader);
        FACEBinaryWriter Ack;Ack.WriteUInt32(Pack.Guid);Ack.WriteUInt32(Self.Guid);Ack.WriteInt32(0);Ack.WriteInt32(1);
        FACEBinaryReader AckReader(Ack.GetData());Session->HandleInventoryPutObjInContainer(AckReader);
        TestEqual(TEXT("Server acknowledgment transfers the pack into inventory"),Session->WorldObjects[Pack.Guid].ContainerId,Self.Guid);
        TestEqual(TEXT("Picking up a pack preserves its contents"),Session->WorldObjects[Contents.Guid].ContainerId,Pack.Guid);
        Session->WorldObjects.Remove(Pack.Guid);Session->WorldObjects.Remove(Contents.Guid);
    }

    // Stock ACE D699 instances 0x7D69901F / 32 / 33 share this retail token
    // setup. Gravity is set even though they rest 1.65 m above outdoor terrain.
    int32 Guid=0x71003000;
    for(const FVector& At:{FVector(4.71251,135.874,375.653),FVector(7.99088,143.587,375.653),FVector(7.96152,146.141,375.653)})
    {
        FACEWorldObject Token;Token.Guid=++Guid;Token.WeenieClassId=43503;Token.Name=TEXT("Token of the Aura of Mana Flow");
        Token.ItemType=ACEItemType::Gem;Token.ItemUseable=1;Token.PhysicsState=1044;
        Token.SetupId=0x02000B20;Token.bHasPosition=true;Token.UseRadius=.6f;
        Token.Position.CellId=At.Y<144?0xD6990006:0xD6990007;Token.Position.Location=At;
        Token.Position.RotationW=-.711158f;Token.Position.RotationXYZ=FVector(0,0,.703032);
        const FVector Origin=Token.Position.ToUnrealLocation(100);
        float TerrainZ=0;TestTrue(TEXT("Fixture has real terrain below the table"),Dat->SampleOutdoorGroundZ(Origin.X,Origin.Y,100,TerrainZ));
        TestTrue(TEXT("Fixture exercises an elevated loose item"),Origin.Z-TerrainZ>150);
        auto* Entity=World->SpawnActor<AACEWorldEntityActor>();Entity->InitializeFromObject(Token,100,false);
        TestTrue(TEXT("Creation before mesh streaming retains server table height"),Entity->GetActorLocation().Equals(Origin,.01));
        Entity->ApplyDatAppearanceFromObject(Token);
        TestTrue(TEXT("Token loads its real retail mesh"),Entity->Appearance->HasAppearance());
        for(int32 Pass=0;Pass<3;++Pass)
        {
            if(Pass==1)Entity->InitializeFromObject(Token,100,true);
            if(Pass==2)
            {
                FACEPosition Contact=Token.Position;Contact.bHasContactState=true;Contact.bIsGrounded=true;
                Entity->ApplyACEPosition(Contact);
            }
            for(int32 Frame=0;Frame<60;++Frame)Entity->Tick(1.f/60);
            TestTrue(TEXT("Refresh, idle ticks and contact updates retain the tabletop origin"),Entity->GetActorLocation().Equals(Origin,.01));
            FBox Bounds;TestTrue(TEXT("Token has visible selection geometry"),Entity->Appearance->GetSelectionWorldBounds(Bounds));
            const FVector Eye=Origin+FVector(180,0,45),Target=Origin+(Bounds.GetCenter()-Entity->GetActorLocation());
            TestEqual(TEXT("Desktop mouse ray reaches token above castle furniture"),
                ACEVisibleObjectPick::Trace(*World,Eye,Target+(Target-Eye).GetSafeNormal()*30,nullptr),static_cast<AActor*>(Entity));
        }
        Session->WorldObjects.Add(Token.Guid,Token);
        Self.Position=Token.Position;Self.Position.Location+=FVector(-2,0,-1.648);
        if(Guid==0x71003002)
        {
            Self.Position.CellId=0xD6990006;Self.Position.Location=FVector(6.408918,143.564311,374.005);
            Self.Position.RotationW=.912523f;Self.Position.RotationXYZ=FVector(0,0,-.409025);
        }
        Session->PlayerPosition=Self.Position;Session->WorldObjects[Self.Guid]=Self;
        PC->PredictedPose=Self.Position;PC->bHavePredictedPose=true;
        Pawn->SetActorLocationAndRotation(Self.Position.ToUnrealLocation(100)+FVector(0,0,Root->GetScaledCapsuleHalfHeight()),Self.Position.ToUnrealQuat());
        TestTrue(TEXT("Token is picked up rather than used directly"),Token.IsWorldLootable());
        TestTrue(TEXT("Pickup begins outside the client's default use radius"),
            PC->GetUseCylinderDistanceCm(Token,Self.Position.ToUnrealLocation(100),Origin)>Token.UseRadius*100);
        Client->SelectObject(Token.Guid);Session->CachedC2SPackets.Reset();PC->InteractWithSelectedObject();
        TestTrue(TEXT("Keyboard hand/F requests pickup immediately, before walking"),Session->PendingInventoryTransfers.Contains(Token.Guid));
        TestFalse(TEXT("F waits for server-directed movement instead of inventing an approach"),PC->bServerMoveToActive);
        TestEqual(TEXT("F sends one retail pickup action"),CountAction(ACEGameAction::PutItemInContainer),1);
        TestEqual(TEXT("F does not send Use for a non-usable token"),CountAction(ACEGameAction::Use),0);
        for(const auto& Packet:Session->CachedC2SPackets)
        {
            FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);
            if(R.CanRead(16)&&R.ReadUInt32()==ACEGameAction::PutItemInContainer)
            {
                TestEqual(TEXT("Pickup payload keeps selected token"),R.ReadUInt32(),uint32(Token.Guid));
                TestEqual(TEXT("Pickup payload targets the player's pack"),R.ReadUInt32(),uint32(Self.Guid));
                TestEqual(TEXT("Pickup payload uses a valid inventory placement"),R.ReadInt32(),0);
            }
        }
        PC->InteractWithSelectedObject();
        TestEqual(TEXT("Repeated F while awaiting confirmation does not duplicate pickup"),CountAction(ACEGameAction::PutItemInContainer),1);
        // Servers may supply a different range. Replay the retail MoveTo packet;
        // arrival must only report movement, never reissue/cancel the inventory action.
        FACEBinaryWriter Move;Move.WriteUInt32(Self.Guid);Move.WriteUInt16(0);Move.WriteUInt16(Guid&0xFFFF);Move.WriteUInt16(0);Move.WriteUInt8(0);Move.Align();
        Move.WriteUInt8(6);Move.WriteUInt8(0);Move.WriteUInt16(0);Move.WriteUInt32(Token.Guid);
        Move.WriteUInt32(Token.Position.CellId);Move.WriteFloat(At.X);Move.WriteFloat(At.Y);Move.WriteFloat(At.Z);
        Move.WriteUInt32(0x400);Move.WriteFloat(1.2f);Move.WriteFloat(0);Move.WriteFloat(15);Move.WriteFloat(1);Move.WriteFloat(15);Move.WriteFloat(0);Move.WriteFloat(1);
        FACEBinaryReader MoveReader(Move.GetData());Session->HandleUpdateMotion(MoveReader);
        TestEqual(TEXT("Pickup follows the server-supplied approach distance"),PC->ServerMoveToDistance,1.2f);
        for(int32 Frame=0;Frame<300&&PC->UseApproachPhase!=AACEPlayerController::EACEUseApproachPhase::Holding;++Frame)PC->PlayerTick(1.f/60);
        TestEqual(TEXT("Server approach does not replay pickup"),CountAction(ACEGameAction::PutItemInContainer),1);
        TestEqual(TEXT("Server approach never sends direct Use"),CountAction(ACEGameAction::Use),0);
        TestEqual(TEXT("Token stays in the world until server confirmation"),Session->WorldObjects[Token.Guid].ContainerId,0);
        if(Guid==0x71003001)
        {
            // This start is behind obstructing furniture. A real collision
            // must still cancel movement, without treating the item as picked up.
            TestFalse(TEXT("Blocked approach terminates instead of running through furniture"),PC->bServerMoveToActive);
            FACEBinaryWriter Failure;Failure.WriteUInt32(Token.Guid);Failure.WriteUInt32(0);
            FACEBinaryReader FailureReader(Failure.GetData());Session->HandleInventoryServerSaveFailed(FailureReader);
            TestFalse(TEXT("Pickup failure releases its pending request immediately"),Session->IsInventoryTransferPending());
            TestEqual(TEXT("Rejected pickup retains world ownership"),Session->WorldObjects[Token.Guid].ContainerId,0);
            PC->InteractWithSelectedObject();
            TestEqual(TEXT("Player may retry F after the server rejects pickup"),CountAction(ACEGameAction::PutItemInContainer),2);
        }
        else
        {
            TestTrue(TEXT("Server-directed pickup reaches the table from the reported side"),PC->UseApproachPhase==AACEPlayerController::EACEUseApproachPhase::Holding);
            TestEqual(TEXT("Pickup approach cannot cancel the server inventory chain with MoveToState"),CountAction(ACEGameAction::MoveToState),0);
            // A pickup has an inventory ACK, not the UseDone gate. Include the
            // arrival and idle transition while that confirmation is delayed.
            for(int32 Frame=0;Frame<120;++Frame)PC->PlayerTick(1.f/60);
            TestEqual(TEXT("Waiting for pickup confirmation never sends an artificial stop"),CountAction(ACEGameAction::MoveToState),0);
            TestEqual(TEXT("One F press remains one pickup through arrival"),CountAction(ACEGameAction::PutItemInContainer),1);
            TestTrue(TEXT("Arrival still reports the final position to the server"),CountAction(ACEGameAction::AutonomousPosition)>0);
            if(Guid==0x71003003)
            {
                // Explicit movement must still take control from the server.
                FACEObjectMotionState Approach;Approach.MovementType=6;Approach.MoveToTargetGuid=Token.Guid;Approach.MoveToDistance=1.2f;
                PC->HandleMotionUpdate(Self.Guid,Approach);
                const FKey ForwardKey=ACEInputBindings::Get(EKeys::W,0).Key;
                PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(ForwardKey,IE_Pressed,1.f));
                PC->PlayerInput->ProcessInputStack({},1.f/60,false);
                PC->MovementKeyPressOrder.Add(ForwardKey,++PC->MovementKeySequence);
                const int32 BeforeManual=CountAction(ACEGameAction::MoveToState);
                PC->PlayerTick(1.f/60);
                TestFalse(TEXT("Manual movement cancels a pending pickup approach"),PC->bServerMoveToActive);
                TestTrue(TEXT("Manual cancellation still sends its movement command"),CountAction(ACEGameAction::MoveToState)>BeforeManual);
                PC->PlayerInput->FlushPressedKeys();PC->MovementKeyPressOrder.Reset();
            }
        }
        FACEBinaryWriter Ack;Ack.WriteUInt32(Token.Guid);Ack.WriteUInt32(Self.Guid);Ack.WriteInt32(0);Ack.WriteInt32(0);
        FACEBinaryReader AckReader(Ack.GetData());Session->HandleInventoryPutObjInContainer(AckReader);
        TestEqual(TEXT("Server confirmation places the token in inventory"),Session->WorldObjects[Token.Guid].ContainerId,Self.Guid);
        TestFalse(TEXT("Confirmation clears the pending transfer"),Session->IsInventoryTransferPending());
        PC->ClearServerMoveTo();Session->PendingInventoryTransfers.Reset();Session->WorldObjects.Remove(Token.Guid);Entity->Destroy();
    }
    {
        FACEWorldObject Bag;Bag.Guid=0x71003100;Bag.Name=TEXT("Backpack");Bag.ItemType=ACEItemType::Container;
        Bag.ObjectDescriptionFlags=ACEObjectDescFlag::Openable;Bag.ItemUseable=32;Bag.bHasPosition=true;
        Bag.Position=Session->PlayerPosition;Bag.Position.Location.X+=2;
        Session->WorldObjects.Add(Bag.Guid,Bag);Client->SelectObject(Bag.Guid);Session->CachedC2SPackets.Reset();
        PC->PickupSelectedObject();
        TestEqual(TEXT("F picks up a portable openable backpack"),CountAction(ACEGameAction::PutItemInContainer),1);
        TestEqual(TEXT("F does not open a backpack left on the ground"),CountAction(ACEGameAction::Use),0);
        Session->PendingInventoryTransfers.Reset();Session->WorldObjects.Remove(Bag.Guid);
    }
    // Stock Academy tabletop, including its actual authored collision, book
    // scale and use radii. No enlarged reach or location-specific exceptions.
    TArray<AACEEnvCellActor*> Rooms;
    FACEPosition AcademyOrigin;AcademyOrigin.CellId=0x8C04023C;
    for(uint32 Cell:{0x8C04023Cu,0x8C04023Du})
    {
        Dat->GetOrBuildEnvCellMesh(Cell,100);
        auto* Room=World->SpawnActor<AACEEnvCellActor>();
        if(!TestTrue(TEXT("Academy room loads"),Room->LoadEnvCell(Cell,AcademyOrigin.ToUnrealLocation(100),100)))return false;
        Room->EnsureStaticObjectsQueued();Room->SetEnvCellCollisionActive(true);
        Rooms.Add(Room);
    }
    const double FurnitureDeadline=FPlatformTime::Seconds()+20;
    do
    {
        ++GFrameCounter;
        for(auto* Room:Rooms)Room->Tick(.016f);
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        FPlatformProcess::Sleep(.001f);
    }while(Rooms.ContainsByPredicate([](auto* Room){return Room->HasPendingStaticObjects();})&&FPlatformTime::Seconds()<FurnitureDeadline);
    TestFalse(TEXT("Academy table collision is loaded"),Rooms.ContainsByPredicate([](auto* Room){return Room->HasPendingStaticObjects();}));
    const FVector BookPositions[]={FVector(57.5877,-31.2211,.95454),FVector(57.9623,-31.6294,.95454),FVector(58.4045,-32.0273,.95454),FVector(58.8709,-32.4703,.95454)};
    auto* CeilingActor=World->SpawnActor<AActor>();
    auto* Ceiling=NewObject<UBoxComponent>(CeilingActor);CeilingActor->SetRootComponent(Ceiling);
    Ceiling->SetBoxExtent(FVector(300,300,5));Ceiling->SetCollisionResponseToAllChannels(ECR_Block);
    Ceiling->SetCollisionEnabled(ECollisionEnabled::NoCollision);Ceiling->RegisterComponent();
    FACEPosition CeilingPosition=AcademyOrigin;CeilingPosition.Location=FVector(58,-32,2.3);
    CeilingActor->SetActorLocation(CeilingPosition.ToUnrealLocation(100));
    for(int Pass=0;Pass<4;++Pass)
    for(int Index=0;Index<(Pass==3?1:4);++Index)
    {
        const bool Blocked=Pass==3;
        const float TickSeconds=1.f/(Pass==0?30:Pass==2?120:60);
        Ceiling->SetCollisionEnabled(Blocked?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
        FACEWorldObject Book;Book.Guid=0x71003200+Index;Book.Name=TEXT("Academy book");Book.ItemType=ACEItemType::Writable;
        Book.SetupId=0x02000153;Book.Scale=1.22f;Book.UseRadius=Index==1?.6f:.3f;Book.ItemUseable=8;
        Book.bHasPosition=true;Book.Position.CellId=0x8C04023C;Book.Position.Location=BookPositions[Index];
        Session->WorldObjects.Add(Book.Guid,Book);
        Self.Position.CellId=0x8C04023D;Self.Position.Location=FVector(58.469445,-31.069468,0);
        Self.Position.RotationW=.286614f;Self.Position.RotationXYZ=FVector(0,0,.958046);
        Session->PlayerPosition=Self.Position;Session->WorldObjects[Self.Guid]=Self;
        PC->PredictedPose=Self.Position;PC->bHavePredictedPose=true;
        Pawn->SetActorLocationAndRotation(Self.Position.ToUnrealLocation(100)+FVector(0,0,Root->GetScaledCapsuleHalfHeight()),Self.Position.ToUnrealQuat());
        Client->SelectObject(Book.Guid);Session->CachedC2SPackets.Reset();PC->InteractWithSelectedObject();
        FACEObjectMotionState Approach;Approach.MovementType=6;Approach.MoveToTargetGuid=Book.Guid;Approach.MoveToDistance=Book.UseRadius;Approach.MoveToSpeed=1;
        PC->HandleMotionUpdate(Self.Guid,Approach);
        for(int Frame=0;Frame<900&&PC->bServerMoveToActive&&PC->UseApproachPhase!=AACEPlayerController::EACEUseApproachPhase::Holding;++Frame)PC->PlayerTick(TickSeconds);
        const float Gap=PC->GetUseCylinderDistanceCm(Book,PC->PredictedPose.ToUnrealLocation(100),Book.Position.ToUnrealLocation(100));
        AddInfo(FString::Printf(TEXT("Academy pass %d book %d: gap %.3f cm, use %.1f cm, player %s"),Pass,Index,Gap,Book.UseRadius*100,*PC->PredictedPose.Location.ToString()));
        if(Blocked)
        {
            TestTrue(TEXT("Low ceiling prevents the tabletop step"),PC->PredictedPose.Location.Z<.5);
            TestTrue(TEXT("Pickup remains out of range when upper-body clearance is blocked"),Gap>Book.UseRadius*100);
        }
        else
        {
            TestTrue(TEXT("Academy book is reachable at the server's actual use radius"),Gap<=Book.UseRadius*100);
            if(Index!=1)TestTrue(TEXT("Tight-range pickup steps onto the table, not through its side"),PC->PredictedPose.Location.Z>.75);
        }
        TestEqual(TEXT("Academy pickup is requested only once"),CountAction(ACEGameAction::PutItemInContainer),1);
        TestEqual(TEXT("Book remains in the world until server confirmation"),Session->WorldObjects[Book.Guid].ContainerId,0);
        if(!Blocked)
        {
            FACEBinaryWriter Ack;Ack.WriteUInt32(Book.Guid);Ack.WriteUInt32(Self.Guid);Ack.WriteInt32(0);Ack.WriteInt32(0);
            FACEBinaryReader AckReader(Ack.GetData());Session->HandleInventoryPutObjInContainer(AckReader);
            TestEqual(TEXT("Server confirmation completes Academy pickup"),Session->WorldObjects[Book.Guid].ContainerId,Self.Guid);
            TestFalse(TEXT("Book confirmation clears the pending request"),Session->IsInventoryTransferPending());
        }
        PC->ClearServerMoveTo();Session->PendingInventoryTransfers.Reset();Session->WorldObjects.Remove(Book.Guid);
    }
    PC->UnPossess();Pawn->Destroy();PC->Destroy();
    return !HasAnyErrors();
}
#endif
