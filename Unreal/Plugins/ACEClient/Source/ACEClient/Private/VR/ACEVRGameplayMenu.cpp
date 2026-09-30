#include "VR/ACEVRComponent.h"
#include "VR/ACEVRMenu.h"
#include "VR/ACEVRWidgetComponent.h"
#include "VR/ACEVRSettings.h"
#include "VR/ACEVRMath.h"
#include "ACECharacterAppearanceComponent.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "Components/WidgetComponent.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Engine/World.h"
#include "Widgets/Layout/SBox.h"

void UACEVRComponent::UpdateGameplayMenu(bool Visible,float Dt)
{
    if(Visible && !GameplayMenuPanel && PC->DatGameplayBinder)
    {
        GameplayMenu=CreateWidget<UACEVRMenu>(PC);
        GameplayMenu->InitializeMenu(this,Client,PC->DatGameplayBinder);
		if(!PendingGameplayPage.IsNone()){GameplayMenu->OpenPage(PendingGameplayPage);PendingGameplayPage=NAME_None;}
        GameplayMenuPanel=NewObject<UACEVRWidgetComponent>(PresentationActor,TEXT("VRGameplayMenu"));
        PresentationActor->AddInstanceComponent(GameplayMenuPanel);
        GameplayMenuPanel->SetWidgetSpace(EWidgetSpace::World);
        GameplayMenuPanel->SetDrawSize(FVector2D(1200,960));
        GameplayMenuPanel->SetBlendMode(EWidgetBlendMode::Transparent);
        GameplayMenuPanel->SetBackgroundColor(FLinearColor::Transparent);
        GameplayMenuPanel->SetWindowFocusable(false);
		GameplayMenuPanel->SetTickWhenOffscreen(true);
		GameplayMenuPanel->SetManuallyRedraw(true);GameplayMenuPanel->SetRedrawTime(1.f/60.f);
        GameplayMenuPanel->SetTwoSided(true);GameplayMenuPanel->SetCastShadow(false);
        GameplayMenuPanel->SetTranslucentSortPriority(30);
        GameplayMenuPanel->SetCollisionProfileName(TEXT("UI"));
        GameplayMenuPanel->RegisterComponent();
        GameplayMenuPanel->SetWidget(GameplayMenu);
    }
    if(GameplayMenuPanel)
    {
		bool Redraw=Visible && !GameplayMenuPanel->IsVisible();
		if(RetailPanel->GetAttachParent() && GameplayMenuPanel->GetAttachParent()!=RetailPanel->GetAttachParent())
			GameplayMenuPanel->AttachToComponent(RetailPanel->GetAttachParent(),FAttachmentTransformRules::KeepWorldTransform);
		else if(!RetailPanel->GetAttachParent() && GameplayMenuPanel->GetAttachParent())
			GameplayMenuPanel->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        GameplayMenuPanel->SetWorldTransform(RetailPanel->GetComponentTransform());
        GameplayMenuPanel->SetVisibility(Visible);
        GameplayMenuPanel->SetComponentTickEnabled(Visible);
        GameplayMenuPanel->SetCollisionEnabled(Visible?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
		if(Visible && GameplayMenu)
		{
			if(bLeftPointerPressed || bRightPointerPressed)
			{
				auto* Pointer=bLeftPointerPressed?LeftPointer.Get():RightPointer.Get();
				GameplayMenu->UpdateItemPointer(Pointer->GetHoveredWidgetComponent()==GameplayMenuPanel,Pointer->Get2DHitLocation());
				Redraw|=GameplayMenu->bItemDragging;
			}
			Redraw|=bTextKeyboardOpen;
			const int32 Before=GameplayMenu->GetRefreshCount();GameplayMenu->RefreshIfDirty();
			Redraw|=Before!=GameplayMenu->GetRefreshCount();
			uint8 PointerState=(bLeftPointerPressed?1:0)|(bRightPointerPressed?2:0);
			for(int32 I=0;I<2;++I)
			{
				auto* Pointer=I==0?LeftPointer.Get():RightPointer.Get();
				if(Pointer->GetHoveredWidgetComponent()!=GameplayMenuPanel)continue;
				PointerState|=uint8(4<<I);
				const FVector2D P=Pointer->Get2DHitLocation();
				if(!P.Equals(MenuPointerPositions[I],.5f)){Redraw=true;MenuPointerPositions[I]=P;}
			}
			Redraw|=MenuPointerState!=PointerState;MenuPointerState=PointerState;
		}
		if(Redraw)GameplayMenuPanel->RequestRedraw();
    }
    const int32 DragGuid=Visible && GameplayMenu && GameplayMenu->bItemDragging?GameplayMenu->DragItem:0;
    if(DragGuid && !MenuDragPanel)
    {
        MenuDragPanel=NewObject<UACEVRWidgetComponent>(PresentationActor,TEXT("NativeInventoryDrag"));
        PresentationActor->AddInstanceComponent(MenuDragPanel);
        MenuDragPanel->SetWidgetSpace(EWidgetSpace::World);MenuDragPanel->SetDrawSize(FVector2D(64,64));
        MenuDragPanel->SetBlendMode(EWidgetBlendMode::Transparent);MenuDragPanel->SetBackgroundColor(FLinearColor::Transparent);
        MenuDragPanel->SetTwoSided(true);MenuDragPanel->SetCastShadow(false);MenuDragPanel->SetTranslucentSortPriority(40);
        MenuDragPanel->SetCollisionEnabled(ECollisionEnabled::NoCollision);MenuDragPanel->SetManuallyRedraw(true);
        MenuDragPanel->RegisterComponent();
    }
    if(MenuDragPanel)
    {
        if(DragGuid && MenuDragGuid!=DragGuid)
        {
            FACEWorldObject Item;Client->GetWorldObject(DragGuid,Item);
            MenuDragPanel->SetSlateWidget(SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)[GameplayMenu->ItemIcon(Item)]);
            MenuDragPanel->RequestRedraw();
        }
        MenuDragGuid=DragGuid;MenuDragPanel->SetVisibility(DragGuid!=0);MenuDragPanel->SetComponentTickEnabled(DragGuid!=0);
        if(DragGuid)
        {
            const auto* Pointer=FeedbackHand==0?LeftPointer.Get():RightPointer.Get();
            const FVector Position=Pointer->GetHoveredWidgetComponent()?FVector(Pointer->GetLastHitResult().ImpactPoint)
                :Pointer->GetComponentLocation()+Pointer->GetForwardVector()*80.f;
            MenuDragPanel->SetWorldLocationAndRotation(Position+FVector(0,0,4),(Head->GetComponentLocation()-Position).Rotation());
            MenuDragPanel->SetWorldScale3D(FVector(.065f));
        }
        else if(MenuDragPanel->GetSlateWidget())MenuDragPanel->SetSlateWidget(nullptr);
    }
    const bool MirrorVisible=Visible && GameplayMenu && (GameplayMenu->GetPage()=="Inventory" || GameplayMenu->GetPage()=="Equipment");
    auto* Source=GetOwner()->FindComponentByClass<UACECharacterAppearanceComponent>();
    if(MirrorVisible && Source && Source->HasAppearance())
    {
        if(!MenuMirrorActor)
        {
            MenuMirrorActor=GetWorld()->SpawnActor<AActor>();
            auto* Root=NewObject<USceneComponent>(MenuMirrorActor,TEXT("MirrorRoot"));
            MenuMirrorActor->SetRootComponent(Root);Root->RegisterComponent();
            MenuMirrorActor->SetActorEnableCollision(false);
            MenuMirrorAppearance=NewObject<UACECharacterAppearanceComponent>(MenuMirrorActor);
            MenuMirrorActor->AddInstanceComponent(MenuMirrorAppearance);MenuMirrorAppearance->RegisterComponent();
            MenuMirrorAppearance->bVRPoseControlled=true;
            MenuMirrorAppearance->SetComponentTickEnabled(false);
        }
        if(!MenuMirrorAppearance->HasAppearance() || MenuMirrorRevision!=Source->GetAppearanceRevision())
        {
            FACEWorldObject Self;
            if(Client->GetWorldObject(Client->GetPlayerGuid(),Self) && MenuMirrorAppearance->ApplyWorldObject(Self,PC->WorldScale,false))
            {
                MenuMirrorRevision=Source->GetAppearanceRevision();
                MenuMirrorAppearance->SetPartsCastShadow(false);
                MenuMirrorAppearance->SetAppearanceVisible(true);
				MenuMirrorAppearance->SetComponentTickEnabled(false);
            }
        }
        const float Scale=Settings->PanelScale;
        const FTransform Frame=RetailPanel->GetComponentTransform();
        const FVector MirrorLocation=Frame.GetLocation()+Frame.GetRotation().RotateVector(FVector(15,760*Scale,-420*Scale));
        // This mirror sits beside the menu. Its reflection plane must face the
        // viewer at the model, not the center of the menu (which turns the face
        // away when the player looks left toward their paper doll).
        const FQuat MirrorFacing=FRotator(0,(Head->GetComponentLocation()-MirrorLocation).Rotation().Yaw,0).Quaternion();
        MenuMirrorActor->SetActorLocationAndRotation(MirrorLocation,MirrorFacing);
        MenuMirrorActor->SetActorScale3D(FVector(Scale*5));
        if(auto* Root=Source->GetMeshRoot();Root && MenuMirrorAppearance->GetMeshRoot())
        {
            MenuMirrorAppearance->GetMeshRoot()->SetRelativeTransform(FTransform::Identity);
            const FTransform SourceFrame(MirrorFacing,Root->GetComponentLocation());
            for(int32 I=0;I<Source->GetPartCount();++I)
            {
                auto* Original=Source->GetPartMesh(I);
                auto* Part=MenuMirrorAppearance->GetPartMesh(I);
                if(!Original || !Part)continue;
                const FTransform Pose=ACEVRMath::MirrorPartPose(Original->GetComponentTransform().GetRelativeTransform(SourceFrame));
                if(!Part->GetRelativeTransform().Equals(Pose,.001f))Part->SetRelativeTransform(Pose);
            }
        }
    }
    if(MenuMirrorActor)MenuMirrorActor->SetActorHiddenInGame(!MirrorVisible);
}
