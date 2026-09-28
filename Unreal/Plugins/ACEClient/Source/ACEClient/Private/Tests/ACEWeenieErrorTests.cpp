#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Protocol/ACECombatChat.h"
#include "ACEOpcodes.h"

namespace
{
    struct FWeenieErrorEntry { uint32 Code; const TCHAR* Text; int32 ChatType; };
    #include "Protocol/ACEWeenieErrorStrings.inl"
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEWeenieErrorTest, "ACE.RetailParity.FailureMessages",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEWeenieErrorTest::RunTest(const FString& Parameters)
{
    using namespace ACECombatChat;
    TestEqual(TEXT("Full health includes the actual self or other player's name"),
        LookupWeenieErrorWithString(0x04FF, TEXT("PlayerName")), FString(TEXT("PlayerName is already at full health!")));
    TestEqual(TEXT("Healing response goes to retail error chat"), WeenieErrorChatType(0x04FF), ACEChatMessageType::ChatError);
    TestEqual(TEXT("Repeated PK name substitution"), LookupWeenieErrorWithString(0x0052, TEXT("Other Player")),
        FString(TEXT("You fail to affect Other Player because Other Player is not a player killer!")));
    TestEqual(TEXT("PK failure retains magic chat"), WeenieErrorChatType(0x0052), ACEChatMessageType::Magic);
    TestEqual(TEXT("Fellowship leadership notification"), LookupWeenieErrorWithString(0x050D, TEXT("Other Player")),
        FString(TEXT("Other Player is now the leader of this fellowship.")));
    TestEqual(TEXT("Fellowship notifications retain system chat"), WeenieErrorChatType(0x050D), ACEChatMessageType::System);
    TestEqual(TEXT("Blocked message includes server detail"), LookupWeenieErrorWithString(0x051F, TEXT("Reason")),
        FString(TEXT("Message Blocked: Reason")));
    TestEqual(TEXT("Salvage without a name uses retail default"), LookupWeenieErrorWithString(0x04BF, TEXT("")),
        FString(TEXT("The item was not suitable for salvaging.")));
    TestEqual(TEXT("Names are data, not recursive format expressions"), LookupWeenieErrorWithString(0x0052, TEXT("A%s{0}")),
        FString(TEXT("You fail to affect A%s{0} because A%s{0} is not a player killer!")));
    for (uint32 Code : {0u, 0x04DEu, 0x04DFu, 0x055Au, 0x055Eu})
    {
        TestEqual(TEXT("Free-text payload stays intact"), LookupWeenieErrorWithString(Code, TEXT("100% {0} %s")), FString(TEXT("100% {0} %s")));
        TestTrue(TEXT("Empty free-text does not manufacture an error"), LookupWeenieErrorWithString(Code, TEXT("")).IsEmpty());
    }
    const FString Fallback(TEXT("The server could not complete that action. Please try again."));
    TestEqual(TEXT("Unknown code never leaks hex into chat"), LookupWeenieErrorWithString(0xDEADBEEF, TEXT("PlayerName")), Fallback);
    TestEqual(TEXT("Known low-level failure has a readable fallback"), LookupWeenieError(0x0003), Fallback);
    TestEqual(TEXT("Missing required name is not shown as a broken sentence"), LookupWeenieError(0x04FF), Fallback);
    for (const FWeenieErrorEntry& Entry : WeenieErrorEntries)
    {
        const FString Label = FString::Printf(TEXT("Catalog 0x%04X"), Entry.Code);
        const FString Message = Entry.Text && FCString::Strstr(Entry.Text, TEXT("%s"))
            ? LookupWeenieErrorWithString(Entry.Code, TEXT("Example")) : LookupWeenieError(Entry.Code);
        TestFalse(Label + TEXT(" has no unresolved name"), Message.Contains(TEXT("%s")) || Message.Contains(TEXT("$s")));
        TestFalse(Label + TEXT(" has no hex fallback"), Message.Contains(TEXT("Error 0x")));
        TestEqual(Label + TEXT(" is silent only by explicit policy"), Message.IsEmpty(), Entry.Text && !*Entry.Text);
        if (Entry.Text && *Entry.Text) TestNotEqual(Label + TEXT(" has a specific explanation"), Message, Fallback);
    }
    return true;
}
#endif
