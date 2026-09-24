#include "ACEVRNativeHUD.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEPlayerController.h"
#include "UI/ACEUIGameplayBinder.h"
#include "Components/WidgetComponent.h"
#include "Camera/CameraComponent.h"

void UACEVRComponent::UpdateNativeHUD(bool Available)
{
	const auto Show=[](UWidgetComponent* Panel,bool Visible){Panel->SetVisibility(Visible);Panel->SetCollisionEnabled(Visible?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);};
 const bool VitalsVisible=Available && Settings->bPinVitalsToView;
 const bool CompassVisible=Available && Settings->bShowCompass;
 const bool NewlyVisible=(VitalsVisible && !VitalsPanel->IsVisible()) || (CompassVisible && !CompassPanel->IsVisible());
 Show(VitalsPanel,VitalsVisible);Show(CompassPanel,CompassVisible);
 VitalsPanel->SetComponentTickEnabled(VitalsVisible);CompassPanel->SetComponentTickEnabled(CompassVisible);
 const double Now=FPlatformTime::Seconds();
 if((!VitalsVisible && !CompassVisible) || (!NewlyVisible && Now<NextNativeHUDUpdate))return;
 NextNativeHUDUpdate=Now+.1;
 if(VitalsVisible && NativeVitals && (NativeVitals->Refresh(Client->GetPlayerVitals()) || NewlyVisible))VitalsPanel->RequestRedraw();
 if(!CompassVisible || !NativeCompass || !Client->GetSession())return;
 // Read the live object map without allocating/copying every world object.
 // A fixed marker cap and 10Hz sampling bound radar work in dense areas.
 const auto Session=Client->GetSession();
 const FACEPosition Self=PC->bHavePredictedPose?PC->PredictedPose:Session->GetPlayerPosition();
 const FVector Feet=Self.ToUnrealLocation(PC->WorldScale);
 const FQuat Facing=FRotator(0,Head->GetComponentRotation().Yaw,0).Quaternion();
 const FVector North=Facing.UnrotateVector(FVector(0,1,0));
 NativeCompass->NorthAngle=FMath::Atan2(North.Y,North.X);
 NativeCompass->Coordinates=UACEUIGameplayBinder::FormatMapCoords(Self);
 if(NativeCompass->Coordinates.IsEmpty())NativeCompass->Coordinates=FString::Printf(TEXT("Cell %08X  %.0f, %.0f"),uint32(Self.CellId),Self.Location.X,Self.Location.Y);
 NativeCompass->Markers.Reset();
 struct FCandidate {const FACEWorldObject* Object;FVector Delta;double Distance;};
 TArray<FCandidate,TInlineAllocator<64>> Nearby;
 const int32 Selected=Client->GetSelectedObject().Guid;
 for(const auto& Pair:Session->GetWorldObjects())
 {
  const auto& O=Pair.Value;
  if(!UACEUIGameplayBinder::ShouldShowOnRadar(O,Client->GetPlayerGuid()))continue;
  const bool Indoor=(uint32(Self.CellId)&0xffff)>=0x100;
  if(Indoor && (uint32(O.Position.CellId)&0xffff0000)!=(uint32(Self.CellId)&0xffff0000))continue;
  const FVector Delta=(O.Position.ToUnrealLocation(PC->WorldScale)-Feet)/PC->WorldScale;
  const double D=Delta.SizeSquared2D();if(D>3600 || FMath::Abs(Delta.Z)>60)continue;
  const double Priority=O.Guid==Selected?-1:D;
  int32 Index=0;while(Index<Nearby.Num() && Nearby[Index].Distance<Priority)++Index;
  if(Index>=64)continue;
  if(Nearby.Num()==64)Nearby.RemoveAt(63);
  Nearby.Insert({&O,Delta,Priority},Index);
 }
 for(const auto& C:Nearby)
 {
  const FVector Local=Facing.UnrotateVector(C.Delta);
  FACEVRRadarMarker M;M.Point={200+float(Local.Y)*2.6f,200-float(Local.X)*2.6f};M.Guid=C.Object->Guid;
  M.Color=UACEUIGameplayBinder::ColorFromRadarBlip(UACEUIGameplayBinder::ResolveRadarColor(*C.Object));
  M.Selected=M.Guid==Selected;M.Height=C.Delta.Z>3?1:C.Delta.Z<-3?-1:0;
  NativeCompass->Markers.Add(M);
 }
 NativeCompass->Invalidate(EInvalidateWidgetReason::Paint);CompassPanel->RequestRedraw();
}
