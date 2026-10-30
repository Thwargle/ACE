#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ConfigContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Mobile/ACEMobileSettings.h"
#include "Mobile/ACEMobileWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEMobilePersistenceTest,"ACE.Mobile.ControlLayoutPersistence",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEMobilePersistenceTest::RunTest(const FString&)
{
    // Use the actual Android default hierarchy and normal SaveConfig path.
    // Saving directly to an arbitrary .ini bypasses SectionsToSave filtering.
    const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/MobilePersistence")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*(Root/TEXT("Config")),true);
    const FString Source=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../Mobile/Project/Config/DefaultACEMobile.ini"));
    ON_SCOPE_EXIT {IFileManager::Get().DeleteDirectory(*Root,false,true);};
    if(!TestEqual(TEXT("Copy shipped phone configuration"),IFileManager::Get().Copy(*(Root/TEXT("Config/DefaultACEMobile.ini")),*Source),uint32(COPY_OK)))return false;
    auto* Defaults=GetMutableDefault<UACEMobileSettings>();
    const auto OriginalControls=Defaults->Controls;
    const bool Enabled=Defaults->bEnabled,Left=Defaults->bLeftHanded,Pinned=Defaults->bPinnedChat;
    const float Sensitivity=Defaults->LookSensitivity,Opacity=Defaults->ControlOpacity;
    const auto Hidden=Defaults->HiddenHUDPlugins,Extra=Defaults->ExtraHUDPlugins;
    ON_SCOPE_EXIT {Defaults->Controls=OriginalControls;Defaults->bEnabled=Enabled;Defaults->bLeftHanded=Left;Defaults->bPinnedChat=Pinned;
        Defaults->LookSensitivity=Sensitivity;Defaults->ControlOpacity=Opacity;Defaults->HiddenHUDPlugins=Hidden;Defaults->ExtraHUDPlugins=Extra;};
    auto Load=[&](FConfigCacheIni& Cache,FString& File)
    {
        auto Context=FConfigContext::ReadIntoConfigSystem(&Cache,TEXT("Android"));
        Context.ProjectConfigDir=Root/TEXT("Config/");Context.GeneratedConfigDir=Root/TEXT("Saved/Config/");
        return Context.Load(TEXT("ACEMobile"),File);
    };
    auto EqualControls=[&](const TCHAR* Message,const TArray<FACETouchControl>& Actual,const TArray<FACETouchControl>& Expected)
    {
        bool Same=Actual.Num()==Expected.Num();
        for(int32 I=0;Same&&I<Actual.Num();++I)
            Same=Actual[I].Id==Expected[I].Id&&Actual[I].Action==Expected[I].Action&&Actual[I].Position.Equals(Expected[I].Position,1.e-6)
                &&Actual[I].Size==Expected[I].Size&&Actual[I].bVisible==Expected[I].bVisible;
        TestTrue(Message,Same);
    };
    const FVector2D Cover=ACEMobile::LogicalSize({2520,1080}),Unfolded=ACEMobile::LogicalSize({2184,1968});
    TArray<FACETouchControl> Saved;FString File;
    {
        FConfigCacheIni Cache(EConfigCacheType::DiskBacked);TGuardValue<FConfigCacheIni*> ConfigScope(GConfig,&Cache);
        if(!TestTrue(TEXT("Android config hierarchy loads"),Load(Cache,File)))return false;
        TestTrue(TEXT("Generated config is isolated from player settings"),File.StartsWith(Root));
        auto* Settings=NewObject<UACEMobileSettings>();Settings->ResetLayout();
        auto* Touch=NewObject<UACEMobileWidget>();Touch->Settings=Settings;Touch->SurfaceResized(Cover);
        Touch->bEditing=true;
        const FVector2D Start=Touch->Layout[2].Position*Cover,End(630,180);
        TestTrue(TEXT("Fixture drag destination is clear"),ACEMobile::CanPlace(Touch->Layout,2,End/Cover,Touch->Layout[2].Size,Cover));
        Touch->TouchBegin(1,Start,Start);Touch->TouchMove(1,End);Touch->TouchEnd(1,End,End);Touch->Action("Done");
        TestTrue(TEXT("Completed drag saves preferred position immediately"),Settings->Controls[2].Position.Equals(End/Cover));
        Touch->SelectedControl=2;Touch->Rebind("Shortcut 14");
        Touch->Layout[2].Size=80;Touch->SaveLayout();
        Touch->Layout[3].bVisible=false;Touch->SaveLayout();
        FACETouchControl Added;Added.Id="Extra13";Added.Action="Spell 3";Added.Position={.5,.4};
        Touch->Layout.Add(Added);Touch->SaveLayout();
        Saved=Settings->Controls;
        Touch->SurfaceResized(Unfolded);Touch->bEditing=true;Touch->Action("Done");Touch->SurfaceResized(Cover);
        EqualControls(TEXT("Folding and Done without edits retain preferred placement and visibility"),Settings->Controls,Saved);
        // Fitting around chat/plugins is transient, including hidden overflow.
        Touch->ControlsTop=312;Touch->FitControls();
        bool Adapted=false;for(int32 I=0;I<Touch->Layout.Num();++I)Adapted|=!Touch->Layout[I].Position.Equals(Saved[I].Position)||Touch->Layout[I].bVisible!=Saved[I].bVisible;
        TestTrue(TEXT("Pinned-panel fixture actually adapts controls"),Adapted);
        Touch->SelectedControl=2;Touch->Rebind("Shortcut 15");Saved[2].Action="Shortcut 15";
        EqualControls(TEXT("Rebinding cannot save temporary offsets or hide overflow permanently"),Settings->Controls,Saved);
        Touch->ControlsTop=100;Touch->FitControls();
        Touch->bEditing=true;const FVector2D Next(730,180),Current=Touch->Layout[2].Position*Cover;
        Touch->TouchBegin(2,Current,Current);Touch->TouchMove(2,Next);Touch->CancelTouches();
        Saved[2].Position=Next/Cover;
        EqualControls(TEXT("Backgrounding during a valid drag retains the last position"),Settings->Controls,Saved);
        Settings->bPinnedChat=true;Settings->LookSensitivity=.31f;Settings->ControlOpacity=.62f;
        Settings->HiddenHUDPlugins={TEXT("huntledger")};Settings->ExtraHUDPlugins={TEXT("supplyledger")};Settings->SaveConfig();
        FString Disk;TestTrue(TEXT("Normal SaveConfig creates a persistent file"),FFileHelper::LoadFileToString(Disk,*File));
        TestTrue(TEXT("Normal Android save writes control bindings"),Disk.Contains(TEXT("Shortcut 15"))&&Disk.Contains(TEXT("Extra13")));
        Touch->bEditing=false;
    }
    {
        // A fresh cache plus deliberately cleared fields prevents the CDO or
        // the old in-memory config from making an unwritten layout look saved.
        FConfigCacheIni Cache(EConfigCacheType::DiskBacked);TGuardValue<FConfigCacheIni*> ConfigScope(GConfig,&Cache);FString ReloadFile;
        if(!TestTrue(TEXT("New session reads the generated config"),Load(Cache,ReloadFile)))return false;
        auto* Restored=NewObject<UACEMobileSettings>();Restored->Controls.Reset();Restored->bPinnedChat=false;
        Restored->LookSensitivity=0;Restored->ControlOpacity=0;Restored->HiddenHUDPlugins.Reset();Restored->ExtraHUDPlugins.Reset();Restored->LoadConfig();
        EqualControls(TEXT("Cold reload retains positions, bindings, sizes, hidden and added buttons"),Restored->Controls,Saved);
        TestTrue(TEXT("Pinned chat survives cold reload"),Restored->bPinnedChat);
        TestEqual(TEXT("Sensitivity survives cold reload"),Restored->LookSensitivity,.31f);
        TestEqual(TEXT("Opacity survives cold reload"),Restored->ControlOpacity,.62f);
        TestTrue(TEXT("Plugin visibility survives cold reload"),Restored->HiddenHUDPlugins.Contains(TEXT("huntledger"))&&Restored->ExtraHUDPlugins.Contains(TEXT("supplyledger")));
        auto* Touch=NewObject<UACEMobileWidget>();Touch->Settings=Restored;Touch->SurfaceResized(Cover);
        if(TestTrue(TEXT("Restored controls populate the touch surface"),Touch->Layout.Num()>2))
            TestTrue(TEXT("Restored attack control renders at its saved position"),Touch->Layout[2].Position.Equals(Saved[2].Position));
    }
    return true;
}
#endif
