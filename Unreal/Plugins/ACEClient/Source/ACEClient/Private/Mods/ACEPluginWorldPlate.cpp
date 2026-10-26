#include "Mods/ACEPluginWorldPlate.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEPluginStyle.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEPlayerController.h"
#include "ACEWorldPresenterComponent.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "VR/ACEVRComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Styling/CoreStyle.h"

namespace
{
bool PlateNumber(const TSharedPtr<FJsonObject>& J,const TCHAR* Key,double& Out,double Min,double Max)
{const auto* V=J->Values.Find(Key);return V&&(*V)->Type==EJson::Number&&J->TryGetNumberField(Key,Out)&&FMath::IsFinite(Out)&&Out>=Min&&Out<=Max;}
bool PlateText(const TSharedPtr<FJsonObject>& J,const TCHAR* Key,FString& Out,int32 Max)
{
    if(!J->HasField(Key))return true;
    if(J->Values[Key]->Type!=EJson::String||!J->TryGetStringField(Key,Out)||Out.Len()>Max)return false;
    for(TCHAR C:Out)if(C<32||C==127)return false;
    return true;
}
bool PlateVital(const TSharedPtr<FJsonObject>& J,const TCHAR* Key,FACEPluginPlateVital& V)
{
    if(!J->HasField(Key)||J->Values[Key]->IsNull())return true;
    const TSharedPtr<FJsonObject>* Field=nullptr;if(!J->TryGetObjectField(Key,Field))return false;
    if((*Field)->HasField(TEXT("current"))&&!PlateNumber(*Field,TEXT("current"),V.Current,0,MAX_int32))return false;
    if((*Field)->HasField(TEXT("max"))&&!PlateNumber(*Field,TEXT("max"),V.Maximum,1,MAX_int32))return false;
    if((*Field)->HasField(TEXT("fraction"))&&!PlateNumber(*Field,TEXT("fraction"),V.Fraction,0,1))return false;
    return V.IsKnown() && (V.Current<0 || (V.Maximum>0 && V.Current<=V.Maximum));
}
}

bool ACEPluginWorldPlate::Parse(const TArray<TSharedPtr<FJsonValue>>& Values,TArray<FACEPluginWorldPlate>& Out,FString& Error,double Now)
{
    if(Values.Num()>4){Error=TEXT("At most four world plates per plugin");return false;}
    TArray<FACEPluginWorldPlate> Parsed;TSet<int32> Ids;
    for(const auto& Value:Values)
    {
        if(!Value||Value->Type!=EJson::Object){Error=TEXT("Invalid world plate");return false;}
        const auto J=Value->AsObject();FACEPluginWorldPlate P;double Id=0,Level=0;
        if(!PlateNumber(J,TEXT("id"),Id,1,MAX_uint32)||FMath::FloorToDouble(Id)!=Id
            ||!PlateText(J,TEXT("name"),P.Name,128)||!PlateText(J,TEXT("role"),P.Role,80)
            ||(J->HasField(TEXT("level"))&&(!PlateNumber(J,TEXT("level"),Level,0,MAX_int32)||Level!=FMath::FloorToDouble(Level))))
        {Error=TEXT("Invalid world plate id, name, role or level");return false;}
        P.Id=int32(uint32(Id));P.Level=int32(Level);
        for(const auto& Field:TArray<TPair<const TCHAR*,double*>>{
            {TEXT("opacity"),&P.Opacity},{TEXT("background_opacity"),&P.BackgroundOpacity},
            {TEXT("vr_opacity"),&P.VROpacity},{TEXT("vr_background_opacity"),&P.VRBackgroundOpacity}})
            if(J->HasField(Field.Key)&&!PlateNumber(J,Field.Key,*Field.Value,0,1))
            {Error=TEXT("World plate opacity must be a number in [0,1]");return false;}
        if(Ids.Contains(P.Id)){Error=TEXT("Duplicate world plate object");return false;}Ids.Add(P.Id);
        if(J->HasField(TEXT("selection_only"))&&(J->Values[TEXT("selection_only")]->Type!=EJson::Boolean||!J->TryGetBoolField(TEXT("selection_only"),P.SelectionOnly)))
        {Error=TEXT("selection_only must be boolean");return false;}
        if(J->HasField(TEXT("refresh"))&&!J->TryGetBoolField(TEXT("refresh"),P.Refresh))
        {Error=TEXT("refresh must be boolean");return false;}
        if(J->HasField(TEXT("color")))
        {
            FString Color;
            static const TMap<FString,FLinearColor> Colors={{TEXT("yellow"),{1,.8f,.15f}},{TEXT("red"),{1,.15f,.1f}},
                {TEXT("blue"),{.15f,.6f,1}},{TEXT("green"),{.15f,1,.3f}},{TEXT("white"),FLinearColor::White},{TEXT("purple"),{.8f,.3f,1}}};
            if(!J->TryGetStringField(TEXT("color"),Color)||!Colors.Contains(Color)){Error=TEXT("Invalid world plate color");return false;}P.Color=Colors[Color];
        }
        if(J->HasField(TEXT("vitals")))
        {
            const TSharedPtr<FJsonObject>* V=nullptr;
            if(!J->TryGetObjectField(TEXT("vitals"),V)||!PlateVital(*V,TEXT("health"),P.Health)
                ||!PlateVital(*V,TEXT("stamina"),P.Stamina)||!PlateVital(*V,TEXT("mana"),P.Mana))
            {Error=TEXT("World plate vitals need current/max or a fraction in [0,1]");return false;}
        }
        if(J->HasField(TEXT("effects")))
        {
            const TArray<TSharedPtr<FJsonValue>>* List=nullptr;
            // Lua encodes an empty nested table as an empty object.
            const TSharedPtr<FJsonObject>* Empty=nullptr;
            if(J->TryGetObjectField(TEXT("effects"),Empty)&&(*Empty)->Values.IsEmpty()){}
            else if(!J->TryGetArrayField(TEXT("effects"),List)||List->Num()>16){Error=TEXT("At most sixteen world plate effects");return false;}
            if(List)for(const auto& E:*List)
            {
                double Icon=0,Remaining=0,Spell=0;
                if(!E||E->Type!=EJson::Object||!PlateNumber(E->AsObject(),TEXT("icon"),Icon,0x06000000,0x06ffffff)
                    ||Icon!=FMath::FloorToDouble(Icon)||!PlateNumber(E->AsObject(),TEXT("remaining"),Remaining,0,86400)
                    ||(E->AsObject()->HasField(TEXT("spell"))&&(!PlateNumber(E->AsObject(),TEXT("spell"),Spell,1,MAX_int32)||Spell!=FMath::FloorToDouble(Spell))))
                {Error=TEXT("Invalid DAT icon or effect timer");return false;}
                if(Remaining>0)P.Effects.Add({uint32(Spell),uint32(Icon),Now+Remaining});
            }
        }
        Parsed.Add(MoveTemp(P));
    }
    Out=MoveTemp(Parsed);return true;
}

