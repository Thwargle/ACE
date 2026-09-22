#include "UI/ACEUICharSelectBinder.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACERetailTextEntry.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIResourceResolver.h"
#include "ACEDatSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "Dat/ACEDatTextLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

namespace
{
TSharedPtr<FACEUIElement> Descendant(const TSharedPtr<FACEUIElement>& Root, uint32 Id)
{
	if (!Root) return nullptr;
	for (auto Child : Root->Children)
	{
		if (Child->ElementId == Id) return Child;
		if (auto Found = Descendant(Child, Id)) return Found;
	}
	return nullptr;
}
}

void UACEUICharSelectBinder::ActionLabel(const TSharedPtr<FACEUIElement>& Element, const FString& Text)
{
	if (!Element || !Canvas || !Canvas->WidgetTree) return;
	auto& Label = ActionLabels.FindOrAdd(Element->InstanceId);
	if (!Label)
	{
		Label = Canvas->WidgetTree->ConstructWidget<UACERetailTextBlock>();
		Canvas->GetElementLayer()->AddChild(Label);
	}
	Label->SetText(FText::FromString(Text));
	Label->SetColorAndOpacity(FSlateColor(Element->TextColor.Get(FLinearColor::White)));
	Label->SetJustification(Element->TextHorizontalJustification == 1 ? ETextJustify::Center : ETextJustify::Left);
	const auto Scale = Canvas->GetLastScale2D();
	Label->SetMargin(FMargin(Element->TextMargins.Left*Scale.X, Element->TextMargins.Top*Scale.Y,
		Element->TextMargins.Right*Scale.X, Element->TextMargins.Bottom*Scale.Y));
	Label->SetAutoWrapText(!Element->bTextOneLine);
	Label->SetVisibility(ESlateVisibility::HitTestInvisible);
	Canvas->PlaceWidgetAtElement(Label, Element, 22000);
}

void UACEUICharSelectBinder::ShowDeleteConfirmation()
{
	if (!Manager || !Canvas || !Characters.IsValidIndex(SelectedIndex) || Characters[SelectedIndex].DeleteSeconds) return;
	const auto Root = Manager->FindElementByName(TEXT("CharacterManagementField"));
	if (!Root) return;
	CloseDialog();
	if (!Dialog)
	{
		// gmCharacterManagementUI uses DialogFactory type 5: text input + OK/Cancel.
		Dialog = UACEUILayoutResolver::LoadTemplate(0x2100003C, 0x2C);
		if (!Dialog) return;
		Dialog->SetElementName(TEXT("CharacterDeleteConfirmation")); Dialog->ZLevel = 10000;
		Root->AddChild(Dialog);
	}
	DeleteCandidateId = SelectedCharacterId;
	ConfirmationWord = CharacterStrings.Text(TEXT("ID_CharacterManagement_DeleteCharacterResponse"));
	const FString& Name = Characters[SelectedIndex].Name;
	if (ConfirmationWord.IsEmpty()) ConfirmationWord = TEXT("DELETE");
	DialogMessage = CharacterStrings.FormatText(TEXT("ID_CharacterManagement_DeleteCharacterConfirmation"), {{TEXT("PLAYER"),Name}});
	if (DialogMessage.IsEmpty()) DialogMessage = FString::Printf(TEXT("Are you sure you want to delete %s?\n\nType \"DELETE\" to confirm, or choose Cancel."),*Name);
	LastManagementError.Reset();
	Dialog->bVisible = true;
	const auto Box = Descendant(Dialog, 0x3D), Body = Descendant(Dialog, 0x3E);
	const auto InputFrame = Descendant(Dialog, 0x1000047E), Input = Descendant(InputFrame, 0x2C);
	if (!Box || !Body || !Input) { CloseDialog(); return; }
	FACEDatFont Font; int32 Height = 200;
	if (Canvas->GetResourceResolver()->ResolveFont(Body->FontId, Font))
		Height = FMath::Clamp(ACEDatText::Layout(Font, DialogMessage, 370, false).Num()*Font.MaxCharHeight + 107, 125, 480);
	const int32 Delta = Height - Box->Height;
	for (auto Child : Box->Children)
	{
		if (Child->Y >= Body->Y+Body->Height) Child->Y += Delta;
		else if (Child != Body && Child->Height > 40) Child->Height += Delta;
	}
	Box->Height = Height; Box->X = 200; Box->Y = (600-Height)/2;
	Body->Height = Height-107;
	InputFrame->X = (Box->Width-InputFrame->Width)/2;
	if (!ConfirmationEntry)
	{
		ConfirmationEntry = Canvas->WidgetTree->ConstructWidget<UACERetailTextEntry>();
		ConfirmationEntry->MaxLength = 32;
		ConfirmationEntry->OnTextCommitted.AddDynamic(this, &UACEUICharSelectBinder::ConfirmationCommitted);
		Canvas->GetElementLayer()->AddChild(ConfirmationEntry);
	}
	ConfirmationEntry->SetRetailElement(Canvas->GetResourceResolver(), Input);
	ConfirmationEntry->SetText(FText::GetEmpty());
	ConfirmationEntry->SetVisibility(ESlateVisibility::Visible);
	RefreshDialog();
	ConfirmationEntry->SetKeyboardFocus();
}

