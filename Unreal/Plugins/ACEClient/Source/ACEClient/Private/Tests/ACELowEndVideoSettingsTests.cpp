#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "ACERuntimeOptions.h"
#include "ACEDatSubsystem.h"
#include "ACEEffectLightSubsystem.h"
#include "ACEEnvCellActor.h"
#include "ACESkyDomeActor.h"
#include "UI/ACEVideoSettingsWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "ProceduralMeshComponent.h"
#include "Scalability.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELowEndVideoSettingsTest,"ACE.Rendering.LowEndVideoSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACELowEndVideoSettingsTest::RunTest(const FString&)
{
    auto* Settings=UGameUserSettings::GetGameUserSettings();
    if(!TestNotNull(TEXT("Game settings available"),Settings)) return false;
    const auto PreviousQuality=Settings->ScalabilityQuality;
    const auto PreviousAppliedQuality=Scalability::GetQualityLevels();
    const FString PreviousIni=GGameUserSettingsIni;
    GGameUserSettingsIni=FPaths::ProjectSavedDir()/TEXT("Automation/LowEndVideoPreferences.ini");
    FConfigFile Config; Config.NoSave=false; Config.bCanSaveAllSections=true;
    GConfig->SetFile(GGameUserSettingsIni,&Config);
    GConfig->EmptySection(TEXT("ACE.Presentation"),GGameUserSettingsIni);
    ON_SCOPE_EXIT {
        Settings->ScalabilityQuality=PreviousQuality;
        Scalability::SetQualityLevels(PreviousAppliedQuality);
        IConsoleManager::Get().CallAllConsoleVariableSinks();
        GGameUserSettingsIni=PreviousIni; ACERuntimeOptions::Apply();
    };
    Settings->SetOverallScalabilityLevel(3);
    ACERuntimeOptions::Apply();
    TestEqual(TEXT("Existing installs retain full effect lighting by default"),ACERuntimeOptions::DynamicLightingQuality(),2);

    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); Context.OwningGameInstance=GI;
    GI->OnWorldChanged(nullptr,World); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    FString DatDirectory=TEXT("C:/Turbine/Asheron's Call"); FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),DatDirectory);
    if(!TestTrue(TEXT("Real doorway DAT loads"),Dat->LoadDatDirectory(DatDirectory))) return false;
    Dat->GetOrBuildEnvCellMesh(0x7D64010C,100);
    auto* Room=World->SpawnActor<AACEEnvCellActor>();
    if(!TestTrue(TEXT("Real entrance room loads"),Room->LoadEnvCell(0x7D64010C,FVector::ZeroVector,100))) return false;
    auto DoorLights=[&]() {
        TArray<UPointLightComponent*> Lights;
        for(TObjectIterator<UPointLightComponent> It;It;++It)
            if(IsValid(*It) && It->IsRegistered() && It->GetWorld()==World && It->GetOwner()
                && It->GetOwner()->ActorHasTag(TEXT("ACEDoorwaySpill"))) Lights.Add(*It);
        return Lights;
    };
    if(!TestTrue(TEXT("Doorway has actual scene lights"),!DoorLights().IsEmpty())) return false;

    auto* Camera=World->SpawnActor<ACameraActor>();
    auto* PC=World->SpawnActor<APlayerController>(); World->AddController(PC);
    if(!PC->PlayerCameraManager) { PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>(); PC->PlayerCameraManager->InitializeFor(PC); }
    PC->SetViewTarget(Camera); PC->PlayerCameraManager->UpdateCamera(0.f);
    auto* Lights=World->GetSubsystem<UACEEffectLightSubsystem>();
    TArray<USceneComponent*> Sources;
    for(int32 I=0;I<10;++I)
    {
        auto* Owner=World->SpawnActor<AActor>(); auto* Source=NewObject<USceneComponent>(Owner);
        Owner->SetRootComponent(Source); Source->RegisterComponent(); Owner->SetActorLocation(FVector(100,I*20,100));
        Sources.Add(Source);
    }
    auto Submit=[&]() {
        for(auto* Source:Sources) {
            FACEEffectLightRequest Request; Request.EmitterId=1; Request.Position=Source->GetComponentLocation();
            Lights->SubmitParticles(Source,MakeArrayView(&Request,1));
        }
        Lights->RefreshLights(.05f);
    };
    auto* Sky=World->SpawnActor<AACESkyDomeActor>();
    FACEDatSkyTimeOfDay Key; Key.DirBright=.8f; Key.DirPitch=55; Key.DirHeading=90;
    Key.DirColor=Key.AmbColor=0xFFFFFFFF; Key.AmbBright=.35f;
    auto* Options=CreateWidget<UACEVideoSettingsWidget>(GI); Options->TakeWidget();
    auto* Shadow=Cast<UComboBoxString>(Options->WidgetTree->FindWidget(TEXT("ShadowQuality")));
    auto* Dynamic=Cast<UComboBoxString>(Options->WidgetTree->FindWidget(TEXT("DynamicLightingQuality")));
    if(!TestNotNull(TEXT("Shadow dropdown"),Shadow)||!TestNotNull(TEXT("Dynamic lighting dropdown"),Dynamic)) return false;
    TestEqual(TEXT("Shadows offer an explicit Off option"),Shadow->GetOptionAtIndex(0),FString(TEXT("Off")));
    TestEqual(TEXT("Effect lighting defaults to Full"),Dynamic->GetSelectedOption(),FString(TEXT("Full")));
    auto Apply=[&]() {
        // Same rendering preference path as ApplyVideo, without changing the test host's display.
        Options->ApplyRenderingOptions(); Scalability::SetQualityLevels(Settings->ScalabilityQuality);
        IConsoleManager::Get().CallAllConsoleVariableSinks();
        // SaveSettings redirects editor scalability to EditorSettings.ini and
        // reloads the global GameUserSettings path for window positions. Save
        // rendering state directly into this isolated fixture instead.
        Scalability::SaveState(GGameUserSettingsIni); Options->ApplyRuntimeOptions();
        Sky->UpdateWorldLighting(Key,Key,0,.5f); Submit();
        Options->ResetVideo();
    };
    for(int32 Level:{3,1,0,2,4,0,3})
    {
        Shadow->SetSelectedIndex(Level); Dynamic->SetSelectedIndex(Level==0?0:Level==1?1:2);
        Apply();
        auto* Sun=Sky->SunLightActor->FindComponentByClass<UDirectionalLightComponent>();
        TestEqual(TEXT("Sun's real shadow flag follows quality"),bool(Sun->CastShadows),Level>0);
        TestTrue(TEXT("Sunlight remains available with shadows off"),Sun->IsVisible() && Sun->Intensity>0.f);
        const bool Mobile=World->GetFeatureLevel()<=ERHIFeatureLevel::ES3_1;
        if(Level>0)
        {
            TestEqual(TEXT("Sun uses the selected cascade count"),Sun->DynamicShadowCascades,Mobile||Level==1?1:Level==2?2:4);
            TestEqual(TEXT("Sun uses the selected shadow distance"),Sun->DynamicShadowDistanceMovableLight,Mobile?4000.f:Level==1?10000.f:Level==2?30000.f:60000.f);
        }
        else TestEqual(TEXT("Renderer globally disables dynamic shadows"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShadowQuality"))->GetInt(),0);
        const int32 Budget=Level==0?0:Level==1?2:Mobile?4:8;
        TestEqual(TEXT("Actual effect light count obeys preference"),Lights->GetVisibleLightCount(),Budget);
        TestEqual(TEXT("Disabled/excess lights are released from renderer"),Lights->GetAllocatedLightCount(),Budget);
        for(auto* Light:DoorLights()) TestEqual(TEXT("Loaded doorway obeys preference"),Light->IsVisible(),Level>1);
        TestEqual(TEXT("Hidden room only casts for enabled doorway lights"),bool(Room->CellMesh->bCastHiddenShadow),Level>1);
    }
    Shadow->SetSelectedIndex(0); Dynamic->SetSelectedIndex(0);
    TestTrue(TEXT("Editing lighting enables Apply"),Options->HasPendingChanges());
    TestEqual(TEXT("Changing selection waits for Apply"),ACERuntimeOptions::DynamicLightingQuality(),2);
    Options->ResetVideo(); TestEqual(TEXT("Reset discards pending lighting change"),Dynamic->GetSelectedIndex(),2);
    Shadow->SetSelectedIndex(0); Dynamic->SetSelectedIndex(0); Apply();
    FConfigFile Saved; Saved.Read(GGameUserSettingsIni); float SavedLights=-1; int32 SavedShadows=-1;
    Saved.GetFloat(TEXT("ACE.Presentation"),TEXT("DynamicLightingQuality"),SavedLights);
    Saved.GetInt(TEXT("ScalabilityGroups"),TEXT("sg.ShadowQuality"),SavedShadows);
    TestEqual(TEXT("Lighting Off persisted to disk"),SavedLights,0.f);
    TestEqual(TEXT("Shadows Off persisted to disk"),SavedShadows,0);
    auto* Reopened=CreateWidget<UACEVideoSettingsWidget>(GI); Reopened->TakeWidget();
    TestEqual(TEXT("Reopened lighting dropdown restores Off"),Cast<UComboBoxString>(Reopened->WidgetTree->FindWidget(TEXT("DynamicLightingQuality")))->GetSelectedIndex(),0);
    TestEqual(TEXT("Reopened shadow dropdown restores Off"),Cast<UComboBoxString>(Reopened->WidgetTree->FindWidget(TEXT("ShadowQuality")))->GetSelectedIndex(),0);
    Room->LoadEnvCell(0x7D64010C,FVector::ZeroVector,100);
    for(auto* Light:DoorLights()) TestFalse(TEXT("Newly streamed entrance respects Off"),Light->IsVisible());
    Room->SetSunCastShadow(true);
    TestTrue(TEXT("Suppressing doorway shadows preserves requested outdoor casting"),Room->CellMesh->CastShadow);
    Options->DefaultsVideo(); Apply();
    TestEqual(TEXT("Defaults restores full dynamic lighting"),Lights->GetVisibleLightCount(),World->GetFeatureLevel()<=ERHIFeatureLevel::ES3_1?4:8);
    for(auto* Source:Sources) Source->GetOwner()->SetActorLocation(FVector(2200,0,100));
    Dynamic->SetSelectedIndex(1); Apply(); TestEqual(TEXT("Low excludes effect lighting beyond 15m"),Lights->GetVisibleLightCount(),0);
    Dynamic->SetSelectedIndex(2); Apply(); TestTrue(TEXT("Full restores distant effect lighting"),Lights->GetVisibleLightCount()>0);
    return true;
}
#endif
