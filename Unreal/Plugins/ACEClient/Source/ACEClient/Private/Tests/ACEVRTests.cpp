#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VR/ACEVRMath.h"
#include "ACEEquipmentRules.h"
#include "ACERuntimeOptions.h"
#include "UI/ACEFrameRateWidget.h"
#include "Dat/ACEPortalViewMask.h"
#include "VR/ACEVRComponent.h"
#include "../VR/ACEEnemyHealthBar.h"
#include "VR/ACEVRSettings.h"
#include "VR/ACEVRWidget.h"
#include "VR/ACEVRRetailSurface.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUICharGenBinder.h"
#include "UI/ACEUIFlow.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "Components/Border.h"
#include "Components/ScrollBox.h"
#include "Components/BoxComponent.h"
#include "Widgets/Layout/SScrollBox.h"
#include "ACEPlayerController.h"
#include "ACEWorldEntityActor.h"
#include "ACEClientSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACECombatStance.h"
#include "ACEDatSubsystem.h"
#include "ACELoginWidget.h"
#include "ACELoginSettings.h"
#include "ACELoadingScreenActor.h"
#include "ACESession.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/EditableTextBox.h"
#include "Blueprint/WidgetTree.h"
#include "Features/IModularFeatures.h"
#include "XRMotionControllerBase.h"
#include "MotionControllerComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/InputSettings.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include <limits>

