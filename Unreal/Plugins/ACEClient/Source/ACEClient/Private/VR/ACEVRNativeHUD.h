#pragma once
#include "VR/ACEVRComponent.h"
#include "VR/ACEVRSettings.h"
#include "Widgets/SLeafWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"

namespace ACEVRHUDArt
{
 inline void Text(FSlateWindowElementList& Out,int32 Layer,const FGeometry& G,FVector2f At,const FString& Value,int32 Size,FLinearColor Color,bool Center=false,bool CenterY=false)
 {
  auto Font=FCoreStyle::GetDefaultFontStyle("Bold",Size);Font.OutlineSettings.OutlineSize=2;
  Font.OutlineSettings.OutlineColor=FLinearColor::Black;
  const auto Extent=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Value,Font);
  if(Center) At.X-=Extent.X*.5f;
  if(CenterY) At.Y-=Extent.Y*.5f;
  FSlateDrawElement::MakeText(Out,Layer,G.ToPaintGeometry(FVector2f(G.GetLocalSize()),FSlateLayoutTransform(At)),Value,Font,ESlateDrawEffect::None,Color);
 }
 inline void Box(FSlateWindowElementList& Out,int32 Layer,const FGeometry& G,FVector2f At,FVector2f Size,FLinearColor Color,float Radius=8)
 {
  const FSlateRoundedBoxBrush Brush(FLinearColor::White,Radius);
  FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(Size,FSlateLayoutTransform(At)),&Brush,ESlateDrawEffect::None,Color);
 }
}

/** Native vector meters; no dependency on a rendered desktop canvas. */
class SACEVRVitals : public SLeafWidget
{
public:
 SLATE_BEGIN_ARGS(SACEVRVitals) {} SLATE_ARGUMENT(TWeakObjectPtr<UACEVRComponent>,Rig) SLATE_END_ARGS()
 void Construct(const FArguments& Args) { Rig=Args._Rig; }
 bool Refresh(const FACEPlayerVitals& V)
 {
  if(!Rig.IsValid())return false;
  const int32 NewValues[]={V.Health,V.MaxHealth,V.Stamina,V.MaxStamina,V.Mana,V.MaxMana,Rig->GetCombatMode()};
  bool Changed=false;for(int I=0;I<7;++I)if(Values[I]!=NewValues[I]){Values[I]=NewValues[I];Changed=true;}
  const bool Controls=Rig->ShouldShowVitalsControls(),Locked=Rig->GetSettings()->bVitalsLocked;
  const FString Timer=Rig->HasCombatTimer()?Rig->GetCombatTimerText():FString();
  const float Progress=Rig->HasCombatTimer()?Rig->GetCombatTimerProgress():1.f;
  if(Controls!=bControls || Locked!=bLocked || Timer!=TimerText || !FMath::IsNearlyEqual(Progress,TimerProgress,.01f))Changed=true;
  bControls=Controls;bLocked=Locked;TimerText=Timer;TimerProgress=Progress;
  if(Changed)Invalidate(EInvalidateWidgetReason::Paint);return Changed;
 }
 virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(480,240); }
 virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 L,const FWidgetStyle&,bool)const override
 {
  using namespace ACEVRHUDArt;
  const TCHAR* Names[]={TEXT("Health"),TEXT("Stamina"),TEXT("Mana")};
  const FLinearColor Colors[]={FLinearColor(.32f,.006f,.004f),FLinearColor(.25f,.11f,.003f),FLinearColor(.003f,.065f,.3f)};
  const TCHAR* Stance=Values[6]==ACECombatMode::Magic?TEXT("MAGIC"):Values[6]==ACECombatMode::Missile?TEXT("MISSILE"):Values[6]==ACECombatMode::Melee?TEXT("MELEE"):TEXT("PEACE");
  Text(Out,L,G,{8,0},Stance,20,FLinearColor(.95f,.82f,.53f));
  for(int I=0;I<3;++I)
  {
   const float Y=32+I*46,Ratio=Values[I*2+1]>0?FMath::Clamp(float(Values[I*2])/Values[I*2+1],0.f,1.f):0.f;
   Box(Out,L,G,{4,Y},{472,40},FLinearColor(.72f,.59f,.32f));
   Box(Out,L+1,G,{6,Y+2},{468,36},FLinearColor(.002f,.003f,.005f));
   if(Ratio>0)Box(Out,L+2,G,{9,Y+5},{462*Ratio,30},Colors[I],5);
   Text(Out,L+3,G,{18,Y+20},Names[I],22,FLinearColor::White,false,true);
   Text(Out,L+3,G,{344,Y+20},FString::Printf(TEXT("%d / %d"),Values[I*2],Values[I*2+1]),22,FLinearColor::White,true,true);
  }
  if(!TimerText.IsEmpty())
  {
   Box(Out,L,G,{4,175},{472,6},FLinearColor(.06f,.07f,.09f));
   Box(Out,L+1,G,{4,175},{472*FMath::Clamp(TimerProgress,0.f,1.f),6},FLinearColor(.9f,.72f,.3f),3);
   Text(Out,L+2,G,{240,184},TimerText,20,FLinearColor::White,true);
  }
  if(bControls)Text(Out,L+3,G,{240,212},bLocked?TEXT("Unlock placement"):TEXT("Drag bars / Lock placement"),18,FLinearColor(.95f,.82f,.53f),true);
  return L+3;
 }
 virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)override
 {
  if(!Rig.IsValid())return FReply::Unhandled();
  if(G.AbsoluteToLocal(E.GetScreenSpacePosition()).Y>=210 && bControls)Rig->ToggleVitalsLock();
  else Rig->BeginVitalsDrag(E.GetPointerIndex()==0);
  return FReply::Handled().CaptureMouse(SharedThis(this));
 }
 virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent&)override
 {if(Rig.IsValid())Rig->EndVitalsDrag();return FReply::Handled().ReleaseMouseCapture();}