class SACEPluginWorldPlate : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SACEPluginWorldPlate){} SLATE_END_ARGS()
    void Construct(const FArguments&){SetVisibility(EVisibility::HitTestInvisible);}
    void SetData(const FACEPluginWorldPlate& In,TSharedRef<FACEPluginStyle> InStyle)
    {
        Plate=In;Style=InStyle;Icons.Reset();for(const auto& E:Plate.Effects)Icons.Add(Style->Image(E.Icon));
        Invalidate(EInvalidateWidgetReason::Paint);
    }
    void SetFrame(FVector2D Size){Body=Size;}
    void SetPresentation(bool VR){VRPresentation=VR;Invalidate(EInvalidateWidgetReason::Paint);}
    virtual FVector2D ComputeDesiredSize(float) const override{return {1024,512};}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle&,bool) const override
    {
        const auto* White=FCoreStyle::Get().GetBrush("WhiteBrush");
        const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        const FLinearColor Background(0,0,0,float(Plate.BackgroundOpacityFor(VRPresentation)));
        auto Box=[&](FVector2D At,FVector2D Size,FLinearColor Color)
        {FSlateDrawElement::MakeBox(Elements,Layer,G.ToPaintGeometry(Size,FSlateLayoutTransform(At)),White,ESlateDrawEffect::None,Color);};
        auto Text=[&](FString Value,FVector2D At,int Size,FLinearColor Color,double Width=400.,bool Center=false)
        {
            const auto Font=FACEPluginStyle::NativeFont(TEXT("Regular"),Size);
            if(Measure->Measure(Value,Font).X>Width)
            {while(Value.Len()>1&&Measure->Measure(Value+TEXT("…"),Font).X>Width)Value.LeftChopInline(1);Value+=TEXT("…");}
            if(Center)At.X+=(Width-Measure->Measure(Value,Font).X)*.5;
            FSlateDrawElement::MakeText(Elements,Layer+1,G.ToPaintGeometry(FVector2D(Width,30),FSlateLayoutTransform(At+FVector2D(1,1))),Value,Font,ESlateDrawEffect::None,FLinearColor::Black);
            FSlateDrawElement::MakeText(Elements,Layer+2,G.ToPaintGeometry(FVector2D(Width,30),FSlateLayoutTransform(At)),Value,Font,ESlateDrawEffect::None,Color);
        };
        const double Left=512-Body.X*.5,Right=512+Body.X*.5,Top=280-Body.Y*.5,Bottom=280+Body.Y*.5;
        for(double X:{Left,Right})for(double Y:{Top,Bottom})
        {Box({X==Left?X:X-20,Y},{20,2},Plate.Color);Box({X,Y==Top?Y:Y-20},{2,20},Plate.Color);}
        const double LabelWidth=FMath::Max(240.,Body.X);
        Box({Left,Top-72},{LabelWidth,66},Background);
        Text(Plate.Role,{Left+6,Top-70},14,FLinearColor::White,LabelWidth-12);
        Text(Plate.Name,{Left+6,Top-49},20,Plate.Color,LabelWidth-12);
        if(Plate.Level>0)Text(FString::Printf(TEXT("Level %d"),Plate.Level),{Left+6,Top-23},14,FLinearColor::White,LabelWidth-12);
        double Y=Bottom-78;
        auto Bar=[&](const FACEPluginPlateVital& V,FLinearColor Color)
        {
            if(!V.IsKnown())return;
            const FVector2D At(Left+6,Y);Box(At,{184,23},Background);
            Box(At+FVector2D(2,2),{180*FMath::Clamp(V.Ratio(),0.,1.),19},Color);
            const FString Label=V.Current>=0?FString::Printf(TEXT("%.0f / %.0f"),V.Current,V.Maximum):FString::Printf(TEXT("%.0f%%"),100*V.Ratio());
            Text(Label,At+FVector2D(2,1),14,FLinearColor::White,180,true);Y+=25;
        };
        Bar(Plate.Health,FLinearColor(.65f,.015f,.04f));Bar(Plate.Stamina,FLinearColor(.75f,.5f,0));Bar(Plate.Mana,FLinearColor(0,.35f,.85f));
        int32 Index=0;const double Now=FPlatformTime::Seconds();
        for(int32 I=0;I<Plate.Effects.Num();++I)
        {
            const double Remaining=Plate.Effects[I].Expires-Now;if(Remaining<=0)continue;
            const FVector2D At(Right+8+(Index/8)*45,Top+(Index%8)*44);++Index;
            Box(At,{40,40},Background);
            if(Icons.IsValidIndex(I)&&Icons[I]&&Icons[I]->GetResourceObject())
                FSlateDrawElement::MakeBox(Elements,Layer+1,G.ToPaintGeometry(FVector2D(36,36),FSlateLayoutTransform(At+FVector2D(2,2))),Icons[I]);
            Box(At+FVector2D(0,24),{40,16},Background);
            const int32 Seconds=FMath::CeilToInt(Remaining);
            const FString Label=Seconds>=60?FString::Printf(TEXT("%d:%02d"),Seconds/60,Seconds%60):FString::FromInt(Seconds);
            Text(Label,At+FVector2D(0,23),11,FLinearColor::White,40,true);
        }
        return Layer+3;
    }
