#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEPlayerController.h"
#include "ACEWorldPresenterComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACERuntimeOptions.h"
#include "ACECreatureFixtures.h"
#include "Components/BoxComponent.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/PlayerInput.h"
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
   auto* Binder=NewObject<UACEUIGameplayBinder>();Binder->Client=Client;Binder->PlayerController=PC;Binder->Canvas=Canvas;
   Layers->Geometry=FGeometry::MakeRoot(FVector2D(1600,900),FSlateLayoutTransform());
   FACEWorldObject Self;Self.Guid=Session->PlayerGuid;Self.ItemType=ACEItemType::Creature;Self.bIsPlayer=true;
   FACEWorldObject Pack;Pack.Guid=600;Pack.ContainerId=Self.Guid;Pack.ItemType=ACEItemType::Container;Pack.ItemsCapacity=24;
   FACEWorldObject Bread;Bread.Guid=601;Bread.ContainerId=Self.Guid;Bread.ItemType=ACEItemType::Food;Bread.Value=120;Bread.StackSize=12;
   FACEWorldObject Retained=Bread;Retained.Guid=602;Retained.ObjectDescriptionFlags=ACEObjectDescFlag::Retained;
   FACEWorldObject Bound=Bread;Bound.Guid=603;Bound.Attuned=1;
   for(const auto& Item:{Self,Pack,Bread,Retained,Bound})Session->WorldObjects.Add(Item.Guid,Item);
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
 PC->PollObjectHover();PC->UpdateDesktopHighlights(true);
 auto* Appearance=Entity->FindComponentByClass<UACECharacterAppearanceComponent>();
 auto Strength=[&](){auto* Part=Cast<UPrimitiveComponent>(Appearance->GetPartMesh(0));const auto& Data=Part->GetCustomPrimitiveData().Data;return Data.IsValidIndex(1)?Data[1]:0.f;};
 TestEqual(TEXT("Gaze hover uses VR glow strength"),Strength(),.45f);
 auto Click=[&](){PC->PlayerInput->FlushPressedKeys();PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Pressed,1.f));PC->PlayerInput->ProcessInputStack({},.016f,false);PC->PollObjectClick();};
 Click();TestEqual(TEXT("Mouselook click selects gaze target"),Client->GetSelectedObject().Guid,Object.Guid);
 PC->UpdateDesktopHighlights(true);TestEqual(TEXT("Selected glow uses VR full strength"),Strength(),1.f);
 Click();TestEqual(TEXT("Double click consumes the interaction pair"),PC->LastLeftClickGuid,0);
 TestTrue(TEXT("Interaction preserves mouselook"),PC->bMouseLookActive);
 ACERuntimeOptions::Set(TEXT("ObjectGlow"),0);PC->UpdateDesktopHighlights(true);
 TestEqual(TEXT("Disabling glow clears existing material highlight"),Strength(),0.f);
 TestTrue(TEXT("Disabling glow retains gaze picking"),PC->PickWorldPointer(HitSelf)==Entity);
 ACERuntimeOptions::Set(TEXT("ObjectGlow"),1);
 View->SetActorRotation(FRotator(0,90,0));PC->PollObjectHover();
 TestFalse(TEXT("Turning camera clears hover"),PC->DesktopHover.IsValid());
 View->SetActorRotation(FRotator::ZeroRotator);
 Entity->ObjectDescriptionFlags|=ACEObjectDescFlag::UiHidden;
 TestNull(TEXT("UiHidden target is not gaze-selectable"),PC->PickWorldPointer(HitSelf));
 PC->UpdateDesktopHighlights(true);TestEqual(TEXT("UiHidden selection cannot glow"),Strength(),0.f);
 Entity->ObjectDescriptionFlags&=~ACEObjectDescFlag::UiHidden;
 auto* Wall=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);Box->SetBoxExtent({10,500,500});
 Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_Camera,ECR_Block);Box->RegisterComponent();Wall->SetActorLocation({500,0,0});
 TestNull(TEXT("Walls block gaze selection"),PC->PickWorldPointer(HitSelf));
 PC->UpdateDesktopHighlights(false);TestEqual(TEXT("Mode switch clears desktop glow"),Strength(),0.f);
 return true;
}
#endif
