#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEWorldEntityActor.h"
#include "Dat/ACEDatCursor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEItemStateAnimationTest,"ACE.RetailParity.ItemStateAnimation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FACEItemStateAnimationTest::RunTest(const FString&)
{
    const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
    auto* GI=NewObject<UGameInstance>(GEngine); World->SetGameInstance(GI); Context.OwningGameInstance=GI;
    GI->OnWorldChanged(nullptr,World); GI->Init();
    ON_SCOPE_EXIT { GI->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
    FString Directory=TEXT("C:/Turbine/Asheron's Call");
    FParse::Value(FCommandLine::Get(),TEXT("RetailDatDir="),Directory);
    auto* Dat=GI->GetSubsystem<UACEDatSubsystem>();
    if(!TestTrue(TEXT("Retail DAT opens"),Dat && Dat->LoadDatDirectory(Directory))) return false;
    TArray<uint8> Bytes;
    auto ReadAnimation=[&](uint32 Id,FACEDatAnimation& Animation)
    {
        if(!Dat->GetPortalDat()->ReadFile(Id,Bytes)) return false;
        FACEDatCursor Cursor(Bytes); return ACEDatUnpack::UnpackAnimation(Cursor,Animation);
    };
    auto FramePart=[](const FACEDatAnimation& Animation,int32 Frame,int32 Part)
    {
        const auto& Raw=Animation.PartFrames[Frame].PartFrames[Part];
        const auto Q=Raw.GetRotation();
        return FTransform(FACEPosition::AceQuatToUnreal(Q.W,FVector(Q.X,Q.Y,Q.Z)),
            FACEPosition::AceVectorToUnreal(FVector(Raw.GetTranslation()),100));
    };
    auto Sample=[&](const FACEDatAnimation& Animation,const FACEDatAnimData& Clip,float Time,int32 Part)
    {
        const int32 High=Clip.HighFrame<0 ? Animation.PartFrames.Num()-1 : FMath::Min(Clip.HighFrame,Animation.PartFrames.Num()-1);
        const int32 Low=FMath::Clamp(Clip.LowFrame,0,High), Length=High-Low+1;
        const float Offset=FMath::Fmod(Time*FMath::Abs(Clip.Framerate),float(Length));
        float Frame=Clip.Framerate<0 ? High-Offset : Low+Offset;
        if(Frame<Low) Frame+=Length;
        const int32 First=FMath::FloorToInt(Frame), Second=First==High ? Low : First+1;
        FTransform Result; Result.Blend(FramePart(Animation,First,Part),FramePart(Animation,Second,Part),Frame-First);
        return Result;
    };

    // All shipped On/Off cycles: doors/chests hold their authored zero-rate pose;
    // crystals, lamps and other active items retain their individual part motion.
    int32 States=0, MovingStates=0;
    for(uint32 Id=0x09000000; Id<0x09001000; ++Id)
    {
        if(!Dat->GetPortalDat()->Contains(Id)) continue;
        Dat->GetPortalDat()->ReadFile(Id,Bytes); FACEDatCursor Cursor(Bytes); FACEDatMotionTable Table;
        if(!ACEDatUnpack::UnpackMotionTable(Cursor,Table)) continue;
        for(uint32 Command : {ACEMotion::On,ACEMotion::Off})
        {
            const auto* Cycle=Table.Cycles.Find((Table.DefaultStyle<<16)|(Command & 0xffffff));
            if(!Cycle) continue;
            if(!TestEqual(TEXT("Stock item state uses one cyclic clip"),Cycle->Anims.Num(),1)) return false;
            const auto& Clip=Cycle->Anims[0]; FACEDatAnimation Animation;
            if(!TestTrue(TEXT("State animation decodes"),ReadAnimation(Clip.AnimId,Animation))) return false;
            ++States; MovingStates+=Clip.Framerate!=0;
            for(float Time : {0.f,.125f,.5f,2.5f,7.f,3600.f})
            {
                TArray<FTransform> Actual; int32 Count=0; bool Finished=false;
                const uint32 From=Command==ACEMotion::On ? ACEMotion::Off : ACEMotion::On;
                if(!TestTrue(TEXT("Settled state evaluates"),Dat->EvaluateStateMotion(Id,From,Command,Time,true,
                    Animation.NumParts,Actual,100,Count,Finished))) return false;
                TestTrue(TEXT("Settled state is not reactivating"),Finished);
                for(int32 Part=0; Part<Count; ++Part)
                    if(!TestTrue(FString::Printf(TEXT("Table %08X command %X part %d at %.3fs matches raw DAT"),Id,Command,Part,Time),
                        Actual[Part].Equals(Sample(Animation,Clip,Time,Part),.005))) return false;
            }
        }
    }
    AddInfo(FString::Printf(TEXT("Checked %d retail item states, including %d moving cycles"),States,MovingStates));
    TestTrue(TEXT("Audit covers stationary and continuously animated items"),States>100 && MovingStates>5);

    FACEWorldObject Object; Object.Guid=0x71003001; Object.WeenieClassId=11714;
    Object.SetupId=0x02000AAF; Object.MotionTableId=0x090000B8;
    Object.InitialMotionCommand=ACEMotion::OnCommandU16;
    Object.PhysicsState=ACEPhysicsState::Ethereal | ACEPhysicsState::IgnoreCollisions;
    Object.bHasPosition=true; Object.Position.CellId=0x016C0101; Object.Position.Location=FVector(50,50,1);
    auto* Actor=World->SpawnActor<AACEWorldEntityActor>(); Actor->InitializeFromObject(Object,100,true);
    auto* Appearance=Actor->FindComponentByClass<UACECharacterAppearanceComponent>();
    if(!TestNotNull(TEXT("Crystal appearance exists"),Appearance) || !Appearance->HasAppearance()) return false;
    FACEDatAnimation Spin,Activation;
    if(!ReadAnimation(0x030008CB,Spin) || !ReadAnimation(0x030008CA,Activation)) return false;
    FACEDatAnimData SpinClip; SpinClip.LowFrame=0; SpinClip.HighFrame=-1; SpinClip.Framerate=15;
    TestEqual(TEXT("Crystal has base, inner and outer parts"),Spin.NumParts,3u);
    const auto InnerDelta=Spin.PartFrames[1].PartFrames[1].GetRotation()*Spin.PartFrames[0].PartFrames[1].GetRotation().Inverse();
    const auto OuterDelta=Spin.PartFrames[1].PartFrames[2].GetRotation()*Spin.PartFrames[0].PartFrames[2].GetRotation().Inverse();
    TestTrue(TEXT("Retail crystal parts turn in opposite directions"),InnerDelta.Z*OuterDelta.Z<0);
    auto CheckParts=[&](float Time)
    {
        for(int32 Part=0; Part<3; ++Part)
        {
            FTransform Actual; Appearance->GetPartCurrentTransform(Part,Actual);
            if(!TestTrue(TEXT("Visible crystal part matches retail spin"),Actual.Equals(Sample(Spin,SpinClip,Time,Part),.005))) return false;
        }
        return true;
    };
    for(int32 Tick=0; Tick<=64; ++Tick)
    {
        if(Tick) Appearance->TickComponent(.125f,LEVELTICK_All,nullptr);
        if(Tick==13) { Actor->InitializeFromObject(Object,100,true); Appearance->PlayDoorMotion(ACEMotion::OnCommandU16); }
        if(!CheckParts(Tick*.125f)) return false;
    }
    // Off reverses the full rise animation, then holds frame zero. On plays it
    // forward once, then enters the 15-fps spin with the remaining tick time.
    Appearance->PlayDoorMotion(ACEMotion::OffCommandU16);
    for(int32 Tick=1; Tick<=40; ++Tick)
    {
        Appearance->TickComponent(.125f,LEVELTICK_All,nullptr);
        if(Tick>=32)
            for(int32 Part=0; Part<3; ++Part)
            {
                FTransform Actual; Appearance->GetPartCurrentTransform(Part,Actual);
                if(!TestTrue(TEXT("Off holds the fully lowered crystal"),Actual.Equals(FramePart(Activation,0,Part),.005))) return false;
            }
    }
    Appearance->PlayDoorMotion(ACEMotion::OnCommandU16);
    for(int32 Tick=1; Tick<=64; ++Tick)
    {
        Appearance->TickComponent(.125f,LEVELTICK_All,nullptr);
        if(Tick>=32 && !CheckParts(Tick*.125f-4.f)) return false;
    }
    Appearance->SetDoorHeldCommand(ACEMotion::OnCommandU16);
    Appearance->TickComponent(4.375f,LEVELTICK_All,nullptr);
    if(!CheckParts(.375f)) return false;
    Actor->Destroy();
    return true;
}
#endif
