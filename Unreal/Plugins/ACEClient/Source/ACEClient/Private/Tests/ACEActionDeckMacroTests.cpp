#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Mods/ACEActionDeckSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEActionDeckMacroSyntaxTest,"ACE.Plugins.ActionDeckMacroSyntax",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEActionDeckMacroSyntaxTest::RunTest(const FString&)
{
    TArray<FACEDeckMacroStep> Steps;FString Error;
    TestTrue(TEXT("User example parses"),UACEActionDeckSubsystem::ParseMacro(TEXT("/hom\n/wait 30000\n/f \"I'm home.\""),Steps,Error));
    if(Steps.Num()!=3){AddError(TEXT("Expected three macro steps"));return false;}
    TestEqual(TEXT("Recall remains a native command"),Steps[0].Command,FString(TEXT("/hom")));
    TestEqual(TEXT("Wait is local, never sent to chat"),Steps[1].Command,FString());
    TestEqual(TEXT("Wait uses milliseconds"),Steps[1].WaitMilliseconds,30000);
    TestEqual(TEXT("Arguments and quotes remain as typed"),Steps[2].Command,FString(TEXT("/f \"I'm home.\"")));
    TestTrue(TEXT("Whitespace, CRLF and @ aliases work"),UACEActionDeckSubsystem::ParseMacro(TEXT(" \r\n\t@hom  \r\n @WAIT\t1000\r\n/f home\n"),Steps,Error));
    TestEqual(TEXT("Empty lines do not create steps"),Steps.Num(),3);
    for(const FString Bad:{TEXT("/hom\n/wait -1"),TEXT("/hom\n/wait 1.5"),TEXT("/hom\n/wait 3s"),TEXT("/hom\n/wait"),
        TEXT("/hom\n/wait 3600001"),TEXT("/hom\n/wait 99999999999999999999"),TEXT("/hom\n/wait 1 extra"),TEXT("/hom\n/"),
        TEXT("/wait 30000"),TEXT("\n "),TEXT("/hom\nhello")})
    {
        TestFalse(TEXT("Malformed macros fail before any command can run"),UACEActionDeckSubsystem::ParseMacro(Bad,Steps,Error));
        TestTrue(TEXT("Failure clears even valid preceding commands"),Steps.IsEmpty());
        TestFalse(TEXT("Failure provides an explanation"),Error.IsEmpty());
    }
    TestTrue(TEXT("Zero and maximum waits supported"),UACEActionDeckSubsystem::ParseMacro(TEXT("/wait 0\n/hom\n/wait 3600000\n/f home"),Steps,Error));
    FString Long;for(int32 I=0;I<129;++I)Long+=TEXT("/f hello\n");
    TestFalse(TEXT("Bounded number of macro steps"),UACEActionDeckSubsystem::ParseMacro(Long,Steps,Error));
    TestFalse(TEXT("Bounded command size"),UACEActionDeckSubsystem::ParseMacro(TEXT("/f ")+FString::ChrN(500,'a'),Steps,Error));
    TestFalse(TEXT("Bounded file content"),UACEActionDeckSubsystem::ParseMacro(FString::ChrN(8193,'a'),Steps,Error));
    return true;
}
#endif
