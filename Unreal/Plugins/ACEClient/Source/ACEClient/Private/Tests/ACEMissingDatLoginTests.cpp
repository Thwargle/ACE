#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACELoginWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Async/TaskGraphInterfaces.h"
#include "Misc/Paths.h"
#include "Dat/ACEDatDatabase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEMissingDatLoginTest,"ACE.RetailParity.MissingDatLogin",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEMissingDatLoginTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
  .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* GI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); GI->Init();
 auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
 Dat->DatDirectory=FPaths::ProjectSavedDir()/TEXT("Automation/MissingDat-")+FGuid::NewGuid().ToString();
 AddExpectedError(TEXT("ACEDat: background index failed"),EAutomationExpectedErrorFlags::Contains,2);
 AddExpectedError(TEXT("ACEDat: file not found:"),EAutomationExpectedErrorFlags::Contains,2);
 auto Wait=[&]()
 {
  const double Deadline=FPlatformTime::Seconds()+10;
  while(Dat->bBackgroundLoadInProgress && FPlatformTime::Seconds()<Deadline)
  { FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread); FPlatformProcess::Sleep(.001f); }
  TestFalse(TEXT("Missing installation completes with an error"),Dat->bBackgroundLoadInProgress);
 };
 Dat->BeginBackgroundLoad(); Wait();
 TestTrue(TEXT("Error identifies the missing file and configured path"),Dat->GetDatLoadError().Contains(Dat->DatDirectory)
  && Dat->GetDatLoadError().Contains(TEXT("client_portal.dat")));
 const int32 Generation=Dat->BackgroundLoadGeneration;
 for(int32 Frame=0;Frame<120;++Frame) Dat->BeginBackgroundLoad();
 TestEqual(TEXT("Tick callers do not repeatedly launch failed indexing"),Dat->BackgroundLoadGeneration,Generation);
 auto* PC=World->SpawnActor<AACEPlayerController>(); PC->Client=GI->GetSubsystem<UACEClientSubsystem>();
 TestFalse(TEXT("Missing files cannot open an empty character screen"),PC->TryPrefetchCharacterSelectAssets());
 TestTrue(TEXT("Character screen exposes the actionable DAT failure"),PC->CharacterSelectLoadError.Contains(TEXT("client_portal.dat")));
 auto* Login=CreateWidget<UACELoginWidget>(GI,UACELoginWidget::StaticClass());
 Login->TakeWidget(); Login->SetVisibility(ESlateVisibility::Visible);
 Login->HandleState(EACESessionState::CharacterSelect);
 Login->HandleCharacters({},TEXT("Test server"));
 TestEqual(TEXT("Auth callbacks preserve the visible loading/error card"),Login->GetVisibility(),ESlateVisibility::Visible);
 Login->SetStatus(PC->CharacterSelectLoadError);
 Login->HandleState(EACESessionState::CharacterSelect);
 FACECharacterInfo Character; Character.CharacterId=1; Character.Name=TEXT("Test character");
 Login->HandleCharacters({Character},TEXT("Test server"));
 TestTrue(TEXT("Loading failure remains readable without DAT fonts"),Login->StatusText && Login->StatusText->GetText().ToString().Contains(TEXT("client_portal.dat")));
 Dat->BeginBackgroundLoad(true);
 TestEqual(TEXT("Explicit login retry permits one new indexing attempt"),Dat->BackgroundLoadGeneration,Generation+1);
 Wait();
 Dat->SetDatDirectory(TEXT("C:/Turbine/Asheron's Call"));
 Dat->BeginBackgroundLoad(); Wait();
 TestTrue(TEXT("Correcting the DAT folder permits recovery without restarting"),Dat->IsDatReady() && Dat->GetDatLoadError().IsEmpty());
 TestTrue(TEXT("Complete installation resolves character selection including packaged UI layouts"),PC->TryPrefetchCharacterSelectAssets());
 // The portal DAT can be intact while an interrupted installer truncates the
 // cell DAT. A failed deferred world index must not trap the player in a tunnel.
 Dat->InstallCellDatBackgroundLoadResult(TUniquePtr<FACEDatDatabase>(),Dat->BackgroundLoadGeneration);
 TestTrue(TEXT("Deferred cell failure identifies the world data file"),Dat->GetDatLoadError().Contains(TEXT("client_cell_1.dat")));
 PC->LoginWidget=Login; PC->bEnterWorldLoading=true;
 TestTrue(TEXT("Missing cell data exits portal wait immediately"),PC->TickWorldEntryRecovery(.016f,TEXT("cell-collision")));
 TestFalse(TEXT("Missing files never trigger a pointless lifestone recall"),PC->bWorldEntryRecoveryAttempted);
 TestFalse(TEXT("Failed world loading is no longer active"),PC->bEnterWorldLoading);
 TestTrue(TEXT("Missing cell data is explained in the login panel"),Login->StatusText->GetText().ToString().Contains(TEXT("client_cell_1.dat")));
 GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
 return !HasAnyErrors();
}
#endif
