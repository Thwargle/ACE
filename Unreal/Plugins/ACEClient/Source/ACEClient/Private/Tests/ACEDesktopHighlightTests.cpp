#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEPlayerController.h"
#include "ACEWorldPresenterComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACECharacterCreation.h"
#include "ACEEnvCellActor.h"
#include "ACERuntimeOptions.h"
#include "ACECreatureFixtures.h"
#include "Components/BoxComponent.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "InputKeyEventArgs.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Slate/SceneViewport.h"
#include "Slate/SGameLayerManager.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUIResourceResolver.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Protocol/ACEBinaryReader.h"
#include "Protocol/ACEBinaryWriter.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEDesktopHighlightTest,"ACE.Input.DesktopHighlights",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEDesktopHighlightTest::RunTest(const FString&)
{
 TGuardValue<FString> Settings(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/HighlightPreferences.ini"));
 FConfigFile Config; Config.NoSave=true; GConfig->SetFile(GGameUserSettingsIni,&Config);
 TestEqual(TEXT("Object glow defaults on"),ACERuntimeOptions::Get(TEXT("ObjectGlow")),1.f);
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* GI=NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
 GI->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(GI);
 ON_SCOPE_EXIT {GI->Shutdown();World->DestroyWorld(false);};
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 if(!TestTrue(TEXT("DAT opens"),Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))return false;
 World->SetGameMode(FURL(nullptr,TEXT("/Game/Test?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
 auto* GM=World->GetAuthGameMode(); if(!TestNotNull(TEXT("Game mode"),GM))return false;
 auto* Presenter=NewObject<UACEWorldPresenterComponent>(GM);GM->AddInstanceComponent(Presenter);
 auto* PC=World->SpawnActor<AACEPlayerController>();World->AddController(PC);
 auto* Client=GI->GetSubsystem<UACEClientSubsystem>(); auto Session=Client->GetSession();
 PC->Client=Client; Session->State=EACESessionState::InWorld;Session->PlayerGuid=1234;
 PC->PlayerInput=NewObject<UPlayerInput>(PC);PC->bMouseLookActive=true;
 FACEWorldObject Object;Object.Guid=555;Object.Name=TEXT("Carenzi");Object.ItemType=ACEItemType::Creature;
 Object.SetupId=ACECreatureFixtures::Models[0].Setup;Object.MotionTableId=ACECreatureFixtures::Models[0].Motion;
 Object.Scale=1.f;Object.bHasPosition=true;Object.Position.CellId=0x7D640001;
 Session->WorldObjects.Add(Object.Guid,Object);
 auto* Entity=World->SpawnActor<AACEWorldEntityActor>();Entity->InitializeFromObject(Object,100,true);Entity->SetActorLocation({1000,0,0});Presenter->Spawned.Add(Object.Guid,Entity);
 auto* View=World->SpawnActor<AActor>(); auto* Root=NewObject<USceneComponent>(View);View->SetRootComponent(Root);Root->RegisterComponent();
 View->SetActorLocation({0,0,25});
 PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();PC->PlayerCameraManager->InitializeFor(PC);PC->SetViewTarget(View);
 bool HitSelf=false;
 if(!TestTrue(TEXT("Mouselook aims from view without any mouse coordinates"),PC->PickWorldPointer(HitSelf)==Entity))return false;
 {
  // Simulate a viewport whose last mouse event was the inventory press. The
  // Slate cursor continues moving under UI capture, including DPI/window offsets.
  class FCapturedViewport : public FSceneViewport
  {
  public:
   FCapturedViewport(const TSharedPtr<SViewport>& Widget):FSceneViewport(Widget){}
   FIntPoint GetSizeXY() const override {return FIntPoint(1600,900);}
   void GetMousePos(FIntPoint& Out,bool Local=true) override {Out=FIntPoint(800,450);}
  };
  class FPointerLayers : public SGameLayerManager
  {
  public:
   FGeometry Geometry;
   FGeometry GetViewportWidgetHostGeometry() const override {return Geometry;}
   bool CustomPrepass(float) override {return true;}
  };
  auto& Slate=FSlateApplication::Get();const FVector2D SavedCursor=Slate.GetCursorPos();
  auto* Context=GI->GetWorldContext();const auto SavedViewport=Context->GameViewport;
  auto* Viewport=NewObject<UGameViewportClient>(GEngine);Context->GameViewport=Viewport;
  auto* Player=NewObject<ULocalPlayer>(GEngine);Player->ViewportClient=Viewport;PC->Player=Player;Player->PlayerController=PC;
  Player->Origin=FVector2D::ZeroVector;Player->Size=FVector2D(1,1);
  const auto Widget=SNew(SViewport);const auto Scene=MakeShared<FCapturedViewport>(Widget);Viewport->Viewport=&Scene.Get();
  // Inject deterministic host geometry without creating a second game window.
  PRAGMA_DISABLE_DEPRECATION_WARNINGS
  const auto Layers=SNew(FPointerLayers);Viewport->SetGameLayerManager(Layers);
  PRAGMA_ENABLE_DEPRECATION_WARNINGS
  ON_SCOPE_EXIT {PC->Player=nullptr;Context->GameViewport=SavedViewport;Viewport->Viewport=nullptr;Slate.SetCursorPos(SavedCursor);PC->bMouseLookActive=true;};
  PC->bMouseLookActive=false;
  {
   // Retail world-drop path, including the main backpack's player GUID.
   // The closed shop must receive Use before its reply can populate Sell.
   const auto SavedObjects=Session->WorldObjects;
   ON_SCOPE_EXIT {Session->WorldObjects=SavedObjects;Session->OpenVendorGuid=0;PC->EndUseApproach();Session->bUseBusy=false;};
   auto* Sockets=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
   Session->SocketC2S=Sockets->CreateSocket(NAME_DGram,TEXT("World vendor drag"),false);
   Session->ServerC2SAddr=Sockets->CreateInternetAddr();bool AddressValid=false;
   Session->ServerC2SAddr->SetIp(TEXT("127.0.0.1"),AddressValid);Session->ServerC2SAddr->SetPort(9);
   Session->IssacClient=MakeUnique<FACEIsaac>(123u);
   auto* Manager=NewObject<UACEUIElementManager>();Manager->Initialize();
   auto* Canvas=NewObject<UACEUICanvasWidget>();Canvas->Initialize();Canvas->InitializeCanvas(Manager);Canvas->TakeWidget();
   auto* Binder=NewObject<UACEUIGameplayBinder>();Binder->Client=Client;Binder->PlayerController=PC;Binder->Canvas=Canvas;Binder->Manager=Manager;
   Layers->Geometry=FGeometry::MakeRoot(FVector2D(1600,900),FSlateLayoutTransform());
   FACEWorldObject Self;Self.Guid=Session->PlayerGuid;Self.ItemType=ACEItemType::Creature;Self.bIsPlayer=true;
   FACEWorldObject Pack;Pack.Guid=600;Pack.ContainerId=Self.Guid;Pack.ItemType=ACEItemType::Container;Pack.ItemsCapacity=24;
   FACEWorldObject Bread;Bread.Guid=601;Bread.ContainerId=Self.Guid;Bread.ItemType=ACEItemType::Food;Bread.Value=120;Bread.StackSize=12;
   FACEWorldObject Retained=Bread;Retained.Guid=602;Retained.ObjectDescriptionFlags=ACEObjectDescFlag::Retained;
   FACEWorldObject Bound=Bread;Bound.Guid=603;Bound.Attuned=1;
   for(const auto& Item:{Self,Pack,Bread,Retained,Bound})Session->WorldObjects.Add(Item.Guid,Item);
   {
    auto* Resources=NewObject<UACEUIResourceResolver>();Resources->Initialize(Dat);Canvas->SetResourceResolver(Resources);
    FWidgetRenderer Renderer(true,true);auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(1600,900),TF_Bilinear,true);
    const auto MarkerCanvas=Canvas->TakeWidget();
    Renderer.DrawWidget(Target,MarkerCanvas,FVector2D(1600,900),.016f);
    PC->PlayerCameraManager->UpdateCamera(.016f);
    Client->SelectObject(Object.Guid);Binder->RefreshSelectionOverlay();
    TestEqual(TEXT("Other creatures retain the visible selection frame"),Binder->SelectionMarkerBox->GetVisibility(),ESlateVisibility::HitTestInvisible);
    // Use the same visible presentation for self: suppression must depend on
    // identity, not missing geometry or the camera being inside the avatar.
    Presenter->Spawned.Add(Self.Guid,Entity);
    Client->SelectObject(Self.Guid);Binder->RefreshSelectionOverlay();
    TestEqual(TEXT("Self stays selected for targeted actions"),Client->GetSelectedObject().Guid,Self.Guid);
    TestEqual(TEXT("Self has no selection frame"),Binder->SelectionMarkerBox->GetVisibility(),ESlateVisibility::Collapsed);
    Binder->SelectionDirectionArrow->SetVisibility(ESlateVisibility::HitTestInvisible);
    Binder->RefreshSelectionOverlay();
    TestEqual(TEXT("Self clears a previous off-screen selection arrow"),Binder->SelectionDirectionArrow->GetVisibility(),ESlateVisibility::Collapsed);
    Presenter->Spawned.Remove(Self.Guid);Client->SelectObject(0);
   }
   Session->WorldObjects[Object.Guid].ObjectDescriptionFlags|=ACEObjectDescFlag::Vendor;
   auto Actions=[&](uint32 Action)
   {
    int Count=0;for(const auto& Pair:Session->CachedC2SPackets)
    {FACEBinaryReader R(Pair.Value.Payload);R.Skip(24);if(R.CanRead(4)&&R.ReadUInt32()==Action)++Count;}
    return Count;
   };
   auto Drop=[&](int32 Guid,FVector2D Point=FVector2D(800,450))
   {
    Binder->InvDragGuid=Guid;Binder->InvDragAmount=1;Binder->InvDragSourcePack=Self.Guid;
    Binder->bInvDragPending=true;Binder->bInvDragActive=true;
    TestTrue(TEXT("Inventory release consumes the world drop"),Binder->TryFinishInventoryDrag(Point));
   };
   auto Reply=[&](int32 Vendor)
   {
    FACEBinaryWriter W;W.WriteUInt32(Vendor);W.WriteUInt32(ACEItemType::Food);
    W.WriteInt32(-1);W.WriteInt32(-1);W.WriteUInt32(1);W.WriteFloat(1);W.WriteFloat(1);
    W.WriteUInt32(0);W.WriteUInt32(0);W.WriteString16L(TEXT(""));W.WriteUInt32(0);
    FACEBinaryReader R(W.GetData());Session->HandleApproachVendor(R);Binder->HandleVendorOpened(Vendor);Session->bUseBusy=false;
   };
   for(int32 Source:{Self.Guid,Pack.Guid})
   {
    for(int32 Id:{Bread.Guid,Retained.Guid,Bound.Guid})Session->WorldObjects[Id].ContainerId=Source;
    Binder->HideVendorPanel();Session->bUseBusy=false;Session->CachedC2SPackets.Reset();
    Drop(Source);
    TestEqual(TEXT("Closed vendor receives one ordinary Use"),Actions(ACEGameAction::Use),1);
    TestEqual(TEXT("Pending pack retains the world vendor identity"),Binder->PendingVendorSellTargetGuid,Object.Guid);
    TestTrue(TEXT("No cart is staged before the server's vendor reply"),Binder->VendorSellCart.IsEmpty());
    Reply(Object.Guid);
    TestEqual(TEXT("ApproachVendor opens the dropped-on vendor"),Binder->OpenVendorGuid,Object.Guid);
    TestEqual(TEXT("Pack drop selects Sell"),Binder->ActiveVendorPage,2);
    TestEqual(TEXT("Bulk drop excludes the pack, bound and retained items"),Binder->VendorSellCart.Num(),1);
    TestTrue(TEXT("Bulk drop includes the complete stack despite a one-unit selection"),
     Binder->VendorSellCart.ContainsByPredicate([&](const auto& Entry){return Entry.Value==Bread.Guid&&Entry.Key==12;}));
    Reply(Object.Guid);Drop(Source);
    TestEqual(TEXT("Open-vendor drop and repeated reply preserve one cart row"),Binder->VendorSellCart.Num(),1);
    TestEqual(TEXT("An already open vendor needs no second Use"),Actions(ACEGameAction::Use),1);
    TestEqual(TEXT("Drag does not send Sell"),Actions(ACEGameAction::Sell),0);
    TestEqual(TEXT("Drag never sends GiveObject"),Actions(ACEGameAction::GiveObjectRequest),0);
   }
   Binder->HideVendorPanel();Session->bUseBusy=false;Drop(Pack.Guid);Reply(Object.Guid+1);
   TestTrue(TEXT("A different vendor reply cannot inherit the pending pack"),Binder->VendorSellCart.IsEmpty());
   Reply(Object.Guid);TestTrue(TEXT("An unrelated reply retires the old offer"),Binder->VendorSellCart.IsEmpty());
   Binder->HideVendorPanel();Session->bUseBusy=false;Drop(Pack.Guid);Binder->HideVendorPanel();Reply(Object.Guid);
   TestTrue(TEXT("Closing cancels a delayed bulk offer"),Binder->VendorSellCart.IsEmpty());
   Binder->HideVendorPanel();Session->CachedC2SPackets.Reset();Session->bUseBusy=true;Drop(Pack.Guid);
   TestEqual(TEXT("Busy use does not retain an unsent sale"),Binder->PendingVendorSellGuid,0);
   TestEqual(TEXT("Busy use does not send a second Use"),Actions(ACEGameAction::Use),0);
   Session->bUseBusy=false;Drop(Self.Guid,FVector2D(1350,700));
   Session->WorldObjects[Object.Guid].ObjectDescriptionFlags&=~ACEObjectDescFlag::Vendor;Drop(Self.Guid);
   TestTrue(TEXT("Main pack over ground or a non-vendor never moves/gives/drops the player"),Session->CachedC2SPackets.IsEmpty());
   // Apostate Nexus: the deposit is a nonattackable object-shaped creature.
   // An attuned resonator can be handed to it even though ground drop is forbidden.
   FACEWorldObject Deposit=Object;Deposit.WeenieClassId=40809;Deposit.Name=TEXT("Apostate Aetherium Deposit");
   Deposit.SetupId=0x02000B82;Deposit.MotionTableId=0x0900013D;Deposit.PhysicsState=6292504;
   Deposit.ItemUseable=32;Deposit.UseRadius=3;Deposit.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck;
   Deposit.InitialMotionStyle=ACEMotion::StanceNonCombat;Deposit.InitialMotionCommand=ACEMotion::Ready;
   FACECharacterCreation Creation;FString Error;
   TestTrue(TEXT("Apostate deposit clothing decoder loads"),Creation.Load(*Dat->GetPortalDat(),Error));
   TestTrue(TEXT("Apostate deposit uses its stock clothing appearance"),Creation.ApplyClothing(0x10000338,Deposit.SetupId,0,0,Deposit.Appearance));
   Session->WorldObjects[Object.Guid]=Deposit;
   Entity->InitializeFromObject(Deposit,100,true);Entity->SetActorLocation({1000,0,0});
   View->SetActorLocation({0,0,150});PC->PlayerCameraManager->UpdateCamera(.016f);
   Bound.Name=TEXT("Aetheric Resonator");Bound.WeenieClassId=40808;Bound.ItemType=ACEItemType::Misc;
   Bound.StackSize=Bound.MaxStackSize=1;Bound.ContainerId=Self.Guid;Session->WorldObjects[Bound.Guid]=Bound;
   TestFalse(TEXT("Attuned resonator is not eligible for a ground drop"),Bound.CanDropToWorld());
   Session->CachedC2SPackets.Reset();Drop(Bound.Guid);
   TestEqual(TEXT("Dropping the bound resonator onto the deposit sends GiveObjectRequest"),Actions(ACEGameAction::GiveObjectRequest),1);
   TestEqual(TEXT("Quest hand-in never attempts a ground drop"),Actions(ACEGameAction::DropItem),0);
   TestEqual(TEXT("Quest hand-in never substitutes targeted item use"),Actions(ACEGameAction::UseWithTarget),0);
   for(const auto& Pair:Session->CachedC2SPackets)
   {
    FACEBinaryReader R(Pair.Value.Payload);R.Skip(24);if(!R.CanRead(4)||R.ReadUInt32()!=ACEGameAction::GiveObjectRequest)continue;
    TestEqual(TEXT("Hand-in packet targets the deposit"),R.ReadUInt32(),uint32(Deposit.Guid));
    TestEqual(TEXT("Hand-in packet contains the resonator"),R.ReadUInt32(),uint32(Bound.Guid));
    TestEqual(TEXT("Hand-in packet gives one resonator"),R.ReadUInt32(),1u);
   }
   // Exercise the actual final room and screen-to-world drop ray, not just a
   // horizontal ray in an empty world. Camera elevation is not player reach.
   Deposit.Position.CellId=0x7F020107;Deposit.Position.Location=FVector(40,-20,.055);
   Session->WorldObjects[Deposit.Guid]=Deposit;Entity->InitializeFromObject(Deposit,100,true);
   FBox DepositBounds;TestTrue(TEXT("Deposit has posed selection artwork"),Entity->Appearance->GetVisualWorldBounds(DepositBounds));
   TArray<AACEEnvCellActor*> DepositRooms;
   if(const auto Info=Dat->GetLandblockInfo(0x7F020000))for(uint32 I=0;I<Info->NumCells;++I)
   {
    const uint32 Id=0x7F020100+I;Dat->GetOrBuildEnvCellMesh(Id,100);const auto* Cell=Dat->FindEnvCellMesh(Id,100);
    if(!Cell || FVector::DistSquared(FVector(Cell->Origin),Deposit.Position.Location)>FMath::Square(35.))continue;
    auto* Room=World->SpawnActor<AACEEnvCellActor>();
    Room->LoadEnvCell(Id,FACEPosition::AceVectorToUnreal(FVector(127*192,2*192,0),100),100);
    Room->SetEnvCellCollisionActive(true);Room->SetEnvCellHiddenInGame(false);DepositRooms.Add(Room);
   }
   for(float Height:{150.f,300.f})for(float Yaw:{0.f,90.f,180.f,270.f})
   {
    const FVector Eye=Deposit.Position.ToUnrealLocation(100)+FRotator(0,Yaw,0).RotateVector(FVector(250,0,Height));
    View->SetActorLocation(Eye);View->SetActorRotation((DepositBounds.GetCenter()-Eye).Rotation());
    PC->PlayerCameraManager->UpdateCamera(.016f);
    const FString ViewLabel=FString::Printf(TEXT("Final room camera height %.0f yaw %.0f"),Height,Yaw);
    TestEqual(*(ViewLabel+TEXT(" selects the deposit")),PC->PickWorldEntityAtScreenPosition(800,450),Entity);
    PC->EndUseApproach();Session->bUseBusy=false;Session->CachedC2SPackets.Reset();Drop(Bound.Guid);
    TestEqual(*(ViewLabel+TEXT(" sends exactly one Give")),Actions(ACEGameAction::GiveObjectRequest),1);
    TestEqual(*(ViewLabel+TEXT(" never drops the bound item")),Actions(ACEGameAction::DropItem),0);
    // Replay this real client action through the server quest integration test.
    for(const auto& Packet:Session->CachedC2SPackets)
    {
     FACEBinaryReader R(Packet.Value.Payload);R.Skip(24);
     if(!R.CanRead(4)||R.ReadUInt32()!=ACEGameAction::GiveObjectRequest)continue;
     const FString Path=FPaths::ProjectSavedDir()/TEXT("Automation/ApostateGiveObject.bin");
     IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
     TestTrue(TEXT("Client hand-in action saved for server replay"),FFileHelper::SaveArrayToFile(
      TArrayView<const uint8>(Packet.Value.Payload.GetData()+24,Packet.Value.Payload.Num()-24),*Path));
    }
   }
   const FVector DepositOrigin=Deposit.Position.ToUnrealLocation(100);
   TestTrue(TEXT("Standing beside the deposit is within its retail use cylinder"),
    PC->GetUseCylinderDistanceCm(Deposit,DepositOrigin+FVector(250,0,0),DepositOrigin)<=300.f);
   TestTrue(TEXT("A high ledge is not incorrectly treated as in hand-in range"),
    PC->GetUseCylinderDistanceCm(Deposit,DepositOrigin+FVector(250,0,1200),DepositOrigin)>300.f);
   for(auto* Room:DepositRooms)Room->Destroy();
   View->SetActorLocation({0,0,150});View->SetActorRotation(FRotator::ZeroRotator);PC->PlayerCameraManager->UpdateCamera(.016f);
   PC->EndUseApproach();Session->bUseBusy=false;Session->CachedC2SPackets.Reset();Drop(Bound.Guid,FVector2D(1350,700));
   TestEqual(TEXT("An empty-world miss never gives to the previous recipient"),Actions(ACEGameAction::GiveObjectRequest),0);
   TestEqual(TEXT("An empty-world miss preserves the bound quest item"),Actions(ACEGameAction::DropItem),0);
   Session->WorldObjects[Object.Guid]=Object;Entity->InitializeFromObject(Object,100,true);Entity->SetActorLocation({1000,0,0});
   View->SetActorLocation({0,0,25});PC->PlayerCameraManager->UpdateCamera(.016f);
  }
  for(float Scale:{1.f,1.5f,2.f})
  {
   Layers->Geometry=FGeometry::MakeRoot(FVector2D(1600,900)/Scale,FSlateLayoutTransform(Scale,FVector2D(120,75)));
   for(const FVector2D Pixel:{FVector2D(800,450),FVector2D(1350,700)})
   {
    Slate.SetCursorPos(Layers->Geometry.LocalToAbsolute(Pixel/Scale));
    float X=0,Y=0;TestTrue(TEXT("Live pointer resolves under captured inventory drag"),PC->GetWorldPointerPosition(X,Y));
    TestTrue(TEXT("Hover follows the current cursor in viewport pixels at each DPI"),FVector2D(X,Y).Equals(Pixel,1.f));
    FIntPoint Cached;Scene->GetMousePos(Cached);TestEqual(TEXT("Viewport press cache remains stale during the fixture"),Cached,FIntPoint(800,450));
    TestEqual(TEXT("World hover and release use the same current ray during capture"),PC->PickWorldPointer(HitSelf),PC->PickWorldEntityAtScreenPosition(Pixel.X,Pixel.Y));
    if(Pixel.X==800)
    {
     TestEqual(TEXT("The press position initially points to the fixture creature"),PC->PickWorldPointer(HitSelf),Entity);
     View->SetActorRotation(FRotator(0,90,0));
     TestNull(TEXT("Rotating during a drag releases the old world highlight"),PC->PickWorldPointer(HitSelf));
     View->SetActorRotation(FRotator::ZeroRotator);
    }
    else TestNull(TEXT("Moving a captured cursor off the creature clears its hover"),PC->PickWorldPointer(HitSelf));
   }
   Slate.SetCursorPos(Layers->Geometry.LocalToAbsolute(FVector2D(-20,300)));
   float X=0,Y=0;TestFalse(TEXT("Dragging outside the viewport cannot highlight a world object"),PC->GetWorldPointerPosition(X,Y));
  }
 }
 PC->PollObjectHover();PC->UpdateObjectHighlights(true);
 auto* Appearance=Entity->FindComponentByClass<UACECharacterAppearanceComponent>();
 auto Strength=[&](){auto* Part=Cast<UPrimitiveComponent>(Appearance->GetPartMesh(0));const auto& Data=Part->GetCustomPrimitiveData().Data;return Data.IsValidIndex(1)?Data[1]:0.f;};
 TestEqual(TEXT("Gaze hover uses VR glow strength"),Strength(),.45f);
 auto Click=[&](){PC->PlayerInput->FlushPressedKeys();PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Pressed,1.f));PC->PlayerInput->ProcessInputStack({},.016f,false);PC->PollObjectClick();};
 Click();TestEqual(TEXT("Mouselook click selects gaze target"),Client->GetSelectedObject().Guid,Object.Guid);
 PC->UpdateObjectHighlights(true);TestEqual(TEXT("Selected glow uses VR full strength"),Strength(),1.f);
 {
  FACEWorldObject Self=Object;Self.Guid=Session->PlayerGuid;Self.bIsSelf=true;Self.bIsPlayer=true;
  Session->WorldObjects.Add(Self.Guid,Self);
  auto* Pawn=World->SpawnActor<ACharacter>();Pawn->SetActorLocation({0,500,0});
  auto* SelfAppearance=NewObject<UACECharacterAppearanceComponent>(Pawn);Pawn->AddInstanceComponent(SelfAppearance);SelfAppearance->RegisterComponent();
  if(!TestTrue(TEXT("Local player DAT appearance builds"),SelfAppearance->ApplyWorldObject(Self,100,false)))return false;
  PC->Possess(Pawn);
  ON_SCOPE_EXIT {PC->UnPossess();PC->SetViewTarget(View);Pawn->Destroy();Session->WorldObjects.Remove(Self.Guid);Client->SelectObject(Object.Guid);};
  TestNull(TEXT("Self has no duplicate world presenter actor"),Presenter->FindEntityActor(Self.Guid));
  auto CheckSelf=[&](const TCHAR* Context,float Expected)
  {
   for(int32 P=0;P<SelfAppearance->GetPartCount();++P)
    if(auto* Part=Cast<UPrimitiveComponent>(SelfAppearance->GetPartMesh(P)))
    {
     const auto& Data=Part->GetCustomPrimitiveData().Data;
     TestEqual(Context,Data.IsValidIndex(1)?Data[1]:0.f,Expected);
    }
  };
  const uint64 AppearanceRevision=SelfAppearance->GetAppearanceRevision();
  Client->SelectObject(Self.Guid);PC->UpdateObjectHighlights(true);
  CheckSelf(TEXT("Selected self receives the full object glow on every part"),1.f);
  PC->UpdateObjectHighlights(true,false);
  CheckSelf(TEXT("VR self selection uses the same local body glow"),1.f);
  Client->SelectObject(Object.Guid);PC->UpdateObjectHighlights(true);
  CheckSelf(TEXT("Selecting another object clears the player glow"),0.f);
  TestEqual(TEXT("Other selected actors still glow"),Strength(),1.f);
  Client->SelectObject(Self.Guid);PC->UpdateObjectHighlights(true);
  ACERuntimeOptions::Set(TEXT("ObjectGlow"),0);PC->UpdateObjectHighlights(true);
  CheckSelf(TEXT("Object glow preference clears the selected player too"),0.f);
  ACERuntimeOptions::Set(TEXT("ObjectGlow"),1);PC->UpdateObjectHighlights(true);
  CheckSelf(TEXT("Re-enabling object glow restores self selection"),1.f);
  PC->UpdateObjectHighlights(false);
  CheckSelf(TEXT("Portal and session transitions clear the local glow"),0.f);
  PC->UpdateObjectHighlights(true);PC->UnPossess();PC->UpdateObjectHighlights(true);
  CheckSelf(TEXT("Changing possession clears the old player model"),0.f);
  TestEqual(TEXT("Selecting self never rebuilds its appearance"),SelfAppearance->GetAppearanceRevision(),AppearanceRevision);
 }
 PC->UpdateObjectHighlights(true);TestEqual(TEXT("Other selected actors still glow"),Strength(),1.f);
 Click();TestEqual(TEXT("Double click consumes the interaction pair"),PC->LastLeftClickGuid,0);
 TestTrue(TEXT("Interaction preserves mouselook"),PC->bMouseLookActive);
 {
  // Stock cow WCID 14: Creature + Stuck + Attackable, Remote use (32),
  // setup 02000006 / motion table 0900000D. Its server Use emote is TippedRight.
  // Retail ItemHolder::UseObject checks useability before attackability.
  FACEWorldObject Cow=Object;Cow.WeenieClassId=14;Cow.Name=TEXT("Cow");
  Cow.SetupId=0x02000006;Cow.MotionTableId=0x0900000D;Cow.Scale=.95f;
  Cow.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck|ACEObjectDescFlag::Attackable;
  Cow.ItemUseable=32;Cow.InitialMotionStyle=ACEMotion::StanceNonCombat;Cow.InitialMotionCommand=ACEMotion::Ready;
  Session->WorldObjects[Cow.Guid]=Cow;Entity->InitializeFromObject(Cow,100,true);Entity->SetActorLocation({1000,0,0});
  FBox CowBounds;TestTrue(TEXT("Cow has retail model bounds"),Appearance->GetVisualWorldBounds(CowBounds));
  View->SetActorLocation(CowBounds.GetCenter()-FVector(1000,0,0));PC->PlayerCameraManager->UpdateCamera(.016f);
  auto ResetUse=[&]() {PC->EndUseApproach();Session->bUseBusy=false;Session->CachedC2SPackets.Reset();PC->LastLeftClickGuid=0;};
  auto UseCount=[&]()
  {
   int32 Count=0;for(const auto& Pair:Session->CachedC2SPackets)
   {
    FACEBinaryReader R(Pair.Value.Payload);R.Skip(24);
    if(!R.CanRead(4)||R.ReadUInt32()!=ACEGameAction::Use)continue;
    TestEqual(TEXT("Use request targets the clicked creature"),R.ReadUInt32(),uint32(Cow.Guid));++Count;
   }
   return Count;
  };
  ResetUse();
  Click();TestEqual(TEXT("Single click only selects the cow"),UseCount(),0);
  TestEqual(TEXT("Cow is selected through the world pick"),Client->GetSelectedObject().Guid,Cow.Guid);
  Click();TestEqual(TEXT("Double clicking an attackable usable cow sends one Use"),UseCount(),1);
  TestTrue(TEXT("Cow use waits for the server-controlled approach"),PC->IsUseApproachActive());
  TestEqual(TEXT("Approach retains the cow's advertised use radius"),PC->ServerMoveToDistance,Cow.UseRadius);
  Click();Click();TestEqual(TEXT("Repeated clicks while busy do not duplicate the cow Use"),UseCount(),1);
  ResetUse();PC->InteractWithObject(Cow.Guid);
  TestEqual(TEXT("Interact key/toolbar shares cow Use after the previous use finishes"),UseCount(),1);
  ResetUse();Session->WorldObjects[Cow.Guid].Name=TEXT("Usable creature");
  Session->WorldObjects[Cow.Guid].WeenieClassId=999999;
  Click();Click();TestEqual(TEXT("Use is driven by server flags, not cow name or template ID"),UseCount(),1);
  for(int32 Useable:{0,1,0x00200000})
  {
   ResetUse();Session->WorldObjects[Cow.Guid].ItemUseable=Useable;
   Click();Click();TestEqual(TEXT("Ordinary monsters and target-only creatures do not send Use"),UseCount(),0);
   TestFalse(TEXT("Non-usable monsters do not start an approach"),PC->IsUseApproachActive());
  }
  ResetUse();Session->WorldObjects[Cow.Guid].ItemUseable=32|0x40;
  Click();Click();TestEqual(TEXT("NeverWalk usable creatures still send Use"),UseCount(),1);
  TestFalse(TEXT("NeverWalk does not start a local approach"),PC->IsUseApproachActive());

  // Replay the stock server emote through the world actor and DAT animator.
  // Tipping must animate the actual mesh without being mistaken for death.
  Appearance->SetPreviewCapture(true);Appearance->TickComponent(.05f,LEVELTICK_All,nullptr);
  TArray<FTransform> Standing;
  for(int32 Part=0;Part<Appearance->GetPartCount();++Part)
   Standing.Add(Appearance->GetPartMesh(Part)->GetRelativeTransform());
  FACEObjectMotionState Tip;Tip.CurrentStyle=ACEMotion::StanceNonCombat;Tip.ForwardCommand=ACEMotion::Ready;
  Tip.ActionCommand=0x1000004F;Tip.ActionSpeed=1;Tip.bNewAction=true;Entity->ApplyMotionState(Tip);
  bool Moved=false;
  for(int32 Frame=0;Frame<60;++Frame)
  {
   Appearance->TickComponent(1.f/30.f,LEVELTICK_All,nullptr);
   for(int32 Part=0;Part<Standing.Num();++Part)
    Moved|=!Standing[Part].Equals(Appearance->GetPartMesh(Part)->GetRelativeTransform(),.01f);
  }
  TestTrue(TEXT("Server TippedRight plays the cow's authored DAT animation"),Moved);
  TestFalse(TEXT("Tipping does not mark the cow as dead"),Entity->HasReceivedDeathMotion());
  ResetUse();Session->WorldObjects[Object.Guid]=Object;Entity->InitializeFromObject(Object,100,true);Entity->SetActorLocation({1000,0,0});
  Appearance->SetPreviewCapture(false);View->SetActorLocation({0,0,25});PC->PlayerCameraManager->UpdateCamera(.016f);
 }
 ACERuntimeOptions::Set(TEXT("ObjectGlow"),0);PC->UpdateObjectHighlights(true);
 TestEqual(TEXT("Disabling glow clears existing material highlight"),Strength(),0.f);
 TestTrue(TEXT("Disabling glow retains gaze picking"),PC->PickWorldPointer(HitSelf)==Entity);
 ACERuntimeOptions::Set(TEXT("ObjectGlow"),1);
 View->SetActorRotation(FRotator(0,90,0));PC->PollObjectHover();
 TestFalse(TEXT("Turning camera clears hover"),PC->DesktopHover.IsValid());
 View->SetActorRotation(FRotator::ZeroRotator);
 Entity->ObjectDescriptionFlags|=ACEObjectDescFlag::UiHidden;
 TestNull(TEXT("UiHidden target is not gaze-selectable"),PC->PickWorldPointer(HitSelf));
 PC->UpdateObjectHighlights(true);TestEqual(TEXT("UiHidden selection cannot glow"),Strength(),0.f);
 Entity->ObjectDescriptionFlags&=~ACEObjectDescFlag::UiHidden;
 auto* Wall=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);Box->SetBoxExtent({10,500,500});
 Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_Camera,ECR_Block);Box->RegisterComponent();Wall->SetActorLocation({500,0,0});
 TestNull(TEXT("Walls block gaze selection"),PC->PickWorldPointer(HitSelf));
 PC->UpdateObjectHighlights(false);TestEqual(TEXT("Mode switch clears desktop glow"),Strength(),0.f);
 return true;
}
#endif
