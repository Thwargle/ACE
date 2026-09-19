#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACELoginProfile.h"
#include "ACELoginSettings.h"
#include "ACELoginWidget.h"
#include "ACEPlayerController.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Components/EditableTextBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/Border.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ImageUtils.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELauncherDirectoryTest,"ACE.Launcher.DirectoryAndProfiles",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELauncherDirectoryTest::RunTest(const FString&)
{
    const FString Xml=TEXT("<ArrayOfServerItem><ServerItem><id>one</id><name>One &amp; Two</name><emu>ACE</emu><server_host>one.invalid</server_host><server_port>9047</server_port><website_url>https://one.invalid</website_url><discord_url>https://discord.gg/example</discord_url></ServerItem>")
        TEXT("<ServerItem><id>two</id><name>GDLE fixture</name><emu>GDL</emu><connect_string>two.invalid:9010</connect_string><DiscordUrl>https://discord.gg/oldschema</DiscordUrl></ServerItem>")
        TEXT("<ServerItem><name>Duplicate</name><emu>ACE</emu><connect_string>ONE.invalid:9047</connect_string></ServerItem>")
        TEXT("<ServerItem><name>Hidden</name><emu>ACE</emu><visibility>Hidden</visibility><connect_string>hidden.invalid:9000</connect_string></ServerItem>")
        TEXT("<ServerItem><name>Unsupported</name><emu>DF</emu><connect_string>df.invalid:9000</connect_string></ServerItem>")
        TEXT("<ServerItem><name>Bad port</name><emu>ACE</emu><connect_string>bad.invalid:65535</connect_string></ServerItem></ArrayOfServerItem>");
    TArray<FACELoginServer> Servers; FString Error;
    TestTrue(TEXT("Current and legacy public directory schemas parse"),ACELoginProfile::ParseDirectory(Xml,Servers,Error));
    TestEqual(TEXT("Duplicates, hidden, unsupported and invalid entries excluded"),Servers.Num(),2);
    if (Servers.Num()!=2) return false;
    TestTrue(TEXT("GDL alias selects GDLE authentication"),Servers[0].bGDLE);
    TestEqual(TEXT("Legacy server imports the exact port in connect_string"),Servers[0].Port,9010);
    TestEqual(TEXT("Current server imports the exact server_port field"),Servers[1].Port,9047);
    TestEqual(TEXT("XML entities decode for display"),Servers[1].Name,FString(TEXT("One & Two")));
    TestTrue(TEXT("Both Discord spellings supported"),!Servers[0].Discord.IsEmpty() && !Servers[1].Discord.IsEmpty());
    TestFalse(TEXT("Non-web external links blocked"),ACELoginProfile::IsWebLink(TEXT("file:///C:/Windows/System32/cmd.exe")));
    TestFalse(TEXT("DTD input rejected"),ACELoginProfile::ParseDirectory(TEXT("<!DOCTYPE bad><ArrayOfServerItem/>"),Servers,Error));
    TestEqual(TEXT("Failed refresh preserves usable directory"),Servers.Num(),2);

    FACELoginSettings P; P.Host=TEXT("old.invalid"); P.Account=TEXT("legacy"); P.Password=TEXT("fixture-only");
    P.MigrateLegacy(); P.MigrateLegacy();
    TestEqual(TEXT("Migration creates one saved server"),P.Servers.Num(),1);
    TestEqual(TEXT("Migration preserves one account without duplication"),P.Accounts.Num(),1);
    FACELoginSettings AccountOnly; AccountOnly.Account=TEXT("saved without server"); AccountOnly.Password=TEXT("fixture"); AccountOnly.MigrateLegacy();
    TestTrue(TEXT("An old account survives even if no host was saved"),AccountOnly.Servers.IsEmpty() && AccountOnly.Accounts.Num()==1);
    P.DatDirectory=TEXT("C:/Fixture game data"); P.Servers.Append(Servers);
    P.Accounts.Add({TEXT("alternate"),TEXT("other"),TEXT("other-secret")});
    const FString Path=FPaths::ProjectSavedDir()/TEXT("Automation/Launcher/Profile-")+FGuid::NewGuid().ToString()+TEXT(".dat");
    TestTrue(TEXT("Multiple profiles save encrypted"),ACELoginSettings::Save(P,Path));
    FACELoginSettings Restored; TestTrue(TEXT("Multiple profiles load"),ACELoginSettings::Load(Restored,Path));
    TestEqual(TEXT("Server list persists"),Restored.Servers.Num(),3);
    TestTrue(TEXT("Published ports survive profile save and reload"),Restored.Servers[1].Port==9010 && Restored.Servers[2].Port==9047);
    TestEqual(TEXT("Alternate accounts persist"),Restored.Accounts.Num(),2);
    TestEqual(TEXT("DAT folder persists"),Restored.DatDirectory,P.DatDirectory);
    TestTrue(TEXT("Active account and password survive restart"),Restored.SelectedAccountId==P.SelectedAccountId && Restored.Accounts[0].Password==P.Password);
    const FString Removed=Restored.SelectedServerId; Restored.RemoveServer(Removed);
    TestEqual(TEXT("Removing a server retains shared accounts"),Restored.Accounts.Num(),2);
    TestTrue(TEXT("Removing a server preserves selected login"),Restored.SelectedAccountId==P.SelectedAccountId && Restored.Password==P.Password);
    IFileManager::Get().Delete(*Path,false,true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELauncherWidgetTest,"ACE.Launcher.ResponsiveUI",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELauncherWidgetTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); GI->Init();
    TStrongObjectPtr<UACELoginWidget> Owner(CreateWidget<UACELoginWidget>(GI,UACELoginWidget::StaticClass())); auto* W=Owner.Get();
    W->ProfilePath=FPaths::ProjectSavedDir()/TEXT("Automation/Launcher/Widget-")+FGuid::NewGuid().ToString()+TEXT(".dat");
    auto Slate=W->TakeWidget();
    TestTrue(TEXT("Fresh installation has no developer server or account supplied"),W->Profile.Servers.IsEmpty() && W->Profile.Accounts.IsEmpty());
    W->RunAction(TEXT("newserver")); W->NameBox->SetText(FText::FromString(TEXT("Dereth · Home")));
    W->HostBox->SetText(FText::FromString(TEXT("home.example.invalid"))); W->RunAction(TEXT("saveserver"));
    TestEqual(TEXT("Custom server saved from form"),W->Profile.Servers.Num(),1);
    const FString HomeId=W->Profile.SelectedServerId;
    W->AccountBox->SetText(FText::FromString(TEXT("Adventurer"))); W->PasswordBox->SetText(FText::FromString(TEXT("fixture-password"))); W->RunAction(TEXT("saveaccount"));
    W->RunAction(TEXT("newaccount")); W->AccountBox->SetText(FText::FromString(TEXT("Archer"))); W->PasswordBox->SetText(FText::FromString(TEXT("fixture-two"))); W->RunAction(TEXT("saveaccount"));
    TestEqual(TEXT("Second identity does not overwrite first"),W->Profile.Accounts.Num(),2);
    W->RunAction(TEXT("newserver")); W->NameBox->SetText(FText::FromString(TEXT("A second world"))); W->HostBox->SetText(FText::FromString(TEXT("second.invalid")));
    W->RunAction(TEXT("gdle")); W->RunAction(TEXT("saveserver"));
    TestEqual(TEXT("Accounts are shared when switching to a new server"),W->AccountBox->GetText().ToString(),FString(TEXT("Archer")));
    TestEqual(TEXT("Every saved account is offered on each server"),W->AccountList->GetChildrenCount(),2);
    TestTrue(TEXT("Selected GDLE type stored"),W->Profile.SelectedServer()->bGDLE);
    W->RunAction(TEXT("selectserver"),HomeId);
    TestEqual(TEXT("Switching servers preserves the chosen identity"),W->AccountBox->GetText().ToString(),FString(TEXT("Archer")));
    W->RunAction(TEXT("selectaccount"),W->Profile.Accounts[1].Id);
    TestEqual(TEXT("Account selection restores alternate"),W->AccountBox->GetText().ToString(),FString(TEXT("Archer")));
    W->RunAction(TEXT("removeaccount")); W->RunAction(TEXT("cancelremove"));
    TestEqual(TEXT("Cancelled removal keeps accounts"),W->Profile.Accounts.Num(),2);
    W->Profile.Servers[0].Description.Reset(); W->SelectServer(HomeId);
    W->Directory=W->Profile.Servers;
    if (FApp::CanEverRender())
    {
        FWidgetRenderer Renderer(false,true);
        auto Capture=[&](const TCHAR* Name,FIntPoint Size)
        {
            TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(FVector2D(Size),TF_Bilinear,false));
            for (int Pass=0;Pass<5;++Pass) { Renderer.DrawWidget(Target.Get(),Slate,FVector2D(Size),0.f); FlushRenderingCommands(); W->NativeTick(W->GetCachedGeometry(),0); }
            TestTrue(TEXT("Responsive dashboard fits horizontal viewport"),W->DashboardSize->GetCachedGeometry().GetAbsoluteSize().X<=Size.X+1);
            TArray<FColor> Pixels; FReadSurfaceDataFlags ReadFlags; ReadFlags.SetLinearToGamma(true); Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,ReadFlags);
            TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
            FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Automation/Launcher")/(FString(Name)+TEXT(".png"))));
        };
        W->SetStatus(TEXT("Ready to return to Dereth.")); Capture(TEXT("Desktop"),FIntPoint(1600,1000));
        Capture(TEXT("VR"),FIntPoint(1100,900));
        TestTrue(TEXT("Status remains visible on headset"),W->StatusText->GetCachedGeometry().GetAbsolutePosition().Y>750 && W->StatusText->GetCachedGeometry().GetAbsolutePosition().Y<880);
        TestTrue(TEXT("Launch has a visible laid-out button"),W->LoginButton->GetCachedGeometry().GetLocalSize().Y>20);
        TestTrue(TEXT("Launch fits inside headset panel without scrolling"),W->LoginButton->GetCachedGeometry().GetAbsolutePosition().Y+W->LoginButton->GetCachedGeometry().GetLocalSize().Y<900);
        Capture(TEXT("Window720p"),FIntPoint(1280,720));
        Capture(TEXT("Desktop4K"),FIntPoint(3840,2160));
        Capture(TEXT("Narrow"),FIntPoint(650,1000));
        W->RunAction(TEXT("browser")); W->RunAction(TEXT("directoryselect"),HomeId); Capture(TEXT("Browser"),FIntPoint(1100,900));
        W->RunAction(TEXT("editserver")); Capture(TEXT("Editor"),FIntPoint(1100,900));
        W->RunAction(TEXT("files")); Capture(TEXT("Files"),FIntPoint(1100,900));
    }
    W->RunAction(TEXT("play")); W->RunAction(TEXT("removeaccount")); W->RunAction(TEXT("confirmremove"));
    TestEqual(TEXT("Confirmed removal deletes only selected account"),W->Profile.Accounts.Num(),1);
    W->RunAction(TEXT("removeserver")); W->RunAction(TEXT("confirmremove"));
    TestEqual(TEXT("Removing a server keeps shared accounts"),W->Profile.Accounts.Num(),1);
    FACELoginSettings Saved; TestTrue(TEXT("Actions survive restart"),ACELoginSettings::Load(Saved,W->ProfilePath));
    TestTrue(TEXT("Server deletion preserves the remaining shared identity after restart"),Saved.Accounts.Num()==1 && Saved.Account==TEXT("Adventurer"));
    TestFalse(TEXT("Removed alternate cannot reappear"),Saved.Accounts.ContainsByPredicate([](const auto& A){return A.Username==TEXT("Archer");}));
    W->NativeDestruct(); IFileManager::Get().Delete(*W->ProfilePath,false,true);
    Owner.Reset(); GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    return true;
}
// Exercise the actual AddToPlayerScreen host. Rendering TakeWidget() directly
// bypasses the viewport slot and cannot detect accidentally cleared anchors.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELauncherViewportTest,"ACE.Launcher.ViewportPlacement",
 EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACELauncherViewportTest::RunTest(const FString&)
{
    UWorld* World=GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
    auto* PC=World ? Cast<AACEPlayerController>(World->GetFirstPlayerController()) : nullptr;
    if (!TestNotNull(TEXT("Run in the launched client with its real player viewport"),PC)) return false;
    auto* W=PC->LoginWidget.Get();
    if (!TestNotNull(TEXT("Login lobby exists in the launched client"),W)) return false;
    TestTrue(TEXT("Login is attached to the player screen"),W->IsInViewport());
    auto* Viewport=UGameViewportSubsystem::Get();
    if (!TestNotNull(TEXT("Viewport subsystem owns the login slot"),Viewport)) return false;
    auto CheckSlot=[&]()
    {
        const auto Slot=Viewport->GetWidgetSlot(W);
        TestTrue(TEXT("Login fills both viewport axes"),Slot.Anchors==FAnchors(0,0,1,1));
        TestTrue(TEXT("Login has no stale position or size margins"),Slot.Offsets==FMargin(0));
    };
    CheckSlot();
    PC->ApplyViewportSizeToLoginWidget(); // Re-show/retry must preserve stretch too.
    CheckSlot();
    const FGeometry Screen=UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(PC);
    const FGeometry Lobby=W->GetCachedGeometry();
    TestTrue(TEXT("Lobby receives the full screen allocation"),Lobby.GetAbsoluteSize().Equals(Screen.GetAbsoluteSize(),2));
    for (UWidget* Control : TArray<UWidget*>{W->AccountBox,W->PasswordBox,W->LoginButton,W->StatusText})
    {
        const FGeometry Geometry=Control->GetCachedGeometry();
        const FVector2D Size=Geometry.GetLocalSize();
        const FVector2D First=Screen.AbsoluteToLocal(Geometry.LocalToAbsolute(FVector2D::ZeroVector));
        const FVector2D Last=Screen.AbsoluteToLocal(Geometry.LocalToAbsolute(Size));
        TestTrue(TEXT("Login controls have usable dimensions"),Size.X>50 && Size.Y>15);
        TestTrue(TEXT("Login controls are fully within the live viewport"),First.X>=0 && First.Y>=0 && Last.X<=Screen.GetLocalSize().X+1 && Last.Y<=Screen.GetLocalSize().Y+1);
    }
    return true;
}
#endif
