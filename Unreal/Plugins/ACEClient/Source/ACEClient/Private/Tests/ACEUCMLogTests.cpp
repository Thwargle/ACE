#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Mods/ACEUCMLog.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginDesktop.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "Engine/GameInstance.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "Widgets/Views/STableViewBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUCMLogTest,"ACE.Plugins.UCMLog",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEUCMLogTest::RunTest(const FString&)
{
    FACEUCMLog Log;
    Log.Add(TEXT("Navigation paused: door remains closed"),TEXT("RedRatCave.nav | Waypoint 4"),EACEUCMLogLevel::Warning);
    for(int32 I=0;I<FACEUCMLog::Capacity+10;++I)Log.Add(FString::Printf(TEXT("Event %d"),I),TEXT(""),EACEUCMLogLevel::Info);
    TestEqual(TEXT("Overnight history is strictly bounded"),Log.GetEntries().Num(),FACEUCMLog::Capacity);
    TestEqual(TEXT("Oldest messages are replaced in chronological order"),Log.GetEntries()[0]->Message,FString(TEXT("Event 10")));
    TestTrue(TEXT("Last problem survives a full cycle of new normal activity"),Log.GetLastProblem()&&Log.GetLastProblem()->Message.Contains(TEXT("door remains closed")));
    Log.Add(TEXT("Repeated failure"),TEXT("Same context"),EACEUCMLogLevel::Error);
    for(int32 I=0;I<100;++I)Log.Add(TEXT("Repeated failure"),TEXT("Same context"),EACEUCMLogLevel::Error);
    TestEqual(TEXT("Repeated problems are coalesced"),Log.GetEntries().Last()->Repeats,101);
    TestEqual(TEXT("Repeats do not increase history size"),Log.GetEntries().Num(),FACEUCMLog::Capacity);
    Log.Add(FString::ChrN(10000,TCHAR('x')),FString::ChrN(10000,TCHAR('y')),EACEUCMLogLevel::Info);
    TestEqual(TEXT("Message size is bounded too"),Log.GetEntries().Last()->Message.Len(),FACEUCMLog::TextLimit);
    TestEqual(TEXT("Context size is bounded too"),Log.GetEntries().Last()->Context.Len(),FACEUCMLog::TextLimit);
    const FString Root=FPaths::ProjectSavedDir()/TEXT("Automation")/(TEXT("UCMLog-")+FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const FString File=Root/TEXT("ucm-log.json");
    TestTrue(TEXT("Bounded log saves"),Log.Save(File));
    FACEUCMLog Restored;TestTrue(TEXT("Log survives client restart"),Restored.Load(File));
    TestEqual(TEXT("All retained events restore"),Restored.GetEntries().Num(),FACEUCMLog::Capacity);
    TestEqual(TEXT("Last problem restores independently"),Restored.GetLastProblem()->Message,FString(TEXT("Repeated failure")));
    TestTrue(TEXT("Log file stays under its size bound"),IFileManager::Get().FileSize(*File)<FACEUCMLog::FileLimit);
    Restored.Clear();TestTrue(TEXT("Clear overwrites existing file"),Restored.Save(File));
    FACEUCMLog Cleared;Cleared.Load(File);TestEqual(TEXT("Cleared log cannot reappear after restart"),Cleared.GetEntries().Num(),0);
    TestFalse(TEXT("Clear removes latest problem"),Cleared.GetLastProblem().IsValid());

    auto* GI=NewObject<UGameInstance>();GI->Init();
    auto* H=GI->GetSubsystem<UACEPluginSubsystem>();H->StorageRoot=Root;H->UCMLog.Clear();
    auto* Client=GI->GetSubsystem<UACEClientSubsystem>();
    const auto Session=Client->GetSession();
    FACEPosition Pos;Pos.CellId=0x01D9010B;Pos.Location=FVector(34.8,-34.5,0);Session->SetLocalPosition(Pos);
    auto P=H->Find(TEXT("ucm"));if(!TestTrue(TEXT("UCM is available"),P.IsValid())){GI->Shutdown();return false;}
    P->Enabled=true;P->Running=true;P->Profile=MakeShared<FJsonObject>();P->ProfileName=TEXT("RedRatCave");
    P->Profile->SetStringField(TEXT("nav_source"),TEXT("RedRatCave.nav"));H->RoutePoint=4;
    H->LogUCMEvent(*P,TEXT("UCM started"));
    auto Intent=MakeShared<FJsonObject>();Intent->SetStringField(TEXT("activity"),TEXT("navigation"));
    Intent->SetStringField(TEXT("action"),TEXT("move"));Intent->SetStringField(TEXT("status"),TEXT("Waypoint 4 / 12"));
    H->LogUCMIntent(*P,Intent);const int32 BeforePolls=H->UCMLog.GetEntries().Num();
    for(int32 I=0;I<5000;++I)H->LogUCMIntent(*P,Intent);
    TestEqual(TEXT("Unchanged route polls never flood the log"),H->UCMLog.GetEntries().Num(),BeforePolls);
    H->LogUCMEvent(*P,TEXT("No movement progress for 5 seconds; requesting route recovery"),EACEUCMLogLevel::Warning);
    Intent->SetStringField(TEXT("action"),TEXT("pause_navigation"));Intent->SetStringField(TEXT("status"),TEXT("Navigation paused: door remains closed after three attempts. UCM remains active; enable Follow route to retry"));
    H->Execute(*P,Intent);
    TestTrue(TEXT("Navigation pause is logged without stopping UCM"),P->Running&&H->UCMLog.GetLastProblem()->Message.Contains(TEXT("door remains closed")));
    TestTrue(TEXT("Failure identifies route and waypoint"),H->UCMLog.GetLastProblem()->Context.Contains(TEXT("RedRatCave.nav"))&&H->UCMLog.GetLastProblem()->Context.Contains(TEXT("Waypoint 4")));
    TestTrue(TEXT("Failure identifies actual cell and location"),H->UCMLog.GetLastProblem()->Context.Contains(TEXT("01D9010B")));
    H->ReportActivityFailure(*P,Intent,TEXT("Route action timed out"));
    TestTrue(TEXT("Native action failures are recorded"),H->UCMLog.GetLastProblem()->Message.Contains(TEXT("Route action timed out")));
    auto Dock=SNew(SACEPluginDesktop).Host(H);Dock->Toggle(TEXT("ucm.log"));
    TestTrue(TEXT("Log opens in its own window"),Dock->IsOpen(TEXT("ucm.log"))&&!Dock->IsOpen(TEXT("ucm")));
    Dock->Tick(FGeometry::MakeRoot(FVector2D(1400,900),FSlateLayoutTransform()),1,.016f);
    Dock->Move(TEXT("ucm.log"),FVector2D(80,40),true);Dock->Resize(TEXT("ucm.log"),FVector2D(-80,60),true);
    const auto SavedPosition=Dock->Windows[TEXT("ucm.log")].Position,SavedSize=Dock->Windows[TEXT("ucm.log")].Size;
    Dock->Toggle(TEXT("ucm.log"));H->LogUCMEvent(*P,TEXT("Vital recovery continues while the log is hidden"));
    TestTrue(TEXT("Closing log leaves UCM running"),P->Running);
    auto NewDock=SNew(SACEPluginDesktop).Host(H);NewDock->Toggle(TEXT("ucm.log"));
    TestEqual(TEXT("Log position restores"),NewDock->Windows[TEXT("ucm.log")].Position,SavedPosition);
    TestEqual(TEXT("Log size restores"),NewDock->Windows[TEXT("ucm.log")].Size,SavedSize);
    if(FApp::CanEverRender())
    {
        FWidgetRenderer Renderer(false,true);auto Panel=H->MakeUCMLogPanel();const FVector2D Size(720,480);
        auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
        for(int32 Pass=0;Pass<6;++Pass){++GFrameCounter;Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();}
        TSharedPtr<STableViewBase> LogList;
        TFunction<void(TSharedRef<SWidget>)> FindList=[&](TSharedRef<SWidget> W)
        {
            if(W->GetTypeAsString().StartsWith(TEXT("SListView")))LogList=StaticCastSharedRef<STableViewBase>(W);
            else if(auto* Children=W->GetChildren())for(int32 I=0;I<Children->Num();++I)FindList(Children->GetChildAt(I));
        };
        FindList(Panel);TestTrue(TEXT("Log uses a virtualized event list"),LogList.IsValid());
        if(LogList)
        {
            TestTrue(TEXT("Opening the log follows the latest event"),LogList->GetScrollDistanceRemaining().Y<.001);
            LogList->ScrollToTop();
            for(int32 Pass=0;Pass<3;++Pass){++GFrameCounter;Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();}
            H->LogUCMEvent(*P,TEXT("A new event should not pull the reader away from older messages"));
            for(int32 Pass=0;Pass<3;++Pass){++GFrameCounter;Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();}
            TestTrue(TEXT("Reading earlier messages pauses auto-follow"),LogList->GetScrollDistanceRemaining().Y>.001);
        }
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(720,480,Pixels,PNG);
        FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("UCMLogReview/LogPanel.png")));Target->ReleaseResource();
    }
    H->Stop(TEXT("ucm"),TEXT("Script error: plugin instruction budget exceeded"));
    TestTrue(TEXT("Script failures preserve their cause after stopping"),H->UCMLog.GetLastProblem()->Level==EACEUCMLogLevel::Error);
    GI->Shutdown();
    // Delete only the known files created by this fixture.
    IFileManager::Get().Delete(*File);IFileManager::Get().Delete(*(Root/TEXT("settings.json")));
    return true;
}
#endif
