#include "Mobile/ACEMobileSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMemory.h"
#include "HAL/IConsoleManager.h"

int32 ACEMobile::TexturePoolMB(uint64 PhysicalBytes)
{
    // Shared mobile RAM also serves the OS, meshes, DAT data and render targets.
    // Use one eighth rounded down to 64 MiB, within a 256-1024 MiB budget.
    const uint64 MB=PhysicalBytes/(1024*1024);
    return int32(FMath::Clamp<uint64>((MB/8/64)*64,256,1024));
}
void UACEMobileSettings::ApplyTextureBudget()
{
#if PLATFORM_ANDROID
    if(!IsEnabled())return; // Quest retains its own device-profile budget.
    const int32 Budget=ACEMobile::TexturePoolMB(FPlatformMemory::GetConstants().TotalPhysical);
    if(auto* Pool=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streaming.PoolSize")))Pool->Set(Budget,ECVF_SetByGameSetting);
    UE_LOG(LogTemp,Log,TEXT("Mobile texture streaming budget: %d MiB (bounded by physical RAM; texture quality remains user-controlled)"),Budget);
#endif
}

bool UACEMobileSettings::IsEnabled()
{
    return GetDefault<UACEMobileSettings>()->bEnabled || FParse::Param(FCommandLine::Get(),TEXT("ACEMobile"));
}
void UACEMobileSettings::ResetLayout()
{
    Controls.Reset();
    auto Add=[this](FName Id,FName Action,float X,float Y,float Size)
    { FACETouchControl C;C.Id=Id;C.Action=Action;C.Position={X,Y};C.Size=Size;Controls.Add(C); };
    Add("Move","Move",.18,.76,168);
    Add("Turn","Turn",.87,.75,160);
    Add("Ability","Attack",.73,.80,72);
    Add("Jump","Jump",.95,.48,64);
    Add("Use","Use",.74,.59,64);
    Add("Target","Next target",.84,.48,64);
    Add("Self","Self",.64,.80,64);
    Add("Spells","Spell wheel",.63,.59,72);
    Add("Stance","Combat",.52,.80,64);
    Add("Walk","Walk",.29,.80,64);
    Add("Slot1","Shortcut 1",.39,.80,64);
    Add("Slot2","Shortcut 2",.40,.59,64);
    Add("Inspect","Inspect",.52,.59,64);
    if(bLeftHanded)for(auto& C:Controls)C.Position.X=1-C.Position.X;
}
void UACEMobileSettings::Sanitize()
{
    LookSensitivity=FMath::Clamp(FMath::IsFinite(LookSensitivity)?LookSensitivity:.22f,.05f,.8f);
    ControlOpacity=FMath::Clamp(FMath::IsFinite(ControlOpacity)?ControlOpacity:.8f,.25f,1.f);
    if(Controls.IsEmpty())ResetLayout();
    // Upgrade existing layouts without replacing user bindings or positions.
    if(!Controls.ContainsByPredicate([](const auto& C){return C.Id=="Inspect";}))
    {FACETouchControl C;C.Id=C.Action="Inspect";C.Position={.52,.59};Controls.Add(C);}
    TSet<FName> Seen;
    Controls.RemoveAll([&](const auto& C){if(C.Id.IsNone() || Seen.Contains(C.Id))return true;Seen.Add(C.Id);return false;});
    if(Controls.Num()>20)Controls.SetNum(20);
    for(auto& C:Controls)
    {
        C.Size=FMath::Clamp(FMath::IsFinite(C.Size)?C.Size:64.f,C.Id=="Turn"?160.f:C.Id=="Move"?168.f:56.f,(C.Id=="Move" || C.Id=="Turn")?224.f:160.f);
        if(C.Position.ContainsNaN())C.Position={.5,.6};
        C.Position.X=FMath::Clamp(C.Position.X,0.,1.);C.Position.Y=FMath::Clamp(C.Position.Y,0.,1.);
        if(C.Id=="Move" || C.Id=="Turn") {C.Action=C.Id;C.bVisible=true;}
        else if(!ACEMobile::Actions().Contains(C.Action))C.Action="Attack";
    }
    if(!Seen.Contains("Move") || !Seen.Contains("Turn"))ResetLayout();
}
FVector2D ACEMobile::LogicalSize(FVector2D Available)
{
    const double Scale=FMath::Max(.01,FMath::Min(Available.X/960.,Available.Y/540.));
    return Available/Scale;
}
ACEMobile::FButtonContent ACEMobile::ButtonContent(FVector2D Size)
{
    const float Scale=FMath::Min(Size.X,Size.Y)/64.f;
    FButtonContent Content;
    Content.Padding=6*Scale;Content.Radius=12*Scale;Content.Border=1.25f*Scale;Content.Font=12*Scale;
    Content.Icon=FSlateRect(Content.Padding,Content.Padding,Size.X-Content.Padding,Size.Y-26*Scale);
    Content.Label=FSlateRect(Content.Padding,Size.Y-22*Scale,Size.X-Content.Padding,Size.Y-Content.Padding);
    return Content;
}
FSlateRect ACEMobile::FitImage(FVector2D ImageSize,const FSlateRect& Bounds)
{
    const FVector2D Box=Bounds.GetSize(),Center=Bounds.GetCenter();
    if(ImageSize.ContainsNaN() || ImageSize.X<=0 || ImageSize.Y<=0 || Box.X<=0 || Box.Y<=0)
        return FSlateRect(Center.X,Center.Y,Center.X,Center.Y);
    const double Scale=FMath::Min(Box.X/ImageSize.X,Box.Y/ImageSize.Y);
    const FVector2D Half=ImageSize*(Scale*.5);
    return FSlateRect(Center.X-Half.X,Center.Y-Half.Y,Center.X+Half.X,Center.Y+Half.Y);
}
FSlateRect ACEMobile::TurnKeyRect(float Width,int32 Side)
{
    const float Gap=Width/5,Key=(Width-Gap)/2;
    const FVector2D Center(Side*(Key+Gap)*.5,0),Half(Key*.5,Key*.5);
    return FSlateRect(Center.X-Half.X,-Half.Y,Center.X+Half.X,Half.Y);
}
FSlateRect ACEMobile::DPadKeyRect(float Width,FVector2D Direction)
{
    const float Cell=Width/3,Gap=Width/42;
    const FVector2D Center=Direction*Cell,Half((Cell-Gap)*.5,(Cell-Gap)*.5);
    return FSlateRect(Center.X-Half.X,Center.Y-Half.Y,Center.X+Half.X,Center.Y+Half.Y);
}
FSlateRect ACEMobile::ControlRect(const FACETouchControl& C,FVector2D Size)
{
    const FVector2D Center=C.Position*Size;
    const float Height=C.Id=="Turn"?TurnKeyRect(C.Size,1).GetSize().Y:C.Size;
    return FSlateRect(float(Center.X-C.Size/2),float(Center.Y-Height/2),float(Center.X+C.Size/2),float(Center.Y+Height/2));
}
bool ACEMobile::CanPlace(const TArray<FACETouchControl>& Controls,int32 Index,FVector2D Position,float Diameter,FVector2D Size,float Top)
{
    if(!Controls.IsValidIndex(Index) || Position.ContainsNaN() || !FMath::IsFinite(Diameter))return false;
    auto C=Controls[Index];C.Position=Position;C.Size=Diameter;
    const auto R=ControlRect(C,Size);
    // The top rail always remains reachable, even after a malformed saved layout.
    if(R.Left<76 || R.Top<Top || R.Right>Size.X-8 || R.Bottom>Size.Y-48)return false;
    for(int32 I=0;I<Controls.Num();++I)if(I!=Index && Controls[I].bVisible)
    {
        const auto Other=ControlRect(Controls[I],Size);
        if(R.Left<Other.Right+8 && R.Right+8>Other.Left && R.Top<Other.Bottom+8 && R.Bottom+8>Other.Top)return false;
    }
    return true;
}
void ACEMobile::FitLayout(TArray<FACETouchControl>& Controls,FVector2D Size,float Top)
{
    // Place deterministically in saved order; only relocate colliding/out-of-bounds
    // controls. This handles changed aspect ratios and Android safe-area insets.
    TArray<FACETouchControl> Placed;
    for(auto& C:Controls)
    {
        if(!C.bVisible)continue;
        Placed.Add(C);const int32 I=Placed.Num()-1;
        if(CanPlace(Placed,I,C.Position,C.Size,Size,Top))continue;
        double Best=TNumericLimits<double>::Max();FVector2D Candidate=C.Position;bool Found=false;
        const float HalfHeight=ControlRect(C,Size).GetSize().Y*.5f;
        for(float Y=Top+HalfHeight;Y<=Size.Y-48-HalfHeight;Y+=8)
            for(float X=76+C.Size/2;X<=Size.X-8-C.Size/2;X+=8)
            {
                const FVector2D P=FVector2D(X,Y)/Size;
                const double D=FVector2D::DistSquared(P*Size,C.Position*Size);
                if(D<Best && CanPlace(Placed,I,P,C.Size,Size,Top)){Best=D;Candidate=P;Found=true;}
            }
        C.Position=Candidate;
        // Extra user buttons may exceed available room. They stay in the editor,
        // but cannot cover another hit target on the gameplay surface.
        if(!Found)C.bVisible=false;
        Placed[I]=C;
    }
}
FVector2D ACEMobile::StickVector(FVector2D Delta,float Radius)
{
    if(Delta.ContainsNaN() || Radius<=0)return FVector2D::ZeroVector;
    const double Length=Delta.Size()/Radius;
    return Length<=.15?FVector2D::ZeroVector:Delta.GetSafeNormal()*FMath::Clamp((Length-.15)/.85,0.,1.);
}
bool ACEMobile::HitControl(const FACETouchControl& Control,FVector2D Size,FVector2D Point)
{
    // The D-pad's transparent corners belong to world gestures.
    if(Control.Id=="Move")
    {
        const FVector2D D=Point-Control.Position*Size;
        return ControlRect(Control,Size).ContainsPoint(Point) && (FMath::Abs(D.X)<=Control.Size/6 || FMath::Abs(D.Y)<=Control.Size/6);
    }
    return ControlRect(Control,Size).ContainsPoint(Point);
}
FVector2D ACEMobile::MovementAxes(FVector2D Delta,float Radius)
{
    if(Delta.ContainsNaN() || Radius<=0 || FMath::Max(FMath::Abs(Delta.X),FMath::Abs(Delta.Y))<=Radius/3)return FVector2D::ZeroVector;
    // Match retail keyboard walk/run commands. A tiny analog entry rate stretches
    // the authored walk-start link even after the stick reaches its outer ring.
    const double Threshold=FMath::Max(FMath::Abs(Delta.X),FMath::Abs(Delta.Y))*.41421356237;
    return {FMath::Abs(Delta.X)>Threshold?FMath::Sign(Delta.X):0.,FMath::Abs(Delta.Y)>Threshold?FMath::Sign(Delta.Y):0.};
}
int32 ACEMobile::WheelIndex(FVector2D Delta,int32 Count,float Inner,float Outer)
{
    if(Count<=0 || Count>12 || Delta.ContainsNaN() || Delta.Size()<Inner || Delta.Size()>Outer)return INDEX_NONE;
    const double Angle=FMath::Atan2(Delta.X,-Delta.Y)+2*PI;
    return FMath::FloorToInt(Angle/(2*PI)*Count+.5)%Count;
}
const TArray<FName>& ACEMobile::Actions()
{
    static const TArray<FName> All=[](){TArray<FName> A={"Attack","Cast","Jump","Use","Inspect","Pick up","Next target","Previous target","Self","Combat","Spell wheel","Inventory","Chat","Plugins","Walk","Cancel"};
        for(int32 I=1;I<=18;++I)A.Add(FName(*FString::Printf(TEXT("Shortcut %d"),I)));
        for(int32 I=1;I<=12;++I)A.Add(FName(*FString::Printf(TEXT("Spell %d"),I)));return A;}();
    return All;
}
FString ACEMobile::ActionLabel(FName Action){return Action.ToString();}
