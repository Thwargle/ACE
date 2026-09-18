#if WITH_DEV_AUTOMATION_TESTS && PLATFORM_ANDROID
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "ACEDatSubsystem.h"
#include "Dat/ACEDatDatabase.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IXRTrackingSystem.h"
#include "DynamicRHI.h"
#include "Engine/StaticMesh.h"

class FACEQuestNativeCheck : public IAutomationLatentCommand
{
public:
    explicit FACEQuestNativeCheck(FAutomationTestBase* InTest) : Test(InTest) {}
    virtual bool Update() override;
private:
    FAutomationTestBase* Test;
    TFuture<int32> CellFileCount;
    double CellStartedAt = 0.0;
};

bool FACEQuestNativeCheck::Update()
{
    if (CellFileCount.IsValid())
    {
        if (!CellFileCount.IsReady())
        {
            if (FPlatformTime::Seconds() - CellStartedAt < 60.0) return false;
            Test->AddError(TEXT("Cell DAT indexing timed out after 60 seconds"));
            return true;
        }
        const int32 Count = CellFileCount.Get();
        Test->TestTrue(TEXT("Installed cell DAT can be opened and indexed on ARM64"), Count > 0);
        Test->AddInfo(FString::Printf(TEXT("Cell DAT entries: %d"), Count));
        return true;
    }
    UWorld* World = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
        if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
    if (!Test->TestNotNull(TEXT("Native game world"), World)) return true;
    auto* Dat = World->GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
    if (!Test->TestNotNull(TEXT("DAT subsystem"), Dat)) return true;
    Test->AddInfo(FString::Printf(TEXT("Android DAT path: %s"), *Dat->GetDatDirectory()));
    Test->TestTrue(TEXT("Native Vulkan renderer"), GDynamicRHI && FString(GDynamicRHI->GetName()).Contains(TEXT("Vulkan")));
    Test->TestTrue(TEXT("Mobile feature level"), GMaxRHIFeatureLevel == ERHIFeatureLevel::ES3_1);
    Test->TestTrue(TEXT("OpenXR runtime"), GEngine->XRSystem.IsValid() && GEngine->XRSystem->GetSystemName() == FName(TEXT("OpenXR")));
    Test->TestTrue(TEXT("HMD enabled"), UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled());
    Test->TestTrue(TEXT("Portal DAT indexed"), Dat->IsDatReady());
    Test->TestTrue(TEXT("DAT error is empty"), Dat->GetDatLoadError().IsEmpty());
    Test->TestTrue(TEXT("Readable installed portal DAT"), IFileManager::Get().FileSize(*(Dat->GetDatDirectory() / TEXT("client_portal.dat"))) > 0);
    if (Dat->IsDatReady())
    {
        Test->TestNotNull(TEXT("Retail human setup builds on ARM64"), Dat->GetOrBuildSetupMesh(0x02000001, 100.f));
        auto* Mesh = Dat->GetOrCreateSetupStaticMesh(0x02000001, 100.f, false);
        if (Test->TestNotNull(TEXT("Runtime static mesh builds on ARM64"), Mesh))
            for (const auto& Material : Mesh->GetStaticMaterials())
                Test->TestTrue(TEXT("Runtime material has valid texture streaming metadata"), Material.UVChannelData.bInitialized);
    }
    // The live subsystem deliberately defers cell indexing until world entry.
    // Validate the installed file independently without enabling world streaming
    // on the login screen or blocking the render thread during the index scan.
    const FString CellPath = Dat->GetDatDirectory() / TEXT("client_cell_1.dat");
    CellStartedAt = FPlatformTime::Seconds();
    CellFileCount = Async(EAsyncExecution::ThreadPool, [CellPath]()
    {
        FACEDatDatabase Cell;
        return Cell.Open(CellPath) ? Cell.GetFileCount() : 0;
    });
    return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEQuestNativeSmokeTest, "ACE.Quest.NativeSmoke",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEQuestNativeSmokeTest::RunTest(const FString&)
{
    // Give the asynchronous DAT index and OpenXR session time to initialize.
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(15.f));
    ADD_LATENT_AUTOMATION_COMMAND(FACEQuestNativeCheck(this));
    return true;
}
#endif
