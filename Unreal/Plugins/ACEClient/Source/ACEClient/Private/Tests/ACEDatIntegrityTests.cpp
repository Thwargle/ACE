#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Dat/ACEDatDatabase.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
constexpr uint32 SectorSize = 2048;
constexpr uint32 DirectoryEntries = 4 + 62 * 4;
void WriteU32(TArray<uint8>& Bytes, int32 Offset, uint32 Value)
{
    FMemory::Memcpy(Bytes.GetData() + Offset, &Value, sizeof(Value));
}
TArray<uint8> MakeDat(int32 Directories = 1)
{
    TArray<uint8> Bytes;
    Bytes.SetNumZeroed((Directories + 2) * SectorSize);
    WriteU32(Bytes, FACEDatDatabase::HeaderOffset + 4, SectorSize);
    WriteU32(Bytes, FACEDatDatabase::HeaderOffset + 8, Bytes.Num());
    WriteU32(Bytes, FACEDatDatabase::HeaderOffset + 32, SectorSize);
    return Bytes;
}
void AddEntry(TArray<uint8>& Bytes, uint32 Directory, uint32 Id, uint32 Size = 4)
{
    WriteU32(Bytes, Directory + DirectoryEntries, 1);
    const uint32 Entry = Directory + DirectoryEntries + 4;
    WriteU32(Bytes, Entry + 4, Id);
    WriteU32(Bytes, Entry + 8, Bytes.Num() - SectorSize);
    WriteU32(Bytes, Entry + 12, Size);
    WriteU32(Bytes, Bytes.Num() - SectorSize + 4, 0x12345678);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEDatIntegrityTest, "ACE.DAT.DirectoryIntegrity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEDatIntegrityTest::RunTest(const FString&)
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/DatIntegrity-") + FGuid::NewGuid().ToString();
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = Directory / TEXT("fixture.dat");
    FACEDatDatabase Database;
    auto Open = [&](const TArray<uint8>& Bytes)
    {
        Database.Close();
        TestTrue(TEXT("Write DAT fixture"), FFileHelper::SaveArrayToFile(Bytes, *Path));
        return Database.Open(Path);
    };
    auto Reject = [&](const TCHAR* Name, const TArray<uint8>& Bytes, bool GraphError)
    {
        if (GraphError) AddExpectedError(TEXT("ACEDat: invalid directory graph"), EAutomationExpectedErrorFlags::Contains, 1);
        AddExpectedError(TEXT("ACEDat: failed to index B-tree"), EAutomationExpectedErrorFlags::Contains, 1);
        TestFalse(Name, Open(Bytes));
        TestFalse(TEXT("Rejected DAT is not open"), Database.IsOpen());
        TestEqual(TEXT("Rejected DAT exposes no partial index"), Database.GetFileCount(), 0);
    };

    auto Bytes = MakeDat();
    AddEntry(Bytes, SectorSize, 0x01000001);
    TestTrue(TEXT("Valid single-leaf DAT opens"), Open(Bytes));
    TArray<uint8> Content;
    TestTrue(TEXT("Valid resource reads"), Database.ReadFile(0x01000001, Content));
    TestEqual(TEXT("Resource length"), Content.Num(), 4);
    if (Content.Num() == 4) TestEqual(TEXT("Resource contents"), Content[0], uint8(0x78));

    Bytes = MakeDat();
    WriteU32(Bytes, SectorSize + 4, SectorSize);
    Reject(TEXT("Self-referencing root fails without recursion"), Bytes, true);

    Bytes = MakeDat(2);
    WriteU32(Bytes, SectorSize + 4, SectorSize * 2);
    WriteU32(Bytes, SectorSize * 2 + 4, SectorSize);
    Reject(TEXT("Two-directory cycle fails"), Bytes, true);

    Bytes = MakeDat(2);
    AddEntry(Bytes, SectorSize, 0x01000002);
    AddEntry(Bytes, SectorSize * 2, 0x01000001);
    WriteU32(Bytes, SectorSize + 4, SectorSize * 2);
    WriteU32(Bytes, SectorSize + 8, SectorSize * 2);
    Reject(TEXT("Shared child is rejected and its partial index is cleared"), Bytes, true);

    Bytes = MakeDat(65);
    for (uint32 Index = 1; Index < 65; ++Index)
        WriteU32(Bytes, Index * SectorSize + 4, (Index + 1) * SectorSize);
    Reject(TEXT("Excessive directory depth is bounded"), Bytes, true);

    Bytes = MakeDat();
    WriteU32(Bytes, SectorSize + 4, Bytes.Num() + SectorSize);
    Reject(TEXT("Directory past end of file fails"), Bytes, false);
    Bytes.SetNum(SectorSize + 100);
    Reject(TEXT("Truncated directory fails"), Bytes, false);

    Bytes.SetNum(100);
    TestFalse(TEXT("Truncated header fails before seeking beyond EOF"), Open(Bytes));

    Bytes = MakeDat();
    AddEntry(Bytes, SectorSize, 0x01000001, SectorSize);
    WriteU32(Bytes, Bytes.Num() - SectorSize, Bytes.Num() + SectorSize);
    TestTrue(TEXT("Resource links are checked when read"), Open(Bytes));
    TestFalse(TEXT("Resource next-sector pointer past EOF fails safely"), Database.ReadFile(0x01000001, Content));

    Bytes = MakeDat();
    AddEntry(Bytes, SectorSize, 0x01000001, SectorSize);
    TestTrue(TEXT("Truncated resource can still be indexed"), Open(Bytes));
    TestFalse(TEXT("Resource payload past EOF fails safely"), Database.ReadFile(0x01000001, Content));

    Bytes = MakeDat();
    AddEntry(Bytes, SectorSize, 0x01000001, 0xffffffffu);
    TestTrue(TEXT("Untrusted resource lengths do not allocate during indexing"), Open(Bytes));
    TestFalse(TEXT("Oversized resource fails before array allocation"), Database.ReadFile(0x01000001, Content));
    TestTrue(TEXT("Failed resource read clears old content"), Content.IsEmpty());

    Bytes = MakeDat(3);
    AddEntry(Bytes, SectorSize, 0x01000002);
    AddEntry(Bytes, SectorSize * 2, 0x01000001);
    AddEntry(Bytes, SectorSize * 3, 0x01000003);
    WriteU32(Bytes, SectorSize + 4, SectorSize * 2);
    WriteU32(Bytes, SectorSize + 8, SectorSize * 3);
    TestTrue(TEXT("Valid multi-level DAT still opens after failures"), Open(Bytes));
    TestEqual(TEXT("Root and both children indexed"), Database.GetFileCount(), 3);
    TestTrue(TEXT("Resource in child reads"), Database.ReadFile(0x01000003, Content));
    Database.Close();
    IFileManager::Get().Delete(*Path);
    IFileManager::Get().DeleteDirectory(*Directory);
    return !HasAnyErrors();
}
#endif
