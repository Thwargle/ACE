#include "VR/ACEVRComponent.h"
#include "VR/ACEVRChat.h"
#include "VR/ACEVRSettings.h"
#include "VR/ACEVRWidgetComponent.h"
#include "VR/ACEVRMenu.h"
#include "ACEPlayerController.h"
#include "ACEClientSubsystem.h"
#include "UI/ACEUICanvasWidget.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Camera/CameraComponent.h"

void UACEVRComponent::ToggleChat()
{
    if(!bActive || !bTracking || !Client || Client->GetSessionState()!=EACESessionState::InWorld)return;
    bChatOpen=!bChatOpen;
    if(bChatOpen){bChatAnchorReady=false;FrontPanel="Chat";}
    else if(ChatWidget && FocusedTextEntry.Get()==ChatWidget->GetEntry())DismissTextEntry();
    UpdatePanels();
}
void UACEVRComponent::UpdateChatPanel(bool Available,float Dt)
{
    const int32 Vendor=Client?Client->GetOpenVendorGuid():0;
    if(Vendor!=ChatVendorGuid)
    {
        ChatVendorGuid=Vendor;bChatDocked=Vendor!=0;
        if(Vendor){bChatOpen=true;FrontPanel="Menu";}
    }
    const bool Visible=Available && bChatOpen;
    if(Visible && PC && PC->DatGameplayBinder && (!ChatWidget || !ChatWidget->IsBoundTo(PC->DatGameplayBinder)))
    {
        if(ChatWidget && FocusedTextEntry.Get()==ChatWidget->GetEntry())DismissTextEntry();
        ChatWidget=CreateWidget<UACEVRChat>(PC);
        ChatWidget->InitializeChat(this,PC->DatGameplayBinder);
        ChatPanel->SetWidget(ChatWidget);ChatPanel->SetManuallyRedraw(true);
        ChatPanel->SetWindowFocusable(false);ChatPanel->SetCastShadow(false);
    }
    const bool WasVisible=ChatPanel->IsVisible();
    ChatPanel->SetVisibility(Visible && ChatWidget);
    ChatPanel->SetComponentTickEnabled(Visible && ChatWidget);
    ChatPanel->SetCollisionEnabled(Visible && ChatWidget?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
    ChatPanel->SetWorldScale3D(FVector(Settings->ChatScale));
    if(!Visible || !ChatWidget)return;
    UpdateChatAnchor(Dt);
    if(bChatDocked && GameplayMenuPanel && GameplayMenuPanel->IsVisible() && GameplayMenu && GameplayMenu->Page=="Vendor")
    {
        // Place beyond both the vendor and optional inspection card; no overlapping quads.
        const float Width=GameplayMenuPanel->GetDrawSize().X*GameplayMenuPanel->GetComponentScale().Y;
        const float Inspect=MenuInspectionPanel && MenuInspectionPanel->IsVisible()?MenuInspectionPanel->GetDrawSize().X*MenuInspectionPanel->GetComponentScale().Y+5.f:0.f;
        const float Offset=Width*.5f+Inspect+ChatPanel->GetDrawSize().X*Settings->ChatScale*.5f+6.f;
        const FTransform MenuPose=GameplayMenuPanel->GetComponentTransform();
        ChatPanel->SetWorldLocationAndRotation(MenuPose.GetLocation()-MenuPose.GetRotation().GetRightVector()*Offset,MenuPose.GetRotation());
    }
    else
    {
        const auto Frame=GetChatAnchorTransform();
        ChatPanel->SetWorldLocationAndRotation(Frame.TransformPosition(Settings->ChatViewOffset),
            Frame.GetRotation()*Settings->ChatViewRotation.Quaternion()*FRotator(0,180,0).Quaternion());
    }
    const bool Hover=(LeftPointer && LeftPointer->GetHoveredWidgetComponent()==ChatPanel)
        || (RightPointer && RightPointer->GetHoveredWidgetComponent()==ChatPanel);
    if(ChatWidget->Refresh() || !WasVisible || Hover || FocusedTextEntry.Get()==ChatWidget->GetEntry())ChatPanel->RequestRedraw();
}
void UACEVRComponent::FocusPanelSurface(UWidgetComponent* Panel)
{
    if(Panel==ChatPanel)FrontPanel="Chat";
    else if(Panel==RetailPanel || Panel==GameplayMenuPanel || Panel==MenuInspectionPanel || Panel==MenuControlsPanel)FrontPanel="Menu";
    else if(Panel==SettingsPanel || Panel==OptionsControlsPanel)FrontPanel="Options";
    UpdatePersonalPanelLayers();
}
void UACEVRComponent::UpdatePersonalPanelLayers()
{
    auto Layer=[](UWidgetComponent* P,int32 N){if(P)UACEVRWidgetComponent::SetPersonalLayer(P,N);};
    const bool MenuVisible=(GameplayMenuPanel && GameplayMenuPanel->IsVisible()) || (RetailPanel && RetailPanel->IsVisible() && RetailPanel->bRenderInMainPass);
    const int32 Menu=FrontPanel=="Menu"?50:30,Chat=FrontPanel=="Chat"?50:30,Options=FrontPanel=="Options"?60:40;
    Layer(RetailPanel,Menu);Layer(GameplayMenuPanel,Menu);Layer(MenuInspectionPanel,Menu+1);Layer(MenuControlsPanel,Menu+2);
    Layer(ChatPanel,Chat);Layer(SettingsPanel,Options);Layer(OptionsControlsPanel,Options+2);Layer(KeyboardPanel,80);
    Layer(VitalsPanel,MenuVisible?0:10);Layer(CompassPanel,MenuVisible?0:10);Layer(FellowshipPanel,MenuVisible?0:10);
    Layer(WristPanel,20);Layer(JumpPanel,15);Layer(MenuDragPanel,90);Layer(FocusPanel,95);
}
