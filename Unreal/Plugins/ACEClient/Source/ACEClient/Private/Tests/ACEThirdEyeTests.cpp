#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACETestDatDirectory.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Interfaces/IPluginManager.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginVM.h"
#include "Mods/ACEPluginStyle.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACESession.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    TSharedPtr<FJsonObject> EyeJSON(const FString& Text)
    {TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O);return O;}
    TSharedPtr<FJsonObject> EyeControl(const TArray<TSharedPtr<FJsonValue>>& Rows,const FString& Id)
    {
        for(const auto& V:Rows)
        {
            auto R=V->AsObject();if(R->GetStringField(TEXT("id"))==Id)return R;
            const TArray<TSharedPtr<FJsonValue>>* Children=nullptr;
            if(R->TryGetArrayField(TEXT("children"),Children))if(auto Found=EyeControl(*Children,Id))return Found;
        }
        return nullptr;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEThirdEyeTest,"ACE.Plugins.ThirdEye",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEThirdEyeTest::RunTest(const FString&)
{
    const FString Base=IPluginManager::Get().FindPlugin(TEXT("ACEClient"))->GetBaseDir();
    FString Script,Error;TSharedPtr<FJsonObject> Out;auto Empty=MakeShared<FJsonObject>();
    if(!TestTrue(TEXT("Third Eye Lua is bundled"),FFileHelper::LoadFileToString(Script,*(Base/TEXT("ClientMods/thirdeye/main.lua")))))return false;
    FACEPluginVM VM;if(!TestTrue(TEXT("Third Eye compiles in the real sandbox"),VM.Load(Script,Error)))return false;
    auto Snapshot=EyeJSON(TEXT(R"({"time":100,"state":{},"selected":1,"plugins":{"waypoint":{"enabled":true}},"objects":[
        {"id":1,"name":"Black Coral Golem","object_type":"creature","wcid":10,"distance":5,"dead":false},
        {"id":2,"name":"Black Coral Crystal","wcid":11,"distance":12,"dead":false},
        {"id":3,"name":"Red Rat","wcid":12,"distance":15,"dead":false},
        {"id":4,"name":"Black Coral Golem","wcid":10,"distance":18,"dead":true}]})"));
    auto Step=[&](const TCHAR* Events=TEXT("[]"))
    {
        auto E=EyeJSON(FString(TEXT("{\"events\":"))+Events+TEXT("}"));Snapshot->SetArrayField(TEXT("events"),E->GetArrayField(TEXT("events")));
        Snapshot->SetNumberField(TEXT("time"),Snapshot->GetNumberField(TEXT("time"))+1);
        const bool Ok=VM.Step(Snapshot,Empty,Out,Error);TestTrue(TEXT("Third Eye tick fits sandbox limits"),Ok);if(!Ok)AddError(Error);return Ok;
    };
    if(!Step())return false;
    TestTrue(TEXT("Empty watch list is explained"),EyeControl(Out->GetArrayField(TEXT("ui")),TEXT("empty")).IsValid());
    if(!Step(TEXT(R"([{"type":"change","id":"draft","value":"  bLaCk Coral  "},{"type":"click","id":"add"}])")))return false;
    TestEqual(TEXT("Partial name matches ignore case; dying objects excluded"),Out->GetStringField(TEXT("status")),FString(TEXT("2 matches")));
    TestEqual(TEXT("New spawn burst uses one local notification"),Out->GetArrayField(TEXT("requests")).Num(),1);
    TestEqual(TEXT("Default markers include both matches"),Out->GetArrayField(TEXT("markers")).Num(),2);
    TestTrue(TEXT("Third Eye clears its old plates"),Out->GetArrayField(TEXT("world_plates")).IsEmpty());
    for(const TCHAR* Id:{TEXT("add_plate"),TEXT("plate_1"),TEXT("plates_tab"),TEXT("opacity"),TEXT("vr_opacity")})
        TestFalse(TEXT("Third Eye has no plate or opacity controls"),EyeControl(Out->GetArrayField(TEXT("ui")),Id).IsValid());
    FACEPluginVM Migrated;Migrated.Load(Script,Error);auto RestoreInput=MakeShared<FJsonObject>(*Snapshot);
    RestoreInput->SetObjectField(TEXT("state"),EyeJSON(TEXT(R"({"rules":[{"name":"Black Coral","plate":true}],"opacity":0.5,"vr_opacity":0.25})")));
    RestoreInput->SetArrayField(TEXT("events"),{});TSharedPtr<FJsonObject> RestoreOutput;
    TestTrue(TEXT("Older Third Eye preferences still load"),Migrated.Step(RestoreInput,Empty,RestoreOutput,Error));
    TestEqual(TEXT("Migration preserves watch matches"),RestoreOutput->GetStringField(TEXT("status")),FString(TEXT("2 matches")));
    TestTrue(TEXT("Old enabled plate preferences cannot create plates"),RestoreOutput->GetArrayField(TEXT("world_plates")).IsEmpty());
    TestFalse(TEXT("Obsolete plate settings removed from saved rules"),RestoreOutput->GetObjectField(TEXT("state"))->GetArrayField(TEXT("rules"))[0]->AsObject()->HasField(TEXT("plate")));
    TestFalse(TEXT("Obsolete opacity preferences removed"),RestoreOutput->GetObjectField(TEXT("state"))->HasField(TEXT("opacity")));
    auto Saved=Out->GetObjectField(TEXT("state"));
    TestEqual(TEXT("Watch list saved without surrounding whitespace"),Saved->GetArrayField(TEXT("rules"))[0]->AsObject()->GetStringField(TEXT("name")),FString(TEXT("bLaCk Coral")));
    if(!Step(TEXT(R"([{"type":"result","id":"detected_alert","ok":false,"error":"Service rate limit; retry after 0.5 seconds"}])")))return false;
    TestEqual(TEXT("Rate-limited alerts retry without losing detections"),Out->GetArrayField(TEXT("requests")).Num(),1);
    if(!Step(TEXT(R"([{"type":"result","id":"detected_alert","ok":true}])")))return false;
    TestTrue(TEXT("Stationary matches do not spam chat"),Out->GetArrayField(TEXT("requests")).IsEmpty());
    const auto KnownObjects=Snapshot->GetArrayField(TEXT("objects"));Snapshot->SetArrayField(TEXT("objects"),{});Step();
    TestEqual(TEXT("Vanished objects leave the detected list"),Out->GetStringField(TEXT("status")),FString(TEXT("0 matches")));
    Snapshot->SetArrayField(TEXT("objects"),KnownObjects);Step();
    TestTrue(TEXT("Brief awareness flicker does not repeat alerts"),Out->GetArrayField(TEXT("requests")).IsEmpty());
    Snapshot->SetArrayField(TEXT("objects"),{});Snapshot->SetNumberField(TEXT("time"),Snapshot->GetNumberField(TEXT("time"))+31);Step();
    Snapshot->SetArrayField(TEXT("objects"),KnownObjects);Step();
    TestEqual(TEXT("Returning after a long absence is announced"),Out->GetArrayField(TEXT("requests")).Num(),1);
    Step(TEXT(R"([{"type":"result","id":"detected_alert","ok":true}])"));
    Step(TEXT(R"([{"type":"change","id":"range","value":10}])"));
    TestEqual(TEXT("Detection range is enforced"),Out->GetStringField(TEXT("status")),FString(TEXT("1 matches")));
    Step(TEXT(R"([{"type":"change","id":"watching","value":false}])"));
    TestTrue(TEXT("Pause clears markers and alerts"),Out->GetArrayField(TEXT("markers")).IsEmpty()&&Out->GetArrayField(TEXT("requests")).IsEmpty());
    Step(TEXT(R"([{"type":"change","id":"watching","value":true},{"type":"change","id":"range","value":240},{"type":"click","id":"remove_1"},{"type":"change","id":"exact","value":true},{"type":"click","id":"add_selected"}])"));
    TestEqual(TEXT("Add selected with exact matching excludes similar names"),Out->GetStringField(TEXT("status")),FString(TEXT("1 matches")));
    const auto ExactSaved=Out->GetObjectField(TEXT("state"));
    Step(TEXT(R"([{"type":"click","id":"add_selected"}])"));
    TestTrue(TEXT("Duplicate watch entries rejected"),EyeControl(Out->GetArrayField(TEXT("ui")),TEXT("feedback"))->GetStringField(TEXT("text")).Contains(TEXT("already")));
    Step(TEXT(R"([{"type":"change","id":"enabled_2","value":false}])"));
    TestEqual(TEXT("Rules can be disabled individually"),Out->GetStringField(TEXT("status")),FString(TEXT("0 matches")));
    Step(TEXT(R"([{"type":"change","id":"enabled_2","value":true}])"));
    // A new VM restores saved preferences, without restoring session-local GUIDs.
    FACEPluginVM Restored;Restored.Load(Script,Error);Snapshot->SetObjectField(TEXT("state"),ExactSaved);Snapshot->SetArrayField(TEXT("events"),{});
    TestTrue(TEXT("Saved watch list survives a new VM"),Restored.Step(Snapshot,Empty,Out,Error));
    TestEqual(TEXT("Exact matching restored"),Out->GetStringField(TEXT("status")),FString(TEXT("1 matches")));
    // Maximum list against 512 distinct names. The script must yield within the
    // real instruction/heap budgets, eventually evaluate every name, and paginate UI.
    FACEPluginVM Crowded;Crowded.Load(Script,Error);auto Stress=EyeJSON(TEXT(R"({"rules":[],"chat_alerts":false})"));
    TArray<TSharedPtr<FJsonValue>> Rules,Objects;
    for(int32 I=0;I<128;++I)Rules.Add(MakeShared<FJsonValueObject>(EyeJSON(FString::Printf(TEXT("{\"name\":\"%s\",\"exact\":false}"),I==127?TEXT("Target"):*FString::Printf(TEXT("Absent%d"),I)))));
    for(int32 I=0;I<512;++I)Objects.Add(MakeShared<FJsonValueObject>(EyeJSON(FString::Printf(TEXT("{\"id\":%d,\"name\":\"Target %d\",\"wcid\":10,\"distance\":%g,\"dead\":false}"),I+10,I,I*.4))));
    Stress->SetArrayField(TEXT("rules"),Rules);auto Busy=MakeShared<FJsonObject>(*Snapshot);Busy->SetObjectField(TEXT("state"),Stress);Busy->SetArrayField(TEXT("objects"),Objects);
    bool Finished=false;
    for(int32 Tick=0;Tick<20;++Tick)
    {
        if(!TestTrue(TEXT("Crowded watch list stays within VM budget"),Crowded.Step(Busy,Empty,Out,Error))){AddError(Error);return false;}
        if(Out->GetStringField(TEXT("status"))==TEXT("512 matches")){Finished=true;break;}
    }
    TestTrue(TEXT("Every crowded-area name is eventually scanned"),Finished);
    TestEqual(TEXT("Markers remain bounded"),Out->GetArrayField(TEXT("markers")).Num(),32);
    // Traverse full lists, including the final partial page. Pagination must
    // reach everything even in a short window, without requiring scrolling.
    Busy->SetObjectField(TEXT("panel"),EyeJSON(TEXT(R"({"width":480,"height":440})")));
    TSet<FString> VisitedRules,VisitedObjects;
    for(int32 Page=0;Page<260;++Page)
    {
        if(!TestTrue(TEXT("Pagination stays in VM budget"),Crowded.Step(Busy,Empty,Out,Error)))return false;
        const auto& UI=Out->GetArrayField(TEXT("ui"));
        for(const auto& V:EyeControl(UI,TEXT("setup_tab"))->GetArrayField(TEXT("children")))
        {const auto Id=V->AsObject()->GetStringField(TEXT("id"));if(Id.StartsWith(TEXT("rule_")))VisitedRules.Add(Id);}
        for(const auto& V:EyeControl(UI,TEXT("detected_tab"))->GetArrayField(TEXT("children")))
        {const auto Id=V->AsObject()->GetStringField(TEXT("id"));if(Id.StartsWith(TEXT("object_")))VisitedObjects.Add(Id);}
        if(!EyeControl(UI,TEXT("rules_next"))->GetBoolField(TEXT("enabled"))&&!EyeControl(UI,TEXT("detected_next"))->GetBoolField(TEXT("enabled")))break;
        Busy->SetArrayField(TEXT("events"),EyeJSON(TEXT(R"({"events":[{"type":"click","id":"rules_next"},{"type":"click","id":"detected_next"}]})"))->GetArrayField(TEXT("events")));
    }
    TestEqual(TEXT("All watch rules reachable by pages"),VisitedRules.Num(),128);
    TestEqual(TEXT("All detections reachable by pages"),VisitedObjects.Num(),512);
    Busy->SetArrayField(TEXT("events"),{});

    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);auto* GI=NewObject<UGameInstance>(GEngine);W->SetGameInstance(GI);
    Context.OwningGameInstance=GI;Context.SetCurrentWorld(W);GI->OnWorldChanged(nullptr,W);GI->Init();
    auto* H=GI->GetSubsystem<UACEPluginSubsystem>();auto* C=GI->GetSubsystem<UACEClientSubsystem>();
    const FString Root=FPaths::ProjectSavedDir()/TEXT("Automation/ThirdEye")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
    H->StorageRoot=Root;H->Settings=MakeShared<FJsonObject>();
    ON_SCOPE_EXIT {GI->Shutdown();GEngine->DestroyWorldContext(W);W->DestroyWorld(false);IFileManager::Get().DeleteDirectory(*Root,false,true);};
    H->Discover();auto P=H->Find(TEXT("thirdeye"));if(!TestTrue(TEXT("Bundled optional plugin discovered"),P&&P->Companion&&!P->Enabled))return false;
    TestEqual(TEXT("Distinct Third Eye name"),P->Name,FString(TEXT("Third Eye")));
    TestFalse(TEXT("Observer does not require outgoing chat"),P->Permissions.Contains(TEXT("say")));
    TestTrue(TEXT("Crowded output is valid native UI"),H->ValidatePluginUI(Out->GetArrayField(TEXT("ui")),Error));
    C->Session=MakeShared<FACESession>();auto& Session=*C->Session;Session.State=EACESessionState::InWorld;Session.PlayerGuid=0x50000001;Session.ServerName=TEXT("Third Eye Test");
    Session.PlayerPosition.CellId=0x7D64000D;Session.PlayerPosition.Location=FVector(34,104,12);Session.PlayerVitals.bValid=true;Session.PlayerVitals.Health=100;Session.PlayerVitals.MaxHealth=100;
    FACEWorldObject Player;Player.Guid=Session.PlayerGuid;Player.Name=TEXT("Observer");Player.Position=Session.PlayerPosition;Player.bIsPlayer=true;Session.WorldObjects.Add(Player.Guid,Player);
    FACEWorldObject Golem;Golem.Guid=1;Golem.Name=TEXT("Black Coral Golem");Golem.ItemType=ACEItemType::Creature;Golem.bHasPosition=true;Golem.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;Golem.Position=Session.PlayerPosition;Golem.Position.Location.X+=5;Session.WorldObjects.Add(Golem.Guid,Golem);
    // Enable the existing local-chat recorder for this test. Third Eye itself
    // needs no chat-reading permission to display a notice.
    P->Permissions.Add(TEXT("chat"));
    H->SetEnabled(P->Id,true);H->TickCompanions();TestTrue(TEXT("Enabled Third Eye starts on login without Start"),P->Running);
    P->SavedState=ExactSaved;P->StateDirty=true;H->FlushPluginState(*P);H->Stop(P->Id);H->Start(P->Id);
    const int32 BeforePackets=Session.CachedC2SPackets.Num();
    H->MovementOwner=TEXT("ucm");H->FastCastOwner=TEXT("ucm");
    auto NativeStep=[&]()
    {
        P->NextService=0;
        const bool Ok=P->VM->Step(H->PublicSnapshot(*P),P->Profile,Out,Error)&&H->ApplyPublicOutput(*P,Out,Error);
        TestTrue(TEXT("Real host accepts Third Eye output"),Ok);if(!Ok)AddError(Error);return Ok;
    };
    if(!NativeStep())return false;
    bool GreenNotice=false;
    for(const auto& E:H->PluginChatEvents)GreenNotice|=E.Value->AsObject()->GetStringField(TEXT("raw_text")).StartsWith(TEXT("[Third Eye] Matching spawn detected:"))&&E.Value->AsObject()->GetNumberField(TEXT("color"))==ACEChatMessageType::Broadcast;
    TestTrue(TEXT("Green detection notice reaches local chat"),GreenNotice);
    TestEqual(TEXT("Detection never sends server packets"),Session.CachedC2SPackets.Num(),BeforePackets);
    TestEqual(TEXT("Detection leaves UCM movement ownership"),H->MovementOwner,FString(TEXT("ucm")));
    const int32 BeforeChat=H->PluginChatEvents.Num();NativeStep();TestEqual(TEXT("Host does not repeat detection chat"),H->PluginChatEvents.Num(),BeforeChat);
    P->Permissions.Remove(TEXT("chat"));
    TestTrue(TEXT("Detection button selects live object"),H->ClickPluginRow(P->Id,TEXT("select_1")));TestEqual(TEXT("Selected object is correct"),C->GetSelectedObject().Guid,1);
    Session.WorldObjects.Remove(1);TestFalse(TEXT("Vanished target cannot be selected"),H->ClickPluginRow(P->Id,TEXT("select_1")));Session.WorldObjects.Add(1,Golem);
    // Notification permissions, line validation and rate limit all use the native service.
    auto Notice=EyeJSON(TEXT(R"({"requests":[{"id":"notice_test","service":"notice","operation":"show","text":"local only"}]})"));
    P->Permissions.Remove(TEXT("ui"));P->NextService=0;H->ApplyPluginServices(*P,Notice,Error);
    TestFalse(TEXT("No UI permission denies local notice"),P->PluginEvents.Last()->AsObject()->GetBoolField(TEXT("ok")));P->Permissions.Add(TEXT("ui"));
    P->NextService=0;H->ApplyPluginServices(*P,Notice,Error);H->ApplyPluginServices(*P,Notice,Error);
    TestFalse(TEXT("Local notices are rate limited"),P->PluginEvents.Last()->AsObject()->GetBoolField(TEXT("ok")));
    H->Stop(P->Id);const FString Path=P->StatePath;Session.PlayerGuid++;H->Start(P->Id);
    TestTrue(TEXT("Watch lists are character-scoped"),P->StatePath!=Path&&!P->SavedState->HasField(TEXT("rules")));
    H->Stop(P->Id);Session.PlayerGuid--;H->Start(P->Id);NativeStep();
    TestEqual(TEXT("Original watch list restored from disk"),P->SavedState->GetArrayField(TEXT("rules")).Num(),1);
    if(FApp::CanEverRender())
    {
        GI->GetSubsystem<UACEDatSubsystem>()->LoadDatDirectory(ACETestDatDirectory());
        TestFalse(TEXT("Third Eye opts out of host scrolling"),P->ScrollPanel());
        FWidgetRenderer Renderer(false,true);auto Panel=H->MakePanel(P->Id);
        TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>,FName)> FindTag;
        FindTag=[&](TSharedRef<SWidget> Widget,FName Tag)->TSharedPtr<SWidget>
        {
            if(Widget->GetTag()==Tag)return Widget;
            auto* Children=Widget->GetChildren();for(int32 I=0;I<Children->Num();++I)if(auto Found=FindTag(Children->GetChildAt(I),Tag))return Found;
            return nullptr;
        };
        TFunction<bool(TSharedRef<SWidget>)> HasScroll=[&](TSharedRef<SWidget> Widget)
        {
            if(!Widget->GetVisibility().IsVisible())return false;
            if(Widget->GetType()==TEXT("SScrollBox")||Widget->GetType()==TEXT("SScrollBar"))return true;
            auto* Children=Widget->GetChildren();for(int32 I=0;I<Children->Num();++I)if(HasScroll(Children->GetChildAt(I)))return true;
            return false;
        };
        // Fresh crowded VM starts on page one; resize the same retained panel.
        Crowded.Load(Script,Error);
        for(const FVector2D Size:{FVector2D(780,650),FVector2D(480,440),FVector2D(1000,360)})
        {
            auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
            auto Frame=[&]()
            {
                auto Public=H->PublicSnapshot(*P);const TSharedPtr<FJsonObject>* Available=nullptr;
                if(Public->TryGetObjectField(TEXT("panel"),Available))Busy->SetObjectField(TEXT("panel"),*Available);
                if(Crowded.Step(Busy,Empty,Out,Error)){P->PanelRows=Out->GetArrayField(TEXT("ui"));++P->PanelRevision;}
                else AddError(Error);
                ++GFrameCounter;Renderer.DrawWidget(Target,Panel,Size,0);FlushRenderingCommands();
            };
            for(int I=0;I<12;++I)Frame();
            for(const TCHAR* Page:{TEXT("detected"),TEXT("setup"),TEXT("options")})
            {
                auto Tab=FindTag(Panel,FName(*FString::Printf(TEXT("PluginTab.tabs.%s_tab"),Page)));
                if(!TestTrue(TEXT("Third Eye tab exists"),Tab.IsValid()))continue;
                StaticCastSharedPtr<SACEPluginTab>(Tab)->SimulateClick();for(int I=0;I<4;++I)Frame();
                TestFalse(TEXT("Third Eye has no visible scrollbar or scroll box"),HasScroll(Panel));
                const FString Prefix=FString(Page)==TEXT("setup")?TEXT("rules"):TEXT("detected");
                if(FString(Page)!=TEXT("options"))
                {
                    auto Prev=FindTag(Panel,FName(*(TEXT("PluginRow.")+Prefix+TEXT("_prev"))));
                    auto Next=FindTag(Panel,FName(*(TEXT("PluginRow.")+Prefix+TEXT("_next"))));
                    if(TestTrue(TEXT("Both pagination buttons present"),Prev&&Next))
                    {
                        TestTrue(TEXT("Pagination arrows keep compact widths"),Prev->GetCachedGeometry().GetLocalSize().X<60&&Next->GetCachedGeometry().GetLocalSize().X<60);
                        TestTrue(TEXT("Pagination arrows stay together"),Next->GetCachedGeometry().GetAbsolutePosition().X-Prev->GetCachedGeometry().GetAbsolutePosition().X<180);
                    }
                    const auto TabId=FString(Page)==TEXT("setup")?TEXT("setup_tab"):TEXT("detected_tab");
                    const auto& Children=EyeControl(P->PanelRows,TabId)->GetArrayField(TEXT("children"));
                    const FString LastId=Children.Last()->AsObject()->GetStringField(TEXT("id"));
                    auto LastRow=FindTag(Panel,FName(*(TEXT("PluginControl.")+LastId)));
                    if(TestTrue(TEXT("Last page row is arranged"),LastRow.IsValid()))
                    {
                        const auto& G=LastRow->GetCachedGeometry();
                        TestTrue(FString::Printf(TEXT("%s rows fit %.0fx%.0f"),Page,Size.X,Size.Y),G.GetAbsolutePosition().Y+G.GetLocalSize().Y<=Size.Y+1);
                    }
                }
                TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags);
                TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(int32(Size.X),int32(Size.Y),Pixels,Png);
                FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("ThirdEye-%s-%.0f.png"),Page,Size.X)));
            }
            Target->ReleaseResource();
        }
    }
    H->SetEnabled(P->Id,false);TestFalse(TEXT("Manager disable stops Third Eye"),P->Running);
    TestEqual(TEXT("Disabling Third Eye leaves UCM alone"),H->MovementOwner,FString(TEXT("ucm")));
    return true;
}
#endif
