#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VR/ACEVRTriggerState.h"
#include "VR/ACEVRPointerVisuals.h"
#include "VR/ACEVRWidgetComponent.h"
#include "Materials/Material.h"
#include "GameFramework/InputSettings.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRPointerInputTest, "ACE.VR.PointerInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEVRPointerInputTest::RunTest(const FString&)
{
	for (bool DigitalFirst : {false, true})
	{
		FACEVRTriggerState State;
		int32 Presses = 0, Releases = 0;
		auto Digital = [&](bool Value) { if (State.SetDigital(Value)) State.IsPressed() ? ++Presses : ++Releases; };
		auto Analog = [&](float Value) { if (State.SetAnalog(Value)) State.IsPressed() ? ++Presses : ++Releases; };
		if (DigitalFirst) { Digital(true); Analog(.8f); }
		else { Analog(.8f); Digital(true); }
		Analog(.52f); Analog(.38f); // Threshold noise while the finger is held.
		if (DigitalFirst) { Digital(false); Analog(.1f); }
		else { Analog(.1f); Digital(false); }
		TestEqual(TEXT("Analog and click from one trigger pull dispatch one press"), Presses, 1);
		TestEqual(TEXT("Either event order releases one action"), Releases, 1);
		TestFalse(TEXT("A light resting touch does not cast"), State.SetAnalog(.4f));
		Analog(.8f); Analog(1.f); Analog(.5f); Analog(.34f);
		TestEqual(TEXT("Analog-only runtime dispatches one additional press"), Presses, 2);
		TestEqual(TEXT("Analog hysteresis holds and releases normally"), Releases, 2);
	}
	for (const TCHAR* Name : {TEXT("VRLeftTriggerValue"), TEXT("VRRightTriggerValue")})
	{
		TArray<FInputAxisKeyMapping> Mappings;
		GetDefault<UInputSettings>()->GetAxisMappingByName(Name, Mappings);
		TestTrue(TEXT("Analog trigger actions exist before OpenXR attaches its action set"), !Mappings.IsEmpty());
	}
	TStrongObjectPtr<UWidgetComponent> Panel(NewObject<UWidgetComponent>());
	Panel->SetVisibility(true); Panel->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Panel->SetRenderInMainPass(true);
	TestTrue(TEXT("A visible UI surface protects its pointer action"), ACEVRPointerVisuals::IsInteractivePanel(Panel.Get()));
	Panel->SetRenderInMainPass(false);
	TestFalse(TEXT("A hidden render source cannot swallow a world cast"), ACEVRPointerVisuals::IsInteractivePanel(Panel.Get()));
	Panel->SetRenderInMainPass(true); Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TestFalse(TEXT("A noninteractive UI surface cannot swallow a world cast"), ACEVRPointerVisuals::IsInteractivePanel(Panel.Get()));
	const auto* World = ACEVRPointerVisuals::GetMaterial(false);
	const auto* Overlay = ACEVRPointerVisuals::GetMaterial(true);
	if (TestNotNull(TEXT("World pointer material exists"), World) && TestNotNull(TEXT("UI pointer material exists"), Overlay))
	{
		TestFalse(TEXT("Casting rays retain wall occlusion"), World->GetMaterial()->bDisableDepthTest);
		TestTrue(TEXT("Menu pointer is visible above personal overlays"), Overlay->GetMaterial()->bDisableDepthTest);
		TestTrue(TEXT("Menu pointer sorts above all personal panels"), ACEVRPointerVisuals::OverlayLayer > UACEVRWidgetComponent::PersonalLayerBase + 9999);
		TestEqual(TEXT("Pointer glow has explicit alpha coverage without scene lighting"), World->GetBlendMode(), BLEND_AlphaComposite);
	}
	return true;
}
#endif