private:
    FACEPluginWorldPlate Plate;
    bool VRPresentation=false;
    FVector2D Body={220,344};
    TSharedPtr<FACEPluginStyle> Style;
    TArray<const FSlateBrush*> Icons;
};

UACEPluginWorldPlateComponent::UACEPluginWorldPlateComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    SetWidgetSpace(EWidgetSpace::World);SetDrawSize({1024,512});SetTwoSided(true);
    SetBlendMode(EWidgetBlendMode::Transparent);SetBackgroundColor(FLinearColor::Transparent);
    SetCollisionEnabled(ECollisionEnabled::NoCollision);SetCastShadow(false);SetCanEverAffectNavigation(false);
    SetTickWhenOffscreen(true);SetVisibility(false);
    // Content follows the local clock; actor/camera transforms still update every frame.
    SetManuallyRedraw(true);SetRedrawTime(0);
}

void UACEPluginWorldPlateComponent::SetPlate(const FACEPluginWorldPlate& Value,UACEPluginSubsystem* InHost)
{
    Plate=Value;Host=InHost;
    if(!Surface){Surface=SNew(SACEPluginWorldPlate);SetSlateWidget(Surface);}
    Surface->SetData(Plate,FACEPluginStyle::Use(InHost));
    auto* PC=InHost?Cast<AACEPlayerController>(InHost->GetGameInstance()->GetFirstLocalPlayerController()):nullptr;
    UpdatePresentation(PC&&PC->IsVRActive());
}

