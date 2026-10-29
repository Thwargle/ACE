#include "ACEPlayerController.h"
#include "Mobile/ACEMobileWidget.h"
#include "Mobile/ACEMobileSettings.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEWorldEntityActor.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIGameplayBinder.h"
#include "GameFramework/SpringArmComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Framework/Application/SlateApplication.h"

bool AACEPlayerController::IsMobileActive() const
{
    return UACEMobileSettings::IsEnabled() && !IsVRActive();
}
void AACEPlayerController::UpdateMobileInterface()
{
    if(!IsLocalController() || !IsMobileActive())return;
    if(!MobileWidget)
    {
        MobileWidget=CreateWidget<UACEMobileWidget>(this);
        MobileWidget->InitializeMobile(this);
        MobileWidget->AddToPlayerScreen(11000);
    }
    const bool InWorld=Client && Client->GetSessionState()==EACESessionState::InWorld;
    const bool Roster=Client && Client->GetSessionState()==EACESessionState::CharacterSelect && !DatCharGenBinder;
    MobileWidget->SetVisibility(InWorld || Roster ? ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    // Retain the retail binder's action/state handling, without duplicate hit areas.
    if(DatCanvasWidget && (InWorld || Roster))
    {
        DatCanvasWidget->SetVisibility(ESlateVisibility::Collapsed);
        if(InWorld)DatCanvasWidget->TickGameplayState(false);
    }
    bShowMouseCursor=false;
    MobileWidget->UpdateMobile();
}
void AACEPlayerController::CancelMobileMovement()
{
    bAutoRun=false;bJumpCharging=false;bSpaceWasDown=false;JumpChargeExtent=0;
    if(bServerMoveToActive)
    {
        HandleMoveToFailed(0);
        if(Client && Client->GetSession())Client->GetSession()->CancelWorldUseApproach();
    }
    if(Client && Client->GetSessionState()==EACESessionState::InWorld && Client->GetSession()
        && (!FMath::IsNearlyZero(ForwardSent) || !FMath::IsNearlyZero(RightSent) || !FMath::IsNearlyZero(TurnSent)))
    {
        Client->GetSession()->SendStopMovement();ForwardAxis=RightAxis=TurnAxis=ForwardSent=RightSent=TurnSent=0;
        bForceMovementResend=true;
    }
    if(DatGameplayBinder){DatGameplayBinder->SetJumpChargeFraction(0);DatGameplayBinder->CancelCombatAttack();}
}
void AACEPlayerController::MobileLook(FVector2D Delta)
{
    if(!IsMobileActive() || IsWorldTransitionActive() || !GetPawn())return;
    ApplyCameraOrbitDelta(GetPawn()->FindComponentByClass<USpringArmComponent>(),Delta.X,Delta.Y);
}
void AACEPlayerController::MobileSelectAt(FVector2D Absolute)
{
    if(!Client || IsWorldTransitionActive() || !IsMobileActive())return;
    const auto G=UWidgetLayoutLibrary::GetViewportWidgetGeometry(this);
    const auto Size=G.GetLocalSize();if(Size.X<=0 || Size.Y<=0)return;
    int32 W=0,H=0;GetViewportSize(W,H);
    const auto Pixel=G.AbsoluteToLocal(Absolute)/Size*FVector2D(W,H);
    if(auto* Actor=PickWorldEntityAtScreenPosition(Pixel.X,Pixel.Y))
    {
        const int32 Guid=Actor->GetACEGuid();
        if(!DatGameplayBinder || !DatGameplayBinder->TryCompletePendingUseWithTarget(Guid))Client->SelectObject(Guid);
    }
}
void AACEPlayerController::MobileAction(FName Action,int32 Spell)
{
    if(!Client || Client->GetSessionState()!=EACESessionState::InWorld || IsWorldTransitionActive() || !DatGameplayBinder)return;
    auto* B=DatGameplayBinder.Get();
    if(Action=="Attack")
    {
        if(B->CombatMode==1)B->ToggleCombatModeHotkey();
        if(B->CombatMode==8)B->CastSelectedHotbarSpell();
        else B->RequestActionDeckAttack(B->CombatAttackHeight);
    }
    else if(Action=="Cast")B->CastSelectedHotbarSpell();
    else if(Action=="Combat")B->ToggleCombatModeHotkey();
    else if(Action=="Use")InteractWithSelectedObject();
    else if(Action=="Pick up")PickupSelectedObject();
    else if(Action=="Self")B->TargetActionDeckSelf();
    else if(Action=="Next target")CycleNearbyTarget(true,1);
    else if(Action=="Previous target")CycleNearbyTarget(true,-1);
    else if(Action=="Cancel"){B->CancelCombatAttack();B->CancelPendingUseWith();EndUseApproach();Client->SelectObject(0);}
    else
    {
        const FString Name=Action.ToString();
        if(Name.StartsWith(TEXT("Shortcut ")))B->UseShortcutSlot(FCString::Atoi(*Name.Mid(9)));
        else if(Name.StartsWith(TEXT("Spell ")))
        {
            const auto Bar=Client->GetSpellBar(Client->GetActiveSpellBar());const int32 Index=FCString::Atoi(*Name.Mid(6))-1;
            if(Bar.IsValidIndex(Index) && Bar[Index])Client->SendCastSpell(Bar[Index]);
        }
    }
}