namespace
{
	class FVRTestControllers : public FXRMotionControllerBase
	{
	public:
		bool Connected = true;
		bool LeftTracked = true;
		FVRTestControllers() { IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this); }
		~FVRTestControllers() { IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this); }
		FName GetMotionControllerDeviceTypeName() const override { return TEXT("ACEVRTest"); }
		bool GetControllerOrientationAndPosition(int32 Index, FName Source, FRotator& Rotation, FVector& Position, float Scale) const override
		{
			if (GetControllerTrackingStatus(Index, Source) != ETrackingStatus::Tracked) return false;
			Rotation = FRotator::ZeroRotator;
			Position = FVector(20, Source.ToString().StartsWith(TEXT("Left")) ? -20 : 20, 130);
			return true;
		}
		ETrackingStatus GetControllerTrackingStatus(int32 Index, FName Source) const override
		{
			return Connected && Index == 0 && (LeftTracked || !Source.ToString().StartsWith(TEXT("Left"))) ? ETrackingStatus::Tracked : ETrackingStatus::NotTracked;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRMathTest, "ACE.VR.GesturesAndSettings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEVRMathTest::RunTest(const FString& Parameters)
{
	if (auto* Scale=IConsoleManager::Get().FindConsoleVariable(TEXT("xr.SecondaryScreenPercentage.HMDRenderTarget")))
	{
		const float Saved=Scale->GetFloat(); const uint32 Priority=Scale->GetFlags() & ECVF_SetByMask;
		Scale->ClearFlags(ECVF_SetByMask);
		auto* Settings=NewObject<UACEVRSettings>(); Settings->RenderScale=80.f; Settings->ApplyRenderScale();
		TestEqual(TEXT("VR resolution applies to the actual OpenXR eye target"),Scale->GetFloat(),80.f);
		Settings->RenderScale=std::numeric_limits<float>::quiet_NaN(); Settings->ApplyRenderScale();
		TestEqual(TEXT("Invalid VR resolution restores a finite eye target"),Scale->GetFloat(),100.f);
		Scale->ClearFlags(ECVF_SetByMask); Scale->Set(Saved,static_cast<EConsoleVariableFlags>(Priority));
	}
	else AddError(TEXT("OpenXR HMD render-target resolution control is missing"));
	if (auto* Samples=IConsoleManager::Get().FindConsoleVariable(TEXT("r.MSAACount")))
	{
		const int32 Saved=Samples->GetInt(); const uint32 Priority=Samples->GetFlags() & ECVF_SetByMask;
		Samples->ClearFlags(ECVF_SetByMask);
		auto* S=NewObject<UACEVRSettings>(); S->MSAASamples=2; S->ApplyEdgeSmoothing();
		TestEqual(TEXT("VR sample setting only affects an MSAA renderer"), Samples->GetInt(), UACEVRSettings::SupportsMSAASettings() ? 2 : Saved);
		S->MSAASamples=8; S->Sanitize(); S->ApplyEdgeSmoothing();
		TestEqual(TEXT("Unsupported sample counts return to the Quest quality default"), S->MSAASamples, 4);
		Samples->ClearFlags(ECVF_SetByMask); Samples->Set(Saved,static_cast<EConsoleVariableFlags>(Priority));
	}
	ACEOutdoorPortalPlan::FAdmittedAperture Door;
	Door.WorldNormal=FVector::ForwardVector;
	Door.WorldVerts={FVector(100,-50,0),FVector(100,50,0),FVector(100,50,200),FVector(100,-50,200)};
	const auto Mask=FACEPortalViewMask::Build(FVector(0,0,100),{Door});
	TestTrue(TEXT("Left eye sees the terrain ray through its doorway edge"),Mask.ContainsFromEye(FVector(300,151,100),FVector(0,-3.2,100)));
	TestFalse(TEXT("Right eye keeps terrain behind its own doorway wall"),Mask.ContainsFromEye(FVector(300,151,100),FVector(0,3.2,100)));
	TestFalse(TEXT("Portal terrain never covers the room floor before the door"),Mask.ContainsFromEye(FVector(50,0,100),FVector(0,-3.2,100)));
	TestTrue(TEXT("A Touch stick at its measured gate reaches full movement speed"),FMath::IsNearlyEqual(ACEVRMath::DeadZone(FVector2D(0,.93),.2,.9).Y,1.));
	for (double X : {-.12,.12})
	{
		const FVector2D Input(X,.99), Output=ACEVRMath::AssistForward(Input,10);
		TestTrue(TEXT("Small forward deflection has no directional strafe bias"),FMath::IsNearlyZero(Output.X));
		TestTrue(TEXT("Forward assistance preserves stick magnitude"),FMath::IsNearlyEqual(Input.Size(),Output.Size()));
		TestTrue(TEXT("Forward assistance can be disabled"),ACEVRMath::AssistForward(Input,0).Equals(Input));
	}
	TestTrue(TEXT("Intentional diagonals remain diagonal"),ACEVRMath::AssistForward(FVector2D(.7,.7),10).Equals(FVector2D(.7,.7)));
	FACEWorldObject OneHand; OneHand.ValidLocations = ACEEquipMask::MeleeWeapon;
	TestTrue(TEXT("Retail one-handed weapon can use shield slot"),ACEEquipmentRules::CanWieldInSlot(OneHand,ACEEquipMask::Shield));
	OneHand.ValidLocations = ACEEquipMask::TwoHanded;
	TestFalse(TEXT("Two-handed weapon cannot become an offhand weapon"),ACEEquipmentRules::CanWieldInSlot(OneHand,ACEEquipMask::Shield));
	OneHand.ValidLocations = ACEEquipMask::Shield;
	TestTrue(TEXT("A shield can use its authored slot"),ACEEquipmentRules::CanWieldInSlot(OneHand,ACEEquipMask::Shield));
	for (const FRotator& Look : {FRotator(60,30,0),FRotator(-55,170,20),FRotator(0,0,45)})
	{
		const FTransform Head(Look,FVector(30,40,175)), Bind(FRotator(0,0,0),FVector(0,0,158));
		const FVector Eye(0,-8,17);
		const FTransform Body=ACEVRMath::BodyFromHead(Head,Bind,Eye,1.f);
		const FQuat Rotation=Head.GetRotation()*FRotator(0,-90,0).Quaternion()*Bind.GetRotation();
		TestTrue(TEXT("Looking up, down or rolling keeps the neck attached to the torso"),
			Body.TransformPosition(Bind.GetLocation()).Equals(Head.GetLocation()-Rotation.RotateVector(Eye),.001));
	}
	FACEWorldObject Arrow; Arrow.ItemType = ACEItemType::MissileWeapon; Arrow.CurrentWieldedLocation = ACEEquipMask::MissileAmmo;
	FACEWorldObject Sword; Sword.ItemType = ACEItemType::MeleeWeapon; Sword.CurrentWieldedLocation = ACEEquipMask::MeleeWeapon;
	FACEWorldObject Bow; Bow.ItemType = ACEItemType::MissileWeapon; Bow.CurrentWieldedLocation = ACEEquipMask::MissileWeapon;
	FACEWorldObject Wand; Wand.ItemType = ACEItemType::Caster; Wand.CurrentWieldedLocation = ACEEquipMask::Held;
	TestEqual(TEXT("Equipped retail arrows do not force missile mode with a sword"),
		ACECombatStance::ResolveEquippedMode({Arrow, Sword}), ACECombatMode::Melee);
	TestEqual(TEXT("Ammunition alone retains unarmed melee"), ACECombatStance::ResolveEquippedMode({Arrow}), ACECombatMode::Melee);
	TestEqual(TEXT("A bow and ammunition select missile mode"), ACECombatStance::ResolveEquippedMode({Arrow, Bow}), ACECombatMode::Missile);
	TestEqual(TEXT("A wand and remaining ammunition select magic"), ACECombatStance::ResolveEquippedMode({Arrow, Wand}), ACECombatMode::Magic);
	TestTrue(TEXT("Stick noise stays still"), ACEVRMath::DeadZone(FVector2D(.1, .1), .2f).IsNearlyZero());
	TestTrue(TEXT("Diagonal movement has unit maximum speed"), FMath::IsNearlyEqual(ACEVRMath::DeadZone(FVector2D(1, 1), .2f).Size(), 1.));
	TestTrue(TEXT("Nonfinite input cannot move"), ACEVRMath::DeadZone(FVector2D(std::numeric_limits<float>::quiet_NaN(), 1), .2f).IsNearlyZero());
	for (const FVector Hand : {FVector(20, 0, 0), FVector(200, 0, 0), FVector::ZeroVector, FVector(0, 0, 20)})
	{
		FVector Elbow, Wrist; ACEVRMath::SolveArm(FVector::ZeroVector, Hand, FVector(0, 0, -40), 30, 27, Elbow, Wrist);
		TestFalse(TEXT("Arm solve is finite"), Elbow.ContainsNaN() || Wrist.ContainsNaN());
		TestTrue(TEXT("Upper arm length preserved"), FMath::IsNearlyEqual(Elbow.Size(), 30., .001));
		TestTrue(TEXT("Forearm length preserved"), FMath::IsNearlyEqual(FVector::Distance(Elbow, Wrist), 27., .001));
	}
	ACEVRMath::FSwing Swing; float Speed = 0;
	{
		ACEVRMath::FSwing Pointing;
		Pointing.Elapsed = .12f; Pointing.Travel = 60.f;
		TestFalse(TEXT("Fast long-blade pointing with a still hand is not a strike"), Pointing.HasDeliberateHandMotion(FVector::ZeroVector, FVector(2, 0, 0)));
		Pointing.Elapsed = .25f;
		TestFalse(TEXT("Slowly reaching the sword toward a target is not a strike"), Pointing.HasDeliberateHandMotion(FVector::ZeroVector, FVector(20, 0, 0)));
		Pointing.Elapsed = .02f;
		TestFalse(TEXT("Brief tracking spikes cannot count as a sustained strike"), Pointing.HasDeliberateHandMotion(FVector::ZeroVector, FVector(30, 0, 0)));
		Pointing.Elapsed = .12f;
		TestTrue(TEXT("A sustained arm swing qualifies"), Pointing.HasDeliberateHandMotion(FVector::ZeroVector, FVector(30, 0, 0)));
		Pointing.Elapsed=.06f; Pointing.Travel=20.f;
		TestTrue(TEXT("A compact deliberate mace strike qualifies"),Pointing.HasDeliberateHandMotion(FVector::ZeroVector,FVector(12,0,0)));
	}
	TestFalse(TEXT("First tracked frame cannot hit"), Swing.Sample(FVector::ZeroVector, .02f, 140, true, Speed));
	int32 Hits = 0;
	for (int32 I = 1; I <= 30; ++I) if (Swing.Sample(FVector(I * 4, 0, 0), .02f, 140, true, Speed)) { ++Hits; Swing.Commit(); }
	TestEqual(TEXT("Continuous fast motion creates one swing"), Hits, 1);
	Swing.Sample(FVector(120, 0, 0), .02f, 140, true, Speed);
	TestFalse(TEXT("A single still frame cannot re-arm a strike"), Swing.bArmed);
	for (int32 I = 0; I < 8; ++I) Swing.Sample(FVector(120, 0, 0), .02f, 140, true, Speed);
	TestTrue(TEXT("Resting after a strike re-arms the next swing"), Swing.bArmed);
	Swing.Reset();
	for (int32 I = 0; I < 100; ++I)
		TestFalse(TEXT("Fast oscillating tracking jitter cannot accumulate a swing"), Swing.Sample(FVector(I % 2 ? 3 : 0, 0, 0), .01f, 140, true, Speed));
	TestFalse(TEXT("Tracking loss cancels swing"), Swing.Sample(FVector(120, 0, 0), .02f, 140, false, Speed));
	TestFalse(TEXT("Reacquired tracking cannot strike"), Swing.Sample(FVector(120, 0, 0), .02f, 140, true, Speed));
	TestFalse(TEXT("Tracking discontinuity cannot strike"), Swing.Sample(FVector(400, 0, 0), .02f, 140, true, Speed));
	float Draw = 0;
	TestTrue(TEXT("Pulling backward draws bow"), ACEVRMath::BowDraw(FVector::ZeroVector, FVector(-60, 0, 0), FVector::ForwardVector, 60, Draw));
	TestEqual(TEXT("Full draw fraction"), Draw, 1.f);
	TestTrue(TEXT("Support controller ray does not restrict two-hand bow aiming"), ACEVRMath::BowDraw(FVector::ZeroVector, FVector(60, 0, 0), FVector::UpVector, 60, Draw));
	TestTrue(TEXT("Diagonal physical draw is valid"), ACEVRMath::BowDraw(FVector::ZeroVector, FVector(-30, 40, 0), FVector::ForwardVector, 60, Draw));
	TestFalse(TEXT("An undrawn nock cannot fire"), ACEVRMath::BowDraw(FVector::ZeroVector, FVector(-5, 0, 0), FVector::ForwardVector, 60, Draw));
	TestFalse(TEXT("Unreachable tracking jump cannot draw"), ACEVRMath::BowDraw(FVector::ZeroVector, FVector(-200, 0, 0), FVector::ForwardVector, 60, Draw));
	TestTrue(TEXT("Small draw setting still permits a full physical arm span"),ACEVRMath::BowDraw(FVector::ZeroVector,FVector(-75,0,0),FVector::ForwardVector,30,Draw));
	TestEqual(TEXT("Overdraw stays at full power"),Draw,1.f);
	TestEqual(TEXT("Shift supports password symbols"), ACEVRMath::KeyboardCharacter('1', true), FString(TEXT("!")));
	TestEqual(TEXT("Shift supports backslash key"), ACEVRMath::KeyboardCharacter('\\', true), FString(TEXT("|")));
	TestEqual(TEXT("Shift supports capital letters"), ACEVRMath::KeyboardCharacter('a', true), FString(TEXT("A")));
	auto* Settings = NewObject<UACEVRSettings>(); Settings->SnapDegrees = std::numeric_limits<float>::quiet_NaN(); Settings->BowFullDraw = 0;
	Settings->Sanitize(); TestEqual(TEXT("Invalid snap resets safely"), Settings->SnapDegrees, 30.f); TestEqual(TEXT("Draw range bounded"), Settings->BowFullDraw, 30.f);
	Settings->MovementDirection=-1;Settings->bHeadRelativeMovement=false;Settings->Sanitize();
	TestEqual(TEXT("Existing hand-relative preference migrates"),Settings->MovementDirection,1);
	Settings->MovementDirection=2;Settings->Sanitize();TestEqual(TEXT("Stick-only direction survives settings sanitation"),Settings->MovementDirection,2);
	Settings->SettingsVersion = 0; Settings->WristScale = .045f; Settings->Sanitize();
	TestEqual(TEXT("Existing default wrist size upgrades once"), Settings->WristScale, .06f);
	Settings->SettingsVersion = 0; Settings->WristScale = .08f; Settings->Sanitize();
	TestEqual(TEXT("Customized wrist size survives migration"), Settings->WristScale, .08f);
	Settings->SettingsVersion = 1; Settings->MovementScale = .65f; Settings->Sanitize();
	TestEqual(TEXT("Old reduced default upgrades to full character speed"), Settings->MovementScale, 1.f);
	Settings->SettingsVersion = 1; Settings->MovementScale = .4f; Settings->Sanitize();
	TestEqual(TEXT("Customized comfort speed survives migration"), Settings->MovementScale, .4f);
	Settings->VitalsViewOffset = FVector(std::numeric_limits<double>::infinity(), 0, 0); Settings->Sanitize();
	TestFalse(TEXT("Invalid saved vitals placement cannot break the view transform"), Settings->VitalsViewOffset.ContainsNaN());
	const auto* Inputs = GetDefault<UInputSettings>();
	const FVector Half=ACEVRMath::BallisticOffset(FVector::ForwardVector,24.f,.5f,1.f,100.f);
	const FVector Full=ACEVRMath::BallisticOffset(FVector::ForwardVector,24.f,1.f,1.f,100.f);
	TestTrue(TEXT("A full draw extends the trajectory with the server launch-speed curve"),FMath::IsNearlyEqual(Half.X,1620.,.01) && FMath::IsNearlyEqual(Full.X,2400.,.01));
	TestTrue(TEXT("Arrow preview follows gravity rather than a straight ray"),FMath::IsNearlyEqual(Full.Z,-490.,.01));
	for (const FName Name : {FName("VRInventory"), FName("VRCombat"), FName("VRSettings"), FName("VRSelect"), FName("VRLeftTrigger"), FName("VRRightTrigger"), FName("VRLeftGrip"), FName("VRRightGrip"), FName("VRJump")})
	{
		TArray<FInputActionKeyMapping> Keys; Inputs->GetActionMappingByName(Name, Keys);
		TestTrue(FString::Printf(TEXT("Touch mapping exists at XR startup: %s"), *Name.ToString()), Keys.Num() > 0 && Keys[0].Key.IsValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRProtocolTest, "ACE.VR.Protocol", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEVRProtocolTest::RunTest(const FString& Parameters)
{
	FACESession Session;
	Session.State = EACESessionState::InWorld; Session.PlayerGuid = 100;
	TestFalse(TEXT("No VR combat without capability ACK"), Session.SendVRCombat(1, 0x12340001, 200, 1, 0, FVector(0, 0, 1.5), FVector(0, 1, 0), 1, 0));
	auto* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	auto Address = Sockets->CreateInternetAddr(); bool Valid = false; Address->SetIp(TEXT("127.0.0.1"), Valid); Address->SetPort(0);
	auto* Receiver = Sockets->CreateSocket(NAME_DGram, TEXT("VR protocol fixture"), false);
	if (!Receiver || !Receiver->Bind(*Address)) { if (Receiver) Sockets->DestroySocket(Receiver); AddError(TEXT("Loopback socket unavailable")); return false; }
	Receiver->GetAddress(*Address); Session.SocketC2S = Sockets->CreateSocket(NAME_DGram, TEXT("VR protocol sender"), false);
	Session.ServerC2SAddr = Address; Session.IssacClient = MakeUnique<FACEIsaac>(123);
	FACEBinaryWriter Ack; Ack.WriteUInt32(100); Ack.WriteUInt32(1); Ack.WriteUInt32(0xF7D0); Ack.WriteUInt32(1); Ack.WriteUInt32(7);
	FACEBinaryReader AR(Ack.GetData()); Session.HandleGameEvent(AR);
	TestTrue(TEXT("v1 feature ACK enables combat"), Session.SupportsVRCombat());
	TestFalse(TEXT("Older servers do not receive unarmed extension attacks"), Session.SendVRCombat(2,0x12340001,0,0,300,FVector(0,0,1),FVector(.3,0,1),1,.1));
	// The current server advertises world membership as an additional bit.
	// Capability negotiation must remain composable when optional features grow.
	FACEBinaryWriter CurrentAck; CurrentAck.WriteUInt32(100); CurrentAck.WriteUInt32(2); CurrentAck.WriteUInt32(0xF7D0);
	CurrentAck.WriteUInt32(1); CurrentAck.WriteUInt32(15); CurrentAck.WriteUInt32(12); CurrentAck.WriteUInt32(0);
	FACEBinaryReader CurrentAR(CurrentAck.GetData()); Session.HandleGameEvent(CurrentAR);
	TestTrue(TEXT("Current server combat plus snapshot ACK still enables combat"), Session.SupportsVRCombat());
	FACEBinaryWriter MissileAck;MissileAck.WriteUInt32(100);MissileAck.WriteUInt32(3);MissileAck.WriteUInt32(0xF7D0);
	MissileAck.WriteUInt32(1);MissileAck.WriteUInt32(55);MissileAck.WriteUInt32(0x80000EB2u);MissileAck.WriteFloat(24.f);
	FACEBinaryReader MissileAR(MissileAck.GetData());Session.HandleGameEvent(MissileAR);
	TestEqual(TEXT("Trajectory uses the speed negotiated for this launcher"),Session.GetVRMissileSpeed(static_cast<int32>(0x80000EB2u)),24.f);
	TestEqual(TEXT("A previous weapon's speed cannot be reused after swapping launchers"),Session.GetVRMissileSpeed(300),0.f);
	Session.CachedC2SPackets.Reset(); Session.TeleportSeq = 12;
	// Public child descriptions omit the wield mask. Repeated world snapshots
	// must preserve the private equipment profile for real high-bit GUIDs.
	FACEWorldObject Profile; Profile.Guid = static_cast<int32>(0x80000EB2u); Profile.WielderId = 100;
	Profile.CurrentWieldedLocation = ACEEquipMask::MissileWeapon;
	Session.UpsertWorldObject(Profile);
	FACEWorldObject PublicBow; PublicBow.Guid = Profile.Guid; PublicBow.ParentGuid = 100; PublicBow.ParentLocation = 1;
	PublicBow.ItemType = ACEItemType::MissileWeapon; PublicBow.SetupId = 0x0200158D;
	Session.UpsertWorldObject(PublicBow);
	TArray<FACEWorldObject> Equipped; Session.GetEquippedItems(Equipped);
	TestTrue(TEXT("Public attached-object refresh retains bow ownership and wield slot"), Equipped.ContainsByPredicate([&](const auto& O)
		{ return O.Guid == Profile.Guid && O.CurrentWieldedLocation == ACEEquipMask::MissileWeapon; }));
	PublicBow.ContainerId = 100; PublicBow.ParentGuid = PublicBow.ParentLocation = 0;
	Session.UpsertWorldObject(PublicBow); Session.GetEquippedItems(Equipped);
	TestFalse(TEXT("A real dequip still clears the wield slot"), Equipped.ContainsByPredicate([&](const auto& O) { return O.Guid == Profile.Guid; }));
	Session.WorldObjects.Remove(Profile.Guid);
	for(int64 WieldSlot:{ACEEquipMask::MissileAmmo,ACEEquipMask::MissileWeapon})
	{
	FACEWorldObject Arrows; Arrows.Guid = static_cast<int32>(0x80000EB4u); Arrows.WielderId = Arrows.ParentGuid = 100;
	Arrows.CurrentWieldedLocation = WieldSlot; Arrows.StackSize = 3; Arrows.MaxStackSize = 2500;
	Arrows.ParentGuid = 0;
	Session.WorldObjects.Add(Arrows.Guid, Arrows);
	FACEBinaryWriter EquipArrows; EquipArrows.WriteUInt32(uint32(Arrows.Guid)); EquipArrows.WriteUInt32(uint32(WieldSlot));
	FACEBinaryReader EquipArrowInput(EquipArrows.GetData()); Session.HandleWieldItem(EquipArrowInput);
	if(WieldSlot==ACEEquipMask::MissileAmmo) TestEqual(TEXT("Equipping arrows does not invent a quiver attachment before server reload"), Session.WorldObjects[Arrows.Guid].ParentGuid, 0);
	TestEqual(TEXT("Unloaded ammunition or thrown stack remains equipped"), Session.WorldObjects[Arrows.Guid].CurrentWieldedLocation, WieldSlot);
	for (int32 Remaining : {2, 1, 0})
	{
		FACEBinaryWriter Pickup; Pickup.WriteUInt32(uint32(Arrows.Guid)); Pickup.WriteUInt16(1); Pickup.WriteUInt16(7 - Remaining * 2);
		FACEBinaryReader PickupInput(Pickup.GetData()); Session.HandlePickupEvent(PickupInput);
		TestEqual(TEXT("Launch detaches only the missile visual, preserving the equipped stack"), Session.WorldObjects[Arrows.Guid].CurrentWieldedLocation, WieldSlot);
		TestEqual(TEXT("Launched stack is never falsely moved into the backpack"), Session.WorldObjects[Arrows.Guid].ContainerId, 0);
		if (Remaining)
		{
			FACEBinaryWriter Count; Count.WriteUInt8(1); Count.WriteUInt32(uint32(Arrows.Guid)); Count.WriteUInt32(Remaining); Count.WriteUInt32(Remaining);
			FACEBinaryReader CountInput(Count.GetData()); Session.HandleSetStackSize(CountInput);
			FACEBinaryWriter Parent; Parent.WriteUInt32(100); Parent.WriteUInt32(uint32(Arrows.Guid)); Parent.WriteUInt32(1); Parent.WriteUInt32(1);
			Parent.WriteUInt16(1); Parent.WriteUInt16(8 - Remaining * 2);
			TestEqual(TEXT("Server reload attachment body is exactly twenty bytes"), Parent.GetData().Num(), 20);
			FACEBinaryReader ParentInput(Parent.GetData()); Session.HandleParentEvent(ParentInput);
			TestEqual(TEXT("Reload reattaches the remaining arrows"), Session.WorldObjects[Arrows.Guid].ParentGuid, 100);
			Session.GetEquippedItems(Equipped);
			TestTrue(TEXT("A second and third draw can resolve the remaining stack"), Equipped.ContainsByPredicate([&](const auto& O) { return O.Guid == Arrows.Guid && O.StackSize == Remaining; }));
		}
		else
		{
			FACEBinaryWriter Remove; Remove.WriteUInt32(uint32(Arrows.Guid)); FACEBinaryReader RemoveInput(Remove.GetData()); Session.HandleInventoryRemoveObject(RemoveInput);
			TestFalse(TEXT("Only consuming the final arrow removes its stack"), Session.WorldObjects.Contains(Arrows.Guid));
		}
	}
	}
	TestTrue(TEXT("Aimed spell sends reliable action"), Session.SendVRCombat(1, 0x12340001, 200, 42, 300, FVector(.2, .3, 1.5), FVector(0, 1, 0), 1, .25));
	uint32 Sequence = 0; for (const auto& Pair : Session.CachedC2SPackets) Sequence = FMath::Max(Sequence, Pair.Key);
	if (TestTrue(TEXT("Action has packet"), Sequence != 0))
	{
		FACEBinaryReader Wire(Session.CachedC2SPackets[Sequence].Payload); Wire.Skip(16);
		TestEqual(TEXT("Authenticated game-action envelope"), Wire.ReadUInt32(), ACEOpcode::GameAction); Wire.ReadUInt32();
		TestEqual(TEXT("Extension opcode"), Wire.ReadUInt32(), 0xF7D0u);
		TestEqual(TEXT("Server v1 payload byte count"), Wire.Remaining(), 64);
		TestEqual(TEXT("Version"), Wire.ReadUInt32(), 1u); TestEqual(TEXT("Kind"), Wire.ReadUInt32(), 1u);
		TestEqual(TEXT("Replay sequence"), Wire.ReadUInt32(), 1u); TestEqual(TEXT("Cell"), Wire.ReadUInt32(), 0x12340001u);
		TestEqual(TEXT("Teleport epoch"), Wire.ReadUInt32(), 12u); TestEqual(TEXT("Weapon"), Wire.ReadUInt32(), 200u);
		TestEqual(TEXT("Spell"), Wire.ReadUInt32(), 42u); TestEqual(TEXT("Target"), Wire.ReadUInt32(), 300u);
		for (float Expected : {.2f, .3f, 1.5f, 0.f, 1.f, 0.f, 1.f, .25f}) TestEqual(TEXT("Aim/cost payload float"), Wire.ReadFloat(), Expected);
	}
	FACEBinaryWriter PunchAck; PunchAck.WriteUInt32(100); PunchAck.WriteUInt32(4); PunchAck.WriteUInt32(0xF7D0);
	PunchAck.WriteUInt32(1); PunchAck.WriteUInt32(135);
	FACEBinaryReader PunchAR(PunchAck.GetData()); Session.HandleGameEvent(PunchAR);
	TestTrue(TEXT("Optional unarmed capability enables empty-hand strikes"), Session.SupportsVRUnarmed());
	TestTrue(TEXT("Empty offhand can send a tracked punch"),Session.SendVRCombat(2,0x12340001,0,1,300,FVector(0,0,1),FVector(.3,0,1),1,.1));
	TestFalse(TEXT("Unarmed capability never permits a weaponless spell"),Session.SendVRCombat(1,0x12340001,0,1,300,FVector(0,0,1),FVector(0,1,0),1,0));
	TestFalse(TEXT("Unknown punch hand is rejected"),Session.SendVRCombat(2,0x12340001,0,2,300,FVector(0,0,1),FVector(.3,0,1),1,.1));
	FACEWorldObject Self; Self.Guid = 100; Session.WorldObjects.Add(100, Self);
	FACEWorldObject Bag; Bag.Guid = 201; Bag.ContainerId = 100; Session.WorldObjects.Add(201, Bag);
	FACEWorldObject Item; Item.Guid = 202; Item.ContainerId = 201; Session.WorldObjects.Add(202, Item);
	FACEWorldObject Worn; Worn.Guid = 203; Worn.WielderId = 100; Session.WorldObjects.Add(203, Worn);
	FACEWorldObject Stock; Stock.Guid = 204; Stock.ContainerId = 900; Session.WorldObjects.Add(204, Stock);
	FACEWorldObject Live; Live.Guid = 300; Session.WorldObjects.Add(300, Live);
	FACEWorldObject Old; Old.Guid = 301; Session.WorldObjects.Add(301, Old);
	Session.SelectObject(301);
	TArray<int32> Deleted; Session.OnObjectDeleted.AddLambda([&](int32 Guid) { Deleted.Add(Guid); });
	for (uint32 Epoch : {11u, 12u})
	{
		FACEBinaryWriter Snapshot; Snapshot.WriteUInt32(Epoch); Snapshot.WriteUInt32(1); Snapshot.WriteUInt32(300);
		FACEBinaryReader Input(Snapshot.GetData()); Session.ApplyVRWorldSnapshot(Input);
		TestEqual(TEXT("Only the current teleport epoch can remove a stale NPC"), Session.WorldObjects.Contains(301), Epoch != 12u);
	}
	TestTrue(TEXT("Authoritative membership preserves the live NPC"), Session.WorldObjects.Contains(300));
	TestEqual(TEXT("Snapshot removal clears an old area's selected enemy"),Session.GetSelectedObject().Guid,0);
	const auto SavedPlayerPosition=Session.PlayerPosition;
	Session.PlayerPosition.CellId=0x7D640001;Session.PlayerPosition.Location=FVector(44,94,12);
	Session.WorldObjects[300].bHasPosition=true;Session.WorldObjects[300].Position=Session.PlayerPosition;
	TestTrue(TEXT("A nearby creature can send health feedback"),Session.IsNearbyHealthObject(300));
	Session.PlayerPosition.CellId=0x00640001;
	TestFalse(TEXT("Retaining a cached creature after distant teleport cannot show its health"),Session.IsNearbyHealthObject(300));
	Session.PlayerPosition=SavedPlayerPosition;
	for (int32 Guid : {100, 201, 202, 203, 204}) TestTrue(TEXT("Snapshot preserves self, nested inventory, equipment, and vendor stock"), Session.WorldObjects.Contains(Guid));
	TestEqual(TEXT("Stale NPC deletion reaches the world presenter once"), Deleted.Num(), 1);
	FACEBinaryWriter Truncated; Truncated.WriteUInt32(12); Truncated.WriteUInt32(2); Truncated.WriteUInt32(100);
	FACEBinaryReader BadSnapshot(Truncated.GetData()); Session.ApplyVRWorldSnapshot(BadSnapshot);
	TestTrue(TEXT("Truncated membership cannot erase live NPCs"), Session.WorldObjects.Contains(300));
	Session.WorldObjects.Add(301,Old);
	FACEBinaryWriter FullAck;for(uint32 V:{100u,5u,0xF7D0u,1u,511u,12u,1u,300u,200u})FullAck.WriteUInt32(V);FullAck.WriteFloat(24.f);
	FACEBinaryReader FullAR(FullAck.GetData());Session.HandleGameEvent(FullAR);
	TestFalse(TEXT("Membership consumes exactly its GUIDs before the missile profile trailer"),Session.WorldObjects.Contains(301));
	TestEqual(TEXT("Membership leaves the missile speed readable"),Session.GetVRMissileSpeed(200),24.f);
	FACEBinaryWriter ProfilePacket;for(uint32 V:{100u,6u,0xF7D3u,2717u})ProfilePacket.WriteUInt32(V);ProfilePacket.WriteFloat(30.f);ProfilePacket.WriteUInt32(1);
	FACEBinaryReader ProfileReader(ProfilePacket.GetData());Session.HandleGameEvent(ProfileReader);
	float ArcSpeed=0;bool Gravity=false;
	TestTrue(TEXT("Negotiated arc preview reads exact authoritative speed and gravity"),Session.GetVRSpellProfile(2717,ArcSpeed,Gravity) && ArcSpeed==30.f && Gravity);
	FACEBinaryWriter RadiusPacket;for(uint32 V:{100u,7u,0xF7D3u,2717u})RadiusPacket.WriteUInt32(V);
	RadiusPacket.WriteFloat(30.f);RadiusPacket.WriteUInt32(1);RadiusPacket.WriteFloat(.12f);
	FACEBinaryReader RadiusReader(RadiusPacket.GetData());Session.HandleGameEvent(RadiusReader);float ArcRadius=0;
	TestTrue(TEXT("Arc preview reads authoritative projectile radius when supplied"),Session.GetVRSpellProfile(2717,ArcSpeed,Gravity,&ArcRadius) && FMath::IsNearlyEqual(ArcRadius,.12f));
	Session.VRCapabilities |= 2048u;
	auto Recovery = [&](uint32 Seq, uint32 Epoch, float Remaining, float Duration)
	{
		FACEBinaryWriter W; W.WriteUInt32(Seq); W.WriteUInt32(Epoch); W.WriteFloat(Remaining); W.WriteFloat(Duration);
		FACEBinaryReader R(W.GetData()); Session.HandleVRRecovery(R);
	};
	Recovery(Session.VRSequence,12,1.f,2.f);
	TestTrue(TEXT("Server recovery gives a live countdown and progress"),Session.GetVRRecoveryRemaining()>.9f && FMath::IsNearlyEqual(Session.GetVRRecoveryProgress(),.5f,.05f));
	const double ReadyAt=Session.VRRecoveryReadyAt;
	Recovery(Session.VRSequence,12,10.f,10.f);
	TestEqual(TEXT("Duplicate recovery cannot restart the countdown"),Session.VRRecoveryReadyAt,ReadyAt);
	++Session.VRSequence;
	Recovery(Session.VRSequence,11,1.f,2.f);
	Recovery(Session.VRSequence,12,3.f,2.f);
	Recovery(Session.VRSequence,12,NAN,2.f);
	Recovery(Session.VRSequence+1,12,1.f,2.f);
	TestEqual(TEXT("Wrong epoch, invalid times and unsolicited sequence cannot change recovery"),Session.VRRecoveryReadyAt,ReadyAt);
	++Session.TeleportSeq;
	TestEqual(TEXT("A portal immediately invalidates recovery"),Session.GetVRRecoveryRemaining(),0.f);
	--Session.TeleportSeq;
	Session.VRRecoveryReadyAt=FPlatformTime::Seconds()-.01;
	TestEqual(TEXT("Expired recovery is ready"),Session.GetVRRecoveryProgress(),1.f);
	Session.VRCapabilities |= 4096u | 16384u;
	const uint32 CastSequence=++Session.VRSequence;
	auto Casting=[&](uint32 Seq,uint32 Epoch,uint32 Phase,float Remaining,float Duration) {
		FACEBinaryWriter W;for(uint32 V:{Seq,Epoch,42u,Phase}) W.WriteUInt32(V);W.WriteFloat(Remaining);W.WriteFloat(Duration);
		FACEBinaryReader R(W.GetData());Session.HandleVRCasting(R);
	};
	Casting(CastSequence,12,1,1,2);
	TestTrue(TEXT("Cast windup reports server remaining time and progress"),Session.GetVRCastPhase()==1 && Session.GetVRCastRemaining()>.9f && FMath::IsNearlyEqual(Session.GetVRCastProgress(),.5f,.05f));
	const double CastReady=Session.VRCastReadyAt;
	Casting(CastSequence,12,1,2,2);Casting(CastSequence+1,12,1,2,2);Casting(CastSequence,11,0,0,0);
	TestEqual(TEXT("Duplicate, future and old-world cast messages cannot restart or cancel it"),Session.VRCastReadyAt,CastReady);
	Casting(CastSequence,12,2,0,0);TestEqual(TEXT("Release moves timer into recovery"),Session.GetVRCastPhase(),2u);
	Casting(CastSequence,12,0,0,0);TestEqual(TEXT("Completion makes casting ready"),Session.GetVRCastPhase(),0u);
	Casting(CastSequence,12,1,1,2);TestEqual(TEXT("A late windup cannot resurrect a completed cast"),Session.GetVRCastPhase(),0u);
	Session.ClearWorldState(); TestFalse(TEXT("Logout clears capabilities"), Session.SupportsVRCombat());
	TestEqual(TEXT("Logout clears the cast state"),Session.VRCastSequence,0u);
	TestEqual(TEXT("Logout clears the old countdown sequence"),Session.VRRecoverySequence,0u);
	TestFalse(TEXT("Logout clears the previous server's spell profiles"),Session.GetVRSpellProfile(2717,ArcSpeed,Gravity));
	Session.State = EACESessionState::Disconnected; Session.Disconnect(); Sockets->DestroySocket(Receiver);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRRigTest, "ACE.VR.RigAndMenus", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FACEVRRigTest::RunTest(const FString& Parameters)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
	auto* GI = NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
	auto* DummyWorld = GI->GetWorld();
	GI->GetWorldContext()->SetCurrentWorld(World); World->SetGameInstance(GI); DummyWorld->DestroyWorld(false);
	auto* PC = World->SpawnActor<AACEPlayerController>(); auto* Pawn = World->SpawnActor<APawn>();
	PC->SetPlayer(NewObject<ULocalPlayer>(GEngine));
	World->AddController(PC);
	auto* Capsule = NewObject<UCapsuleComponent>(Pawn); Capsule->InitCapsuleSize(34, 96); Pawn->SetRootComponent(Capsule); Capsule->RegisterComponent();
	auto* Camera = NewObject<UCameraComponent>(Pawn); Camera->SetupAttachment(Capsule); Camera->RegisterComponent();
	PC->Possess(Pawn);
	auto* VR = NewObject<UACEVRComponent>(Pawn); Pawn->AddInstanceComponent(VR); VR->RegisterComponent();
	VR->PC = PC; VR->Client = GI->GetSubsystem<UACEClientSubsystem>(); PC->Client = VR->Client;
	VR->Settings = NewObject<UACEVRSettings>(); VR->ActivateRig();
	TestTrue(TEXT("Rig activated"), VR->IsActive()); TestFalse(TEXT("Desktop camera disabled"), Camera->IsActive());
	// This fixture drives render ticks without advancing World time.
	for (auto* Panel : {VR->RetailPanel.Get(), VR->SettingsPanel.Get(), VR->WristPanel.Get(), VR->VitalsPanel.Get(),
		VR->ChatPanel.Get(), VR->JumpPanel.Get(), VR->KeyboardPanel.Get(), VR->FocusPanel.Get()}) Panel->SetRedrawTime(0);
	TestTrue(TEXT("Head tracking owns camera"), VR->Head->bLockToHmd && VR->Head->IsActive());
	TestTrue(TEXT("Tracking origin ignores body rotation and translation"), VR->TrackingOrigin->IsUsingAbsoluteRotation() && VR->TrackingOrigin->IsUsingAbsoluteLocation());
	TestTrue(TEXT("Wrist smoothing is outside the controller late-update hierarchy"), VR->WristPanel->GetAttachParent() != VR->LeftGrip);
	TestTrue(TEXT("Rig updates before camera evaluation"), VR->PrimaryComponentTick.TickGroup == TG_PostPhysics);
	// Reproduce reconstruction of the same login widget during the viewport-to-VR handoff.
	auto* Login = CreateWidget<UACELoginWidget>(PC);
	Login->ProfilePath=FPaths::ProjectSavedDir()/TEXT("Automation/VR/Login-")+FGuid::NewGuid().ToString()+TEXT(".dat");
	FACELoginSettings LoginFixture; LoginFixture.Host=TEXT("localhost"); LoginFixture.Account=TEXT("VR fixture"); LoginFixture.Password=TEXT("fixture-only");
	ACELoginSettings::Save(LoginFixture,Login->ProfilePath);
	{
		auto SlateLogin = Login->TakeWidget();
		Login->NativeConstruct();
		auto LoginBindings = [&](const auto& Delegate)
		{
			return Delegate.GetAllObjects().FilterByPredicate([&](UObject* Object) { return Object == Login; }).Num();
		};
		TestEqual(TEXT("Reconstructed login has one session listener"), LoginBindings(VR->Client->OnSessionStateChanged), 1);
		TestEqual(TEXT("Reconstructed login has one character listener"), LoginBindings(VR->Client->OnCharacterList), 1);
		TestEqual(TEXT("Reconstructed login has one log listener"), LoginBindings(VR->Client->OnLogMessage), 1);
		Login->NativeDestruct();
		TestEqual(TEXT("Detached login releases its session listener"), LoginBindings(VR->Client->OnSessionStateChanged), 0);
		TestEqual(TEXT("Detached login releases its character listener"), LoginBindings(VR->Client->OnCharacterList), 0);
		TestEqual(TEXT("Detached login releases its log listener"), LoginBindings(VR->Client->OnLogMessage), 0);
		Login->NativeConstruct();
		TestEqual(TEXT("Login reattaches after moving into VR"), LoginBindings(VR->Client->OnSessionStateChanged), 1);
	}
	PC->LoginWidget = Login;
	Pawn->SetActorEnableCollision(false); Pawn->SetActorHiddenInGame(true);
	{
		FVRTestControllers Controllers;
		auto PollHands = [&]()
		{
			for (auto* Hand : {VR->LeftGrip.Get(), VR->RightGrip.Get(), VR->LeftAim.Get(), VR->RightAim.Get()})
				Hand->TickComponent(.016f, LEVELTICK_All, nullptr);
		};
		// Start with no head pose, then put on the headset several frames later.
		VR->UpdatePanels();
		const FVector StartupPanel = VR->RetailPanel->GetComponentLocation();
		VR->Head->SetRelativeLocationAndRotation(FVector(35, -12, 165), FRotator(0, 35, 0));
		PollHands(); VR->UpdateTrackingState(true); VR->UpdatePanels(); VR->UpdateArms();
		const float SavedFPS=ACERuntimeOptions::Get(TEXT("ShowFrameRate"));
		ACERuntimeOptions::Set(TEXT("ShowFrameRate"),1);PC->UpdateFrameRateOverlay();VR->UpdatePanels();
		TestNotNull(TEXT("Enabling the FPS counter creates its VR surface"),VR->FrameRatePanel.Get());
		if(VR->FrameRatePanel)
		{
			TestTrue(TEXT("FPS surface is visible and follows tracked head pose"),VR->FrameRatePanel->IsVisible() && VR->FrameRatePanel->GetAttachParent()==VR->Head);
			TestEqual(TEXT("FPS surface cannot intercept a pointer"),VR->FrameRatePanel->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
			ACERuntimeOptions::Set(TEXT("ShowFrameRate"),0);PC->UpdateFrameRateOverlay();VR->UpdatePanels();
			TestFalse(TEXT("Disabling FPS stops drawing and ticking its surface"),VR->FrameRatePanel->IsVisible() || VR->FrameRatePanel->IsComponentTickEnabled());
		}
		ACERuntimeOptions::Set(TEXT("ShowFrameRate"),SavedFPS);PC->UpdateFrameRateOverlay();VR->UpdatePanels();
		TestFalse(TEXT("First tracked pose replaces the startup menu location"), StartupPanel.Equals(VR->RetailPanel->GetComponentLocation(), 1));
		TestTrue(TEXT("Login card is near tracked eye height"), FMath::Abs(VR->RetailPanel->GetComponentLocation().Z - VR->Head->GetComponentLocation().Z) < 25);
		const FVector AnchoredPanel = VR->RetailPanel->GetComponentLocation();
		VR->Head->AddLocalRotation(FRotator(0, 10, 0)); VR->UpdateTrackingState(true);
		TestTrue(TEXT("A menu stays anchored while the user turns to read it"), AnchoredPanel.Equals(VR->RetailPanel->GetComponentLocation()));
		VR->Head->AddLocalRotation(FRotator(0, -10, 0));
		VR->bTracking = false; VR->UpdatePanels();
		TestTrue(TEXT("An unworn headset does not restart the login flow"), PC->IsLoginUIVisible());
		VR->bTracking = true; VR->UpdatePanels();
		VR->InventoryPressed(); VR->UpdateInventoryHold(.2f); VR->InventoryReleased();
		TestFalse(TEXT("Short inventory tap does not open VR options"),VR->bSettingsOpen);
		VR->InventoryPressed(); VR->UpdateInventoryHold(.7f);
		TestTrue(TEXT("Holding X opens options without a runtime Menu event, including in lobby"),VR->bSettingsOpen && VR->SettingsPanel->IsVisible());
		VR->UpdateInventoryHold(2.f); VR->InventoryReleased();
		TestTrue(TEXT("Held X opens options only once and release keeps them open"),VR->bSettingsOpen);
		VR->ToggleSettings();
		VR->InventoryPressed(); VR->UpdateInventoryHold(.3f); VR->UpdateTrackingState(false); VR->UpdateTrackingState(true); VR->UpdateInventoryHold(1.f);
		TestFalse(TEXT("Runtime focus loss cancels a pending menu hold"),VR->bSettingsOpen);
		TestTrue(TEXT("Both controller poses work while the login pawn is hidden"), VR->LeftGrip->IsTracked() && VR->RightAim->IsTracked());
		TestTrue(TEXT("Hand markers remain visible in the unlit lobby"), VR->FallbackArms[2]->IsVisible() && VR->FallbackArms[5]->IsVisible() && !VR->PresentationActor->IsHidden());
		TestTrue(TEXT("Menu beams appear before a widget is hit"), VR->PointerBeams[0]->IsVisible() && VR->PointerBeams[1]->IsVisible());
		TestEqual(TEXT("Disabled player collision does not disable the VR login panel"), VR->RetailPanel->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
		FHitResult Hit;
		const FVector PanelCenter = VR->RetailPanel->GetComponentLocation(), Normal = VR->RetailPanel->GetForwardVector();
		TestTrue(TEXT("Login panel can be hit by an actual world trace"), World->LineTraceSingleByChannel(Hit, PanelCenter + Normal * 50, PanelCenter - Normal * 50, ECC_Visibility));
		TestTrue(TEXT("Trace hits the login widget"), Hit.GetComponent() == VR->RetailPanel);
		TestFalse(TEXT("Keyboard stays hidden until a login field is selected"),VR->KeyboardPanel->IsVisible());
		VR->FocusTextEntry(Login->AccountBox); VR->UpdatePanels();
		TestTrue(TEXT("Selecting a field shows the world-space keyboard"), VR->KeyboardPanel->IsVisible() && VR->KeyboardPanel->GetWidgetSpace() == EWidgetSpace::World);
		TestTrue(TEXT("Login form stays visible while editing"),VR->RetailPanel->IsVisible());
		if (FApp::CanEverRender())
		{
			if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
			for (int32 Pass = 0; Pass < 3; ++Pass)
			{
				VR->RetailPanel->TickComponent(.016f, LEVELTICK_All, nullptr);
				VR->KeyboardPanel->TickComponent(.016f, LEVELTICK_All, nullptr);
				FlushRenderingCommands();
			}
			// Screenshots contain only fixture text; never save or expose local credentials.
			for (const FName Field : {FName("HostBox"), FName("PortBox"), FName("AccountBox"), FName("PasswordBox")})
				if (auto* Box = Cast<UEditableTextBox>(Login->WidgetTree->FindWidget(Field)))
				{
					Box->OnTextChanged.RemoveAll(Login); Box->OnTextCommitted.RemoveAll(Login);
					Box->SetText(FText::FromString(Field == TEXT("HostBox") ? TEXT("localhost") : Field == TEXT("PortBox") ? TEXT("9000") : TEXT("VR test")));
				}
			VR->RetailPanel->TickComponent(.016f, LEVELTICK_All, nullptr); FlushRenderingCommands();
			auto* LoginButton = Login->WidgetTree->FindWidget(TEXT("LoginButton"));
			if (TestNotNull(TEXT("The rendered card contains a Login button"), LoginButton))
			{
				const FGeometry Geometry = LoginButton->GetCachedGeometry();
				const FVector2D ButtonCenter = Login->GetCachedGeometry().AbsoluteToLocal(Geometry.LocalToAbsolute(Geometry.GetLocalSize() * .5));
				TestTrue(TEXT("Login button fits inside the launcher surface"), Geometry.GetLocalSize().X > 1 && ButtonCenter.Y > 0 && ButtonCenter.Y < VR->RetailPanel->GetDrawSize().Y);
				auto TraceControl = [&](UWidgetComponent* Panel, FVector2D Pixel)
				{
					const FVector2D Size = Panel->GetDrawSize();
					const FVector Control = Panel->GetComponentTransform().TransformPosition(FVector(0, Size.X * .5 - Pixel.X, Size.Y * .5 - Pixel.Y));
					const FVector Eye = VR->Head->GetComponentLocation();
					FVector FromEye = Control - Eye;
					const double DownAngle = FMath::RadiansToDegrees(FMath::Atan2(FromEye.Z, FromEye.Size2D()));
					TestTrue(TEXT("Login and keys are within 35 degrees of eye level"), FMath::Abs(DownAngle) < 35.);
					VR->RightAim->SetWorldLocationAndRotation(Eye + FVector(0, 12, -12), (Control - Eye - FVector(0, 12, -12)).Rotation());
					VR->RightPointer->TickComponent(.016f, LEVELTICK_All, nullptr);
					TestTrue(TEXT("Controller ray reaches the intended panel without another panel covering it"), VR->RightPointer->GetHoveredWidgetComponent() == Panel);
					AddInfo(FString::Printf(TEXT("%s control pixel=%s pointer=%s"), *Panel->GetName(), *Pixel.ToString(), *VR->RightPointer->Get2DHitLocation().ToString()));
					TestTrue(Panel->GetName() + TEXT(" control accepts pointer interaction"), VR->RightPointer->IsOverInteractableWidget());
				};
				TraceControl(VR->RetailPanel, ButtonCenter);
				TraceControl(VR->KeyboardPanel, FVector2D(50, 70));
			}
			TestTrue(TEXT("Keyboard stays above the template floor in the lobby"), VR->KeyboardPanel->Bounds.GetBox().Min.Z > 0);
			const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/VR"); IFileManager::Get().MakeDirectory(*Directory, true);
			for (auto* Panel : {VR->RetailPanel.Get(), VR->KeyboardPanel.Get()})
			{
				auto* Surface = Panel->GetRenderTarget();
				TArray<FColor> Pixels; Surface->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
				TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Surface->SizeX, Surface->SizeY, Pixels, PNG);
				FFileHelper::SaveArrayToFile(PNG, *(Directory / (Panel->GetName() + TEXT(".png"))));
				TestTrue(Panel->GetName() + TEXT(" renders visible controls"), Pixels.ContainsByPredicate([](FColor Pixel) { return Pixel.R > 100 || Pixel.G > 100 || Pixel.B > 100; }));
			}
			PollHands(); VR->RightPointer->TickComponent(.016f, LEVELTICK_All, nullptr);
		}
		// Character creation reuses the retail canvas after character selection.
		// Exercise real controller rays, capture, keyboard keys and final submission.
		if (FApp::CanEverRender())
		{
			VR->DismissTextEntry();
			TestTrue(TEXT("Creator loads retail DAT"),GI->GetSubsystem<UACEDatSubsystem>()->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")));
			VR->Client->Session->State=EACESessionState::CharacterSelect;
			auto* CreationCanvas=CreateWidget<UACEUICanvasWidget>(PC);
			CreationCanvas->InitializeCanvas(VR->Client->GetUIElementManager());
			CreationCanvas->SetResourceResolver(VR->Client->GetUIResourceResolver());
			PC->DatCanvasWidget=CreationCanvas;
			PC->ShowCharacterCreationUI();
			auto* Creator=PC->DatCharGenBinder.Get();
			if (TestNotNull(TEXT("VR opens the real character creator"),Creator))
			{
				TestFalse(TEXT("Opening creation keeps controller input in game mode"),PC->bShowMouseCursor);
				auto PaintCreator=[&]
				{
					for(int Pass=0;Pass<3;++Pass)
					{
						VR->UpdatePanels();VR->RetailPanel->TickComponent(.016f,LEVELTICK_All,nullptr);
						VR->KeyboardPanel->TickComponent(.016f,LEVELTICK_All,nullptr);FlushRenderingCommands();
					}
				};
				auto AimControl=[&](bool Left,UWidgetComponent* Panel,FVector2D Pixel)
				{
					const FVector2D Size=Panel->GetDrawSize();
					const FVector Control=Panel->GetComponentTransform().TransformPosition(FVector(0,Size.X*.5-Pixel.X,Size.Y*.5-Pixel.Y));
					const FVector From=VR->Head->GetComponentLocation()+FVector(0,Left?-15:15,-20);
					(Left?VR->LeftAim:VR->RightAim)->SetWorldLocationAndRotation(From,(Control-From).Rotation());
					auto* Pointer=Left?VR->LeftPointer.Get():VR->RightPointer.Get();
					Pointer->TickComponent(.016f,LEVELTICK_All,nullptr);
					TestTrue(TEXT("Creator controller ray reaches its visible panel"),Pointer->GetHoveredWidgetComponent()==Panel);
				};
				auto AimElement=[&](bool Left,const TCHAR* Name)
				{
					auto Element=VR->Client->GetUIElementManager()->FindElementByName(Name);
					if(!TestTrue(FString(TEXT("Creator control exists: "))+Name,Element.IsValid()))return;
					AimControl(Left,VR->RetailPanel,CreationCanvas->LayoutToViewport(FVector2D(Element->GetScreenOrigin())+FVector2D(Element->Width*.5,Element->Height*.5)));
				};
				auto ClickElement=[&](bool Left,const TCHAR* Name)
				{
					AimElement(Left,Name);VR->Trigger(Left,true);PaintCreator();VR->Trigger(Left,false);PaintCreator();
				};
				PaintCreator();
				TestFalse(TEXT("No keyboard covers creator navigation before name entry"),VR->KeyboardPanel->IsVisible());
				Creator->Model.SelectHeritage(1);
				ClickElement(true,TEXT("RadioSho"));TestEqual(TEXT("Left trigger changes heritage"),Creator->Model.Selection.Heritage,3u);
				ClickElement(false,TEXT("CGProfessionButton"));TestEqual(TEXT("Right trigger navigates to profession"),Creator->GetPage(),2);
				ClickElement(true,TEXT("WarButton"));TestEqual(TEXT("Left trigger chooses profession"),Creator->Model.Selection.Profession,4);
				ClickElement(false,TEXT("CGSkillsButton"));TestEqual(TEXT("Controller opens skills"),Creator->GetPage(),3);
				AimElement(true,TEXT("CGAppearanceButton"));VR->Trigger(true,true);
				VR->CancelGestures();PaintCreator();
				TestEqual(TEXT("Lost tracking cancels the pending navigation click"),Creator->GetPage(),3);
				ClickElement(false,TEXT("CGAppearanceButton"));TestEqual(TEXT("Other hand works after capture cancellation"),Creator->GetPage(),4);
				ClickElement(true,TEXT("CGTownButton"));TestEqual(TEXT("Controller opens starting town"),Creator->GetPage(),5);
				ClickElement(false,TEXT("CGSummaryButton"));TestEqual(TEXT("Controller opens summary"),Creator->GetPage(),6);
				ClickElement(true,TEXT("NameTextBox"));
				TestTrue(TEXT("Creator name is an explicit VR text session"),VR->bTextKeyboardOpen && VR->FocusedTextEntry.Get()==CreationCanvas);
				TestTrue(TEXT("Summary remains visible beside its keyboard"),VR->RetailPanel->bRenderInMainPass);
				const FVector2D KeySize=VR->KeyboardPanel->GetDrawSize();
				const FVector KeyboardTop=VR->KeyboardPanel->GetComponentTransform().TransformPosition(FVector(0,0,KeySize.Y*.5));
				const FVector CreatorBottom=VR->RetailPanel->GetComponentTransform().TransformPosition(FVector(0,0,-VR->RetailPanel->GetDrawSize().Y*.5));
				TestTrue(TEXT("Creator keyboard cannot overlap Finish/name controls"),KeyboardTop.Z<CreatorBottom.Z-3.f);
				VR->TypeText(TEXT("VR Hero"));
				TestEqual(TEXT("PC VR keyboard types into the actual name"),Creator->Model.Selection.Name,FString(TEXT("VR Hero")));
				AimControl(false,VR->KeyboardPanel,FVector2D(50,120));VR->Trigger(false,true);VR->Trigger(false,false);PaintCreator();
				TestTrue(TEXT("Clicking an actual keyboard key retains canvas focus"),Creator->Model.Selection.Name.StartsWith(TEXT("VR Hero")) && Creator->Model.Selection.Name.Len()==8);
				ClickElement(false,TEXT("CGTownButton"));TestEqual(TEXT("One click dismisses keyboard and navigates"),Creator->GetPage(),5);
				TestFalse(TEXT("Leaving name entry hides its keyboard"),VR->bTextKeyboardOpen || VR->KeyboardPanel->IsVisible());
				ClickElement(true,TEXT("CGSummaryButton"));ClickElement(false,TEXT("NameTextBox"));
				TestTrue(TEXT("Name keyboard reopens with the other hand"),VR->bTextKeyboardOpen);
				Creator->SetNameFromKeyboard(TEXT(""));VR->TypeKey(EKeys::Enter);PaintCreator();
				TestFalse(TEXT("Enter closes editing and displays retail name validation"),Creator->CanEditName());
				TestFalse(TEXT("Validation never leaves an inert keyboard covering the dialog"),VR->KeyboardPanel->IsVisible());
				Creator->KeyDown(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));PaintCreator();
				ClickElement(true,TEXT("NameTextBox"));VR->TypeText(TEXT("Creation Hero"));
				VR->TypeKey(EKeys::Enter);PaintCreator();
				// This fixture is deliberately not connected; reaching the normal
				// submission/error dialog proves Enter was delivered, without creating
				// an account character. The creation wire tests use a loopback socket.
				TestFalse(TEXT("VR Enter reaches the existing submission path"),Creator->CanEditName());
				VR->DismissTextEntry();Creator->Shutdown();CreationCanvas->SetCharGenBinder(nullptr);
			}
			PC->DatCharGenBinder=nullptr;PC->DatCanvasWidget=nullptr;
			VR->Client->GetUIFlow()->SetMode(ACEUI::EACEUIFlowMode::CharacterManagement);
			VR->UpdatePanels();
		}
		VR->Client->Session->State = EACESessionState::InWorld;
		VR->ResetTrackingOrigin();
		TestTrue(TEXT("World entry removes the separate lobby elevation"), VR->TrackingOrigin->GetComponentLocation().Z < 100.f);
		const bool OriginalSeated = VR->Settings->bSeated;
		for (int32 I=0;I<6;++I)
		{
			VR->Settings->bSeated=(I%2)==0; VR->ResetTrackingOrigin();
			const float EyeHeight = VR->Head->GetComponentLocation().Z - (Pawn->GetActorLocation().Z-Capsule->GetScaledCapsuleHalfHeight());
			TestTrue(TEXT("Repeated posture changes never accumulate floor/eye offsets"),EyeHeight>=100.f && EyeHeight<=240.f);
		}
		VR->Settings->bSeated=OriginalSeated;VR->ResetTrackingOrigin();
		VR->Client->Session->PlayerGuid = 100;
		VR->Client->Session->PlayerVitals.CombatMode = ACECombatMode::Magic;
		VR->Client->Session->KnownSpells = {1, 2, 3};
		FACEWorldObject Wand; Wand.Guid = 200; Wand.WielderId = 100; Wand.CurrentWieldedLocation = ACEEquipMask::Held;
		VR->Client->Session->WorldObjects.Add(Wand.Guid, Wand);
		auto* Dat = GI->GetSubsystem<UACEDatSubsystem>();
		// The creator has already used the resolver's font/texture caches. Do not
		// replace their underlying DAT database halfway through this fixture.
		TestTrue(TEXT("Retail UI and avatar fixture loads DAT"), Dat->GetPortalDat() || Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")));
		auto* Manager = NewObject<UACEUIElementManager>(PC); Manager->Initialize();
		auto* Resources = NewObject<UACEUIResourceResolver>(PC); Resources->Initialize(Dat);
		auto* Layout = NewObject<UACEUILayoutResolver>(PC); Layout->Initialize(Dat, Manager);
		TestTrue(TEXT("Retail gameplay layout loads"), Layout->LoadLayout(0x21000005));
		auto* GameCanvas = CreateWidget<UACEUICanvasWidget>(PC);
		GameCanvas->InitializeCanvas(Manager); GameCanvas->SetResourceResolver(Resources); GameCanvas->TakeWidget();
		auto* Gameplay = NewObject<UACEUIGameplayBinder>(PC);
		Gameplay->Initialize(VR->Client, Manager, GameCanvas, PC); GameCanvas->SetGameplayBinder(Gameplay);
		PC->DatCanvasWidget = GameCanvas; PC->DatGameplayBinder = Gameplay;
		FACEWorldObject SelfRecord; SelfRecord.Guid = 100; SelfRecord.ItemsCapacity = 102;
		VR->Client->Session->WorldObjects.Add(100, SelfRecord);
		Gameplay->ApplyCombatMode(ACECombatMode::Magic);
		VR->Head->SetRelativeLocation(FVector(0, 0, 165)); PollHands();
		VR->Head->SetWorldRotation((VR->LeftGrip->GetComponentLocation() + FVector(0, 0, 12) - VR->Head->GetComponentLocation()).Rotation());
		auto PaintRetail = [&]()
		{
			for (int32 Pass = 0; Pass < 5; ++Pass)
			{
				VR->UpdatePanels(); VR->RetailPanel->TickComponent(.016f, LEVELTICK_All, nullptr);
				VR->WristPanel->TickComponent(.016f, LEVELTICK_All, nullptr);
				VR->VitalsPanel->TickComponent(.016f, LEVELTICK_All, nullptr);
				VR->KeyboardPanel->TickComponent(.016f, LEVELTICK_All, nullptr); FlushRenderingCommands();
			}
		};
		VR->bInventoryOpen = true;
		PaintRetail();
		{
			const bool ValidBefore = VR->Client->Session->PlayerVitals.bValid;
			VR->Client->Session->PlayerVitals.bValid = true;
			VR->Client->Session->NotifyVitalsChanged();
			VR->ToggleCombat(); PaintRetail();
			TestTrue(TEXT("Y preserves the open inventory interface"),VR->bInventoryOpen && VR->RetailPanel->IsVisible());
			TestEqual(TEXT("Y switches the retail binder to peace immediately"),Gameplay->CombatMode,int32(ACECombatMode::NonCombat));
			TestTrue(TEXT("Peace icon changes without waiting for server stat traffic"),Manager->FindElementByName(TEXT("PeaceModeButton"))->bVisible);
			TestFalse(TEXT("Y leaving combat hides the wrist spellbar"),VR->WristPanel->IsVisible());
			VR->ToggleCombat(); PaintRetail();
			TestEqual(TEXT("Y with a wand updates both session and retail to magic"),Gameplay->CombatMode,int32(ACECombatMode::Magic));
			TestTrue(TEXT("Y exposes retail magic art and the wrist surface"),Manager->FindElementByName(TEXT("MagicModeButton"))->bVisible && VR->WristPanel->IsVisible());
			VR->Client->Session->ApplyServerCombatMode(ACECombatMode::NonCombat); PaintRetail();
			TestTrue(TEXT("Transient server peace motion cannot flicker an armed wristbar"),Gameplay->CombatMode==int32(ACECombatMode::Magic) && VR->WristPanel->IsVisible());
			VR->Client->Session->PlayerVitals.bValid = ValidBefore;
			VR->bInventoryOpen = true; PaintRetail();
		}
		TestEqual(TEXT("A newly opened empty spell bank creates every visible tile"), Gameplay->SpellBarSlotBgs.Num(), 13);
		for (auto* Panel : {VR->RetailPanel.Get(), VR->WristPanel.Get()})
		{
			auto* Target = Panel->GetRenderTarget(); TArray<FColor> Pixels;
			Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
			for (int32 I = 0; I < 9; ++I)
			{
				const auto Geometry = Gameplay->SpellBarSlotBgs[I]->GetCachedGeometry();
				FVector2D Corner = GameCanvas->GetCachedGeometry().AbsoluteToLocal(Geometry.LocalToAbsolute(FVector2D::ZeroVector));
				if (Panel == VR->WristPanel) Corner -= VR->WristRetail->ToCanvas(FVector2D::ZeroVector);
				int32 Blue = 0;
				for (int32 Y = 2; Y < 30; ++Y) for (int32 X = 2; X < 30; ++X)
				{
					const int32 Pixel = (FMath::RoundToInt(Corner.Y) + Y) * Target->SizeX + FMath::RoundToInt(Corner.X) + X;
					if (Pixels.IsValidIndex(Pixel)) { const auto P = Pixels[Pixel]; Blue += P.B > 35 && P.B > P.R * 2 && P.B > P.G; }
				}
				TestTrue(FString::Printf(TEXT("%s empty slot %d visibly paints blue artwork before any spell is added"), *Panel->GetName(), I + 1), Blue > 20);
			}
			if (Panel == VR->WristPanel)
			{
				TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Target->SizeX, Target->SizeY, Pixels, PNG);
				FFileHelper::SaveArrayToFile(PNG, *(FPaths::ProjectSavedDir() / TEXT("Automation/VR/EmptyWristHotbar.png")));
			}
		}
		VR->Client->Session->SendAddSpellToBar(1, 0, 0); VR->Client->Session->SendAddSpellToBar(2, 1, 0); PaintRetail();
		TestTrue(TEXT("Retail spell selection routes to VR"), VR->Client->SendCastSpell(2));
		TestEqual(TEXT("Retail and wrist selection agree"), VR->SelectedSpell, 2);
		TestTrue(TEXT("Selecting a spell preserves the open inventory menu"), VR->bInventoryOpen);
		VR->UpdatePanels();
		TestTrue(TEXT("Wrist spell menu is visible with a wand equipped"), VR->WristPanel->IsVisible());
		TestEqual(TEXT("Wrist uses the retail surface instead of a custom list"), VR->WristPanel->GetWidget(), static_cast<UUserWidget*>(VR->WristRetail.Get()));
		{
			const FTransform Rig=VR->TrackingOrigin->GetComponentTransform();
			VR->UpdatePanels(.2f);
			const FTransform Wrist=VR->WristPanel->GetComponentTransform().GetRelativeTransform(Rig);
			VR->TrackingOrigin->AddWorldOffset(FVector(300,400,0)); VR->UpdatePanels(.011f);
			TestTrue(TEXT("Travel moves the smoothed wrist menu with the controllers without lag"),
				VR->WristPanel->GetComponentTransform().GetRelativeTransform(VR->TrackingOrigin->GetComponentTransform()).GetLocation().Equals(Wrist.GetLocation(),.1f));
			VR->TrackingOrigin->SetWorldTransform(Rig); VR->UpdatePanels(.2f);
			const float MainScale=VR->Settings->PanelScale;
			VR->Settings->VitalsScale=.13f; VR->Settings->ChatScale=.09f; VR->Settings->PanelScale=.08f; VR->UpdatePanels();
			TestTrue(TEXT("Pinned vitals and chat scales are independent of central canvas"),
				FMath::IsNearlyEqual(VR->VitalsPanel->GetComponentScale().X,.13) && FMath::IsNearlyEqual(VR->ChatPanel->GetComponentScale().X,.09));
			VR->Settings->PanelScale=MainScale; VR->UpdatePanels();
		}
		auto LocalCenter = [&](UWidget* W)
		{
			const FGeometry G = W->GetCachedGeometry();
			return GameCanvas->GetCachedGeometry().AbsoluteToLocal(G.LocalToAbsolute(G.GetLocalSize() * .5));
		};
		auto PointAt = [&](UWidgetComponent* Panel, FVector2D Pixel)
		{
			const FVector2D Size = Panel->GetDrawSize();
			const FVector Control = Panel->GetComponentTransform().TransformPosition(FVector(0, Size.X * .5 - Pixel.X, Size.Y * .5 - Pixel.Y));
			const FVector From = Control + Panel->GetForwardVector() * 35.f;
			VR->RightAim->SetWorldLocationAndRotation(From, (Control - From).Rotation());
			VR->RightPointer->TickComponent(.016f, LEVELTICK_All, nullptr);
			TestEqual(TEXT("Controller trace reaches the retail interaction surface"), VR->RightPointer->GetHoveredWidgetComponent(), Panel);
		};
		{
			TestTrue(TEXT("Vitals controls show while the inventory menu is open"),VR->ShouldShowVitalsControls());
			VR->bInventoryOpen = false; VR->UpdatePanels();
			VR->LeftAim->SetWorldLocationAndRotation(FVector(0,0,200),FRotator(0,180,0));
			VR->RightAim->SetWorldLocationAndRotation(FVector(0,0,200),FRotator(0,180,0));
			VR->LeftPointer->TickComponent(.016f,LEVELTICK_All,nullptr); VR->RightPointer->TickComponent(.016f,LEVELTICK_All,nullptr);
			TestFalse(TEXT("Vitals lock label hides when neither menu nor hover is active"),VR->ShouldShowVitalsControls());
			const FTransform VitalsBefore = VR->VitalsPanel->GetComponentTransform();
			const FVector2D SizeBefore = VR->VitalsPanel->GetDrawSize();
			PointAt(VR->VitalsPanel, SizeBefore * FVector2D(.5,.25));
			TestTrue(TEXT("Hovering the vitals reveals lock controls"),VR->ShouldShowVitalsControls());
			TestTrue(TEXT("Revealing lock controls cannot move or resize the vitals"),VitalsBefore.Equals(VR->VitalsPanel->GetComponentTransform()) && SizeBefore == VR->VitalsPanel->GetDrawSize());
			TestTrue(TEXT("Hover information sorts above the central interface"),VR->FocusPanel->TranslucencySortPriority > VR->RetailPanel->TranslucencySortPriority);
			VR->PlaceFocusPopup(VR->Head->GetComponentLocation() + VR->Head->GetForwardVector()*5000.f, false);
			const double FarScale = VR->FocusPanel->GetComponentScale().X;
			TestTrue(TEXT("Distant target label remains within 1.8m reading distance"), FVector::Distance(VR->FocusPanel->GetComponentLocation(),VR->Head->GetComponentLocation()) < 180.f);
			VR->PlaceFocusPopup(VR->Head->GetComponentLocation() + VR->Head->GetForwardVector()*50000.f, false);
			TestEqual(TEXT("Further targets cannot shrink the hover text"),VR->FocusPanel->GetComponentScale().X,FarScale);
			VR->bInventoryOpen = true; PollHands(); PaintRetail();
		}
		{
			FACEWorldObject NPC; NPC.Guid=602; NPC.Name=TEXT("Bookie"); NPC.ItemType=ACEItemType::Creature; NPC.bHasPosition=true; NPC.Position.CellId=1;
			VR->Client->Session->WorldObjects.Add(NPC.Guid,NPC);
			auto* Actor=World->SpawnActor<AACEWorldEntityActor>(); Actor->InitializeFromObject(NPC,100,false);
			Actor->SetActorLocation(VR->Head->GetComponentLocation()+VR->Head->GetForwardVector()*220.f-FVector(0,0,180));
            const uint32 OldCapabilities=VR->Client->Session->VRCapabilities;
            VR->Client->Session->VRCapabilities=127;
            VR->HealthFeedback(602,-42,1); auto& Magic=VR->WorldNotices.Last();
            TestTrue(TEXT("Outgoing magic damage uses the target lane and gold without a minus sign"),Magic.Actor.Get()==Actor && Magic.NumberLane==1 && Magic.Text==TEXT("42 DAMAGE") && Magic.Color.Equals(FLinearColor(1.f,.8f,.25f)));
            VR->HealthFeedback(602,21,1);TestTrue(TEXT("Healing uses green and a plus sign"),VR->WorldNotices.Last().Text==TEXT("+21 HEALTH") && VR->WorldNotices.Last().Color.Equals(FLinearColor(.5f,1.f,.498f)));
            VR->HealthFeedback(602,-33,2);TestTrue(TEXT("Critical marker precedes damage"),VR->WorldNotices.Last().Text==TEXT("Crit! 33 DAMAGE") && VR->WorldNotices.Last().Color.Equals(FLinearColor(1.f,.8f,.25f)));
            VR->HealthFeedback(VR->Client->GetPlayerGuid(),-17,1);TestTrue(TEXT("Incoming damage uses the YOU lane and red, without a minus sign"),VR->WorldNotices.Last().NumberLane==-1 && VR->WorldNotices.Last().Text==TEXT("17 DAMAGE") && VR->WorldNotices.Last().Color.Equals(FLinearColor(1.f,.247f,.247f)));
            Actor->SetActorLocation(Actor->GetActorLocation()+FVector(10000,0,0)); VR->UpdateWorldNotices();
            for (const auto& N:VR->WorldNotices) if(N.Kind==2)
                TestTrue(TEXT("Distant combat stays at a fixed readable distance"),FVector::Distance(N.Panel->GetComponentLocation(),VR->Head->GetComponentLocation())<170.f);
            Actor->SetActorLocation(VR->Head->GetComponentLocation()+VR->Head->GetForwardVector()*220.f-FVector(0,0,180));
            const int32 Count=VR->WorldNotices.Num();VR->CombatFeedback(TEXT("Bookie"),42,false,false);
            FACEPlayerVitals V;V.bValid=true;V.Health=10;VR->VitalsFeedback(V);V.Health=5;VR->VitalsFeedback(V);
            TestEqual(TEXT("Retail combat and vital packets cannot duplicate confirmed numbers"),VR->WorldNotices.Num(),Count);
            VR->Client->Session->VRCapabilities=OldCapabilities;
            for(auto& N:VR->WorldNotices) N.Panel->DestroyComponent(); VR->WorldNotices.Empty();
			VR->Client->SelectObject(602);
			VR->NPCSpeech(602,TEXT("Welcome to the monster fight."));
			TestEqual(TEXT("Speaking NPC is deselected"),VR->Client->GetSelectedObject().Guid,0);
			TestEqual(TEXT("Speaking NPC hover remains suppressed until looking away"),VR->ConversationHoverGuid,602);
			FACEWorldObject Corpse; Corpse.Guid=603;Corpse.ObjectDescriptionFlags=ACEObjectDescFlag::Corpse;
			VR->Client->Session->WorldObjects.Add(603,Corpse);VR->Client->SelectObject(603);VR->RevealRetailDialog(603);
			TestEqual(TEXT("Opening loot retains corpse selection"),VR->Client->GetSelectedObject().Guid,603);
			VR->Client->SelectObject(0);
			TestEqual(TEXT("NPC speech creates a single card"),VR->WorldNotices.Num(),1);
			auto& Short=VR->WorldNotices[0];
			TestTrue(TEXT("Short dialogue is smaller than the old maximum"),Short.Panel->GetDrawSize().Y<360.f && Short.Panel->GetDrawSize().Y>=240.f);
			NPC.SetupId=0x02000001; Actor->ApplyDatAppearanceFromObject(NPC);
			for (int32 P=0;P<Actor->Appearance->GetPartCount();++P)
				if (auto* Part=Actor->Appearance->GetPartMesh(P))
				{ Part->SetRelativeLocation(Part->GetRelativeLocation()*3); Part->SetRelativeScale3D(FVector(3)); }
			VR->UpdateWorldNotices(); FBox NPCBounds(ForceInit);
			TestTrue(TEXT("Tall NPC has visible model bounds"),Actor->Appearance->GetVisualWorldBounds(NPCBounds));
			TestTrue(TEXT("Entire dialogue panel clears a tall NPC regardless of collision pill height"),
				Short.Panel->GetComponentLocation().Z-Short.Panel->GetDrawSize().Y*.05f>NPCBounds.Max.Z);
			FString LongText; for(int32 I=0;I<30;++I) LongText+=TEXT("Bring your winning tickets here after the fight and I will exchange them for a prize. ");
			VR->NPCSpeech(602,LongText);
			auto& Long=VR->WorldNotices[0];
			TestTrue(TEXT("Long dialogue preserves its opening and the full new text"),Long.Text.StartsWith(TEXT("Welcome")) && Long.Text.Contains(LongText));
			TestTrue(TEXT("Long dialogue has time to scroll and read"),Long.Expires-Long.Started>18. && Long.ScrollEnd>0.f);
			TestTrue(TEXT("Long card keeps the old size ceiling"),Long.Panel->GetDrawSize().X<=720.f && Long.Panel->GetDrawSize().Y<=360.f);
			Long.ScrollStarts=FPlatformTime::Seconds()-2.; Long.LastRedraw=0.; VR->UpdateWorldNotices();
			TestTrue(TEXT("Dialogue scrolls slowly after its initial reading hold"),Long.Scroll->GetScrollOffset()>=24.f && Long.Scroll->GetScrollOffset()<30.f);
			VR->ShowWorldNotice(TEXT("Bookie gives you a Monster Fight Ticket."),0,1,FLinearColor::White);
			TestEqual(TEXT("Receipt resolves the speaking NPC"),VR->WorldNotices[1].Actor.Get(),Actor);
			const auto* Speech=VR->WorldNotices[0].Panel.Get(); const auto* Receipt=VR->WorldNotices[1].Panel.Get();
			TestTrue(TEXT("NPC receipt is beside the dialogue, not over its text"),FVector::Distance(Speech->GetComponentLocation(),Receipt->GetComponentLocation()) > (Speech->GetDrawSize().X+Receipt->GetDrawSize().X)*.05f);
			for(int32 I=0;I<15;++I) VR->ShowWorldNotice(TEXT("123"),602,2,FLinearColor::Yellow);
			TestEqual(TEXT("Each damage lane is capped at three notices, preserving dialogue and receipt"),VR->WorldNotices.Num(),5);
			TestTrue(TEXT("Combat bursts cannot replace unread NPC dialogue"),VR->WorldNotices[0].Kind==0 && VR->WorldNotices[0].Text.Contains(LongText));
			for(auto& N:VR->WorldNotices)
			{
				if(N.Kind==2) TestTrue(TEXT("Combat numbers have extended reading time"),N.Expires-N.Started>=2.7);
				N.Panel->SetRedrawTime(0); N.Panel->TickComponent(.016f,LEVELTICK_All,nullptr);
			}
			FlushRenderingCommands();
			for(int32 I=0;I<3;++I)
			{
				auto* Target=VR->WorldNotices[I].Panel->GetRenderTarget();
				if(TestNotNull(TEXT("Notification paints an actual render target"),Target))
				{
					TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
					TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Target->SizeX,Target->SizeY,Pixels,PNG);
					FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Automation/VR/Notice%d.png"),I)));
				}
			}
			VR->bInventoryOpen=false;
			VR->CombatMessage(TEXT("You are too busy!"),TEXT(""),ACEChatMessageType::TransientInfo);
			auto* Alert=VR->WorldNotices.FindByPredicate([](const auto& N){return N.Kind==3;});
			TestTrue(TEXT("Yellow server warning appears outside the closed retail UI"),Alert && Alert->Panel->IsVisible() && Alert->Text==TEXT("You are too busy!"));
			if(Alert) TestTrue(TEXT("Warning draws above menus and cannot intercept clicks"),Alert->Panel->TranslucencySortPriority>VR->SettingsPanel->TranslucencySortPriority && Alert->Panel->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
			VR->bInventoryOpen=true;
			for(auto& N:VR->WorldNotices) N.Panel->DestroyComponent(); VR->WorldNotices.Empty();
			Actor->Destroy(); VR->Client->Session->WorldObjects.Remove(NPC.Guid);
		}
		Gameplay->ShowPanelPage(TEXT("SpellManagementPanel_Field")); Gameplay->HandleNamedClick(TEXT("SpellbookTab"));
		Gameplay->TickRefresh(); PaintRetail();
		for (int32 TabIndex=1;TabIndex<=8;++TabIndex)
			if (auto Tab=Manager->FindElementByName(FString::Printf(TEXT("Spellcast_Tab%d"),TabIndex)))
				TestEqual(TEXT("Retail hotbar selected tab uses state 12; idle uses 11"),Tab->DefaultState, TabIndex-1==VR->Client->GetActiveSpellBar()?12u:11u);
		FACEBinaryWriter Confirmation;Confirmation.WriteUInt32(100);Confirmation.WriteUInt32(1);Confirmation.WriteUInt32(0x274);
		Confirmation.WriteUInt32(7);Confirmation.WriteUInt32(123);Confirmation.WriteString16L(TEXT("Would you like to start a monster fight?"));
		FACEBinaryReader ConfirmationReader(Confirmation.GetData());VR->Client->Session->HandleGameEvent(ConfirmationReader);
		Gameplay->TickRefresh();PaintRetail();
		TestTrue(TEXT("Server confirmation reveals the retail canvas in VR"),VR->bInventoryOpen && Gameplay->ServerConfirmRoot && Gameplay->ServerConfirmRoot->bVisible);
		if (auto Yes=Manager->FindElementByName(TEXT("ServerConfirmationYes")))
		{
			PointAt(VR->RetailPanel,GameCanvas->LayoutToViewport(FVector2D(Yes->GetScreenOrigin())+FVector2D(Yes->Width,Yes->Height)*.5));
			VR->Trigger(false,true);VR->Trigger(false,false);PaintRetail();
			TestTrue(TEXT("Controller accepts the actual bookie confirmation"),VR->Client->Session->GetConfirmations().IsEmpty());
		}
		else AddError(TEXT("Confirmation Yes button is missing"));
		if (TestTrue(TEXT("Retail spellbook provides a draggable row"), !Gameplay->SpellbookRowIds.IsEmpty()))
		{
			int32 Row = Gameplay->SpellbookRowIds.IndexOfByPredicate([&](int32 Id) { return Id != 0 && !VR->Client->GetSpellBar(0).Contains(Id); });
			if (Row == INDEX_NONE) Row = 0;
			const int32 Spell = Gameplay->SpellbookRowIds[Row];
			const FVector2D Start = LocalCenter(Gameplay->SpellbookRowBackgrounds[Row]);
			TestTrue(TEXT("Spells can be inspected from their actual visible row"),Gameplay->InspectSpellAt(Start));
			TestEqual(TEXT("Inspection uses the hovered spell"),Gameplay->ExaminedSpellId,Spell);
			TestTrue(TEXT("Spell description uses the retail spell examination body"),Manager->FindElementByName(TEXT("SpellExamineUI"))->bVisible);
			Gameplay->ShowExamination(false);Gameplay->RefreshExaminationOverlay();PaintRetail();
			PointAt(VR->RetailPanel, Start); VR->Trigger(false, true);
			TestTrue(TEXT("Controller press starts a pending spell drag without closing the menu"), Gameplay->bSpellDragPending && VR->bInventoryOpen);
			const FVector2D Other = GameCanvas->GetCachedGeometry().LocalToAbsolute(Start + FVector2D(100, 0));
			const int32 VirtualUser = FSlateApplication::Get().FindOrCreateVirtualUser(VR->RightPointer->VirtualUserIndex)->GetUserIndex();
			FPointerEvent OtherHand(VirtualUser, 0, Other, Other, TSet<FKey>{}, EKeys::Invalid, 0, FModifierKeysState());
			GameCanvas->NativeOnMouseMove(GameCanvas->GetCachedGeometry(), OtherHand);
			TestFalse(TEXT("The other hand cannot move a captured spell drag"), Gameplay->bSpellDragActive);
			PointAt(VR->RetailPanel, Start + FVector2D(30, 0));
			TestTrue(TEXT("Tracked controller movement activates the spell drag"), Gameplay->bSpellDragActive);
			const FVector2D Destination = LocalCenter(Gameplay->SpellBarSlotBgs[2]);
			PointAt(VR->WristPanel, Destination - VR->WristRetail->ToCanvas(FVector2D::ZeroVector));
			VR->Trigger(false, false);
			TestFalse(TEXT("Wrist rejects a dragged spell so its bank cannot be reordered"), VR->Client->GetSpellBar(0).Contains(Spell));
			TestFalse(TEXT("Cross-surface drop releases the drag"), Gameplay->bSpellDragPending);
			PointAt(VR->RetailPanel, Start); VR->Trigger(false, true);
			PointAt(VR->RetailPanel, Destination); VR->Trigger(false, false);
			TestTrue(TEXT("Spellbook drag still adds a spell on the central hotbar"), VR->Client->GetSpellBar(0).Contains(Spell));
			TestTrue(TEXT("Dragging preserves the spellbook window"), VR->bInventoryOpen && Gameplay->ActivePanelPage == TEXT("SpellManagementPanel_Field"));
			PaintRetail();
			PointAt(VR->WristPanel, LocalCenter(Gameplay->SpellBarSlotBgs[0]) - VR->WristRetail->ToCanvas(FVector2D::ZeroVector));
			const auto BeforeWristDrag = VR->Client->GetSpellBar(0);
			VR->Trigger(false, true);
			TestFalse(TEXT("Wrist selection never leaves a reorder pending"), Gameplay->bSpellDragPending);
			PointAt(VR->WristPanel, LocalCenter(Gameplay->SpellBarSlotBgs[1]) - VR->WristRetail->ToCanvas(FVector2D::ZeroVector));
			VR->Trigger(false, false);
			TestTrue(TEXT("Dragging across wrist icons cannot change spell order"), VR->Client->GetSpellBar(0) == BeforeWristDrag);
			TestEqual(TEXT("The retail wrist icon selects the same spell as the original bank"), VR->SelectedSpell, VR->Client->GetSpellBar(0)[0]);
		}
		// Focus uses the same virtual user as both controller pointers. Typing is
		// exercised locally without Enter, so this fixture never sends a chat message.
		if (TestNotNull(TEXT("Gameplay chat entry exists"), Gameplay->ChatEntry.Get()))
		{
			PointAt(VR->RetailPanel, LocalCenter(Gameplay->ChatEntry));
			VR->Trigger(false, true); VR->Trigger(false, false); PaintRetail();
			TestTrue(TEXT("Focusing chat automatically opens the headset keyboard"), VR->bTextKeyboardOpen && VR->KeyboardPanel->IsVisible());
			TestFalse(TEXT("The central quad cannot occlude text entry"), VR->RetailPanel->bRenderInMainPass);
			Gameplay->ChatEntry->SetText(FText::GetEmpty());
			VR->TypeText(TEXT("vr draft"));
			TestEqual(TEXT("Controller keyboard types into focused chat"), Gameplay->ChatEntry->GetText().ToString(), FString(TEXT("vr draft")));
			PointAt(VR->KeyboardPanel, FVector2D(50, 70)); VR->Trigger(false, true); VR->Trigger(false, false);
			TestTrue(TEXT("Clicking a virtual keyboard key retains chat focus and inserts text"), Gameplay->ChatEntry->GetText().ToString().Contains(TEXT("`")));
			Gameplay->ChatEntry->SetText(FText::FromString(TEXT("vr draft")));
			VR->ToggleInventory(); PaintRetail();
			TestFalse(TEXT("X dismisses chat and its keyboard"), VR->bTextKeyboardOpen || VR->bKeyboardOpen);
			TestTrue(TEXT("X restores the open central interface"), VR->RetailPanel->bRenderInMainPass);
			TestEqual(TEXT("Dismissing preserves the unsent draft"), Gameplay->ChatEntry->GetText().ToString(), FString(TEXT("vr draft")));
			Gameplay->FocusChatEntryWindow(0); PaintRetail();
			TestTrue(TEXT("Retail chat focus helper uses the VR keyboard too"), VR->bTextKeyboardOpen);
			VR->RightAim->SetWorldLocationAndRotation(FVector(0, 0, 200), FRotator(0, 180, 0));
			VR->RightPointer->TickComponent(.016f, LEVELTICK_All, nullptr);
			VR->Trigger(false, true); VR->Trigger(false, false); PaintRetail();
			TestFalse(TEXT("Clicking outside keyboard releases chat without trapping input"), VR->bTextKeyboardOpen);
			Gameplay->ChatEntry->SetText(FText::GetEmpty());
			Gameplay->FocusChatEntryWindow(0); PaintRetail();
			VR->TypeKey(EKeys::Enter); PaintRetail();
			TestFalse(TEXT("Committing empty chat also closes its keyboard without leaving inert keys"), VR->bTextKeyboardOpen || VR->bKeyboardOpen);
		}
		VR->Settings->VitalsAnchorMode=0;VR->UpdatePanels();
		const FVector OriginalVitals = VR->Settings->VitalsViewOffset;
		// Slate can restore text focus when clicking non-focusable window chrome.
		VR->RightPointer->SetFocus(Gameplay->ChatEntry); PaintRetail();
		TestFalse(TEXT("Restored Slate focus alone cannot open the keyboard"), VR->bTextKeyboardOpen);
		Gameplay->ShowPanelPage(TEXT("InventoryPanel_Field")); Manager->SetUiLocked(false); PaintRetail();
		if (const auto Window = Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field")))
		{
			Manager->BringFloatyToFront(Window); PaintRetail();
			TSharedPtr<FACEUIElement> Handle;
			TFunction<void(TSharedPtr<FACEUIElement>)> FindHandle = [&](TSharedPtr<FACEUIElement> Node)
			{
				if (!Node->bVisible) return;
				if (Manager->IsFloatyDragHandle(Node) && Node->Width > 30 && Node->Height > 0) Handle = Node;
				for (const auto& Child : Node->Children) if (!Handle) FindHandle(Child);
			};
			FindHandle(Window);
			if (TestTrue(TEXT("Unlocked inventory has a real drag handle"), Handle.IsValid()))
			{
				const FIntPoint Old(Window->UserDragX, Window->UserDragY);
				const FVector2D Start = FVector2D(Handle->GetScreenOrigin()) + FVector2D(Handle->Width, Handle->Height) * .5;
				PointAt(VR->RetailPanel, Start); VR->Trigger(false, true);
				PointAt(VR->RetailPanel, Start + FVector2D(-35, 20)); VR->Trigger(false, false); PaintRetail();
				TestFalse(TEXT("Dragging window chrome never opens the keyboard"), VR->bTextKeyboardOpen);
				TestTrue(TEXT("Controller drag moves the original inventory window"), Window->UserDragX != Old.X || Window->UserDragY != Old.Y);
				Window->UserDragX = Old.X; Window->UserDragY = Old.Y; PaintRetail();
			}
			if (const auto Close = Manager->FindElementUnder(TEXT("RootGameplay_FloatyPanel_Field"), TEXT("CloseInvPanelButton")))
			{
				PointAt(VR->RetailPanel, FVector2D(Close->GetScreenOrigin()) + FVector2D(Close->Width, Close->Height) * .5);
				VR->Trigger(false, true); VR->Trigger(false, false); PaintRetail();
				TestFalse(TEXT("Closing inventory never opens the keyboard"), VR->bTextKeyboardOpen);
				TestTrue(TEXT("Controller close button closes the original panel"), Gameplay->ActivePanelPage.IsEmpty());
			}
		}
		Manager->SetUiLocked(true);
		VR->Settings->bVitalsLocked = false;
		PointAt(VR->VitalsPanel, VR->VitalsPanel->GetDrawSize() * FVector2D(.5, .25));
		VR->BeginVitalsDrag(false); VR->UpdateVitalsDrag();
		TestTrue(TEXT("Grabbing the vitals preserves their initial position"), VR->Settings->VitalsViewOffset.Equals(OriginalVitals, .01));
		VR->RightAim->AddWorldOffset(VR->Head->GetRightVector() * 10.f); VR->UpdatePanels();
		TestTrue(TEXT("Dragging vitals moves their position in view space"), VR->Settings->VitalsViewOffset.Y > OriginalVitals.Y + 9.f);
		VR->EndVitalsDrag(false); VR->Settings->bVitalsLocked = true;
		const FVector LockedVitals = VR->Settings->VitalsViewOffset;
		VR->BeginVitalsDrag(false); VR->RightAim->AddWorldOffset(VR->Head->GetRightVector() * 10.f); VR->UpdateVitalsDrag();
		TestTrue(TEXT("Locked vitals cannot be dragged"), VR->Settings->VitalsViewOffset.Equals(LockedVitals));
		VR->Settings->VitalsViewOffset = OriginalVitals;
		const FTransform OldHead=VR->Head->GetComponentTransform();
		VR->Settings->VitalsAnchorMode=2;VR->bVitalsAnchorReady=false;VR->UpdatePanels();
		const FTransform WorldVitals=VR->VitalsPanel->GetComponentTransform();
		VR->Head->AddWorldOffset(FVector(20,5,3));VR->Head->AddWorldRotation(FRotator(15,10,7));VR->UpdatePanels();
		TestTrue(TEXT("World-pinned vitals stay still during head movement"),VR->VitalsPanel->GetComponentTransform().Equals(WorldVitals,.01));
		VR->Head->SetWorldTransform(OldHead);VR->Settings->VitalsAnchorMode=1;VR->bVitalsAnchorReady=false;VR->UpdatePanels();
		const FVector OwnerBeforeRun = Pawn->GetActorLocation();
		const FTransform TrackingBeforeRun = VR->TrackingOrigin->GetComponentTransform();
		const FVector VitalsBeforeRun = VR->VitalsPanel->GetComponentLocation();
		for(int32 I=0;I<90;++I)
		{
			const FVector Step = VR->Head->GetForwardVector()*12.f;
			Pawn->AddActorWorldOffset(Step);VR->TrackingOrigin->AddWorldOffset(Step);VR->UpdatePanels(1.f/90.f);
		}
		TestTrue(TEXT("Vitals retain their relative position at full running speed"),
			(VR->VitalsPanel->GetComponentLocation()-VitalsBeforeRun).Equals(Pawn->GetActorLocation()-OwnerBeforeRun,.1));
		Pawn->SetActorLocation(OwnerBeforeRun);VR->TrackingOrigin->SetWorldTransform(TrackingBeforeRun);VR->bVitalsAnchorReady=false;VR->UpdatePanels();
		const FVector BeforeTurn = VR->Head->GetComponentTransform().InverseTransformPosition(VR->VitalsPanel->GetComponentLocation());
		for(int32 I=0;I<4;++I) { VR->RotateTracking(45.f);VR->UpdatePanels(); }
		TestTrue(TEXT("Repeated stick turns keep stabilized vitals in front of the player"),
			VR->Head->GetComponentTransform().InverseTransformPosition(VR->VitalsPanel->GetComponentLocation()).Equals(BeforeTurn,.1));
		VR->TrackingOrigin->SetWorldTransform(TrackingBeforeRun);VR->bVitalsAnchorReady=false;VR->UpdatePanels();
		// Put settings directly behind the retail inventory and verify the top
		// visual layer also receives the controller, independent of distance.
		const FTransform SettingsBefore = VR->SettingsPanel->GetComponentTransform();
		VR->SettingsPanel->SetWorldTransform(VR->RetailPanel->GetComponentTransform());
		VR->SettingsPanel->AddWorldOffset(-VR->RetailPanel->GetForwardVector()*2.f);
		VR->SettingsPanel->SetVisibility(true);
		VR->SettingsPanel->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		PointAt(VR->SettingsPanel,FVector2D(300,100));
		TestEqual(TEXT("Overlapping VR settings take hit priority over main UI"),VR->RightPointer->GetHoveredWidgetComponent(),VR->SettingsPanel.Get());
		VR->SettingsPanel->SetVisibility(false);VR->SettingsPanel->SetWorldTransform(SettingsBefore);VR->UpdatePanels();
		{
			auto& Session=*VR->Client->Session;
			Session.Book.bOpen=true; Session.Book.BookGuid=70001;Session.Book.CurrentPage=0;Session.Book.NumPages=2;
			FString LongPage;for(int32 I=0;I<100;++I)LongPage+=TEXT("A long page must remain readable and scroll within the parchment.\n");
			Session.Book.Pages={LongPage,TEXT("Second page.")};
			Gameplay->ShowPanelPage(TEXT("BookPanel_Field"));Gameplay->RefreshBookOverlays();PaintRetail();Gameplay->RefreshBookOverlays();PaintRetail();
			TestNotNull(TEXT("Book uses a bounded scrolling text container"),Gameplay->BookScroll.Get());
			const auto Bar=Gameplay->GetBookScrollbar();
			TestTrue(TEXT("Book resolves its authored parchment scrollbar"),Bar.IsValid());
			if(Bar && Gameplay->BookScroll)
			{
				AddInfo(FString::Printf(TEXT("Book bar %s, %dx%d, scrollmax=%.1f"),*Bar->ElementName,Bar->Width,Bar->Height,Gameplay->BookScroll->GetScrollOffsetOfEnd()));
				TestTrue(TEXT("Long book page has real overflow"),Gameplay->BookScroll->GetScrollOffsetOfEnd()>500);
				const FVector2D Click=FVector2D(Bar->GetScreenOrigin())+FVector2D(Bar->Width*.5,Bar->Height*.65);
				TestTrue(TEXT("Book thumb can be grabbed"),Gameplay->TryBeginScrollbarDrag(Click));
				Gameplay->UpdateScrollbarDrag(Click);Gameplay->CancelPointerGestures();PaintRetail();
				TestTrue(TEXT("Dragging book thumb scrolls text"),Gameplay->BookScroll->GetScrollOffset()>100);
				if(auto* RT=VR->RetailPanel->GetRenderTarget())
				{
					TArray<FColor> Pixels;RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);TArray64<uint8> PNG;
					FImageUtils::PNGCompressImageArray(RT->SizeX,RT->SizeY,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Automation/VR/BookScroll.png")));
				}
				Session.Book.CurrentPage=1;Gameplay->RefreshBookOverlays();PaintRetail();
				TestEqual(TEXT("Changing book pages resets scroll to the top"),Gameplay->BookScroll->GetScrollOffset(),0.f);
			}
			Session.Book={};Gameplay->HidePanel();
		}
		FACEWorldObject Bread; Bread.Guid = 301; Bread.ContainerId = 100; Bread.Name = TEXT("VR vendor test bread");
		Bread.ItemType = ACEItemType::Food; Bread.StackSize = 3; Bread.Value = 30; Bread.IconId = 0x060010FA;
		VR->Client->Session->WorldObjects.Add(Bread.Guid, Bread);
		VR->Client->Session->VendorItemTypes = ACEItemType::Food;
		VR->Client->Session->VendorMaxValue = -1;
		Gameplay->ShowPanelPage(TEXT("InventoryPanel_Field")); Gameplay->ShowVendorPanel(400);
		{
			FACEWorldObject Stock;Stock.Guid=75002;Stock.Name=TEXT("Vendor arrow stock");Stock.ItemType=ACEItemType::MissileWeapon;
			Stock.StackSize=1;Stock.MaxStackSize=2500;Stock.VendorQuantityAvailable=-1;Stock.Value=1;Stock.IconId=0x060010FA;
			VR->Client->Session->WorldObjects.Add(Stock.Guid,Stock);VR->Client->Session->VendorMerchandise={Stock};
			Gameplay->VendorSelectedGuid=Stock.Guid;VR->Client->SelectObject(Stock.Guid);Gameplay->TickRefresh();PaintRetail();
			TestEqual(TEXT("Unlimited vendor stock exposes the entire stack quantity range"),Gameplay->SelectedStackMax,2500);
			Gameplay->SelectedStackAmount=200;Gameplay->HandleSelectionChanged(VR->Client->GetSelectedObject());
			TestEqual(TEXT("Vendor refresh retains chosen purchase quantity"),Gameplay->SelectedStackAmount,200);
			Gameplay->AddSelectedVendorItemToBuyCart();
			TestTrue(TEXT("Buy cart receives the chosen quantity rather than one"),Gameplay->VendorBuyCart.ContainsByPredicate([&](const auto& P){return P.Value==Stock.Guid && P.Key==200;}));
			const FTransform PanelPose=VR->RetailPanel->GetComponentTransform();VR->Head->AddWorldRotation(FRotator(0,30,0));VR->RevealRetailDialog(400);
			TestTrue(TEXT("Refreshing an open vendor leaves its VR panel anchored"),VR->RetailPanel->GetComponentTransform().Equals(PanelPose,.001));
			VR->Head->AddWorldRotation(FRotator(0,-30,0));Gameplay->VendorBuyCart.Reset();VR->Client->Session->VendorMerchandise.Reset();VR->Client->Session->WorldObjects.Remove(Stock.Guid);VR->Client->SelectObject(0);
		}
		Gameplay->TickRefresh(); PaintRetail();
		if (auto Tab = Manager->FindElementByName(TEXT("VendorSellTab")))
		{
			PointAt(VR->RetailPanel, FVector2D(Tab->GetScreenOrigin()) + FVector2D(Tab->Width, Tab->Height) * .5);
			VR->Trigger(false, true); VR->Trigger(false, false);
			TestEqual(TEXT("Controller clicks switch the actual vendor tab"), Gameplay->ActiveVendorPage, 2);
		}
		PaintRetail();
		const int32 BreadIndex = Gameplay->InventorySlotGuids.Find(Bread.Guid);
		if (TestTrue(TEXT("Inventory fixture exposes the sellable item"), BreadIndex != INDEX_NONE))
		{
			const FVector2D ItemPoint = LocalCenter(Gameplay->InventorySlots[BreadIndex]);
			Gameplay->LastInvClickGuid = Bread.Guid; Gameplay->LastInvClickTime = FPlatformTime::Seconds() - .65;
			PointAt(VR->RetailPanel, ItemPoint); VR->Trigger(false, true);
			TestTrue(TEXT("VR accepts a comfortably spaced second inventory click"), Gameplay->bInvDoubleClickPending);
			PointAt(VR->RetailPanel, ItemPoint + FVector2D(18, 0));
			TestFalse(TEXT("Small controller jitter does not turn a double click into a drag"), Gameplay->bInvDragActive);
			Gameplay->CancelPointerGestures(); VR->Trigger(false, false);
			PointAt(VR->RetailPanel, LocalCenter(Gameplay->InventorySlots[BreadIndex])); VR->Trigger(false, true);
			TestTrue(TEXT("Item press preserves inventory and starts a drag"), VR->bInventoryOpen && Gameplay->bInvDragPending);
			if (auto SellList = Manager->FindElementUnder(TEXT("VendorSellPage"), TEXT("VendorSellList")))
			{
				PointAt(VR->RetailPanel, FVector2D(SellList->GetScreenOrigin()) + FVector2D(15, 15)); VR->Trigger(false, false);
				TestTrue(TEXT("Controller drag stages inventory in the vendor sell cart"), Gameplay->VendorSellCart.ContainsByPredicate([&](const auto& P) { return P.Value == Bread.Guid && P.Key == 3; }));
			}
		}
		Gameplay->HandleEscape(); PaintRetail();
		// Exercise release outside the UI, including a quick drag with no canvas
		// move event, using the opposite hand from the configured weapon hand.
		auto* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		auto Address = Sockets->CreateInternetAddr(); bool Valid = false; Address->SetIp(TEXT("127.0.0.1"), Valid); Address->SetPort(0);
		auto* Receiver = Sockets->CreateSocket(NAME_DGram, TEXT("VR item transfer fixture"), false);
		if (Receiver && Receiver->Bind(*Address) && BreadIndex != INDEX_NONE)
		{
			Receiver->GetAddress(*Address); auto& Session = *VR->Client->Session;
			Session.SocketC2S = Sockets->CreateSocket(NAME_DGram, TEXT("VR item transfer sender"), false);
			Session.ServerC2SAddr = Address; Session.IssacClient = MakeUnique<FACEIsaac>(123);
			// Inspect uses the actual pointer, sends identify once, and cannot equip,
			// use, drag, or fall through to the previous spell while over inventory.
			Session.CachedC2SPackets.Reset();
			PointAt(VR->RetailPanel, LocalCenter(Gameplay->InventorySlots[BreadIndex]));
			VR->FeedbackHand = 1;
			const uint64 IdentifyBefore = VR->Client->GetIdentifyRequestSerial();
			VR->PreviousSpell();
			TestEqual(TEXT("B requests one appraisal for the pointed item"), VR->Client->GetIdentifyRequestSerial(), IdentifyBefore + 1);
			TestEqual(TEXT("B identifies the hovered inventory item"), VR->Client->GetIdentifyRequestGuid(), Bread.Guid);
			TestFalse(TEXT("B does not start an inventory drag"), Gameplay->bInvDragPending || Gameplay->bInvDragActive);
			TestTrue(TEXT("B keeps the inventory panel open"), VR->bInventoryOpen);
			int32 IdentifyActions=0;
			for (const auto& Packet : Session.CachedC2SPackets)
			{
				FACEBinaryReader Wire(Packet.Value.Payload); Wire.Skip(16);
				if (Wire.ReadUInt32()!=ACEOpcode::GameAction) continue; Wire.ReadUInt32();
				const uint32 Action=Wire.ReadUInt32();
				if (Action==ACEGameAction::QueryHealth) continue; // Retail selection also queries target health.
				TestEqual(TEXT("Inspect sends no use/equip/drop action"), Action, ACEGameAction::IdentifyObject);
				++IdentifyActions;
			}
			TestEqual(TEXT("B produces exactly one item action"), IdentifyActions, 1);
			VR->bRightPointerPressed=true; VR->PreviousSpell();
			TestEqual(TEXT("B cannot interrupt a held inventory gesture"), VR->Client->GetIdentifyRequestSerial(), IdentifyBefore+1);
			VR->bRightPointerPressed=false;
			VR->bActive=false; VR->PreviousSpell();
			TestEqual(TEXT("B has no effect outside VR"), VR->Client->GetIdentifyRequestSerial(), IdentifyBefore+1);
			VR->bActive=true;
			Gameplay->CancelPointerGestures();
			FACEWorldObject NPC; NPC.Guid = 601; NPC.ItemType = ACEItemType::Creature; NPC.bHasPosition = true;
			NPC.UseRadius = .6f; NPC.Position.CellId = 1;
			auto* Actor = World->SpawnActor<AACEWorldEntityActor>();
			for (float Distance : {20.f, 1.f})
			{
				NPC.Position.Location = FVector(0, Distance, 0); Session.WorldObjects.Add(NPC.Guid, NPC);
				Actor->InitializeFromObject(NPC, 100, false); Actor->ConfigureWorldCollision(true);
				Session.CachedC2SPackets.Reset();
				// Start with the left controller and point the right controller away.
				PointAt(VR->RetailPanel, LocalCenter(Gameplay->InventorySlots[BreadIndex]));
				VR->LeftAim->SetWorldTransform(VR->RightAim->GetComponentTransform()); VR->LeftPointer->TickComponent(.016f, LEVELTICK_All, nullptr);
				VR->Trigger(true, true);
				TestTrue(TEXT("Off-hand item press starts a pending transfer"), Gameplay->bInvDragPending);
				const FVector Center = Actor->FindComponentByClass<UCapsuleComponent>()->Bounds.Origin;
				VR->LeftAim->SetWorldLocationAndRotation(Center - FVector(80, 0, 0), FRotator::ZeroRotator);
				VR->RightAim->SetWorldLocationAndRotation(FVector(0, 0, 200), FRotator(0, 180, 0));
				VR->LeftPointer->TickComponent(.016f, LEVELTICK_All, nullptr);
				VR->Trigger(true, false);
				bool Gave = false;
				for (const auto& Packet : Session.CachedC2SPackets)
				{
					FACEBinaryReader Wire(Packet.Value.Payload); Wire.Skip(16);
					if (Wire.ReadUInt32() != ACEOpcode::GameAction) continue; Wire.ReadUInt32();
					if (Wire.ReadUInt32() != ACEGameAction::GiveObjectRequest) continue;
					Gave = true; TestEqual(TEXT("Transfer targets the releasing hand's NPC"), Wire.ReadUInt32(), 601u);
					TestEqual(TEXT("Transfer preserves the dragged item"), Wire.ReadUInt32(), 301u);
				}
				TestEqual(TEXT("World transfer requires reach and never asks the server to auto-walk"), Gave, Distance == 1.f);
			}
			Actor->Destroy(); Session.WorldObjects.Remove(NPC.Guid);
			const uint32 DropCapabilities = Session.VRCapabilities;
			for (bool VRRelease : {false, true}) for (int32 Amount : {3, 1})
			{
				Session.VRCapabilities = VRRelease ? DropCapabilities | 1024u : DropCapabilities & ~1024u;
				Session.CachedC2SPackets.Reset(); Gameplay->LastInvClickGuid = 0;
				PointAt(VR->RetailPanel, LocalCenter(Gameplay->InventorySlots[BreadIndex])); VR->Trigger(false, true);
				Gameplay->SelectedStackAmount = Amount;
				VR->RightAim->SetWorldLocationAndRotation(FVector(0, 0, 200), FRotator(-45, 180, 0));
				VR->Trigger(false, false);
				int32 Drops = 0;
				for (const auto& Packet : Session.CachedC2SPackets)
				{
					FACEBinaryReader Wire(Packet.Value.Payload); Wire.Skip(16);
					if (Wire.ReadUInt32() != ACEOpcode::GameAction) continue; Wire.ReadUInt32();
					const uint32 Action = Wire.ReadUInt32();
					if (VRRelease && Action == 0xF7D0u)
					{
						TestEqual(TEXT("VR release protocol version"), Wire.ReadUInt32(), 1u);
						TestEqual(TEXT("VR drop has an explicit release action"), Wire.ReadUInt32(), 6u);
						Wire.Skip(12);
						TestEqual(TEXT("VR release preserves inventory item"), Wire.ReadUInt32(), uint32(Bread.Guid));
						TestEqual(TEXT("VR release preserves selected stack amount"), Wire.ReadUInt32(), Amount == 3 ? 0u : 1u);
						const float X=Wire.ReadFloat(), Y=Wire.ReadFloat(), Z=Wire.ReadFloat();
						TestTrue(TEXT("VR release sends a reachable hand position"), FMath::IsFinite(X+Y+Z) && Z>=.1f && Z<=2.5f);
						TestEqual(TEXT("VR release has no trailing fields"), Wire.Remaining(), 0);
						++Drops; continue;
					}
					if (Action != ACEGameAction::DropItem && Action != ACEGameAction::StackableSplitTo3D) continue;
					TestFalse(TEXT("Supported VR release never also sends desktop placement"), VRRelease);
					++Drops; TestEqual(TEXT("Ground release preserves stack or selected split"), Action,
						Amount == 3 ? ACEGameAction::DropItem : ACEGameAction::StackableSplitTo3D);
					TestEqual(TEXT("Ground release sends the dragged item to the server"), Wire.ReadUInt32(), uint32(Bread.Guid));
					if (Amount == 1) TestEqual(TEXT("Split preserves selected amount"), Wire.ReadUInt32(), 1u);
				}
				TestEqual(TEXT("One controller release sends exactly one ground drop"), Drops, 1);
				TestFalse(TEXT("Ground release clears the captured inventory gesture"), Gameplay->IsInventoryDragActive());
			}
			VR->bActive=false;
			TestFalse(TEXT("Desktop uses its unchanged placement even on a VR-capable server"), VR->TryDropInventoryItem(Bread.Guid));
			VR->bActive=true; Session.VRCapabilities=DropCapabilities;
			FACEWorldObject Falling = Bread;
			Falling.Guid=602; Falling.ContainerId=Falling.WielderId=Falling.ParentGuid=0;
			Falling.PhysicsState=ACEPhysicsState::Gravity;
			Falling.bHasPosition=Falling.bHasVelocity=true;
			Falling.Position.CellId=1; Falling.Position.Location=FVector(0,0,1.1f);
			Falling.Position.bHasVelocity=false; Falling.Position.bIsGrounded=false;
			Falling.Velocity=FVector(0,0,-.15f);
			auto* Dropped=World->SpawnActor<AACEWorldEntityActor>();
			Dropped->InitializeFromObject(Falling,100,false);
			const double ReleaseZ=Falling.Position.ToUnrealLocation(100).Z;
			TestTrue(TEXT("CreateObject fall velocity preserves hand-height spawn without floor snapping"),FMath::Abs(Dropped->GetActorLocation().Z-ReleaseZ)<.1);
			Dropped->Tick(.1f);
			TestTrue(TEXT("Released inventory model integrates gravity"),Dropped->GetActorLocation().Z<ReleaseZ-3.f);
			Dropped->Destroy();
			Falling.Velocity=FVector(.8f,0,.55f);
			auto* Tossed=World->SpawnActor<AACEWorldEntityActor>();Tossed->InitializeFromObject(Falling,100,false);
			const FVector TossStart=Tossed->GetActorLocation(); const FQuat TossRotation=Tossed->GetActorQuat();
			Tossed->Tick(.05f);
			TestTrue(TEXT("Outward release rises slightly and travels away from the hand"),Tossed->GetActorLocation().Z>TossStart.Z && FMath::Abs(Tossed->GetActorLocation().X-TossStart.X)>3.5f);
			TestTrue(TEXT("A dropped item does not turn to face its velocity"),Tossed->GetActorQuat().Equals(TossRotation,.001f));
			Tossed->Tick(.2f);
			TestTrue(TEXT("The short outward arc turns downward under gravity"),Tossed->GetActorLocation().Z<TossStart.Z-10.f);
			Tossed->Destroy();
			{
				const bool OldMenu=VR->bInventoryOpen;const int32 OldMode=Session.PlayerVitals.CombatMode;
				VR->bInventoryOpen=false;Session.PlayerVitals.CombatMode=ACECombatMode::NonCombat;VR->UpdatePanels();PollHands();VR->ResetHandContacts();
				const FVector Eyes=VR->Head->GetComponentLocation();
				VR->Head->SetWorldRotation(FRotator::ZeroRotator);
				VR->LeftGrip->SetWorldLocation(Eyes+FVector(55,-20,-20));VR->RightGrip->SetWorldLocation(Eyes+FVector(55,20,-20));
				FACEWorldObject Stone;Stone.Guid=75001;Stone.SetupId=0x02000001;Stone.ItemType=ACEItemType::LifeStone;Stone.ObjectDescriptionFlags=ACEObjectDescFlag::Stuck;
				Stone.ItemUseable=32;Stone.UseRadius=3;Stone.bHasPosition=true;Stone.Position.CellId=1;
				Stone.Position.SetLocationFromUnreal(Eyes+FVector(65,0,-100),100);Session.WorldObjects.Add(Stone.Guid,Stone);
				auto* StoneActor=World->SpawnActor<AACEWorldEntityActor>();StoneActor->InitializeFromObject(Stone,100,true);
				auto* Surface=NewObject<UBoxComponent>(StoneActor);StoneActor->AddInstanceComponent(Surface);Surface->SetupAttachment(StoneActor->GetRootComponent());
				Surface->SetBoxExtent(FVector(3,60,100));Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Surface->SetCollisionResponseToAllChannels(ECR_Block);Surface->RegisterComponent();
				Surface->SetWorldLocation(Eyes+FVector(65,0,-40));
				VR->RightAim->SetWorldLocationAndRotation(Eyes+FVector(0,0,-20),FRotator::ZeroRotator);
				const uint64 BeforeID=VR->Client->GetIdentifyRequestSerial();VR->PreviousSpell();
				TestEqual(TEXT("B identifies the world object in peace with menus closed"),VR->Client->GetIdentifyRequestSerial(),BeforeID+1);
				TestEqual(TEXT("World B identifies the pointed stone"),VR->Client->GetIdentifyRequestGuid(),Stone.Guid);
				VR->bInventoryOpen=true;VR->PreviousSpell();TestEqual(TEXT("Open UI cannot identify through itself into the world"),VR->Client->GetIdentifyRequestSerial(),BeforeID+1);VR->bInventoryOpen=false;
				Session.CachedC2SPackets.Reset();VR->ContactTriangles.Reset();VR->MoveStick=FVector2D::ZeroVector;
				// The first gesture after closing a menu must not require an unseen
				// retract/rearm gesture. Use a tall visible model whose center is
				// above the gaze cone, while its surface is directly ahead.
				VR->bTouchUseArmed=true;VR->bTouchUsePrevious=false;
				FACEVRContactTriangle Touch;Touch.ObjectGuid=Stone.Guid;Touch.A=Eyes+FVector(65,-100,-100);Touch.B=Eyes+FVector(65,100,-100);Touch.C=Eyes+FVector(65,0,100);
				Touch.Bounds=FBox(Touch.A,Touch.A);Touch.Bounds+=Touch.B;Touch.Bounds+=Touch.C;
				StoneActor->AddActorWorldOffset(FVector(110,0,0));Stone.Position.SetLocationFromUnreal(StoneActor->GetActorLocation(),100);Session.WorldObjects[Stone.Guid]=Stone;
				StoneActor->SetActorScale3D(FVector(1,1,6));
				FBox TallBounds(ForceInit);StoneActor->Appearance->GetVisualWorldBounds(TallBounds);
				TestTrue(TEXT("Tall usable fixture has its center outside the gaze cone"),FVector::DotProduct((TallBounds.GetCenter()-Eyes).GetSafeNormal(),VR->Head->GetForwardVector())<.8f);
				auto UseCount=[&](){int32 Count=0;for(const auto& Packet:Session.CachedC2SPackets){FACEBinaryReader Wire(Packet.Value.Payload);Wire.Skip(16);if(Wire.ReadUInt32()!=ACEOpcode::GameAction)continue;Wire.ReadUInt32();if(Wire.ReadUInt32()==ACEGameAction::Use)++Count;}return Count;};
				VR->LeftGrip->AddWorldOffset(FVector(-50,0,0));for(int32 I=0;I<60;++I)VR->UpdateTwoHandUse(.02f);TestEqual(TEXT("One hand alone cannot use an object"),UseCount(),0);
				VR->LeftGrip->AddWorldOffset(FVector(50,0,0));VR->MoveStick=FVector2D(0,1);for(int32 I=0;I<60;++I)VR->UpdateTwoHandUse(.02f);TestEqual(TEXT("Walking past with both hands cannot use an object"),UseCount(),0);VR->MoveStick=FVector2D::ZeroVector;
				for(int32 I=0;I<10;++I)VR->UpdateTwoHandUse(.02f);TestEqual(TEXT("Brief two-hand brush does not use"),UseCount(),0);
				for(int32 I=0;I<50;++I)VR->UpdateTwoHandUse(.02f);TestEqual(TEXT("Deliberate two-hand reach within use range sends one use without mesh contact"),UseCount(),1);
				for(int32 I=0;I<60;++I)VR->UpdateTwoHandUse(.02f);TestEqual(TEXT("Holding both hands cannot repeat use"),UseCount(),1);
				VR->ContactTriangles.Reset();VR->UpdateTwoHandUse(.02f);Session.PlayerVitals.CombatMode=ACECombatMode::Melee;VR->ContactTriangles.Add(Touch);
				for(int32 I=0;I<60;++I)VR->UpdateTwoHandUse(.02f);TestEqual(TEXT("Combat disables contact use"),UseCount(),1);
				StoneActor->Destroy();Session.WorldObjects.Remove(Stone.Guid);VR->ContactTriangles.Reset();VR->ResetHandContacts();
				Session.PlayerVitals.CombatMode=OldMode;VR->bInventoryOpen=OldMenu;PollHands();VR->UpdatePanels();
			}
			Session.SocketC2S->Close(); Sockets->DestroySocket(Session.SocketC2S); Session.SocketC2S = nullptr;
		}
		else AddError(TEXT("World transfer loopback fixture could not initialize"));
		if (Receiver) Sockets->DestroySocket(Receiver);
		VR->SelectSpell(2); PaintRetail();
		if (auto* Surface = VR->WristPanel->GetRenderTarget())
		{
			TArray<FColor> Pixels; Surface->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
			TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Surface->SizeX, Surface->SizeY, Pixels, PNG);
			FFileHelper::SaveArrayToFile(PNG, *(FPaths::ProjectSavedDir() / TEXT("Automation/VR/RetailWristHotbar.png")));
			TestTrue(TEXT("Retail wrist surface paints its real icons"), Pixels.ContainsByPredicate([](FColor P) { return P.R > 80 || P.G > 80 || P.B > 80; }));
		}
		VR->ToggleInventory();
		TestFalse(TEXT("Explicitly closing inventory hides its scene quad"), VR->RetailPanel->bRenderInMainPass);
		for (float Roll : {-170.f, -90.f, 0.f, 90.f, 170.f})
		{
			VR->LeftGrip->SetRelativeRotation(FRotator(0, 0, Roll)); VR->UpdatePanels();
			TestTrue(TEXT("Turning the wrist cannot turn text away from the eyes"), FVector::DotProduct(VR->WristPanel->GetForwardVector(),
				(VR->Head->GetComponentLocation() - VR->WristPanel->GetComponentLocation()).GetSafeNormal()) > .999);
		}
		const FVector OldWrist = VR->WristPanel->GetComponentLocation();
		VR->LeftGrip->AddWorldOffset(FVector(10, 0, 0)); VR->UpdatePanels(1.f / 90.f);
		const float WristStep = FVector::Distance(OldWrist, VR->WristPanel->GetComponentLocation());
		TestTrue(TEXT("Wrist stabilization filters a tracked-position step"), WristStep > 0.f && WristStep < 5.f);
		const FQuat LookingAtWrist = VR->Head->GetComponentQuat();
		VR->Head->SetWorldRotation(FRotator(0, 180, 0)); VR->UpdatePanels(.1f);
		TestTrue(TEXT("Brief gaze loss does not flicker the wrist"), VR->WristPanel->IsVisible());
		VR->UpdatePanels(.1f);
		TestTrue(TEXT("Looking away keeps the combat wrist hotbar visible"), VR->WristPanel->IsVisible());
		VR->Head->SetWorldRotation(LookingAtWrist); VR->UpdatePanels();
		TestTrue(TEXT("Looking back reveals the wrist hotbar"), VR->WristPanel->IsVisible());
		{
			auto& Session=*VR->Client->Session; const auto Bars=Session.SpellBars; const int32 Tab=Session.ActiveSpellBar;
			const int32 Mode=Session.PlayerVitals.CombatMode;Session.PlayerVitals.CombatMode=ACECombatMode::NonCombat;
			VR->ToggleSpellWheel();TestFalse(TEXT("No wand means no spell wheel"),VR->bSpellWheelOpen);
			FACEWorldObject WheelWand;WheelWand.Guid=75002;WheelWand.WielderId=Session.PlayerGuid;
			WheelWand.CurrentWieldedLocation=ACEEquipMask::Held;WheelWand.ItemType=ACEItemType::Caster;Session.WorldObjects.Add(WheelWand.Guid,WheelWand);
			Session.SpellBars.SetNum(8); Session.SpellBars[0]={1,2,3}; Session.SpellBars[1].Reset(); Session.ActiveSpellBar=0;
			VR->TurnStick=FVector2D::ZeroVector; VR->ToggleSpellWheel();
			TestTrue(TEXT("Left-stick click opens the radial picker without blocking movement"),VR->bSpellWheelOpen && !VR->IsInputBlocked());
			VR->TurnStick=FVector2D(0,1); VR->UpdateSpellWheel();
			TestEqual(TEXT("Up selects the first spell in the current tab"),VR->WheelHover,0);
			for(int32 I=0;I<5;++I){VR->UpdateSpellWheel();VR->SpellWheelPanel->TickComponent(.016f,LEVELTICK_All,nullptr);FlushRenderingCommands();}
			if(auto* RT=VR->SpellWheelPanel->GetRenderTarget())
			{
				TArray<FColor> Pixels; RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
				TestTrue(TEXT("Spell wheel renders icons and ring"),Pixels.ContainsByPredicate([](FColor P){return P.A>100 && P.R>80;}));
				TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(RT->SizeX,RT->SizeY,Pixels,PNG);
				FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Automation/VR/SpellWheel.png")));
			}
			VR->SelectPressed();TestTrue(TEXT("A changes to the next tab, including an empty tab"),Session.ActiveSpellBar==1 && VR->WheelSpells.IsEmpty());
			const uint32 Sequence=Session.VRSequence;VR->ConfirmWheelSpell();
			TestTrue(TEXT("Empty tab cannot confirm or cast"),VR->bSpellWheelOpen && Session.VRSequence==Sequence);
			VR->PreviousSpell();VR->TurnStick=FVector2D::ZeroVector;VR->UpdateSpellWheel();VR->TurnStick=FVector2D(0,1);VR->UpdateSpellWheel();
			VR->Trigger(VR->Settings->bLeftHanded,true); VR->Trigger(VR->Settings->bLeftHanded,false);
			TestTrue(TEXT("Trigger confirms selection without casting or leaving a held trigger"),!VR->bSpellWheelOpen && VR->SelectedSpell==1 && Session.VRSequence==Sequence);
			TestEqual(TEXT("Selecting from the wheel leaves peace stance unchanged"),Session.PlayerVitals.CombatMode,ACECombatMode::NonCombat);
			Session.SpellBars[0]={1,2,3,1,2,3,1,2,3,1,2,3,2}; VR->ToggleSpellWheel(); VR->ChangeWheelPage(1);
			TestEqual(TEXT("Long hotbars have a second radial page"),VR->WheelPage,1);
			VR->ToggleInventory();TestFalse(TEXT("X cancels the wheel without opening inventory"),VR->bSpellWheelOpen || VR->bInventoryOpen);
			Session.SpellBars=Bars;Session.ActiveSpellBar=Tab;VR->SelectSpell(2);VR->TurnStick=FVector2D::ZeroVector;
			Session.WorldObjects.Remove(WheelWand.Guid);Session.PlayerVitals.CombatMode=Mode;
		}
		for(int32 Mode:{ACECombatMode::Melee,ACECombatMode::Missile,ACECombatMode::NonCombat,ACECombatMode::Magic})
		{
			VR->Client->Session->PlayerVitals.CombatMode=Mode;VR->UpdatePanels();
			TestEqual(TEXT("Only magic stance shows the wrist spellbar"),VR->WristPanel->IsVisible(),Mode==ACECombatMode::Magic);
		}
		{
			FACEWorldObject Mob;Mob.Guid=200001;Mob.ItemType=ACEItemType::Creature;Mob.SetupId=0x0200003D;
			auto* Enemy=World->SpawnActor<AACEWorldEntityActor>();Enemy->InitializeFromObject(Mob,100,true);
			Enemy->SetActorLocation(VR->Head->GetComponentLocation()+FVector(200,0,-100));
			VR->EnemyHealth(Mob.Guid,0.f);
			TestTrue(TEXT("One-hit kills never allocate a black meter"),VR->EnemyHealthBars.IsEmpty());
			VR->EnemyHealth(Mob.Guid,.75f);
			TestEqual(TEXT("Confirmed damage creates one overhead meter"),VR->EnemyHealthBars.Num(),1);
			if(!VR->EnemyHealthBars.IsEmpty())
			{
				auto& Bar=VR->EnemyHealthBars[0];
				TestTrue(TEXT("Overhead health is visible and uses authoritative fraction"),Bar.Panel->IsVisible() && Bar.Meter->GetFraction()==.75f);
				for(int32 Pass=0;Pass<5;++Pass){VR->UpdateEnemyHealthBars();Bar.Panel->TickComponent(.016f,LEVELTICK_All,nullptr);FlushRenderingCommands();}
				if(auto* Target=Bar.Panel->GetRenderTarget())
				{
					TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
					int32 VisiblePixels=0,RedPixels=0;for(const auto P:Pixels){VisiblePixels+=P.A>100;RedPixels+=P.R>80 && P.R>P.G*2;}
					AddInfo(FString::Printf(TEXT("Enemy target %dx%d visible=%d red=%d"),Target->SizeX,Target->SizeY,VisiblePixels,RedPixels));
					TestTrue(TEXT("Vector overhead bar paints a clear red health fill"),RedPixels>Target->SizeX*Target->SizeY*.4f);
					TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Target->SizeX,Target->SizeY,Pixels,PNG);
					FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/TEXT("Automation/VR/EnemyHealth.png")));
				}
				else AddError(TEXT("Overhead bar did not allocate a render target"));
				VR->EnemyHealth(Mob.Guid,.25f);TestEqual(TEXT("Subsequent damage updates the same bar"),Bar.Meter->GetFraction(),.25f);
				VR->EnemyHealth(Mob.Guid,1.f);TestEqual(TEXT("Healing updates that bar"),Bar.Meter->GetFraction(),1.f);
				Enemy->SetActorLocation(VR->Head->GetComponentLocation()+FVector(3000,0,-100));VR->UpdateEnemyHealthBars();
				TestTrue(TEXT("Distant targets retain readable angular width"),Bar.Panel->GetComponentScale().X*Bar.Panel->GetDrawSize().X>=400.f);
				TestEqual(TEXT("Health bar has a shorter profile"),Bar.Panel->GetDrawSize().Y,40.);
				VR->EnemyHealth(Mob.Guid,0.f);TestFalse(TEXT("Death immediately hides an existing meter"),Bar.Panel->IsVisible());
				Bar.Expires=0;VR->UpdateEnemyHealthBars();TestFalse(TEXT("Expired health bars hide"),Bar.Panel->IsVisible());
			}
			Enemy->Destroy();
		}
		VR->Settings->bPinHotbarToView = VR->Settings->bPinMenuToView = true; VR->UpdatePanels();
		TestEqual(TEXT("Pinned retail menus use camera late update"), VR->RetailPanel->GetAttachParent(), static_cast<USceneComponent*>(VR->Head.Get()));
		TestEqual(TEXT("Pinned hotbar uses camera late update"), VR->WristPanel->GetAttachParent(), static_cast<USceneComponent*>(VR->Head.Get()));
		VR->Settings->bPinHotbarToView = VR->Settings->bPinMenuToView = false; VR->UpdatePanels();
		PollHands();
		VR->RightPointer->TickComponent(.016f, LEVELTICK_All, nullptr);
		VR->Trigger(false, true); VR->Trigger(false, false);
		TestTrue(TEXT("Unsupported server gives a visible casting reason"), VR->GetCastFeedback().Contains(TEXT("server")));
		FACEBinaryWriter CombatAck; CombatAck.WriteUInt32(100); CombatAck.WriteUInt32(2); CombatAck.WriteUInt32(0xF7D0);
		CombatAck.WriteUInt32(1); CombatAck.WriteUInt32(15); CombatAck.WriteUInt32(0); CombatAck.WriteUInt32(0);
		FACEBinaryReader CombatAckInput(CombatAck.GetData()); VR->Client->Session->HandleGameEvent(CombatAckInput);
		const uint32 BeforeCast = VR->Client->Session->VRSequence;
		VR->Trigger(false, true); VR->Trigger(false, false);
		TestEqual(TEXT("A trigger after selecting produces one VR spell action"), VR->Client->Session->VRSequence, BeforeCast+1u);
		VR->Client->OnChatMessage.Broadcast(TEXT("Not enough mana."), TEXT(""), ACEChatMessageType::TransientInfo);
		TestEqual(TEXT("Server cast errors remain visible on the wrist"), VR->GetCastFeedback(), FString(TEXT("Not enough mana.")));
		if (TestTrue(TEXT("Avatar fixture loads retail DAT"), Dat->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call"))))
		{
			auto CombatAddress = Sockets->CreateInternetAddr(); CombatAddress->SetIp(TEXT("127.0.0.1"), Valid); CombatAddress->SetPort(0);
			auto* CombatReceiver = Sockets->CreateSocket(NAME_DGram, TEXT("VR combat fixture"), false);
			TestTrue(TEXT("Combat packet receiver binds"), CombatReceiver && CombatReceiver->Bind(*CombatAddress));
			CombatReceiver->GetAddress(*CombatAddress);
			auto& CombatSession = *VR->Client->Session;
			CombatSession.SocketC2S = Sockets->CreateSocket(NAME_DGram, TEXT("VR combat sender"), false);
			CombatSession.ServerC2SAddr = CombatAddress;
			{
				TGuardValue<bool> InventoryClosed(VR->bInventoryOpen, false);
				TGuardValue<bool> SettingsClosed(VR->bSettingsOpen, false);
				TGuardValue<bool> Handedness(VR->Settings->bLeftHanded, false);
				TGuardValue<int32> Spell(VR->SelectedSpell, 1);
				const auto PreviousSelection = VR->Client->GetSelectedObject();
				const auto KnownSpells = CombatSession.KnownSpells;
				const auto Profiles = CombatSession.VRSpellProfiles;
				CombatSession.KnownSpells.Append({1, 2, 3, 5, 23, 27});
				for (int32 Id : {1, 2, 3, 5, 23}) CombatSession.VRSpellProfiles.Add(Id, FVector::ZeroVector);
				FACEWorldObject Recipient; Recipient.Guid = 71001; Recipient.Name = TEXT("Buff recipient");
				Recipient.ItemType = ACEItemType::Creature; Recipient.bIsPlayer = true;
				Recipient.bHasPosition = true; Recipient.Position.CellId = 1;
				CombatSession.WorldObjects.Add(Recipient.Guid, Recipient);
				auto* Player = World->SpawnActor<AACEWorldEntityActor>(); Player->InitializeFromObject(Recipient, 100, false);
				Player->ConfigureWorldCollision(true);
				auto* Body = Player->FindComponentByClass<UCapsuleComponent>();
				Body->SetCapsuleSize(30, 80);
				FACEWorldObject Other = Recipient; Other.Guid = 71002;
				CombatSession.WorldObjects.Add(Other.Guid, Other);
				auto PointAtPlayer = [&]()
				{
					// Restore tracked poses and aim forward below eye height. A hand
					// above the head points through its head-facing wrist panel.
					PollHands();
					VR->ResetHandContacts();
					const FVector Hand = VR->Head->GetComponentLocation() + FVector(2000, 0, -60);
					VR->WeaponGrip()->SetWorldLocationAndRotation(Hand, FRotator::ZeroRotator);
					VR->WeaponAim()->SetWorldLocationAndRotation(Hand, FRotator::ZeroRotator);
					FVector Origin, Direction; VR->GetSpellAim(Origin, Direction);
					Player->SetActorLocation(Origin + Direction * 500 - FVector(0, 0, 80));
					Body->SetWorldLocation(Origin + Direction * 500);
					VR->UpdatePanels();
					TestFalse(TEXT("Buff gesture points into the world, clear of wrist UI"), VR->IsPointerNearPanel(VR->Settings->bLeftHanded));
				};
				auto ClickSpell = [&](int32 ExpectedTarget, int32 ExpectedCount = 1)
				{
					CombatSession.CachedC2SPackets.Reset(); VR->LastCast = -100;
					VR->Trigger(VR->Settings->bLeftHanded, true); VR->Trigger(VR->Settings->bLeftHanded, false);
					int32 Casts = 0;
					for (const auto& Packet : CombatSession.CachedC2SPackets)
					{
						FACEBinaryReader Wire(Packet.Value.Payload); Wire.Skip(16);
						if (Wire.ReadUInt32() != ACEOpcode::GameAction) continue; Wire.ReadUInt32();
						if (Wire.ReadUInt32() != 0xF7D0u) continue;
						Wire.ReadUInt32(); if (Wire.ReadUInt32() != 1u) continue;
						Wire.Skip(12); Wire.ReadUInt32();
						TestEqual(TEXT("Trigger sends the selected spell"), Wire.ReadUInt32(), uint32(VR->SelectedSpell));
						TestEqual(TEXT("Trigger sends the expected buff recipient"), Wire.ReadUInt32(), uint32(ExpectedTarget));
						++Casts;
					}
					TestEqual(TEXT("One trigger click sends exactly one cast unless blocked"), Casts, ExpectedCount);
				};
				for (bool LeftHanded : {false, true})
				{
					VR->Settings->bLeftHanded = LeftHanded; PointAtPlayer();
					for (int32 Buff : {1, 5, 23}) // Strength Other, Heal Other, Armor Other from retail DAT.
					{
						VR->SelectSpell(Buff); VR->Client->SelectObject(Other.Guid);
						TestTrue(TEXT("Retail Other buff recognizes a live player recipient"), VR->IsPointedBuffRecipient(Player));
						VR->UpdateCombat(.016f);
						TestEqual(TEXT("Other buff preview highlights the pointed player over an old selection"), VR->PredictedCombatTarget.Get(), Player);
						TestEqual(TEXT("Aiming does not change selection before the click"), VR->Client->GetSelectedObject().Guid, Other.Guid);
						ClickSpell(Recipient.Guid);
						TestEqual(TEXT("Trigger selects the player it buffs"), VR->Client->GetSelectedObject().Guid, Recipient.Guid);
					}
				}
				VR->Client->SelectObject(0); ClickSpell(Recipient.Guid);
				TestEqual(TEXT("Buff trigger also selects a player from no selection"), VR->Client->GetSelectedObject().Guid, Recipient.Guid);
				for (int32 UnchangedSpell : {2, 3, 27}) // Self buff, harmful Other, projectile.
				{
					VR->SelectSpell(UnchangedSpell); VR->Client->SelectObject(Other.Guid);
					TestFalse(TEXT("Other spell kinds retain their targeting rules"), VR->IsPointedBuffRecipient(Player));
					ClickSpell(Other.Guid);
					TestEqual(TEXT("Self, debuff and projectile clicks do not replace selection"), VR->Client->GetSelectedObject().Guid, Other.Guid);
				}
				VR->SelectSpell(1); Player->bIsPlayer = false;
				ClickSpell(Other.Guid); Player->bIsPlayer = true;
				VR->bInventoryOpen = true; VR->UpdatePanels(); ClickSpell(Other.Guid, 0);
				TestEqual(TEXT("Open menus cannot select a buff target behind them"), VR->Client->GetSelectedObject().Guid, Other.Guid);
				VR->bInventoryOpen = false; VR->UpdatePanels();
				auto* Blocker = World->SpawnActor<AActor>();
				auto* Wall = NewObject<UBoxComponent>(Blocker); Blocker->SetRootComponent(Wall);
				Wall->SetBoxExtent(FVector(20, 100, 100)); Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Wall->SetCollisionResponseToAllChannels(ECR_Ignore); Wall->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
				Wall->RegisterComponent(); Blocker->SetActorLocation(Body->GetComponentLocation() - FVector(200, 0, 0));
				VR->Client->SelectObject(0); VR->UpdateCombat(.016f);
				TestNull(TEXT("A player behind a wall is not highlighted for a buff"), VR->PredictedCombatTarget.Get());
				ClickSpell(0);
				TestFalse(TEXT("Trigger cannot select a buff recipient through a wall"), VR->Client->GetSelectedObject().bValid);
				Blocker->Destroy(); Player->Destroy();
				CombatSession.WorldObjects.Remove(Recipient.Guid); CombatSession.WorldObjects.Remove(Other.Guid);
				CombatSession.KnownSpells = KnownSpells; CombatSession.VRSpellProfiles = Profiles;
				VR->Client->SelectObject(PreviousSelection.Guid); VR->LastCast = -100;
				PollHands(); VR->ResetHandContacts();
			}
			auto* App = NewObject<UACECharacterAppearanceComponent>(Pawn); Pawn->AddInstanceComponent(App); App->RegisterComponent();
			App->bAlignMeshToCapsuleBottom = true;
			FACEWorldObject Self; Self.SetupId = 0x02000001; Self.bIsPlayer = true;
			TestTrue(TEXT("Retail human fixture builds"), App->ApplyWorldObject(Self, 100, false));
			VR->AvatarEyeHeight = 165.f; VR->Head->SetRelativeLocationAndRotation(FVector(25,15,165),FRotator::ZeroRotator); VR->UpdateArms();
			FTransform HeadBind; App->GetPartBindTransform(16, HeadBind);
			const FVector BodyEye = App->GetPartMesh(16)->GetComponentTransform().TransformPosition(FVector(0, -8, 17));
			TestTrue(TEXT("Avatar eye position aligns with room-scale headset"), BodyEye.Equals(VR->Head->GetComponentLocation(), .1));
			const FVector Shoulder = App->GetPartMesh(10)->GetComponentLocation();
			TestTrue(TEXT("Eyes remain anatomically above the shoulders"), BodyEye.Z - Shoulder.Z > 20);
			for (int32 Side = 0; Side < 2; ++Side)
			{
				int32 Part; FTransform Holding; Dat->GetHoldingLocation(Self.SetupId, Side == 0 ? 8 : 1, Part, Holding, 100);
				auto* Grip = Side == 0 ? VR->LeftGrip.Get() : VR->RightGrip.Get();
				const FTransform Calibrated = VR->GetAvatarGrip(Side == 0);
				const FTransform WorldHold = Holding * App->GetPartMesh(Part)->GetComponentTransform();
				TestTrue(TEXT("Authored hand socket aligns with the controller grip"), WorldHold.GetLocation().Equals(Grip->GetComponentLocation(), .1));
				TestTrue(TEXT("Weapon shaft follows the calibrated grasp"), FVector::DotProduct(WorldHold.GetUnitAxis(EAxis::Z), Calibrated.GetUnitAxis(EAxis::X)) > .999);
				TestTrue(TEXT("Retail fingertips point down from the grip instead of toward the wearer"),
					FVector::DotProduct(-App->GetPartMesh(Part)->GetUpVector(), -Calibrated.GetUnitAxis(EAxis::Z)) > .999);
				TestEqual(TEXT("Equipment anchors use the hand, not the forearm"), VR->GetHeldAnchor(Side == 0 ? 2 : 1), App->GetPartMesh(Part));
			}
			VR->Settings->bShowBody = false; VR->UpdateArms();
			for (int32 I = 0; I < App->GetPartCount(); ++I)
			{
				auto* Part = Cast<UPrimitiveComponent>(App->GetPartMesh(I));
				const bool Arm = (I >= 10 && I <= 15) || I == 27 || I == 28;
				TestEqual(FString::Printf(TEXT("Arms-only owner visibility part %d"), I), bool(Part->bOwnerNoSee), !Arm);
				TestTrue(TEXT("Every body part remains available for a complete player shadow"), Part->IsVisible() && Part->CastShadow && Part->bCastHiddenShadow);
				TestEqual(TEXT("Body parts cannot collide with player motion"), Part->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
			}
			for (float Height : {110.f, 165.f, 190.f})
			{
				VR->Head->SetRelativeLocation(FVector(25, 15, Height)); VR->UpdateArms();
				TestTrue(TEXT("Camera remains at the avatar eyes across user heights"), App->GetPartMesh(16)->GetComponentTransform()
					.TransformPosition(FVector(0, -8, 17)).Equals(VR->Head->GetComponentLocation(), .1));
			}
			for (FRotator Rotation : {FRotator::ZeroRotator, FRotator(20, 30, 60), FRotator(-20, -25, -75)})
			{
				VR->LeftGrip->SetWorldRotation(Rotation); VR->RightGrip->SetWorldRotation(Rotation); VR->UpdateArms();
				for (int32 Part : {11, 14})
				{
					FTransform LowerBind, HandBind; App->GetPartBindTransform(Part, LowerBind); App->GetPartBindTransform(Part + 1, HandBind);
					const FVector LocalAxis = LowerBind.GetRotation().UnrotateVector(HandBind.GetLocation() - LowerBind.GetLocation()).GetSafeNormal();
					auto* Lower = App->GetPartMesh(Part); auto* Hand = App->GetPartMesh(Part + 1);
					const FVector Axis = (Hand->GetComponentLocation() - Lower->GetComponentLocation()).GetSafeNormal();
					TestTrue(TEXT("Both forearms point from elbow toward their wrist across controller rotations"),
						FVector::DotProduct(Lower->GetComponentQuat().RotateVector(LocalAxis), Axis) > .999);
					TestTrue(TEXT("Forearm roll stays on the palm's side rather than reversing"), FVector::DotProduct(
						FVector::VectorPlaneProject(Lower->GetRightVector(), Axis).GetSafeNormal(),
						FVector::VectorPlaneProject(Hand->GetRightVector(), Axis).GetSafeNormal()) > 0.f);
					const FVector ActualElbow = Lower->GetComponentTransform().TransformPosition(FVector(0, 0, 14));
					const FVector ActualWrist = Lower->GetComponentTransform().TransformPosition(LowerBind.InverseTransformPosition(HandBind.GetLocation()));
					TestTrue(TEXT("Visible forearm wrist meets the hand, including the forearm's internal origin offset"), ActualWrist.Equals(Hand->GetComponentLocation(), .1));
					FTransform UpperBind; App->GetPartBindTransform(Part - 1, UpperBind);
					const FVector UpperElbow = App->GetPartMesh(Part - 1)->GetComponentTransform().TransformPosition(
						UpperBind.InverseTransformPosition(LowerBind.TransformPosition(FVector(0, 0, 14))));
					TestTrue(TEXT("Visible upper arm and forearm share their actual elbow landmark"), ActualElbow.Equals(UpperElbow, .1));
					TestTrue(TEXT("Forearm extends a full anatomical length, not just origin-to-wrist"), FVector::Distance(ActualElbow, ActualWrist) > 25.f);
				}
			}
			// Render the actual retail meshes for visual inspection, not only joint axes.
			Pawn->SetActorHiddenInGame(false);
			PollHands(); VR->Head->SetRelativeLocation(FVector(0, 0, 165)); VR->UpdateArms();
			auto* CaptureOwner = World->SpawnActor<AActor>();
			auto* Capture = NewObject<USceneCaptureComponent2D>(CaptureOwner); CaptureOwner->AddInstanceComponent(Capture);
			Capture->RegisterComponent(); Capture->bCaptureEveryFrame = Capture->bCaptureOnMovement = false;
			Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			Capture->ShowOnlyActorComponents(Pawn); Capture->ShowFlags.SetLighting(false);
			Capture->CaptureSource = SCS_FinalColorLDR; Capture->ShowFlags.SetEyeAdaptation(false); Capture->ShowFlags.SetAtmosphere(false);
			auto* ArmImage = NewObject<UTextureRenderTarget2D>(CaptureOwner); ArmImage->InitCustomFormat(1024, 1024, PF_B8G8R8A8, false); Capture->TextureTarget = ArmImage;
			FBox ArmBounds(ForceInit); for (int32 Part = 10; Part <= 15; ++Part) ArmBounds += Cast<UPrimitiveComponent>(App->GetPartMesh(Part))->Bounds.GetBox();
			const FVector Center = ArmBounds.GetCenter(); const FVector Eye = Center + VR->Head->GetForwardVector() * 130 + FVector(0, 0, 60);
			Capture->SetWorldLocationAndRotation(Eye, (Center - Eye).Rotation()); Capture->FOVAngle = 70;
			World->SendAllEndOfFrameUpdates(); Capture->CaptureScene(); FlushRenderingCommands();
			TArray<FColor> ArmPixels; ArmImage->GameThread_GetRenderTargetResource()->ReadPixels(ArmPixels);
			TArray64<uint8> ArmPNG; FImageUtils::PNGCompressImageArray(1024, 1024, ArmPixels, ArmPNG);
			FFileHelper::SaveArrayToFile(ArmPNG, *(FPaths::ProjectSavedDir() / TEXT("Automation/VR/ArmGeometry.png")));
			CaptureOwner->Destroy();
			PollHands(); VR->UpdateArms();
			Wand.SetupId = 0x0200017C; Wand.ParentGuid = 100; Wand.ParentLocation = 1;
			VR->Client->Session->WorldObjects[Wand.Guid] = Wand;
			auto* WandActor = World->SpawnActor<AACEWorldEntityActor>(); WandActor->InitializeFromObject(Wand, 100, true);
			WandActor->AttachToActor(Pawn, FAttachmentTransformRules::KeepWorldTransform);
			int32 WandHandPart; FTransform WandHolding; Dat->GetHoldingLocation(Self.SetupId, 1, WandHandPart, WandHolding, 100);
			WandActor->SetActorTransform(WandHolding * App->GetPartMesh(WandHandPart)->GetComponentTransform());
			const FVector Tip = VR->GetWeaponTip();
			TestTrue(TEXT("Real wand muzzle is away from the controller and at the outward end"), FVector::Distance(Tip, VR->WeaponGrip()->GetComponentLocation()) > 35.f);
			TestTrue(TEXT("Wand aim follows the rendered part shaft"), FVector::DotProduct(
				WandActor->GetActorTransform().TransformVectorNoScale(VR->WeaponAxisLocal), WandActor->Appearance->GetPartMesh(0)->GetUpVector()) > .999);
			const FTransform WandTransform = WandActor->GetActorTransform();
			const int32 WandSetup = Wand.SetupId;
			Wand.SetupId = 0x02000B5A; // Dark Sorcerer's Phylactery: no meaningful shaft.
			CombatSession.WorldObjects[Wand.Guid] = Wand;
			WandActor->InitializeFromObject(Wand, 100, true);
			for (float Yaw : {0.f, 180.f})
			{
				WandActor->SetActorLocationAndRotation(VR->WeaponGrip()->GetComponentLocation(), FRotator(0,Yaw,0));
				FVector CastOrigin, CastDirection; VR->GetSpellAim(CastOrigin,CastDirection);
				TestTrue(TEXT("Phylactery casts along the controller aim, even with a reversed model"),CastDirection.Equals(VR->WeaponAim()->GetForwardVector(),.001));
				TestTrue(TEXT("Phylactery muzzle remains near the held item"),FVector::Dist(CastOrigin,WandActor->GetActorLocation())<60.f);
			}
			VR->UpdatePointerVisuals(true,false);
			TestTrue(TEXT("Magic stance displays a visible casting direction"),VR->PointerBeams[VR->Settings->bLeftHanded?0:1]->IsVisible());
			Wand.SetupId=WandSetup;CombatSession.WorldObjects[Wand.Guid]=Wand;
			WandActor->InitializeFromObject(Wand,100,true);WandActor->SetActorTransform(WandTransform);
			// Animation of the rendered weapon must not manufacture a controller swing.
			Wand.CurrentWieldedLocation = ACEEquipMask::MeleeWeapon; VR->Client->Session->WorldObjects[Wand.Guid] = Wand;
			VR->Client->Session->PlayerVitals.CombatMode = ACECombatMode::Melee;
			VR->UpdateCombat(.02f);
			for (int32 Frame = 0; Frame < 30; ++Frame)
			{
				WandActor->AddActorWorldOffset(FVector(Frame % 2 ? 40 : -40, 0, 0)); VR->UpdateCombat(.02f);
				TestTrue(TEXT("A moving visual weapon with a still controller cannot create a swing"), VR->Swing.Travel == 0.f);
			}
			Gameplay->bCombatRepeatActive = Gameplay->bCombatAttackRequestPending = true; Gameplay->TickCombatAutoAttack(.1f);
			TestFalse(TEXT("Retail auto attack cannot bypass tracked VR combat"), Gameplay->bCombatRepeatActive || Gameplay->bCombatAttackRequestPending);
			const uint32 BeforeWalking = CombatSession.VRSequence;
			const FTransform BeforeWalkTracking = VR->TrackingOrigin->GetComponentTransform();
			for (int32 Frame = 0; Frame < 30; ++Frame)
			{
				VR->TrackingOrigin->AddWorldOffset(FVector(0, 12, 0)); VR->UpdateCombat(.02f);
				TestTrue(TEXT("Moving the tracking rig cannot manufacture controller swing travel"), VR->Swing.Travel < .01f);
			}
			TestEqual(TEXT("Walking with a still sword cannot dispatch a strike"), CombatSession.VRSequence, BeforeWalking);
			VR->TrackingOrigin->SetWorldTransform(BeforeWalkTracking); VR->Swing.Reset();
			// Sweep a calibrated blade through a real world-entity collision proxy,
			// then inspect the reliable packet rather than only a local counter.
			VR->WeaponGrip()->SetWorldLocationAndRotation(FVector(70, -90, 130), FRotator::ZeroRotator);
			VR->Swing.Reset(); VR->UpdateCombat(.02f);
			const FVector BladeSample = VR->WeaponGrip()->GetComponentTransform().TransformPosition(VR->BladeTipInGrip * .7f);
			FACEWorldObject Creature; Creature.Guid = static_cast<int32>(0x80000777u); Creature.ItemType = ACEItemType::Creature;
			Creature.ObjectDescriptionFlags = ACEObjectDescFlag::Attackable;
			Creature.SetupId = 0x020007DD; Creature.bHasPosition = true; Creature.Position.CellId = 1;
			CombatSession.WorldObjects.Add(Creature.Guid, Creature);
			auto* CreatureActor = World->SpawnActor<AACEWorldEntityActor>(); CreatureActor->InitializeFromObject(Creature, 100, false);
			CreatureActor->ConfigureWorldCollision(true);
			auto* CreatureCapsule = CreatureActor->FindComponentByClass<UCapsuleComponent>();
			CreatureActor->SetActorLocation(BladeSample + FVector(0,45,-80));
			CreatureCapsule->SetCapsuleSize(18,55); CreatureCapsule->SetWorldLocation(BladeSample + FVector(0,45,0));
			CombatSession.CachedC2SPackets.Reset(); const uint32 BeforeSwing = CombatSession.VRSequence;
			for (int32 Frame = 0; Frame < 12; ++Frame)
			{
				VR->WeaponGrip()->AddWorldOffset(FVector(0, 8, 0)); VR->UpdateCombat(.02f);
			}
			TestEqual(TEXT("Deliberate controller sweep sends one attack through a creature"), CombatSession.VRSequence, BeforeSwing + 1);
			TestEqual(TEXT("Struck creature becomes the selected target"), VR->Client->GetSelectedObject().Guid, Creature.Guid);
			bool SentSwing = false;
			for (const auto& Packet : CombatSession.CachedC2SPackets)
			{
				FACEBinaryReader Wire(Packet.Value.Payload); Wire.Skip(16);
				if (Wire.ReadUInt32() != ACEOpcode::GameAction) continue; Wire.ReadUInt32();
				if (Wire.ReadUInt32() != 0xF7D0u) continue;
				Wire.ReadUInt32(); if (Wire.ReadUInt32() != 2u) continue;
				Wire.Skip(12); TestEqual(TEXT("Swing packet identifies equipped weapon"), Wire.ReadUInt32(), uint32(Wand.Guid));
				Wire.ReadUInt32(); TestEqual(TEXT("Swing packet identifies struck high-bit creature GUID"), Wire.ReadUInt32(), uint32(Creature.Guid));
				SentSwing = true;
			}
			TestTrue(TEXT("Tracked melee reaches the reliable network packet"), SentSwing);
			// Retail arrows share ItemType::MissileWeapon with bows. Exercise the
			// desktop charge-to-packet path with arrows still equipped after a swap.
			FACEWorldObject RemainingArrows; RemainingArrows.Guid = 7777; RemainingArrows.WielderId = 100;
			RemainingArrows.CurrentWieldedLocation = ACEEquipMask::MissileAmmo;
			RemainingArrows.ItemType = ACEItemType::MissileWeapon;
			CombatSession.WorldObjects.Add(RemainingArrows.Guid, RemainingArrows);
			Gameplay->PlayerController = nullptr; // Desktop input, independent of the test's tracked pawn.
			Gameplay->ApplyCombatMode(Gameplay->ResolveEquippedCombatMode());
			TestEqual(TEXT("Desktop enters melee while a sword and arrows are equipped"), Gameplay->CombatMode, int32(ACECombatMode::Melee));
			Gameplay->RequestedAttackPower = .75f;
			Gameplay->BeginCombatPowerCharge(2);
			Gameplay->CombatPowerBuildStartTime -= 2.;
			CombatSession.CachedC2SPackets.Reset();
			Gameplay->TickCombatAutoAttack(.1f);
			bool SentDesktopMelee = false;
			for (const auto& Packet : CombatSession.CachedC2SPackets)
			{
				FACEBinaryReader Wire(Packet.Value.Payload); Wire.Skip(16);
				if (Wire.ReadUInt32() != ACEOpcode::GameAction) continue; Wire.ReadUInt32();
				if (Wire.ReadUInt32() != ACEGameAction::TargetedMeleeAttack) continue;
				TestEqual(TEXT("Charged desktop melee targets the creature for server approach and attack"), Wire.ReadUInt32(), uint32(Creature.Guid));
				TestEqual(TEXT("Desktop melee retains the chosen attack height"), Wire.ReadUInt32(), 2u);
				TestEqual(TEXT("Desktop melee retains charged power"), Wire.ReadFloat(), .75f);
				SentDesktopMelee = true;
			}
			TestTrue(TEXT("Desktop charge dispatches melee instead of silently rejecting a missile stance"), SentDesktopMelee);
			Gameplay->PlayerController = PC;
			Gameplay->bCombatRepeatActive = Gameplay->bCombatAttackRequestPending = Gameplay->bCombatPowerCharging = false;
			CombatSession.WorldObjects.Remove(RemainingArrows.Guid);
			// Drive both real rig hands through the same creature with no weapon.
			CombatSession.WorldObjects.Remove(Wand.Guid);
			WandActor->SetActorEnableCollision(false);
			CombatSession.VRCapabilities |= 128u;
			CombatSession.PlayerVitals.CombatMode = ACECombatMode::Melee;
			const FVector PunchFeet = Pawn->GetActorLocation()-FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());
			Creature.SetupId=0x0200003D; // Real rat, including its physical body.
			CreatureActor->InitializeFromObject(Creature,100,false); CreatureActor->ConfigureWorldCollision(true);
			for (bool LeftHanded : {false,true}) for (bool Left : {false,true})
			{
				VR->Settings->bLeftHanded=LeftHanded; VR->CancelGestures(); VR->ResetHandContacts();
				auto* Hand = Left ? VR->LeftGrip.Get() : VR->RightGrip.Get();
				const FVector Start = PunchFeet+FVector(20,0,25);
				Hand->SetWorldLocation(Start);
				CreatureActor->SetActorLocation(PunchFeet+FVector(95,0,0));
				VR->UpdateHandContacts(.02f); VR->UpdateCombat(.02f); const uint32 BeforePunch=CombatSession.VRSequence;
				for(int32 Frame=0;Frame<10;++Frame) { Hand->AddWorldOffset(FVector(6,0,0)); VR->UpdateHandContacts(.02f); VR->UpdateCombat(.02f); }
				TestEqual(TEXT("Either empty hand punches with either handedness setting"),CombatSession.VRSequence,BeforePunch+1);
				TestFalse(TEXT("Combat fist is not stopped by the rat mesh or locomotion body"),VR->ContactHands[Left?0:1].State.bContact);
				TestTrue(TEXT("Combat fist follows tracking through the rat while submitting damage"),
					VR->GetPhysicalGrip(Left).GetLocation().Equals(Hand->GetComponentLocation(),.02));
				for(int32 Frame=0;Frame<20;++Frame) VR->UpdateCombat(.02f);
				TestEqual(TEXT("Resting fist on creature cannot repeat damage"),CombatSession.VRSequence,BeforePunch+1);
				VR->CancelGestures(); Hand->SetWorldLocation(Start); VR->bSpellWheelOpen=true;
				VR->UpdateCombat(.02f);
				for(int32 Frame=0;Frame<10;++Frame) { Hand->AddWorldOffset(FVector(6,0,0)); VR->UpdateHandContacts(.02f); VR->UpdateCombat(.02f); }
				TestEqual(TEXT("Choosing a spell cannot punch a creature with an empty hand"),CombatSession.VRSequence,BeforePunch+1);
				VR->bSpellWheelOpen=false; VR->UpdateCombat(.02f);
				TestEqual(TEXT("Closing the wheel cannot release a queued punch"),CombatSession.VRSequence,BeforePunch+1);
				FACEWorldObject Shield; Shield.Guid=13001; Shield.WielderId=100; Shield.CurrentWieldedLocation=ACEEquipMask::Shield;
				CombatSession.WorldObjects.Add(Shield.Guid,Shield);
				VR->CancelGestures(); Hand=LeftHanded ? VR->RightGrip.Get() : VR->LeftGrip.Get();
				Hand->SetWorldLocation(Start); VR->UpdateCombat(.02f);
				for(int32 Frame=0;Frame<10;++Frame) { Hand->AddWorldOffset(FVector(6,0,0)); VR->UpdateHandContacts(.02f); VR->UpdateCombat(.02f); }
				TestEqual(TEXT("Shield hand cannot submit a punch"),CombatSession.VRSequence,BeforePunch+1);
				CombatSession.WorldObjects.Remove(Shield.Guid);
			}
			// Switching stances affects only hand resistance, not creature collision
			// used by the pawn or the independent melee damage query.
			for (int32 Mode : {ACECombatMode::NonCombat,ACECombatMode::Melee,ACECombatMode::Missile,ACECombatMode::Magic,ACECombatMode::NonCombat})
			{
				CombatSession.PlayerVitals.CombatMode=Mode; VR->ResetHandContacts();
				VR->RightGrip->SetWorldLocation(PunchFeet+FVector(80,0,25)); VR->UpdateHandContacts(.02f);
				TestEqual(TEXT("Only peaceful hands stop at the visible creature surface"),VR->ContactHands[1].State.bContact,Mode==ACECombatMode::NonCombat);
				TestTrue(TEXT("Combat hand pass-through leaves creature world collision enabled"),CreatureActor->GetActorEnableCollision());
			}
			VR->Settings->bLeftHanded=false; VR->CancelGestures(); VR->ResetHandContacts();
			CreatureActor->Destroy(); CombatSession.WorldObjects.Remove(Creature.Guid);
			WandActor->Destroy(); VR->Client->Session->WorldObjects.Remove(Wand.Guid);
			FACEWorldObject TwoHand;TwoHand.Guid=12001;TwoHand.WielderId=100;TwoHand.CurrentWieldedLocation=ACEEquipMask::TwoHanded;
			CombatSession.WorldObjects.Add(TwoHand.Guid,TwoHand);CombatSession.PlayerVitals.CombatMode=ACECombatMode::Melee;
			for(bool LeftHanded:{false,true})
			{
				VR->Settings->bLeftHanded=LeftHanded;
				TestEqual(TEXT("Retail left-handed two-hand attachment follows VR dominant hand"),VR->GetHeldAnchor(2,true),App->GetPartMesh(LeftHanded?12:15));
				const FTransform Before=VR->GetAvatarGrip(!LeftHanded);
				VR->BowGrip()->AddWorldOffset(FVector(30,10,0));
				TestTrue(TEXT("Two-handed support grip cannot steer the weapon"),Before.Equals(VR->GetAvatarGrip(!LeftHanded),.001));
				VR->WeaponGrip()->AddWorldOffset(FVector(10,0,0));
				TestTrue(TEXT("Support hand follows the dominant hand's weapon"),VR->GetAvatarGrip(!LeftHanded).GetLocation().Equals(Before.GetLocation()+FVector(10,0,0),.001));
			}
			CombatSession.WorldObjects.Remove(TwoHand.Guid);VR->Settings->bLeftHanded=false;
			FACEWorldObject Bow; Bow.Guid = static_cast<int32>(0x80000EB2u); Bow.SetupId = 0x0200158D; Bow.WielderId = Bow.ParentGuid = 100;
			Bow.ParentLocation = 1; Bow.CurrentWieldedLocation = ACEEquipMask::MissileWeapon; Bow.ItemType = ACEItemType::MissileWeapon; Bow.AmmoType=1;
			FACEWorldObject Ammo; Ammo.Guid = static_cast<int32>(0x80000EB4u); Ammo.SetupId = 0x02001A87; Ammo.WielderId = 100;
			Ammo.CurrentWieldedLocation = ACEEquipMask::MissileAmmo; Ammo.StackSize = 20; Ammo.AmmoType = 1;
			VR->Client->Session->WorldObjects.Add(Bow.Guid, Bow); VR->Client->Session->WorldObjects.Add(Ammo.Guid, Ammo);
			VR->Client->Session->PlayerVitals.CombatMode = ACECombatMode::Missile;
			auto* BowActor = World->SpawnActor<AACEWorldEntityActor>(); BowActor->InitializeFromObject(Bow, 100, true);
			CombatSession.VRMissileWeapon=Bow.Guid; CombatSession.VRMissileSpeed=24.f;
			BowActor->AttachToActor(Pawn, FAttachmentTransformRules::KeepWorldTransform);
			{
				TGuardValue<bool> InventoryClosed(VR->bInventoryOpen,false);
				TGuardValue<bool> SettingsClosed(VR->bSettingsOpen,false);
				TGuardValue<int32> Spell(VR->SelectedSpell,70001);
				CombatSession.VRSpellProfiles.Add(70001,FVector(10,1,.05));
				const FVector Origin(50000,50000,2000), Direction=FVector::ForwardVector;
				FACEWorldObject Body; Body.Guid=70002; Body.ItemType=ACEItemType::Creature;
				Body.SetupId=0x02000001; Body.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;
				Body.bHasPosition=true; Body.Position.CellId=1;
				auto* First=World->SpawnActor<AACEWorldEntityActor>(); First->InitializeFromObject(Body,100,true); First->ConfigureWorldCollision(true);
				Body.Guid=70003;
				auto* Second=World->SpawnActor<AACEWorldEntityActor>(); Second->InitializeFromObject(Body,100,true); Second->ConfigureWorldCollision(true);
				const FVector ArcCenter=Origin+ACEVRMath::BallisticOffset(Direction,10,1,.5f,100);
				First->SetActorLocation(ArcCenter-FVector(0,0,First->GetMeleeBodyHeight()*.5f));
				Second->SetActorLocation(First->GetActorLocation()+FVector(40,0,0));
				VR->FocusPanel->SetVisibility(false);
				VR->UpdateMissileTrajectory(Origin,Direction,1,10,true);
				TestEqual(TEXT("Arc preview highlights the first overlapping creature body"),VR->PredictedCombatTarget.Get(),First);
				TestFalse(TEXT("Arc aiming cannot open a combat help panel"),VR->FocusPanel->IsVisible());
				First->SetActorLocation(First->GetActorLocation()+FVector(0,300,0));
				VR->UpdateMissileTrajectory(Origin,Direction,1,10,true);
				TestEqual(TEXT("Arc highlight follows the next creature when the first leaves the path"),VR->PredictedCombatTarget.Get(),Second);
				Second->SetActorLocation(Origin+FVector(500,0,-Second->GetMeleeBodyHeight()*.5f));
				VR->UpdateMissileTrajectory(Origin,Direction,1,10,false);
				TestEqual(TEXT("Straight magic uses the creature-body contact test too"),VR->PredictedCombatTarget.Get(),Second);
				TestEqual(TEXT("Switching arc to bolt rebuilds the straight-line topology"),VR->MissileTrajectory->GetProcMeshSection(0)->GetRenderIndexCount(),6);
				VR->UpdateWorldSelectionHighlights(VR->PredictedCombatTarget.Get(),nullptr);
				const auto* Part=Cast<UPrimitiveComponent>(Second->Appearance->GetPartMesh(0));
				TestTrue(TEXT("Predicted hit uses the same model tint as world selection"),Part && Part->GetCustomPrimitiveData().Data.IsValidIndex(1) && Part->GetCustomPrimitiveData().Data[1]==1.f);
				auto* Blocker=World->SpawnActor<AActor>();
				auto* Wall=NewObject<UBoxComponent>(Blocker); Blocker->SetRootComponent(Wall);
				Wall->SetBoxExtent(FVector(20,100,100)); Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Wall->SetCollisionResponseToAllChannels(ECR_Ignore); Wall->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
				Wall->RegisterComponent(); Blocker->SetActorLocation(Origin+FVector(200,0,0));
				VR->UpdateMissileTrajectory(Origin,Direction,1,10,false);
				TestNull(TEXT("A wall before the creature prevents a misleading magic highlight"),VR->PredictedCombatTarget.Get());
				VR->UpdateWorldSelectionHighlights(VR->PredictedCombatTarget.Get(),nullptr);
				TestEqual(TEXT("Losing a predicted hit removes the previous model tint"),Part->GetCustomPrimitiveData().Data[1],0.f);
				Blocker->Destroy(); First->Destroy(); Second->Destroy(); CombatSession.VRSpellProfiles.Remove(70001);
			}
			for (bool LeftHanded : {false, true})
			{
				VR->Settings->bLeftHanded = LeftHanded; VR->CancelGestures(); PollHands();
				VR->Settings->BowAnchorOffset=0; // Uncalibrated center-line reference.
				VR->BowAim()->SetWorldRotation(FRotator(70, 20, 30)); VR->BowGrip()->SetWorldRotation(FRotator::ZeroRotator);
				VR->Client->Session->PlayerVitals.CombatMode = ACECombatMode::NonCombat;
				TestTrue(TEXT("Bow uses support hand immediately while peaceful"), VR->UpdateMissileAttachment(BowActor));
				TestTrue(TEXT("Peaceful bow sits at offhand grip"), BowActor->GetActorLocation().Equals(VR->BowGrip()->GetComponentLocation(), .1));
				VR->Client->Session->PlayerVitals.CombatMode = ACECombatMode::Missile;
				VR->UpdateCombat(.02f);
				TestTrue(TEXT("Missing public combat style still resolves the equipped bow"), VR->IsAmmoLauncher());
				TestTrue(TEXT("Launcher accepts offhand VR placement"), VR->UpdateMissileAttachment(BowActor));
				TestEqual(TEXT("Bow effects stay outside controller late update"), BowActor->GetAttachParentActor(), static_cast<AActor*>(Pawn));
				TestTrue(TEXT("Bow limbs point up and shaft faces along offhand aim"), FVector::DotProduct(BowActor->GetActorUpVector(), -VR->BowAim()->GetForwardVector()) > .999);
				const FQuat OffhandBowRotation = BowActor->GetActorQuat();
				const FVector DrawHandLocation = VR->WeaponGrip()->GetComponentLocation();
				for (bool Drawing : {true, false})
				{
					VR->bDrawing = Drawing;
					for (FVector Offset : {FVector::ZeroVector, FVector(30, 20, 10), FVector(-50, -20, 5)})
					{
						VR->WeaponGrip()->SetWorldLocation(VR->BowGrip()->GetComponentLocation() + Offset);
						VR->UpdateMissileAttachment(BowActor);
						TestTrue(TEXT("Drawing and releasing cannot swivel the bow independently of the offhand"), BowActor->GetActorQuat().Equals(OffhandBowRotation, .001));
					}
				}
				VR->WeaponGrip()->SetWorldLocation(DrawHandLocation);
				if (TestNotNull(TEXT("Equipped arrow has a ready visual before drawing"), VR->AmmoActor.Get()))
				{
					auto* AmmoAppearance = VR->AmmoActor->FindComponentByClass<UACECharacterAppearanceComponent>();
					TestTrue(TEXT("Arrow renders the actual equipped DAT setup"), AmmoAppearance->HasAppearance() && AmmoAppearance->GetSetupId() == Ammo.SetupId && !VR->AmmoActor->IsHidden());
				}
				VR->WeaponGrip()->SetWorldLocation(VR->BowGrip()->GetComponentLocation() - FVector(12, 0, 0));
				VR->Grip(LeftHanded, true); TestTrue(TEXT("Grabbing the visible arrow nocks it"), VR->bDrawing);
				for (int32 Frame = 1; Frame <= 10; ++Frame)
				{
					VR->WeaponGrip()->SetWorldLocation(VR->BowGrip()->GetComponentLocation() - FVector(12 + Frame * 4.8f, 0, 0));
					VR->UpdateCombat(.02f);
					TestTrue(TEXT("Nocked bow paints its arc from partial through full draw"),VR->MissileTrajectory && VR->MissileTrajectory->IsVisible());
				}
				TestTrue(TEXT("Pulling equipped arrow behind the bow reaches full draw"), VR->BowFraction > .99f);
				VR->Settings->BowAnchorOffset=10;
				// The support hand is deliberately pitched/rolled above. A cheek-side
				// anchor is lateral in that hand's aim frame, not in world-space Y.
				const FVector CheekOffset=VR->GetPhysicalAim(!LeftHanded).GetUnitAxis(EAxis::Y)
					* (LeftHanded ? -1.f : 1.f) * 10.f * VR->BowFraction;
				VR->WeaponGrip()->AddWorldOffset(CheekOffset);
				TestTrue(TEXT("Mirrored cheek-side draw offset retains forward aim"),VR->BowDrawDirection().Equals(FVector::ForwardVector,.001));
				VR->WeaponGrip()->AddWorldOffset(-CheekOffset); VR->Settings->BowAnchorOffset=0;
				CombatSession.CachedC2SPackets.Reset();
				const uint32 Before = VR->Client->Session->VRSequence; VR->Grip(LeftHanded, false);
				TestEqual(TEXT("Releasing drawn arrow sends one VR missile action"), VR->Client->Session->VRSequence, Before + 1);
				bool SentArrow = false;
				for (const auto& Packet : CombatSession.CachedC2SPackets)
				{
					FACEBinaryReader Wire(Packet.Value.Payload); Wire.Skip(16);
					if (Wire.ReadUInt32() != ACEOpcode::GameAction) continue; Wire.ReadUInt32();
					if (Wire.ReadUInt32() != 0xF7D0u) continue;
					Wire.ReadUInt32(); if (Wire.ReadUInt32() != 3u) continue;
					Wire.Skip(12); TestEqual(TEXT("Arrow packet retains high-bit bow GUID"), Wire.ReadUInt32(), uint32(Bow.Guid));
					Wire.ReadUInt32(); TestEqual(TEXT("Free shot does not require a selected enemy"), Wire.ReadUInt32(), 0u);
					Wire.Skip(12); const float AimX = Wire.ReadFloat(), AimY = Wire.ReadFloat(), AimZ = Wire.ReadFloat();
					TestTrue(TEXT("Arrow packet follows the physical two-hand direction"), FVector(AimX, AimY, AimZ).Equals(FACEPosition::AceVectorToUnreal(FVector::ForwardVector), .001));
					TestTrue(TEXT("Full draw strength reaches the server"), Wire.ReadFloat() > .99f);
					TestTrue(TEXT("Held draw duration reaches the server"), Wire.ReadFloat() >= .15f); SentArrow = true;
				}
				TestTrue(TEXT("Releasing the actual equipped arrow produces a reliable missile packet"), SentArrow);
				{
					TGuardValue<uint32> RecoveryCapabilities(CombatSession.VRCapabilities,CombatSession.VRCapabilities|2048u);
					CombatSession.VRRecoveryTeleport=CombatSession.TeleportSeq;CombatSession.VRRecoveryDuration=1;CombatSession.VRRecoveryReadyAt=FPlatformTime::Seconds()+1;
					VR->bDrawing=true;VR->BowHoldTime=.3f;
					VR->ReleaseArrow();
					TestEqual(TEXT("Bow release respects the same shot recovery as other missiles"),CombatSession.VRSequence,Before+1);
					TestFalse(TEXT("Recovery cancels the released draw without a delayed shot"),VR->bDrawing);
					CombatSession.VRRecoveryReadyAt=0;
				}
				VR->Client->Session->WorldObjects.Remove(Ammo.Guid);
				VR->WeaponGrip()->SetWorldLocation(VR->BowGrip()->GetComponentLocation() - FVector(12, 0, 0)); VR->Grip(LeftHanded, true);
				TestFalse(TEXT("Without equipped ammo a new arrow cannot be drawn"), VR->bDrawing);
				VR->Client->Session->WorldObjects.Add(Ammo.Guid, Ammo);
			}
			Bow.SetupId=0x0200012D; Bow.DefaultCombatStyle=0x20; Ammo.AmmoType=2;
			CombatSession.WorldObjects[Bow.Guid]=Bow;CombatSession.WorldObjects[Ammo.Guid]=Ammo;
			BowActor->InitializeFromObject(Bow,100,true);
			for(bool LeftHanded : {false,true})
			{
				VR->Settings->bLeftHanded=LeftHanded; VR->CancelGestures(); PollHands();
				VR->BowAim()->SetWorldRotation(FRotator(10,35,0));
				VR->UpdateMissileAttachment(BowActor); VR->UpdateCombat(.02f);
				for (int32 Setup:{0x0200012D,0x0200134C,0x0200134D,0x02001596})
				{
					Bow.SetupId=Setup;CombatSession.WorldObjects[Bow.Guid]=Bow;BowActor->InitializeFromObject(Bow,100,true);VR->UpdateMissileAttachment(BowActor);
					AddInfo(FString::Printf(TEXT("Crossbow %08X grip=%s muzzle=%s"),Setup,*VR->CrossbowGripLocal.ToString(),*VR->CrossbowMuzzleLocal.ToString()));
					TestTrue(TEXT("All crossbow families preserve held-frame barrel alignment"),FVector::DotProduct(-BowActor->GetActorRightVector(),VR->BowAim()->GetForwardVector())>.999);
					TestTrue(TEXT("Crossbow limbs lie across the aim, not vertically"),FVector::DotProduct(-BowActor->GetActorForwardVector(),VR->BowAim()->GetUpVector())>.999);
					TestTrue(TEXT("Crossbow is held beneath the rail, not by its top"),VR->CrossbowGripLocal.X>VR->CrossbowMuzzleLocal.X);
					TestTrue(TEXT("Bolt lies on the stock centerline"),FMath::Abs(VR->CrossbowMuzzleLocal.Z-VR->CrossbowGripLocal.Z)<.01);
					if (!LeftHanded)
					{
						BowActor->SetActorHiddenInGame(false);
						for(int32 PartIndex=0;PartIndex<BowActor->Appearance->GetPartCount();++PartIndex)
							if(auto* Part=Cast<UPrimitiveComponent>(BowActor->Appearance->GetPartMesh(PartIndex)))
							{Part->SetVisibility(true);Part->SetHiddenInGame(false);Part->SetOwnerNoSee(false);Part->SetOnlyOwnerSee(false);}
						auto* C=NewObject<USceneCaptureComponent2D>(BowActor);BowActor->AddInstanceComponent(C);C->RegisterComponent();
						C->bCaptureEveryFrame=C->bCaptureOnMovement=false;C->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
						C->ShowOnlyActorComponents(BowActor);C->ShowFlags.SetLighting(false);C->ShowFlags.SetAtmosphere(false);C->ShowFlags.SetEyeAdaptation(false);C->CaptureSource=SCS_FinalColorLDR;
						auto* T=NewObject<UTextureRenderTarget2D>(BowActor);T->InitCustomFormat(768,512,PF_B8G8R8A8,false);C->TextureTarget=T;
						FBox B(ForceInit);BowActor->Appearance->GetVisualWorldBounds(B);const FVector CrossbowCenter=B.GetCenter();
						const FVector CrossbowEye=CrossbowCenter-VR->BowAim()->GetForwardVector()*100+VR->BowAim()->GetRightVector()*90+VR->BowAim()->GetUpVector()*65;
						C->SetWorldLocationAndRotation(CrossbowEye,(CrossbowCenter-CrossbowEye).Rotation());C->FOVAngle=65;
						World->SendAllEndOfFrameUpdates();C->CaptureScene();FlushRenderingCommands();TArray<FColor> Pixels;T->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
						TestTrue(TEXT("Crossbow fixture renders visible geometry"),Pixels.ContainsByPredicate([](FColor Color){return Color.R>30 || Color.G>30 || Color.B>30;}));
						TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(768,512,Pixels,PNG);
						FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Automation/VR/Crossbow-%08X.png"),Setup)));C->DestroyComponent();
					}
				}
				VR->Grip(LeftHanded,true);VR->Grip(LeftHanded,false);
				TestFalse(TEXT("Crossbow never requires or starts a bow draw"),VR->bDrawing);
				CombatSession.CachedC2SPackets.Reset();const uint32 Before=CombatSession.VRSequence;
				VR->Trigger(LeftHanded,true);VR->Trigger(LeftHanded,false);
				TestEqual(TEXT("One crossbow trigger click sends one shot without pulling a string"),CombatSession.VRSequence,Before+1);
				bool SentBolt=false;
				for(const auto& Packet:CombatSession.CachedC2SPackets)
				{
					FACEBinaryReader Wire(Packet.Value.Payload);Wire.Skip(16);
					if(Wire.ReadUInt32()!=ACEOpcode::GameAction)continue;Wire.ReadUInt32();
					if(Wire.ReadUInt32()!=0xF7D0u)continue;Wire.ReadUInt32();if(Wire.ReadUInt32()!=3u)continue;
					Wire.Skip(36);
					const float X=Wire.ReadFloat(),Y=Wire.ReadFloat(),Z=Wire.ReadFloat();
					TestTrue(TEXT("Bolt trajectory matches the visible crossbow barrel"),FVector(X,Y,Z).Equals(FACEPosition::AceVectorToUnreal(VR->BowAim()->GetForwardVector()),.001));
					TestEqual(TEXT("Trigger fires a full-power bolt"),Wire.ReadFloat(),1.f);
					TestEqual(TEXT("Crossbow does not fabricate a held draw duration"),Wire.ReadFloat(),0.f);SentBolt=true;
				}
				TestTrue(TEXT("Crossbow trigger reaches reliable combat protocol"),SentBolt);
				CombatSession.WorldObjects.Remove(Ammo.Guid);const uint32 Empty=CombatSession.VRSequence;
				VR->Trigger(LeftHanded,true);VR->Trigger(LeftHanded,false);
				TestEqual(TEXT("Empty crossbow cannot send another shot"),CombatSession.VRSequence,Empty);
				CombatSession.WorldObjects.Add(Ammo.Guid,Ammo);
			}
			for(bool LeftHanded:{false,true})
			{
				VR->Settings->bLeftHanded=LeftHanded;VR->CancelGestures();PollHands();
				Bow.DefaultCombatStyle=LeftHanded?0:0x400;Bow.AmmoType=4;CombatSession.WorldObjects[Bow.Guid]=Bow;
				TestEqual(TEXT("Atlatl is recognized with explicit or public-only weapon metadata"),VR->MissileStyle(),0x400);
				VR->WeaponGrip()->SetWorldRotation(FRotator(20,45,35));VR->WeaponAim()->SetWorldRotation(FRotator(-5,-60,0));VR->ResetHandContacts();
				FVector Origin,Direction;VR->GetThrownAim(Origin,Direction);
				TestTrue(TEXT("Atlatl aims out the heel of the palm, independent of pointing ray"),Direction.Equals(-VR->WeaponGrip()->GetForwardVector(),.001));
				TestTrue(TEXT("Atlatl path starts below the grasp"),Origin.Equals(VR->WeaponGrip()->GetComponentLocation()+Direction*8.f,.001));
				Ammo.AmmoType=2;CombatSession.WorldObjects.Add(Ammo.Guid,Ammo);
				const uint32 WrongAmmo=CombatSession.VRSequence;VR->Trigger(LeftHanded,true);VR->Trigger(LeftHanded,false);
				TestEqual(TEXT("Atlatl does not consume crossbow bolts"),CombatSession.VRSequence,WrongAmmo);
				VR->Grip(LeftHanded,true);TestFalse(TEXT("Atlatl never asks for a bow draw"),VR->bDrawing);VR->Grip(LeftHanded,false);
				Ammo.AmmoType=4;CombatSession.WorldObjects.Add(Ammo.Guid,Ammo);
				const uint32 BeforeAtlatl=CombatSession.VRSequence;VR->Trigger(LeftHanded,true);VR->Trigger(LeftHanded,false);
				TestEqual(TEXT("Atlatl fires on the click without a held draw"),CombatSession.VRSequence,BeforeAtlatl+1);
				{
					TGuardValue<uint32> RecoveryCapabilities(CombatSession.VRCapabilities,CombatSession.VRCapabilities|2048u);
					CombatSession.VRRecoveryTeleport=CombatSession.TeleportSeq;CombatSession.VRRecoveryDuration=1;CombatSession.VRRecoveryReadyAt=FPlatformTime::Seconds()+1;
					VR->Trigger(LeftHanded,true);VR->Trigger(LeftHanded,false);
					TestEqual(TEXT("Click during missile recovery cannot send another shot"),CombatSession.VRSequence,BeforeAtlatl+1);
					TestTrue(TEXT("Missile recovery uses next-shot HUD"),VR->GetCombatTimerText().StartsWith(TEXT("Next shot")));
					CombatSession.VRRecoveryReadyAt=0;
				}
				Bow.DefaultCombatStyle=0;Bow.AmmoType=0;CombatSession.WorldObjects[Bow.Guid]=Bow;CombatSession.WorldObjects.Remove(Ammo.Guid);
				TestEqual(TEXT("A missile with no ammo type is a thrown weapon"),VR->MissileStyle(),0x80);
				VR->Grip(LeftHanded,true);TestFalse(TEXT("Thrown weapon cannot start a bow draw"),VR->bDrawing);
				VR->GetThrownAim(Origin,Direction);TestTrue(TEXT("Thrown item uses the same heel-of-palm direction as the atlatl"),Direction.Equals(-VR->WeaponGrip()->GetForwardVector(),.001));
				CombatSession.WorldObjects.Add(Ammo.Guid,Ammo);TestEqual(TEXT("Unrelated equipped arrows are ignored by a thrown weapon"),VR->EquippedAmmo().Guid,0);
				const uint32 Before=CombatSession.VRSequence;VR->Trigger(LeftHanded,true);TestEqual(TEXT("Thrown trigger fires immediately without hold time"),CombatSession.VRSequence,Before+1);
				VR->Trigger(LeftHanded,false);TestEqual(TEXT("Releasing trigger cannot throw twice"),CombatSession.VRSequence,Before+1);
				CombatSession.WorldObjects.Remove(Ammo.Guid);
			}
			VR->Settings->bLeftHanded = false; VR->CancelGestures(); VR->Client->Session->PlayerVitals.CombatMode = ACECombatMode::NonCombat;
			VR->UpdateMissileAttachment(BowActor); BowActor->Destroy();
			VR->Client->Session->WorldObjects.Remove(Bow.Guid); VR->Client->Session->WorldObjects.Remove(Ammo.Guid); VR->UpdateCombat(.02f);
			CombatSession.SocketC2S->Close(); Sockets->DestroySocket(CombatSession.SocketC2S); CombatSession.SocketC2S = nullptr;
			Sockets->DestroySocket(CombatReceiver);
		}
		Controllers.LeftTracked = false; PollHands(); VR->UpdatePanels();
		TestTrue(TEXT("Losing a hand does not unregister the shared UI user"), VR->LeftPointer->IsActive() && VR->RightPointer->IsActive());
		TestTrue(TEXT("Remaining hand keeps hit testing"), !VR->LeftPointer->bEnableHitTesting && VR->RightPointer->bEnableHitTesting);
		// Components cache the provider for render-thread late updates. Clear those
		// references and finish queued rendering before the scoped provider goes away.
		Controllers.Connected = false; PollHands(); FlushRenderingCommands();
	}
	VR->Head->SetRelativeLocation(FVector(30, 15, 165)); const FVector Eye = VR->Head->GetComponentLocation();
	VR->RotateTracking(30); TestTrue(TEXT("Snap turns pivot around user's actual head"), Eye.Equals(VR->Head->GetComponentLocation(), .001));
	float F = 1, R = 1; bool Run = false; VR->MoveStick = FVector2D(1, 1); VR->bTracking = false; VR->GetMovement(F, R, Run);
	TestTrue(TEXT("Tracking loss stops locomotion"), F == 0 && R == 0);
	VR->bTracking = true; VR->bInventoryOpen = VR->bSettingsOpen = VR->bKeyboardOpen = false;
	VR->MoveStick = FVector2D(0, 1); VR->SmoothedMoveStick = FVector2D::ZeroVector;
	VR->PrepareMovement(1.f / 90.f); VR->GetMovement(F, R, Run);
	TestTrue(TEXT("Starting locomotion ramps toward character Run speed"), F > 0 && F < .1f && Run);
	for (int32 I = 0; I < 120; ++I) VR->PrepareMovement(1.f / 90.f);
	VR->GetMovement(F, R, Run); TestTrue(TEXT("Movement settles at the configured scale"), FMath::IsNearlyEqual(F, VR->Settings->MovementScale, .001f));
	VR->Settings->MovementScale = 1.f;
	for (float Rate : {30.f,45.f,72.f,90.f,144.f})
	{
		VR->MoveStick=FVector2D(.04f,.93f); VR->SmoothedMoveStick=FVector2D::ZeroVector;
		for (int32 I=0;I<FMath::CeilToInt(Rate*2.f);++I) VR->PrepareMovement(1.f/Rate);
		VR->GetMovement(F,R,Run);
		TestTrue(TEXT("Full forward Touch input reaches retail maximum independently of FPS"),FMath::IsNearlyEqual(F,1.f,.0001f) && FMath::IsNearlyZero(R));
	}
	VR->Client->SetRunSkill(100); const float LowSkillSpeed = F * VR->Client->GetLocomotionSpeed(Run);
	VR->Client->SetRunSkill(600); const float HighSkillSpeed = F * VR->Client->GetLocomotionSpeed(Run);
	TestTrue(TEXT("VR speed increases with the character's Run skill"), HighSkillSpeed > LowSkillSpeed);
	VR->ChangeSetting(TEXT("Run")); VR->GetMovement(F, R, Run);
	TestFalse(TEXT("VR settings toggle selects walking"), Run);
	TestTrue(TEXT("Walking uses the actual retail walking speed"), FMath::IsNearlyEqual(VR->Client->GetLocomotionSpeed(Run), 3.12f));
	VR->bLeftGripHeld = VR->bRightGripHeld = true; VR->GetMovement(F, R, Run);
	TestFalse(TEXT("Combat grips cannot change the saved movement mode"), Run);
	VR->ChangeSetting(TEXT("Run")); VR->GetMovement(F, R, Run); TestTrue(TEXT("VR settings toggle restores running"), Run);
	VR->Client->SetRunSkill(200);
	VR->MoveStick = VR->SmoothedMoveStick = FVector2D::ZeroVector;
	VR->Client->SetJumpSkill(400);
	float TapVelocity = 0.f;
	for (float Hold : {.1f, 1.1f})
	{
		PC->bJumpAirborne = PC->bJumpCharging = false; PC->ForwardAxis = PC->RightAxis = 0.f;
		VR->JumpDown();
		for (int32 Frame = 0; Frame < FMath::RoundToInt(Hold * 100); ++Frame)
		{
			VR->PrepareMovement(.01f); VR->JumpDown(); // duplicate presses must not reset charge
		}
		TestTrue(TEXT("Holding jump builds the requested charge"), FMath::IsNearlyEqual(PC->JumpChargeExtent, FMath::Min(Hold, 1.f), .001f));
		VR->UpdatePanels();
		TestTrue(TEXT("Jump charge shows the retail height bar in the headset with inventory closed"), VR->JumpPanel->IsVisible());
		TestEqual(TEXT("Jump meter cannot intercept controller or melee traces"), VR->JumpPanel->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		VR->JumpUp();
		VR->UpdatePanels(); TestFalse(TEXT("Releasing the jump hides its temporary meter"), VR->JumpPanel->IsVisible());
		TestTrue(TEXT("Release uses character Jump skill and held charge"), FMath::IsNearlyEqual(float(PC->JumpLocalAceVelocity.Z),
			FMath::Sqrt(VR->Client->GetJumpHeight(FMath::Min(Hold, 1.f)) * 19.6f), .001f));
		if (Hold < 1.f) TapVelocity = PC->JumpLocalAceVelocity.Z;
		else TestTrue(TEXT("Full charge launches higher than a tap"), PC->JumpLocalAceVelocity.Z > TapVelocity);
	}
	PC->bJumpAirborne = false; VR->JumpDown(); VR->PrepareMovement(.1f); VR->CancelGestures();
	TestFalse(TEXT("Cancelling input cancels the held jump"), PC->bJumpCharging || VR->IsJumpHeld());
	VR->bInventoryOpen = true; VR->PrepareMovement(1.f / 90.f); VR->GetMovement(F, R, Run);
	TestTrue(TEXT("Opening a menu stops motion immediately"), F == 0 && R == 0 && VR->SmoothedMoveStick.IsNearlyZero());
	VR->bDrawing = true; VR->CancelGestures();
	TestFalse(TEXT("Cancel releases nocked shots"), VR->bDrawing);
	VR->Client->Session->State = EACESessionState::InWorld; VR->Client->Session->PlayerGuid = 100;
	VR->Client->Session->KnownSpells = {1, 2, 3, 4, 5, 6, 7, 8};
	VR->Client->OnVendorOpened.Broadcast(200);
	TestTrue(TEXT("Vendor response reveals the interactive retail panel"), VR->bInventoryOpen);
	TestFalse(TEXT("Vendor response cannot leave a drawn bow armed"), VR->bDrawing);
	// Follow a sustained uphill/downhill motion through the actual rig tick.
	// Comfort filtering must not accumulate height error along a long slope.
	VR->LastPawnLocation = Pawn->GetActorLocation(); VR->VerticalComfortOffset = 0;
	const FVector TerrainStartPawn = Pawn->GetActorLocation(), TerrainStartRig = VR->TrackingOrigin->GetComponentLocation();
	for (float Step : {4.f, -4.f})
		for (int32 Frame = 0; Frame < 90; ++Frame)
		{
			Pawn->AddActorWorldOffset(FVector(6, 0, Step)); VR->TickComponent(1.f / 90.f, LEVELTICK_All, nullptr);
			const FVector Expected = TerrainStartRig + Pawn->GetActorLocation() - TerrainStartPawn;
			TestTrue(TEXT("Sustained terrain travel keeps the headset floor within 3cm"), FMath::Abs(VR->TrackingOrigin->GetComponentLocation().Z - Expected.Z) <= 3.01f);
		}
	PC->bJumpAirborne = true; Pawn->AddActorWorldOffset(FVector(0, 0, 15)); VR->TickComponent(1.f / 90.f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("A jump immediately clears floor smoothing"), VR->VerticalComfortOffset == 0.f);
	PC->bJumpAirborne = false;
	{
		auto* Portal = World->SpawnActor<AACELoadingScreenActor>(); PC->LoadingScreenActor = Portal;
		Portal->BeginTunnel(); Portal->bMeshReady = true;
		VR->bWasLoading = PC->bEnterWorldLoading = true; VR->bTracking = true;
		VR->UpdatePortalView(); VR->UpdateComfort(1.f);
		TestTrue(TEXT("VR renders original portal space instead of a black loading view"), VR->bPortalViewActive && !Portal->IsHidden() && VR->Fade == 0);
		auto* PortalWeapon = World->SpawnActor<AACEWorldEntityActor>();
		PortalWeapon->AttachToActor(Pawn, FAttachmentTransformRules::KeepWorldTransform);
		PortalWeapon->SetActorHiddenInGame(false);
		VR->UpdatePortalEquipmentVisibility();
		TestTrue(TEXT("Equipped weapons are hidden throughout portal space"), PortalWeapon->IsHidden());
		auto* LateWeapon = World->SpawnActor<AACEWorldEntityActor>();
		LateWeapon->AttachToActor(Pawn, FAttachmentTransformRules::KeepWorldTransform);
		LateWeapon->SetActorHiddenInGame(false);
		TestTrue(TEXT("Equipment attached after the rig tick remains in portal space"), VR->UpdateMissileAttachment(LateWeapon));
		TestTrue(TEXT("Late attachment updates cannot reveal a portal weapon"), LateWeapon->IsHidden());
		auto* HiddenAmmo = World->SpawnActor<AACEWorldEntityActor>();
		HiddenAmmo->AttachToActor(Pawn, FAttachmentTransformRules::KeepWorldTransform);
		HiddenAmmo->SetActorHiddenInGame(true);
		VR->UpdatePortalEquipmentVisibility();
		const FVector PortalOrigin = VR->TrackingOrigin->GetComponentLocation();
		Pawn->AddActorWorldOffset(FVector(250, 100, 50)); VR->MoveStick = FVector2D(0, 1); VR->PrepareMovement(.016f);
		VR->GetMovement(F, R, Run); VR->UpdatePortalView();
		TestTrue(TEXT("Portal loading locks stick and room-scale pawn movement"), F == 0 && R == 0 && VR->GetRoomScaleDelta().IsNearlyZero());
		TestTrue(TEXT("Destination pawn movement cannot move the portal view"), VR->TrackingOrigin->GetComponentLocation().Equals(PortalOrigin));
		const FQuat BeforeLook = VR->Head->GetComponentQuat(); VR->Head->AddLocalRotation(FRotator(0, 45, 0)); VR->UpdatePortalView();
		TestFalse(TEXT("The HMD can look around inside portal space"), VR->Head->GetComponentQuat().Equals(BeforeLook));
		VR->bWasLoading = PC->bEnterWorldLoading = VR->bPortalViewActive = false; VR->ResetTrackingOrigin();
		VR->UpdatePortalEquipmentVisibility();
		TestFalse(TEXT("Portal exit restores visible equipment"), PortalWeapon->IsHidden());
		TestFalse(TEXT("Portal exit also restores late equipment"), LateWeapon->IsHidden());
		TestTrue(TEXT("Portal exit preserves intentionally hidden equipped ammo"), HiddenAmmo->IsHidden());
		PortalWeapon->Destroy(); HiddenAmmo->Destroy(); LateWeapon->Destroy();
		TestTrue(TEXT("Portal exit restores the player floor"), FVector::Distance(VR->TrackingOrigin->GetComponentLocation(), Pawn->GetActorLocation()) < 300);
		PC->LoadingScreenActor = nullptr; Portal->Destroy();
	}
	if (FApp::CanEverRender())
	{
		FWidgetRenderer Renderer(true, true);
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/VR"); IFileManager::Get().MakeDirectory(*Directory, true);
		for (int32 I = 0; I < 3; ++I)
		{
			auto* Widget = I == 0 ? VR->MenuWidget.Get() : I == 1 ? VR->WristWidget.Get() : VR->KeyboardWidget.Get();
			const FIntPoint Size = I == 0 ? FIntPoint(720, 1000) : I == 1 ? FIntPoint(480, 800) : FIntPoint(1200, 400);
			auto* Target = FWidgetRenderer::CreateTargetFor(FVector2D(Size), TF_Bilinear, true);
			for (int32 Pass = 0; Pass < 3; ++Pass) { Renderer.DrawWidget(Target, Widget->TakeWidget(), FVector2D(Size), 0); FlushRenderingCommands(); }
			TArray<FColor> Pixels; Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
			TestEqual(TEXT("VR menu render has expected size"), Pixels.Num(), Size.X * Size.Y);
			TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
			FFileHelper::SaveArrayToFile(PNG, *(Directory / FString::Printf(TEXT("Menu%d.png"), I)));
		}
	}
	VR->Client->Session->State = EACESessionState::Disconnected;
	Login->NativeDestruct(); IFileManager::Get().Delete(*Login->ProfilePath,false,true);
	Pawn->Destroy(); PC->Destroy(); GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
	return true;
}
#endif
