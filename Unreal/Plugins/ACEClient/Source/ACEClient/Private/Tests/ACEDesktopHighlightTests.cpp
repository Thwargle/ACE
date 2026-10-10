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