private:
 TWeakObjectPtr<UACEVRComponent> Rig;
 int32 Values[7]={};bool bControls=false,bLocked=true;float TimerProgress=1;FString TimerText;
};

struct FACEVRRadarMarker { FVector2f Point; FLinearColor Color; int32 Guid=0; int32 Height=0; bool Selected=false; };
class SACEVRCompass : public SLeafWidget
{
public:
 SLATE_BEGIN_ARGS(SACEVRCompass){} SLATE_ARGUMENT(TFunction<void(int32)>,OnSelect) SLATE_END_ARGS()
 void Construct(const FArguments& Args){Select=Args._OnSelect;}
 TArray<FACEVRRadarMarker> Markers;
 FString Coordinates;
 float NorthAngle=0;
 virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(400,460);}
 virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 L,const FWidgetStyle&,bool)const override
 {
  using namespace ACEVRHUDArt;
  // A wider engraved bezel reserves space for equally centered compass labels.
  const FLinearColor Gold(.72f,.49f,.19f),LightGold(.95f,.79f,.43f),DarkGold(.15f,.085f,.025f);
  Box(Out,L,G,{3,3},{394,394},DarkGold,197);
  Box(Out,L+1,G,{6,6},{388,388},LightGold,194);
  Box(Out,L+2,G,{9,9},{382,382},DarkGold,191);
  Box(Out,L+3,G,{13,13},{374,374},Gold,187);
  Box(Out,L+4,G,{17,17},{366,366},FLinearColor(.018f,.012f,.007f),183);
  Box(Out,L+5,G,{38,38},{324,324},Gold,162);
  Box(Out,L+6,G,{40,40},{320,320},FLinearColor(.002f,.004f,.007f,.98f),160);
  for(int I=0;I<32;++I)
  {
   const float A=NorthAngle+I*PI/16;
   if(I%8==0)continue;
   const FVector2D D(FMath::Sin(A),-FMath::Cos(A)),C(200,200);
   TArray<FVector2D> Tick={C+D*185,C+D*(I%4==0?164:179)};
   FSlateDrawElement::MakeLines(Out,L+7,G.ToPaintGeometry(),Tick,ESlateDrawEffect::None,LightGold,true,I%4==0?2.f:1.f);
  }
  const TCHAR* Directions[]={TEXT("N"),TEXT("E"),TEXT("S"),TEXT("W")};
  for(int I=0;I<4;++I)
  {
   const float A=NorthAngle+I*HALF_PI;
   Text(Out,L+8,G,{200+FMath::Sin(A)*174,200-FMath::Cos(A)*174},Directions[I],22,LightGold,true,true);
  }
  for(const auto& M:Markers)
  {
   if(M.Selected)Box(Out,L+7,G,M.Point-FVector2f(12,12),{24,24},FLinearColor::White,4);
   Box(Out,L+8,G,M.Point-FVector2f(7,7),{14,14},M.Color,7);
   if(M.Height)Text(Out,L+9,G,M.Point+FVector2f(0,M.Height>0?-28:5),M.Height>0?TEXT("^"):TEXT("v"),18,M.Color,true);
  }
  TArray<FVector2D> Arrow={{200,188},{193,205},{200,201},{207,205},{200,188}};
  FSlateDrawElement::MakeLines(Out,L+10,G.ToPaintGeometry(),Arrow,ESlateDrawEffect::None,FLinearColor::White,true,2.5f);
  Text(Out,L+8,G,{200,410},Coordinates,24,FLinearColor::White,true);
  return L+10;
 }
 virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)override
 {
  const FVector2f P(G.AbsoluteToLocal(E.GetScreenSpacePosition()));
  int32 Id=0;float Best=18*18;
  for(const auto& M:Markers){const float D=(M.Point-P).SizeSquared();if(D<Best){Best=D;Id=M.Guid;}}
  if(Id && Select)Select(Id);
  return FReply::Handled();
 }
private:
 TFunction<void(int32)> Select;
};