void UACEPluginWorldPlateComponent::UpdatePresentation(bool VR)
{
    VRPresentation=VR;
    // The material premultiplies this alpha once, after Slate has composited the
    // text, icons and backing. Transparent canvas stays transparent at opacity 1.
    SetTintColorAndOpacity(FLinearColor(1,1,1,float(Plate.OpacityFor(VR))));
    if(Surface)Surface->SetPresentation(VR);
    RequestRedraw();
}

void UACEPluginWorldPlateComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    bool Visible=false;
    auto* H=Host.Get();auto* GI=H?H->GetGameInstance():nullptr;
    auto* C=GI?GI->GetSubsystem<UACEClientSubsystem>():nullptr;
    auto* PC=GI?Cast<AACEPlayerController>(GI->GetFirstLocalPlayerController()):nullptr;
    if(VRPresentation!=bool(PC&&PC->IsVRActive()))UpdatePresentation(PC&&PC->IsVRActive());
    const auto P=H?H->Find(PluginId):nullptr;
    const auto S=C?C->GetSession():nullptr;
    const auto* Object=S?S->GetWorldObjects().Find(Plate.Id):nullptr;
    if(P && P->Enabled && P->Running && P->VMSession.Pin()==S && P->Player==C->GetPlayerGuid() && P->Server==C->GetServerName()
        && P->Permissions.Contains(TEXT("ui")) && P->Permissions.Contains(TEXT("markers"))
        && C->GetSessionState()==EACESessionState::InWorld && Object && !Object->bDying && !Object->IsCorpse() && !H->DefeatedTargets.Contains(Plate.Id)
        && Object->IsSelectableWorldObject() && PC && !PC->IsWorldTransitionActive() && !PC->IsDesktopInterfaceHidden()
        && (!Plate.SelectionOnly||C->GetSelectedObject().Guid==Plate.Id)
        && P->WorldPlates.ContainsByPredicate([&](const auto& V){return V.Id==Plate.Id;}))
    {
        auto* GM=GetWorld()->GetAuthGameMode();
        auto* Presenter=GM?GM->FindComponentByClass<UACEWorldPresenterComponent>():nullptr;
        auto* Actor=Presenter?Presenter->FindEntityActor(Plate.Id):nullptr;
        if(Actor && !Actor->IsHidden() && Actor->IsCellVisible() && !Actor->IsMeshSuppressed())
        {
            FVector View;FRotator Rotation;PC->GetPlayerViewPoint(View,Rotation);
            if(PC->IsVRActive())if(auto* VR=PC->GetVRComponent();VR&&VR->Head)View=VR->Head->GetComponentLocation();
            FBox ObjectBounds;bool HasBounds=Actor->Appearance&&Actor->Appearance->GetSelectionWorldBounds(ObjectBounds);
            if(!HasBounds)
            {
                const float Height=FMath::Max(10.f,Actor->GetMeleeBodyHeight()),Radius=FMath::Max(5.f,Actor->GetMeleeBodyRadius());
                ObjectBounds=FBox(Actor->GetActorLocation()-FVector(Radius,Radius,0),Actor->GetActorLocation()+FVector(Radius,Radius,Height));
            }
            const FVector Center=ObjectBounds.GetCenter();const FVector ToCamera=View-Center;
            if(ToCamera.SizeSquared()<=FMath::Square(24000.) && ToCamera.SizeSquared()>25)
            {
                const FRotator Facing=ToCamera.Rotation();
                const FVector Right=Facing.RotateVector(FVector::RightVector),Up=Facing.RotateVector(FVector::UpVector);
                const FVector Extent=ObjectBounds.GetExtent();
                const float Width=2*FVector::DotProduct(Extent,Right.GetAbs()),Height=2*FVector::DotProduct(Extent,Up.GetAbs());
                const float Scale=FMath::Max3(Width/640.f,Height/344.f,.05f);
                const FVector2D Body(Width/Scale+16,Height/Scale+16);
                const int64 Second=FMath::FloorToInt64(FPlatformTime::Seconds());
                if(!Body.Equals(LastBody,.25) || Second!=LastPaintSecond || !IsVisible())
                {Surface->SetFrame(Body);LastBody=Body;LastPaintSecond=Second;RequestRedraw();}
                // Body center is y=280 on the transparent texture (pivot y=.5).
                SetWorldLocationAndRotation(Center+Up*(24*Scale),Facing);
                SetWorldScale3D(FVector(Scale));Visible=true;
            }
        }
    }
    if(IsVisible()!=Visible)SetVisibility(Visible);
    Super::TickComponent(DeltaTime,TickType,TickFunction);
}