void UACEUICharSelectBinder::ConfirmationCommitted(const FText&, ETextCommit::Type Method)
{
	// Keyboard dismissal/focus loss must not authorize deletion.
	if (Method == ETextCommit::OnEnter) ConfirmDelete();
}

void UACEUICharSelectBinder::ConfirmDelete()
{
	if (!Dialog || !Dialog->bVisible || !DeleteCandidateId || !ConfirmationEntry) return;
	if (!ConfirmationEntry->GetText().ToString().Equals(ConfirmationWord, ESearchCase::IgnoreCase)) return;
	const int32 Id = DeleteCandidateId;
	CloseDialog();
	const auto Session = Client ? Client->GetSession() : nullptr;
	if (Session && !Session->DeleteCharacter(Id))
	{
		// Selection may have expired/changed while the dialog was open. Never send
		// an unchecked row number; the session resolves this GUID in its live list.
		LastManagementError = TEXT("The character list changed or the connection is unavailable. Please try again.");
	}
}

void UACEUICharSelectBinder::CloseDialog()
{
	DeleteCandidateId = 0;
	if (Dialog) Dialog->bVisible = false;
	if (ConfirmationEntry) ConfirmationEntry->SetVisibility(ESlateVisibility::Collapsed);
	for (auto& Pair : ActionLabels) Pair.Value->SetVisibility(ESlateVisibility::Collapsed);
}

void UACEUICharSelectBinder::RefreshDialog()
{
	if (!Canvas || !Manager) return;
	if (Dialog && Dialog->bVisible)
	{
		ActionLabel(Descendant(Dialog, 0x3E), DialogMessage);
		ActionLabel(Descendant(Dialog, 0x2E), TEXT("OK"));
		ActionLabel(Descendant(Dialog, 0x2F), TEXT("Cancel"));
		if (ConfirmationEntry) Canvas->PlaceWidgetAtElement(ConfirmationEntry,
			Descendant(Descendant(Dialog, 0x1000047E), 0x2C), 22001);
		return;
	}
	const auto Session = Client ? Client->GetSession() : nullptr;
	if (!Session) return;
	if (!Session->GetCharacterManagementError().IsEmpty()) LastManagementError = Session->GetCharacterManagementError();
	const FString Message = Session->IsCharacterManagementPending() ? TEXT("Please wait...") : LastManagementError;
	if (Message.IsEmpty()) return;
	const auto Root = Manager->FindElementByName(TEXT("CharacterManagementField"));
	if (!Root) return;
	auto Status = Manager->FindElementByName(TEXT("CharacterManagementStatus"));
	if (!Status)
	{
		Status = UACEUILayoutResolver::LoadTemplate(0x21000003,0x10000411);
		if (!Status) return;
		Status->SetElementName(TEXT("CharacterManagementStatus"));
		Status->X=20; Status->Y=548; Status->Width=760; Status->Height=42;
		Status->FontId=0x40000000; Status->TextHorizontalJustification=1; Status->bActivatable=false;
		Root->AddChild(Status);
	}
	ActionLabel(Status, Message);
}

bool UACEUICharSelectBinder::KeyDown(const FKeyEvent& Event)
{
	if (Event.GetKey().IsGamepadKey()) return false;
	if (bCredits) { SetCreditsVisible(false); return true; }
	if (Dialog && Dialog->bVisible)
	{
		if (Event.GetKey()==EKeys::Escape) CloseDialog();
		return true;
	}
	return false;
}

