#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACELoginSettings.h"
#include "ACELoginWidget.h"
#include "UI/ACERetailTextBlock.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACELocalLoginSettingsTest,"ACE.RetailParity.LocalLoginSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACELocalLoginSettingsTest::RunTest(const FString& Parameters)
{
    const auto* Defaults=GetDefault<UACELoginWidget>();
    TestTrue(TEXT("Fresh login has no supplied server or credentials"),
        Defaults->DefaultHost.IsEmpty() && Defaults->DefaultAccount.IsEmpty() && Defaults->DefaultPassword.IsEmpty());
    const FString Path=FPaths::ProjectSavedDir()/TEXT("Automation/LoginSettings-")+FGuid::NewGuid().ToString()+TEXT(".dat");
    FACELoginSettings Entered;
    Entered.Host=TEXT("test.example.invalid"); Entered.Port=TEXT("9001");
    Entered.Account=TEXT("Local persistence fixture"); Entered.Password=TEXT("Test-only secret \u2603 \u00e9");
    FACELoginSettings Loaded;
    TestFalse(TEXT("Missing file does not invent saved values"),ACELoginSettings::Load(Loaded,Path));
    if (!TestTrue(TEXT("Local entries save successfully"),ACELoginSettings::Save(Entered,Path))) return false;
    TestTrue(TEXT("Entries restore in a new settings instance"),ACELoginSettings::Load(Loaded,Path));
    TestEqual(TEXT("Address round trips"),Loaded.Host,Entered.Host);
    TestEqual(TEXT("Port round trips"),Loaded.Port,Entered.Port);
    TestEqual(TEXT("Account round trips"),Loaded.Account,Entered.Account);
    TestTrue(TEXT("Unicode password round trips without printing it"),Loaded.Password==Entered.Password);
    TArray<uint8> Bytes; FFileHelper::LoadFileToArray(Bytes,*Path);
    const FTCHARToUTF8 Secret(*Entered.Password);
    bool ContainsSecret=false;
    for (int32 I=0;I+Secret.Length()<=Bytes.Num();++I)
        ContainsSecret |= FMemory::Memcmp(Bytes.GetData()+I,Secret.Get(),Secret.Length())==0;
    TestFalse(TEXT("Local file does not contain the plaintext password"),ContainsSecret);
    TArray<uint8> Tampered = Bytes; Tampered.Last() ^= 1;
    FFileHelper::SaveArrayToFile(Tampered,*Path);
    TestFalse(TEXT("Authenticated encryption rejects modified ciphertext"),ACELoginSettings::Load(Loaded,Path));
    TestTrue(TEXT("Invalid ciphertext cannot replace previously loaded credentials"),Loaded.Password==Entered.Password);
    Entered.Host.Empty(); Entered.Account.Empty(); Entered.Password.Empty(); Entered.Port.Empty();
    TestTrue(TEXT("Clearing fields replaces previously saved values"),ACELoginSettings::Save(Entered,Path));
    TestTrue(TEXT("Cleared fields restore"),ACELoginSettings::Load(Loaded,Path));
    TestTrue(TEXT("Cleared credentials cannot return at the next launch"),Loaded.Host.IsEmpty()
        && Loaded.Port.IsEmpty() && Loaded.Account.IsEmpty() && Loaded.Password.IsEmpty());
    FFileHelper::SaveArrayToFile(TArray<uint8>{1,2,3,4},*Path);
    TestFalse(TEXT("Damaged settings are rejected without crashing"),ACELoginSettings::Load(Loaded,Path));
    TestTrue(TEXT("Damaged settings leave empty defaults untouched"),Loaded.Host.IsEmpty() && Loaded.Password.IsEmpty());
    IFileManager::Get().Delete(*Path,false,true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailTextLifetimeTest,"ACE.RetailParity.TextLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACERetailTextLifetimeTest::RunTest(const FString& Parameters)
{
    // Slate may retain a detached label after its UMG owner is collected. In
    // the reported stack this fell into PlainTextLayoutMarshaller::SetText,
    // reading the StrikeBrush pointer installed by UTextBlock::SynchronizeProperties.
    TStrongObjectPtr<UACERetailTextBlock> Owner(NewObject<UACERetailTextBlock>());
    Owner->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14));
    Owner->SetText(FText::FromString(TEXT("Detached retail label")));
    auto Slate=Owner->TakeWidget();
    Slate->SlatePrepass(1.f);
    TestTrue(TEXT("Live label still supports fallback before DAT is available"),Slate->GetDesiredSize().X>0);
    TWeakObjectPtr<UACERetailTextBlock> Released(Owner.Get());
    Owner.Reset();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Retained Slate does not keep the detached UObject alive"),Released.IsValid());
    Slate->Invalidate(EInvalidateWidgetReason::Layout);
    Slate->SlatePrepass(1.f);
    TestTrue(TEXT("Prepass after owner release does not access stale text style"),Slate->GetDesiredSize().IsNearlyZero());
    if (FApp::CanEverRender())
    {
        FWidgetRenderer Renderer(true,true);
        TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(FVector2D(256,32),TF_Bilinear,true));
        Renderer.DrawWidget(Target.Get(),Slate,FVector2D(256,32),0.f);
        FlushRenderingCommands();
        TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        bool Clear=!Pixels.IsEmpty();
        for (const FColor Pixel:Pixels) Clear &= Pixel.A==0;
        TestTrue(TEXT("Paint after owner release emits no stale text"),Clear);
    }
    return true;
}
#endif
