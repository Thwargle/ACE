#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACECameraRetail.h"
#include "ACECameraSettings.h"
#include "ACEKeyboardRouter.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ACELedgeSlide.h"
#include "ACEPlayerController.h"
#include "ACEInputBindings.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "GameFramework/Pawn.h"
#include "Engine/GameInstance.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACECameraEdgeTest,"ACE.RetailParity.CameraAndEdges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACECameraEdgeTest::RunTest(const FString& Parameters)
{
    // Use a separate settings file: this test must not change player preferences.
    TGuardValue<FString> SettingsPath(GGameUserSettingsIni,FPaths::ProjectSavedDir()/TEXT("Automation/CameraFixture.ini"));
    FConfigFile FixtureConfig; FixtureConfig.NoSave=false; FixtureConfig.bCanSaveAllSections=true;
    GConfig->SetFile(GGameUserSettingsIni,&FixtureConfig);
    TestEqual(TEXT("Unset mouse speed uses the faster default"),ACECameraSettings::GetMouseDegreesPerPixel(),0.75f);
    const uint32 Scans[]={0x52,0x4F,0x50,0x51,0x4B,0x4C,0x4D,0x47,0x48,0x49};
    TestEqual(TEXT("Keypad Enter is distinct from chat Enter"),FACEKeyboardRouter::NumpadVirtualKey(0x0D,0x1C,true),uint32(0x1000D));
    TestEqual(TEXT("Main Enter remains Enter"),FACEKeyboardRouter::NumpadVirtualKey(0x0D,0x1C,false),uint32(0x0D));
    const uint32 Navigation[]={0x2D,0x23,0x28,0x22,0x25,0x0C,0x27,0x24,0x26,0x21};
    for (int32 N=0;N<10;++N)
    {
        TestEqual(TEXT("Num Lock off still resolves the physical numpad key"),FACEKeyboardRouter::NumpadVirtualKey(Navigation[N],Scans[N],false),uint32(0x60+N));
        TestEqual(TEXT("Dedicated navigation keys remain unchanged"),FACEKeyboardRouter::NumpadVirtualKey(Navigation[N],Scans[N],true),Navigation[N]);
        TestEqual(TEXT("Num Lock on keeps the same numpad key"),FACEKeyboardRouter::NumpadVirtualKey(0x60+N,Scans[N],false),uint32(0x60+N));
    }
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto* Controller=World->SpawnActor<AACEPlayerController>();
    if (FParse::Param(FCommandLine::Get(),TEXT("RenderOffScreen")))
        TestFalse(TEXT("Offscreen clients do not install a native keyboard handler"),
            FACEKeyboardRouter::Create(Controller,[]{return true;}).IsValid());
    auto* GI=NewObject<UGameInstance>();
    auto* Client=NewObject<UACEClientSubsystem>(GI);
    Client->Session=MakeShared<FACESession>(); Client->Session->State=EACESessionState::InWorld;
    Controller->Client=Client; Controller->bRetailCursorInstalled=true;
    TestTrue(TEXT("Mouse turning defaults on with fresh local settings and retail's off bit"),Client->IsCharacterOptionSet(0x31));
    const uint32 InitialOptions2=Client->GetCharacterOptions2();
    Client->SendSetSingleCharacterOption(0x31,false);
    FConfigFile SavedTurning;SavedTurning.Read(GGameUserSettingsIni);
    bool SavedTurningValue=true;SavedTurning.GetBool(TEXT("ACE.Camera"),TEXT("UseMouseTurning"),SavedTurningValue);
    TestFalse(TEXT("An explicit mouse-turning opt-out is saved"),SavedTurningValue);
    GConfig->SetFile(GGameUserSettingsIni,&SavedTurning);
    TestFalse(TEXT("The opt-out survives settings reload"),Client->IsCharacterOptionSet(0x31));
    Client->SendCharacterOptions(Client->GetCharacterOptions1(),InitialOptions2);
    TestTrue(TEXT("Resetting an options snapshot restores mouse turning too"),Client->IsCharacterOptionSet(0x31));
    Client->SendSetSingleCharacterOption(0x31,false);
    auto* Pawn=World->SpawnActor<APawn>();
    auto* Boom=NewObject<USpringArmComponent>(Pawn);
    Pawn->SetRootComponent(Boom); Boom->RegisterComponent(); Controller->Possess(Pawn);
    Boom->TargetArmLength=300.f;
    Controller->UpdateMouseButtons(true,false,false,false);
    TestFalse(TEXT("Mouse turning OFF never starts orbit or capture"),Controller->bMouseLookActive);
    Controller->ApplyMouseLookDelta(100,100,Boom);
    TestTrue(TEXT("Mouse turning OFF ignores orbit deltas"),Boom->GetRelativeRotation().IsZero());
    TestTrue(TEXT("Right click still identifies when mouse turning is OFF"),Controller->UpdateMouseButtons(false,false,false,false));
    Controller->bInstantMouseLookHeld=true;
    Controller->UpdateMouseButtons(true,false,false,false);
    TestTrue(TEXT("Bound instant mouse look works with the ordinary mouse-turn option off"),Controller->bMouseLookActive);
    Controller->ApplyMouseLookDelta(2,0,Boom);
    TestTrue(TEXT("Instant mouse look applies camera motion"),FMath::Abs(Boom->GetRelativeRotation().Yaw)>1.f);
    Controller->bInstantMouseLookHeld=false;
    Controller->UpdateMouseButtons(false,false,false,false);
    TestFalse(TEXT("Releasing instant mouse look releases capture"),Controller->bMouseLookActive);
    Boom->SetRelativeRotation(FRotator::ZeroRotator);
    Client->SendSetSingleCharacterOption(0x31,true);
    Controller->UpdateMouseButtons(false,true,false,false);
    TestFalse(TEXT("Left mouse alone does not move the player or capture selection clicks"),Controller->bMouseForwardActive || Controller->bMouseLookActive);
    Controller->UpdateMouseButtons(true,false,false,true);
    TestFalse(TEXT("A right press over the UI cannot start orbit"),Controller->bMouseLookActive);
    Controller->UpdateMouseButtons(true,true,false,false);
    TestFalse(TEXT("Dragging from the UI into the world cannot start mouse movement"),Controller->bMouseForwardActive);
    TestFalse(TEXT("Releasing a UI gesture does not identify a world object"),Controller->UpdateMouseButtons(false,false,false,false));
    Controller->UpdateMouseButtons(true,false,false,false);
    TestTrue(TEXT("Enabled right mouse starts captured orbit"),Controller->bMouseLookActive && Controller->bMouseLookUsesCapture);
    Boom->SetRelativeRotation(FRotator(-15,90,0));
    Controller->ApplyMouseLookDelta(2,0,Boom);
    TestTrue(TEXT("Default mouse speed turns 1.5 degrees for two pixels"),FMath::IsNearlyEqual(Boom->GetRelativeRotation().Yaw,91.5,0.001));
    Controller->ApplyMouseLookDelta(0,0,Boom);
    Controller->ApplyMouseLookDelta(2,0,Boom);
    TestTrue(TEXT("Pausing between short movements does not reintroduce a dead zone"),Boom->GetRelativeRotation().Yaw>90.8);
    Controller->ApplyMouseLookDelta(0,20,Boom);
    TestTrue(TEXT("Moving the mouse down tilts the view down"),Boom->GetRelativeRotation().Pitch<-19);
    ACECameraSettings::SetMouseTurnSpeed(2.f);
    Boom->SetRelativeRotation(FRotator(-15,90,0));
    Controller->ApplyMouseLookDelta(10,10,Boom);
    TestTrue(TEXT("Changing speed immediately scales yaw"),FMath::IsNearlyEqual(Boom->GetRelativeRotation().Yaw,105.0,0.001));
    TestTrue(TEXT("Changing speed immediately scales pitch"),FMath::IsNearlyEqual(Boom->GetRelativeRotation().Pitch,-30.0,0.001));
    FConfigFile SavedCamera; SavedCamera.Read(GGameUserSettingsIni);
    float SavedSpeed=0; SavedCamera.GetFloat(TEXT("ACE.Camera"),TEXT("MouseTurnSpeed"),SavedSpeed);
    TestEqual(TEXT("Mouse speed is written to disk"),SavedSpeed,2.f);
    GConfig->SetFile(GGameUserSettingsIni,&SavedCamera);
    TestEqual(TEXT("Mouse speed survives a settings reload"),ACECameraSettings::GetMouseTurnSpeed(),2.f);
    ACECameraSettings::SetMouseTurnSpeed(999.f);
    TestEqual(TEXT("Mouse speed has a safe upper bound"),ACECameraSettings::GetMouseTurnSpeed(),3.f);
    ACECameraSettings::SetMouseTurnSpeed(-1.f);
    TestEqual(TEXT("Mouse speed has a positive lower bound"),ACECameraSettings::GetMouseTurnSpeed(),0.1f);
    ACECameraSettings::SetMouseTurnSpeed(ACECameraSettings::DefaultMouseTurnSpeed);
    for(bool X:{false,true}) for(bool Y:{false,true})
    {
        ACECameraSettings::SetMouseInversion(X,Y);
        Boom->SetRelativeRotation(FRotator(-20,0,0));
        Controller->ApplyMouseLookDelta(8,8,Boom);
        TestTrue(TEXT("Mouse horizontal inversion is independent"),FMath::IsNearlyEqual(Boom->GetRelativeRotation().Yaw,X?-6.:6.,.01));
        TestTrue(TEXT("Mouse vertical inversion is independent"),FMath::IsNearlyEqual(Boom->GetRelativeRotation().Pitch,Y?-14.:-26.,.01));
        FConfigFile Reload;Reload.Read(GGameUserSettingsIni);bool SavedX=false,SavedY=false;
        Reload.GetBool(TEXT("ACE.Camera"),TEXT("InvertMouseX"),SavedX);Reload.GetBool(TEXT("ACE.Camera"),TEXT("InvertMouseY"),SavedY);
        TestTrue(TEXT("Both axis preferences persist independently"),SavedX==X && SavedY==Y);
    }
    ACECameraSettings::SetMouseInversion(false,false);
    Controller->ApplyMouseLookDelta(0,10000,Boom);
    TestTrue(TEXT("Orbit pitch remains clamped"),FMath::IsNearlyEqual(Boom->GetRelativeRotation().Pitch,-89.0,0.01));
    Controller->UpdateMouseButtons(true,true,false,false);
    TestTrue(TEXT("Both buttons advance the player"),Controller->bMouseForwardActive);
    Controller->UpdateMouseButtons(true,false,false,false);
    TestFalse(TEXT("Releasing left stops advancing while right continues orbit"),Controller->bMouseForwardActive);
    TestTrue(TEXT("Releasing left leaves orbit active"),Controller->bMouseLookActive);
    TestFalse(TEXT("Releasing a movement gesture never identifies"),Controller->UpdateMouseButtons(false,false,false,false));
    Controller->UpdateMouseButtons(true,true,false,false);
    TestFalse(TEXT("Releasing right first also never identifies"),Controller->UpdateMouseButtons(false,true,false,false));
    TestFalse(TEXT("Releasing right stops mouse movement"),Controller->bMouseForwardActive);
    TestTrue(TEXT("Releasing right keeps held left mouse in free orbit"),Controller->bMouseLookActive && !Controller->bMouseForwardActive);
    Controller->UpdateMouseButtons(false,false,false,false);
    TestTrue(TEXT("Releasing both orbit buttons restores the cursor"),Controller->bShowMouseCursor && !Controller->bMouseLookUsesCapture);
    Controller->UpdateMouseButtons(true,true,false,false);
    Client->SendSetSingleCharacterOption(0x31,false);
    Controller->UpdateMouseButtons(true,true,false,false);
    TestFalse(TEXT("Disabling the option cancels active orbit and forward movement"),Controller->bMouseLookActive || Controller->bMouseForwardActive);
    Client->SendSetSingleCharacterOption(0x31,true);
    Controller->UpdateMouseButtons(true,true,false,false);
    TestFalse(TEXT("Re-enabling while held requires a fresh world press"),Controller->bMouseLookActive);
    TestFalse(TEXT("A cancelled gesture cannot become an inspect click"),Controller->UpdateMouseButtons(false,false,false,false));
    Controller->UpdateMouseButtons(true,true,false,false);
    Controller->UpdateMouseButtons(true,true,true,false);
    TestFalse(TEXT("Chat/keybind focus cancels mouse controls"),Controller->bMouseLookActive || Controller->bMouseForwardActive);
    Controller->UpdateMouseButtons(false,false,false,false);
    Controller->UpdateMouseButtons(true,false,false,false);
    TestTrue(TEXT("An ordinary enabled RMB click still identifies"),Controller->UpdateMouseButtons(false,false,false,false));
    // Exercise wheel math without depending on the physical desktop cursor:
    // headless automation can run with that cursor over the editor's own UI.
    if (Controller->IsMouseOverBlockingUI())
    {
        Controller->ApplyCameraWheelZoom(1);
        TestEqual(TEXT("Wheel over an interface leaves camera distance alone"),Boom->TargetArmLength,300.f);
    }
    for (bool bMouseTurning : {false,true})
    {
        Client->SendSetSingleCharacterOption(0x31,bMouseTurning);
        Controller->AdjustMouseCameraDistance(1);
        const float ZoomedIn=Boom->TargetArmLength;
        TestTrue(TEXT("Wheel zooms in with mouse turning either OFF or ON"),ZoomedIn<300.f);
        Controller->AdjustMouseCameraDistance(-1);
        TestTrue(TEXT("Wheel zooms out with mouse turning either OFF or ON"),Boom->TargetArmLength>ZoomedIn);
        const float BeforeZero=Boom->TargetArmLength;
        Controller->AdjustMouseCameraDistance(0);
        TestEqual(TEXT("Zero wheel input does not move the camera"),Boom->TargetArmLength,BeforeZero);
        ACEInputBindings::BeginEdit();
        Controller->ApplyCameraWheelZoom(1);
        TestEqual(TEXT("Key binding focus still blocks zoom in both turning modes"),Boom->TargetArmLength,BeforeZero);
        ACEInputBindings::Cancel();
    }
    Controller->ResetCameraToRetailDefaults(Boom);
    const float SavedArm = Boom->TargetArmLength;
    const FRotator SavedRotation = Boom->GetRelativeRotation();
    Controller->SetCameraMapMode(Boom,true);
    TestEqual(TEXT("Retail map camera is 450 game units away"),Boom->TargetArmLength,450.f*Controller->GetCameraScaleCm());
    TestTrue(TEXT("Map camera looks down with retail's direction"),FMath::IsNearlyEqual(Boom->GetRelativeRotation().Pitch,ACECameraRetail::LookDownPitchDegrees(),.01));
    TestFalse(TEXT("Map camera is not pulled into intervening roofs"),Boom->bDoCollisionTest);
    Controller->SyncUserCameraArmLength(Boom);
    TestEqual(TEXT("Following ticks cannot clamp map mode to ordinary zoom"),Boom->TargetArmLength,450.f*Controller->GetCameraScaleCm());
    Controller->SetCameraMapMode(Boom,false);
    TestEqual(TEXT("Exiting map mode restores manual camera distance"),Boom->TargetArmLength,SavedArm);
    TestTrue(TEXT("Exiting map mode restores angle and collision"),Boom->GetRelativeRotation().Equals(SavedRotation,.01) && Boom->bDoCollisionTest);
    Controller->SetCameraMapMode(Boom,true);
    Controller->ResetCameraToRetailDefaults(Boom);
    TestFalse(TEXT("Reset camera leaves map mode"),Controller->bCameraMapMode);
    Boom->bDoCollisionTest=false; Boom->bEnableCameraLag=true;
    Boom->bEnableCameraRotationLag=false; Boom->CameraLagSpeed=ACECameraRetail::TranslationLagSpeed;
    Boom->SetWorldRotation(FRotator::ZeroRotator); Boom->TargetArmLength=300.f;
    Boom->bEnableCameraLag=false; Boom->SetWorldLocation(FVector::ZeroVector);
    Boom->TickComponent(1.f/60,LEVELTICK_All,nullptr); Boom->bEnableCameraLag=true;
    for(int32 I=1;I<=120;++I) { Boom->SetWorldLocation(FVector(I*10.f,0,0)); Boom->TickComponent(1.f/60,LEVELTICK_All,nullptr); }
    const float RunningDistance=1200.f-Boom->GetSocketLocation(USpringArmComponent::SocketName).X;
    TestTrue(TEXT("Running gives the retail translation-stiffness pullback"),RunningDistance>400 && RunningDistance<450);
    for(int32 I=0;I<120;++I) Boom->TickComponent(1.f/60,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Stopping restores the chosen camera distance"),FMath::Abs(1200.f-Boom->GetSocketLocation(USpringArmComponent::SocketName).X-300.f)<.1f);
    TestEqual(TEXT("Movement lag does not change manual zoom"),Boom->TargetArmLength,300.f);
    Controller->Client=nullptr;
    World->DestroyWorld(false);
    for (float Yaw : {0.f, 45.f, 180.f, 270.f, 450.f})
    {
        const float Turn=ACECameraRetail::FollowTurnDegrees(Yaw,1.f/60,true,true);
        TestTrue(TEXT("Mouse forward keeps the player's back to the camera immediately"),
            FMath::IsNearlyZero(FMath::UnwindDegrees(Yaw-Turn-90.f)));
    }
    TestEqual(TEXT("Orbit at rest does not turn the character"),ACECameraRetail::FollowTurnDegrees(180,1.f/60,false),0.f);
    float Yaw60=180, Yaw30=180;
    for (int32 I=0; I<60; ++I) Yaw60-=ACECameraRetail::FollowTurnDegrees(Yaw60,1.f/60,true);
    for (int32 I=0; I<30; ++I) Yaw30-=ACECameraRetail::FollowTurnDegrees(Yaw30,1.f/30,true);
    TestTrue(TEXT("Camera follow converges behind the character"),FMath::Abs(Yaw60-90)<0.1f);
    TestTrue(TEXT("Camera follow is frame-rate independent"),FMath::Abs(Yaw60-Yaw30)<0.001f);
    auto Support=[](const FVector2D& P) { return P.X <= 0; };
    const FVector2D Straight=ACELedgeSlide::Resolve(FVector2D(-1,0),FVector2D(10,0),Support);
    TestTrue(TEXT("Head-on edge input does not bounce sideways"),Straight.X<=0 && FMath::Abs(Straight.Y)<0.001);
    FVector2D At(-1,0);
    for (int32 I=0; I<100; ++I)
    {
        const FVector2D Next=ACELedgeSlide::Resolve(At,FVector2D(5,5),Support);
        TestTrue(TEXT("Diagonal ledge slide stays supported and never reverses"),Next.X<=0 && Next.Y>=At.Y);
        At=Next;
    }
    TestTrue(TEXT("Diagonal input makes progress along the edge"),At.Y>300);
    return true;
}
#endif
