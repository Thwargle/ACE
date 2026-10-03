#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "ACELoginWidget.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Components/EditableTextBox.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Async/TaskGraphInterfaces.h"
#include "Misc/Paths.h"
#include "Dat/ACEDatDatabase.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "ImageUtils.h"
#include "UObject/StrongObjectPtr.h"

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
 auto* LocalPlayer=NewObject<ULocalPlayer>(GEngine);
 PC->SetPlayer(LocalPlayer); World->AddController(PC);
 Login->SetPlayerContext(FLocalPlayerContext(LocalPlayer,World));
 Login->ProfilePath=Dat->DatDirectory/TEXT("test-profile.dat");
 Login->TakeWidget(); Login->SetVisibility(ESlateVisibility::Visible);
 TestEqual(TEXT("First launch without DATs opens the recovery page before authentication"),Login->Pages->GetActiveWidgetIndex(),3);
 TestTrue(TEXT("Startup message names both required files and the searched folder"),
  Login->FileProblem->GetText().ToString().Contains(TEXT("client_portal.dat"))
  && Login->FileProblem->GetText().ToString().Contains(TEXT("client_cell_1.dat"))
  && Login->FileProblem->GetText().ToString().Contains(Dat->DatDirectory));
 if (FApp::CanEverRender())
 {
  const FVector2D Size(1100,900); // Shared launcher surface in the headset.
  FWidgetRenderer Renderer(false,true);
  TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,false));
  const auto Slate=Login->TakeWidget();
  for(int Pass=0;Pass<5;++Pass)
  { Renderer.DrawWidget(Target.Get(),Slate,Size,0.f); FlushRenderingCommands(); Login->NativeTick(Login->GetCachedGeometry(),0.f); }
  const auto& Bounds=Login->FileProblem->GetCachedGeometry();
  TestTrue(TEXT("Missing-data instructions are laid out inside the headset view"),Bounds.GetAbsoluteSize().Y>40
   && Bounds.GetAbsolutePosition().Y>=0 && Bounds.GetAbsolutePosition().Y+Bounds.GetAbsoluteSize().Y<900);
  TArray<FColor> Pixels; FReadSurfaceDataFlags Flags; Flags.SetLinearToGamma(true);
  Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
  TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(1100,900,Pixels,PNG);
  FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Automation/MissingDatLogin.png")));
 }
 Login->RunAction(TEXT("play"));
 const auto StateBeforeLogin=PC->Client->GetSessionState();
 Login->DoLogin();
 TestEqual(TEXT("Launch redirects to Game files even before choosing credentials"),Login->Pages->GetActiveWidgetIndex(),3);
 TestTrue(TEXT("Missing files cannot start a network login"),PC->Client->GetSessionState()==StateBeforeLogin);
 Login->HandleLog(TEXT("Unrelated network status"));
 TestTrue(TEXT("Persistent repair instructions survive routine status messages"),
  Login->FileProblem->GetVisibility()!=ESlateVisibility::Collapsed && Login->FileProblem->GetText().ToString().Contains(TEXT("client_portal.dat")));
 // Presence checks must catch an incomplete installation with only the portal
 // file. The asynchronous index loader separately validates its contents.
 const FString MissingDir=Dat->DatDirectory;
 IFileManager::Get().MakeDirectory(*MissingDir,true);
 const FString PortalStub=MissingDir/TEXT("client_portal.dat");
 FFileHelper::SaveStringToFile(TEXT("presence fixture"),*PortalStub);
 TestFalse(TEXT("Portal-only installation is rejected before entering the world"),Login->ValidateGameFiles());
 TestFalse(TEXT("Portal-only error does not claim the portal file is missing"),Login->FileProblem->GetText().ToString().Contains(TEXT("client_portal.dat")));
 TestTrue(TEXT("Portal-only error identifies the missing cell file"),Login->FileProblem->GetText().ToString().Contains(TEXT("client_cell_1.dat")));
 FFileHelper::SaveStringToFile(TEXT(""),*PortalStub);
 TestFalse(TEXT("Empty DAT files are rejected too"),Login->ValidateGameFiles());
 TestTrue(TEXT("Empty portal file appears in the report"),Login->FileProblem->GetText().ToString().Contains(TEXT("client_portal.dat")));
 IFileManager::Get().Delete(*PortalStub);
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
 Login->DatBox->SetText(FText::FromString(TEXT("C:/Turbine/Asheron's Call")));
 Login->RunAction(TEXT("savedat"));
 TestTrue(TEXT("Saving a complete installation clears the error and allows login"),Login->ValidateGameFiles()
  && Login->FileProblem->GetVisibility()==ESlateVisibility::Collapsed);
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
