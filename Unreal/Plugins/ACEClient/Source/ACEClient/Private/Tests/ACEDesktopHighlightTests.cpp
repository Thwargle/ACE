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