void UACEUICharSelectBinder::SetCreditsVisible(bool bShow)
{
	if (!Manager || !Canvas) return;
	CloseDialog();
	if (!bShow)
	{
		bCredits = false; if (CreditsRoot) CreditsRoot->bVisible=false;
		RefreshOverlays();
		return;
	}
	if (!CreditsRoot)
	{
		const auto Root = Manager->FindElementByName(TEXT("CharacterManagementField"));
		if (!Root || !Canvas->GetResourceResolver()->GetDatSubsystem()) return;
		FACECharacterCreation Strings;
		if (!Strings.LoadStrings(Canvas->GetResourceResolver()->GetDatSubsystem()->GetDatDirectory(), 0x23000008)) return;
		CreditText.Reset(); int32 Count=0;
		for (int32 I=1; I<1000; ++I)
		{
			const FString Text = Strings.Text(FString::Printf(TEXT("ID_Credits%d"), I));
			if (Text.IsEmpty()) break;
			CreditText += Text; ++Count;
		}
		if (!Count) return;
		CreditsRoot=UACEUILayoutResolver::LoadTemplate(0x21000003,0x10000413);
		if (!CreditsRoot) return;
		CreditsRoot->SetElementName(TEXT("CharacterCredits"));
		CreditsRoot->Width=800; CreditsRoot->Height=600; CreditsRoot->ZLevel=10000;
		CreditsRoot->ImageFileId=0x06004D0B; CreditsRoot->DrawMode=3;
		Root->AddChild(CreditsRoot);
		auto TextField=UACEUILayoutResolver::LoadTemplate(0x21000003,0x10000410);
		auto PictureField=UACEUILayoutResolver::LoadTemplate(0x21000003,0x10000413);
		if (!TextField || !PictureField) { ShutdownActions(); return; }
		CreditsRoot->AddChild(TextField); CreditsRoot->AddChild(PictureField);
		CreditsText=Descendant(TextField,0x10000411);
		FACEDatFont Font;
		if (!CreditsText || !Canvas->GetResourceResolver()->ResolveFont(CreditsText->FontId,Font)) { ShutdownActions(); return; }
		CreditsText->Width=400; CreditsText->Height=ACEDatText::Layout(Font,CreditText,400,false).Num()*Font.MaxCharHeight;
		// gmCreditsUI::Initialize uses 20s per visible page, including entry/exit.
		CreditsSpeed=(600.f + float(CreditsText->Height)/Count)/20.f;
		for (int I=0; I<3; ++I)
		{
			auto Picture=UACEUILayoutResolver::LoadTemplate(0x21000003,0x10000415);
			PictureField->AddChild(Picture); CreditPictures.Add(Picture);
		}
	}
	bCredits=true; CreditsRoot->bVisible=true; CreditsElapsed=0; NextCreditPicture=3;
	if (LetterboxFill) if (auto* Slot=Cast<UCanvasPanelSlot>(LetterboxFill->Slot)) Slot->SetZOrder(3000);
	CreditsText->Y=600;
	for (int I=0; I<CreditPictures.Num(); ++I)
	{
		CreditPictures[I]->Y=600+I*301;
		CreditPictures[I]->ImageFileId=0x06005F14+I;
	}
	RefreshOverlays(); TickCredits(0);
	Canvas->SetKeyboardFocus();
}

void UACEUICharSelectBinder::TickCredits(float DeltaSeconds)
{
	if (!bCredits || !CreditsText) return;
	const int32 Previous=FMath::FloorToInt(CreditsElapsed*CreditsSpeed);
	CreditsElapsed+=FMath::Max(0.f,DeltaSeconds);
	const int32 Scroll=FMath::FloorToInt(CreditsElapsed*CreditsSpeed);
	CreditsText->Y=600-Scroll;
	if (CreditsText->Y+CreditsText->Height<=0) { SetCreditsVisible(false); return; }
	for (auto Picture : CreditPictures) Picture->Y-=Scroll-Previous;
	for (auto Picture : CreditPictures)
		if (Picture->Y+Picture->Height<0)
		{
			int32 Bottom=0;
			for (auto Other : CreditPictures) Bottom=FMath::Max(Bottom,Other->Y+Other->Height);
			Picture->Y=Bottom+1; Picture->ImageFileId=0x06005F14+(NextCreditPicture++%7);
		}
	ActionLabel(CreditsText,CreditText);
}

void UACEUICharSelectBinder::ShutdownActions()
{
	CloseDialog();
	for (auto& Pair : ActionLabels) if (Pair.Value) Pair.Value->RemoveFromParent();
	ActionLabels.Reset();
	if (ConfirmationEntry) ConfirmationEntry->RemoveFromParent();
	ConfirmationEntry=nullptr;
	for (auto Root : {Dialog,CreditsRoot}) if (Root) if (auto Parent=Root->Parent.Pin()) Parent->RemoveChild(Root);
	Dialog.Reset(); CreditsRoot.Reset(); CreditsText.Reset(); CreditPictures.Reset();
	CreditText.Reset(); LastManagementError.Reset(); bCredits=false;
}
