#include "UI/ACEUIGameplayBinder.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACERetailTextBlock.h"
#include "Dat/ACEDatTextLayout.h"
#include "Blueprint/WidgetTree.h"
#include "ACEPlayerController.h"
#include "GameFramework/Pawn.h"
#include "VR/ACEVRComponent.h"

void UACEUIGameplayBinder::RefreshServerPopup()
{
	const auto Session = Client ? Client->GetSession() : nullptr;
	// Action confirmations take precedence. Do not let an informational OK button
	// hide a timed fellowship invitation or be mistaken for accepting it.
	if (!Session || Session->GetState() != EACESessionState::InWorld || Session->GetServerPopups().IsEmpty()
		|| !ServerConfirmPrompt.IsEmpty() || ManaStoneConfirmSource)
	{
		if (ServerPopupRoot) ServerPopupRoot->bVisible = false;
		for (UTextBlock* Label : ServerPopupLabels) if (Label) Label->SetVisibility(ESlateVisibility::Collapsed);
		ServerPopupId = 0; ServerPopupPrompt.Reset();
		return;
	}
	if (!Manager || !Canvas || !Canvas->WidgetTree) return;
	if (!ServerPopupRoot)
	{
		// DialogFactory::CreateDialog_(type=3): MessageDialog template 36, OK=38.
		ServerPopupRoot = UACEUILayoutResolver::LoadTemplate(0x2100003C, 0x24);
		if (!ServerPopupRoot) return;
		ServerPopupRoot->SetElementName(TEXT("RootGameplay_FloatyServerMessage_Field"));
		TFunction<void(TSharedPtr<FACEUIElement>)> NameControls = [&](TSharedPtr<FACEUIElement> Node)
		{
			if (Node->ElementId == 0x3D) Node->LeftEdge = Node->RightEdge = Node->TopEdge = Node->BottomEdge = 3;
			if (Node->ElementId == 0x3E) Node->SetElementName(TEXT("ServerMessageBody"));
			if (Node->ElementId == 0x26) Node->SetElementName(TEXT("ServerMessageOK"));
			for (const auto& Child : Node->Children) NameControls(Child);
		};
		NameControls(ServerPopupRoot);
		if (auto GameplayRoot = Manager->FindElementByName(TEXT("RootGameplay_Field"))) GameplayRoot->AddChild(ServerPopupRoot);
		else Manager->AddRoot(ServerPopupRoot);
		for (int32 I=0; I<2; ++I)
			ServerPopupLabels.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
	}
	const auto& Popup = Session->GetServerPopups()[0];
	if (!ServerPopupRoot->bVisible || ServerPopupId != Popup.Id)
	{
		ServerPopupId = Popup.Id; ServerPopupPrompt = Popup.Message;
		ServerPopupRoot->bVisible = true;
		const auto Body = Manager->FindElementByName(TEXT("ServerMessageBody"));
		FACEDatFont Font;
		if (Body && Canvas->GetResourceResolver()->ResolveFont(Body->FontId, Font))
		{
			const int32 H = ACEDatText::Layout(Font, ServerPopupPrompt, Body->Width-Body->TextMargins.Left-Body->TextMargins.Right, false).Num()*Font.MaxCharHeight
				+ Body->TextMargins.Top + Body->TextMargins.Bottom;
			const int32 Delta = H-Body->Height;
			if (auto Box = Body->Parent.Pin())
			{
				Box->Height += Delta; Box->LayoutAuthoredH = Box->LastReflowH = Box->Height;
				for (const auto& Child : Box->Children)
				{
					if (Child->TopEdge == 2) Child->Y += Delta;
					else if (Child->BottomEdge == 1) Child->Height += Delta;
					Child->LayoutAuthoredH = Child->LastReflowH = Child->Height;
				}
			}
		}
		Manager->BringFloatyToFront(ServerPopupRoot);
		if (PlayerController && PlayerController->GetPawn())
			if (auto* VR = PlayerController->GetPawn()->FindComponentByClass<UACEVRComponent>(); VR && VR->IsActive())
				VR->OpenRetailPanel(NAME_None);
	}
	PlaceTextOnElement(ServerPopupLabels[0], TEXT("ServerMessageBody"), ServerPopupPrompt, 12, FLinearColor::White, 200020);
	PlaceTextOnElement(ServerPopupLabels[1], TEXT("ServerMessageOK"), TEXT("OK"), 12, FLinearColor::White, 200020, true);
}

void UACEUIGameplayBinder::FinishServerPopup(uint64 Id)
{
	if (Client) if (auto Session = Client->GetSession()) Session->DismissServerPopup(Id);
	RefreshServerPopup();
}
