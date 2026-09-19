#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ACELoginSettings.h"
#include "ACESession.h"

// Explicit opt-in only: use this device's selected encrypted profile and stop at
// character selection. Never enter the world, write credentials, or auto-retry.
class FACELiveLoginCheck : public IAutomationLatentCommand
{
public:
    FACELiveLoginCheck(FAutomationTestBase* InTest, TSharedPtr<FACESession> InSession)
        : Test(InTest), Session(MoveTemp(InSession)), Started(FPlatformTime::Seconds()), LastTick(Started) {}
    virtual bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        Session->Tick(float(Now - LastTick)); LastTick = Now;
        if (Session->GetState() == EACESessionState::CharacterSelect)
        {
            Test->AddInfo(FString::Printf(TEXT("Live login reached character selection: %s, %d characters"),
                *Session->GetServerName(), Session->GetCharacters().Num()));
        }
        else if (Session->GetState() == EACESessionState::Failed) Test->AddError(Session->GetConnectionError());
        else if (Now - Started < 45.0) return false;
        else Test->AddError(TEXT("Live login did not reach character selection within 45 seconds"));
        Session->Disconnect();
        return true;
    }
private:
    FAutomationTestBase* Test;
    TSharedPtr<FACESession> Session;
    double Started, LastTick;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELiveLoginTest, "ACE.Network.LiveLogin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACELiveLoginTest::RunTest(const FString&)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("ACELiveLogin")))
    {
        AddInfo(TEXT("Skipped live connection (requires explicit -ACELiveLogin)."));
        return true;
    }
    FACELoginSettings Profile;
    FString ProfilePath = ACELoginSettings::GetPath();
    FParse::Value(FCommandLine::Get(), TEXT("ACELiveLoginProfile="), ProfilePath);
    if (!TestTrue(TEXT("Encrypted local login profile loads"), ACELoginSettings::Load(Profile, ProfilePath))) return false;
    const auto* Server = Profile.SelectedServer();
    const auto* Account = Profile.Accounts.FindByPredicate([&](const auto& A) { return A.Id == Profile.SelectedAccountId; });
    if (!TestNotNull(TEXT("Selected saved server"), Server) || !TestNotNull(TEXT("Selected saved account"), Account)) return false;
    FACELoginCredentials Credentials;
    Credentials.Host = Server->Host; Credentials.Port = Server->Port; Credentials.bGDLE = Server->bGDLE;
    Credentials.Account = Account->Username; Credentials.Password = Account->Password;
    auto Session = MakeShared<FACESession>();
    if (!TestTrue(TEXT("Open live connection"), Session->Connect(Credentials))) return false;
    ADD_LATENT_AUTOMATION_COMMAND(FACELiveLoginCheck(this, Session));
    return true;
}
#endif
