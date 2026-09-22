#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIElementManager.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEUILayoutPreparationTest, "ACE.RetailParity.UILayoutPreparation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FACEUILayoutPreparationTest::RunTest(const FString&)
{
    TStrongObjectPtr<UACEUIElementManager> Manager(NewObject<UACEUIElementManager>());
    Manager->Initialize();
    TStrongObjectPtr<UACEUILayoutResolver> Layout(NewObject<UACEUILayoutResolver>());
    Layout->Initialize(nullptr, Manager.Get());
    if (!Layout->LoadLayout(ACEUI::LayoutId::CharacterManagement)) return false;
    const auto Enter = Manager->FindElementByName(TEXT("EnterGameButton"));
    if (!TestTrue(TEXT("Character-selection control exists before preparation"), Enter.IsValid())) return false;
    Manager->SetFocusElement(Enter);
    const double Deadline = FPlatformTime::Seconds() + 15;
    int32 Batches = 0;
    bool Ready = false;
    do
    {
        // Zero budget still permits one unit of forward progress, never an
        // unbounded spin or a complete layout rebuild in a single frame.
        Ready = Layout->PrepareLayout(ACEUI::LayoutId::ClassicGameplay, 0);
        ++Batches;
        if (!Ready) FPlatformProcess::Sleep(.0001f);
    } while (!Ready && FPlatformTime::Seconds() < Deadline);
    TestTrue(TEXT("Preparation completes in multiple bounded batches"), Ready && Batches > 100);
    TestTrue(TEXT("Preparation preserves the displayed character-selection tree"), Manager->FindElementByName(TEXT("EnterGameButton")) == Enter);
    TestTrue(TEXT("Preparation preserves active text/button focus"), Manager->GetFocusElement() == Enter);
    if (!Ready || !Layout->LoadLayout(ACEUI::LayoutId::ClassicGameplay)) return false;

    TArray<FString> PreparedSnapshot;
    auto Snapshot = [](const TSharedPtr<FACEUIElement>& Root, TArray<FString>& Result)
    {
        TFunction<void(const TSharedPtr<FACEUIElement>&)> Visit;
        Visit = [&](const TSharedPtr<FACEUIElement>& Node)
        {
            Result.Add(FString::Printf(TEXT("%08X/%08X/%s/%d,%d,%d,%d/%d/%d/%d/%08X/%d"),
                Node->ElementId, Node->Type, *Node->ElementName, Node->X, Node->Y, Node->Width, Node->Height,
                Node->bPanelTab, Node->bActivatable, Node->bDefaultHidden, Node->ImageFileId, Node->Children.Num()));
            for (const auto& Child : Node->Children) Visit(Child);
        };
        Visit(Root);
    };
    Snapshot(Manager->GetSyntheticRoot(), PreparedSnapshot);
    TestTrue(TEXT("Real gameplay fixture includes the full retail tree"), PreparedSnapshot.Num() > 1000);
    // Compare against the existing synchronous retail loader, including child
    // order, visibility defaults and panel-registered text tab activation.
    if (!Layout->LoadLayout(ACEUI::LayoutId::ClassicGameplay)) return false;
    TArray<FString> SynchronousSnapshot;
    Snapshot(Manager->GetSyntheticRoot(), SynchronousSnapshot);
    TestTrue(TEXT("Prepared UI retains retail controls, geometry and tab behavior"), PreparedSnapshot == SynchronousSnapshot);

    // Cancellation must not leave a worker referring to an old manager.
    Layout->PrepareLayout(ACEUI::LayoutId::CharacterManagement);
    Layout->Shutdown();
    Layout->Initialize(nullptr, Manager.Get());
    TestTrue(TEXT("Shutdown during preparation permits a fresh layout load"), Layout->LoadLayout(ACEUI::LayoutId::CharacterManagement));
    return !HasAnyErrors();
}
#endif
