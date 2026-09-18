#include "Components/Border.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIFontStyles.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUIElement.h"
#include "ACEClientSubsystem.h"
#include "ACEOpcodes.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Styling/CoreStyle.h"

namespace
{
	static const FLinearColor DlgGold(0.95f, 0.82f, 0.35f, 1.f);
	static const FLinearColor DlgWhite(0.92f, 0.92f, 0.88f, 1.f);
	static const FLinearColor DlgDim(0.72f, 0.72f, 0.68f, 1.f);

	static const TCHAR* AbuseIntro =
		TEXT("Use this form to report a player who is harassing you or otherwise\n")
		TEXT("abusing the terms of service.\n\n")
		TEXT("Click OK to continue, or Cancel to close this window.");

	static const TCHAR* AbuseNamePrompt =
		TEXT("Enter the name of the character you wish to report:");

	static const TCHAR* AbuseComplaintPrompt =
		TEXT("Describe the problem. Include as much detail as you can:");

	static const TCHAR* AbuseDone =
		TEXT("Your complaint has been submitted.\n\nThank you.");

	static const TCHAR* UrgentIntro =
		TEXT("Urgent Assistance is for situations that require immediate help\n")
		TEXT("from a Customer Service Representative.\n\n")
		TEXT("Click OK to continue, or Cancel to close this window.");

	static const TCHAR* UrgentComplaintPrompt =
		TEXT("Please describe your problem:");

	static const TCHAR* UrgentDone =
		TEXT("Your request has been noted.\n\n")
		TEXT("On most ACE servers there is no live CSR queue — contact your\n")
		TEXT("server's Discord or website if you still need help.");

	static UTextBlock* EnsureLabel(UWidgetTree* Tree, TArray<TObjectPtr<UTextBlock>>& Arr, int32 Index)
	{
		if (!Tree)
		{
			return nullptr;
		}
		while (Arr.Num() <= Index)
		{
			UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
			T->SetVisibility(ESlateVisibility::Collapsed);
			T->SetAutoWrapText(true);
			Arr.Add(T);
		}
		return Arr[Index];
	}

	static void StyleTransparentEntry(UEditableTextBox* Box)
	{
		if (!Box)
		{
			return;
		}
		FEditableTextBoxStyle Style = Box->GetWidgetStyle();
		Style.BackgroundImageNormal.DrawAs = ESlateBrushDrawType::NoDrawType;
		Style.BackgroundImageHovered.DrawAs = ESlateBrushDrawType::NoDrawType;
		Style.BackgroundImageFocused.DrawAs = ESlateBrushDrawType::NoDrawType;
		Style.BackgroundImageReadOnly.DrawAs = ESlateBrushDrawType::NoDrawType;
		Box->SetWidgetStyle(Style);
	}
}

void UACEUIGameplayBinder::HandleBookChanged()
{
	if (!Client)
	{
		return;
	}
	const FACEBookInfo Info = Client->GetBookInfo();
	if (Info.bOpen)
	{
		ShowPanelPage(TEXT("BookPanel_Field"));
	}
	RefreshBookOverlays();
}

void UACEUIGameplayBinder::EnsureDialogEntryBoxes()
{
	if (!Canvas || !Canvas->WidgetTree)
	{
		return;
	}
	UWidgetTree* Tree = Canvas->WidgetTree;
	if (!AbuseNameEntry)
	{
		AbuseNameEntry = Tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		AbuseNameEntry->SetVisibility(ESlateVisibility::Collapsed);
		AbuseNameEntry->SetHintText(FText::FromString(TEXT("Character name")));
		StyleTransparentEntry(AbuseNameEntry);
		AbuseNameEntry->SetClearKeyboardFocusOnCommit(true);
	}
	if (!AbuseComplaintEntry)
	{
		AbuseComplaintEntry = Tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		AbuseComplaintEntry->SetVisibility(ESlateVisibility::Collapsed);
		AbuseComplaintEntry->SetHintText(FText::FromString(TEXT("Describe the problem…")));
		StyleTransparentEntry(AbuseComplaintEntry);
		AbuseComplaintEntry->SetClearKeyboardFocusOnCommit(false);
	}
	if (!UrgentComplaintEntry)
	{
		UrgentComplaintEntry = Tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		UrgentComplaintEntry->SetVisibility(ESlateVisibility::Collapsed);
		UrgentComplaintEntry->SetHintText(FText::FromString(TEXT("Describe your problem…")));
		StyleTransparentEntry(UrgentComplaintEntry);
		UrgentComplaintEntry->SetClearKeyboardFocusOnCommit(false);
	}
	bDialogEntriesBound = true;
}

void UACEUIGameplayBinder::HideDialogOverlays()
{
	for (UTextBlock* T : AbuseTextLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UTextBlock* T : AbuseButtonLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UTextBlock* T : UrgentTextLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UTextBlock* T : UrgentButtonLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UTextBlock* T : MiniGameLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	if (AbuseNameEntry) { AbuseNameEntry->SetVisibility(ESlateVisibility::Collapsed); }
	if (AbuseComplaintEntry) { AbuseComplaintEntry->SetVisibility(ESlateVisibility::Collapsed); }
	if (UrgentComplaintEntry) { UrgentComplaintEntry->SetVisibility(ESlateVisibility::Collapsed); }
	if (BookPageTextLabel) { BookPageTextLabel->SetVisibility(ESlateVisibility::Collapsed); }
	if (BookScroll) { BookScroll->SetVisibility(ESlateVisibility::Collapsed); }
	if (BookTitleLabel) { BookTitleLabel->SetVisibility(ESlateVisibility::Collapsed); }
}

void UACEUIGameplayBinder::SyncAbusePanelPage(int32 PageIndex)
{
	if (!Manager)
	{
		return;
	}
	AbuseWizardPage = FMath::Clamp(PageIndex, 1, 3);
	static const TCHAR* Pages[] = {
		TEXT("Abuse_PageOne"), TEXT("Abuse_PageTwo"), TEXT("Abuse_PageThree")
	};
	for (int32 i = 0; i < 3; ++i)
	{
		if (TSharedPtr<FACEUIElement> El = Manager->FindElementUnder(TEXT("AbusePanel_Field"), Pages[i]))
		{
			El->bVisible = (AbuseWizardPage == i + 1);
		}
	}
	RefreshAbuseOverlays();
}

void UACEUIGameplayBinder::SyncUrgentPanelPage(int32 PageIndex)
{
	if (!Manager)
	{
		return;
	}
	UrgentWizardPage = FMath::Clamp(PageIndex, 1, 3);
	static const TCHAR* Pages[] = {
		TEXT("UrgentAssistance_PageOne"), TEXT("UrgentAssistance_PageTwo"),
		TEXT("UrgentAssistance_PageThree")
	};
	for (int32 i = 0; i < 3; ++i)
	{
		if (TSharedPtr<FACEUIElement> El = Manager->FindElementUnder(
			TEXT("UrgentAssistancePanel_Field"), Pages[i]))
		{
			El->bVisible = (UrgentWizardPage == i + 1);
		}
	}
	RefreshUrgentOverlays();
}

void UACEUIGameplayBinder::RefreshAbuseOverlays()
{
	if (ActivePanelPage != TEXT("AbusePanel_Field") || !Manager || !Canvas || !Canvas->WidgetTree)
	{
		for (UTextBlock* T : AbuseTextLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
		for (UTextBlock* T : AbuseButtonLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
		if (AbuseNameEntry) { AbuseNameEntry->SetVisibility(ESlateVisibility::Collapsed); }
		if (AbuseComplaintEntry) { AbuseComplaintEntry->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}
	EnsureOverlays();
	EnsureDialogEntryBoxes();
	UWidgetTree* Tree = Canvas->WidgetTree;

	PlaceTextOnElement(EnsureLabel(Tree, AbuseTextLabels, 0),
		Manager->FindElementUnder(TEXT("AbusePanel_Field"), TEXT("TitleText")),
		TEXT("Report Abuse"), 10, DlgGold, 700, true);

	if (AbuseNameEntry) { AbuseNameEntry->SetVisibility(ESlateVisibility::Collapsed); }
	if (AbuseComplaintEntry) { AbuseComplaintEntry->SetVisibility(ESlateVisibility::Collapsed); }

	if (AbuseWizardPage == 1)
	{
		PlaceTextOnElement(EnsureLabel(Tree, AbuseTextLabels, 1),
			Manager->FindElementUnder(TEXT("Abuse_PageOne"), TEXT("Abuse_PageOne_Text")),
			AbuseIntro, 9, DlgWhite, 701);
		PlaceTextOnElement(EnsureLabel(Tree, AbuseButtonLabels, 0),
			Manager->FindElementUnder(TEXT("Abuse_PageOne"), TEXT("Abuse_CancelButton")),
			TEXT("Cancel"), 9, DlgGold, 702, true);
		PlaceTextOnElement(EnsureLabel(Tree, AbuseButtonLabels, 1),
			Manager->FindElementUnder(TEXT("Abuse_PageOne"), TEXT("Abuse_PageOne_OKButton")),
			TEXT("OK"), 9, DlgGold, 702, true);
	}
	else if (AbuseWizardPage == 2)
	{
		PlaceTextOnElement(EnsureLabel(Tree, AbuseTextLabels, 1),
			Manager->FindElementUnder(TEXT("Abuse_PageTwo"), TEXT("Abuse_PageTwo_TextOne")),
			AbuseNamePrompt, 9, DlgWhite, 701);
		PlaceTextOnElement(EnsureLabel(Tree, AbuseTextLabels, 2),
			Manager->FindElementUnder(TEXT("Abuse_PageTwo"), TEXT("Abuse_PageTwo_TextTwo")),
			AbuseComplaintPrompt, 9, DlgWhite, 701);
		if (AbuseNameEntry)
		{
			if (TSharedPtr<FACEUIElement> El = Manager->FindElementUnder(
				TEXT("Abuse_PageTwo"), TEXT("Abuse_PageTwo_NameBox")))
			{
				AbuseNameEntry->SetVisibility(ESlateVisibility::Visible);
				Canvas->PlaceWidgetAtElement(AbuseNameEntry, El, 705, FMargin(2.f));
			}
		}
		if (AbuseComplaintEntry)
		{
			if (TSharedPtr<FACEUIElement> El = Manager->FindElementUnder(
				TEXT("Abuse_PageTwo"), TEXT("Abuse_PageTwo_EntryBox")))
			{
				AbuseComplaintEntry->SetVisibility(ESlateVisibility::Visible);
				Canvas->PlaceWidgetAtElement(AbuseComplaintEntry, El, 706, FMargin(2.f));
			}
		}
		PlaceTextOnElement(EnsureLabel(Tree, AbuseButtonLabels, 0),
			Manager->FindElementUnder(TEXT("Abuse_PageTwo"), TEXT("Abuse_CancelButton")),
			TEXT("Cancel"), 9, DlgGold, 702, true);
		PlaceTextOnElement(EnsureLabel(Tree, AbuseButtonLabels, 1),
			Manager->FindElementUnder(TEXT("Abuse_PageTwo"), TEXT("Abuse_PageTwo_ContinueButton")),
			TEXT("Continue"), 9, DlgGold, 702, true);
	}
	else
	{
		PlaceTextOnElement(EnsureLabel(Tree, AbuseTextLabels, 1),
			Manager->FindElementUnder(TEXT("Abuse_PageThree"), TEXT("Abuse_PageThree_Text")),
			AbuseDone, 9, DlgWhite, 701);
		PlaceTextOnElement(EnsureLabel(Tree, AbuseButtonLabels, 0),
			Manager->FindElementUnder(TEXT("Abuse_PageThree"), TEXT("Abuse_PageThree_DoneButton")),
			TEXT("Done"), 9, DlgGold, 702, true);
		if (AbuseButtonLabels.IsValidIndex(1) && AbuseButtonLabels[1])
		{
			AbuseButtonLabels[1]->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UACEUIGameplayBinder::RefreshUrgentOverlays()
{
	if (ActivePanelPage != TEXT("UrgentAssistancePanel_Field") || !Manager || !Canvas
		|| !Canvas->WidgetTree)
	{
		for (UTextBlock* T : UrgentTextLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
		for (UTextBlock* T : UrgentButtonLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
		if (UrgentComplaintEntry) { UrgentComplaintEntry->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}
	EnsureOverlays();
	EnsureDialogEntryBoxes();
	UWidgetTree* Tree = Canvas->WidgetTree;

	PlaceTextOnElement(EnsureLabel(Tree, UrgentTextLabels, 0),
		Manager->FindElementUnder(TEXT("UrgentAssistancePanel_Field"), TEXT("TitleText")),
		TEXT("Urgent Assistance"), 10, DlgGold, 710, true);

	if (UrgentComplaintEntry) { UrgentComplaintEntry->SetVisibility(ESlateVisibility::Collapsed); }

	if (UrgentWizardPage == 1)
	{
		PlaceTextOnElement(EnsureLabel(Tree, UrgentTextLabels, 1),
			Manager->FindElementUnder(TEXT("UrgentAssistance_PageOne"),
				TEXT("UrgentAssistance_PageOne_Text")),
			UrgentIntro, 9, DlgWhite, 711);
		PlaceTextOnElement(EnsureLabel(Tree, UrgentButtonLabels, 0),
			Manager->FindElementUnder(TEXT("UrgentAssistance_PageOne"),
				TEXT("UrgentAssistance_CancelButton")),
			TEXT("Cancel"), 9, DlgGold, 712, true);
		PlaceTextOnElement(EnsureLabel(Tree, UrgentButtonLabels, 1),
			Manager->FindElementUnder(TEXT("UrgentAssistance_PageOne"),
				TEXT("UrgentAssistance_PageOne_OKButton")),
			TEXT("OK"), 9, DlgGold, 712, true);
	}
	else if (UrgentWizardPage == 2)
	{
		PlaceTextOnElement(EnsureLabel(Tree, UrgentTextLabels, 1),
			Manager->FindElementUnder(TEXT("UrgentAssistance_PageTwo"),
				TEXT("UrgentAssistance_PageTwo_TextOne")),
			UrgentComplaintPrompt, 9, DlgWhite, 711);
		PlaceTextOnElement(EnsureLabel(Tree, UrgentTextLabels, 2),
			Manager->FindElementUnder(TEXT("UrgentAssistance_PageTwo"),
				TEXT("UrgentAssistance_PageTwo_TextTwo")),
			TEXT(""), 9, DlgWhite, 711);
		if (UrgentComplaintEntry)
		{
			if (TSharedPtr<FACEUIElement> El = Manager->FindElementUnder(
				TEXT("UrgentAssistance_PageTwo"), TEXT("UrgentAssistance_PageTwo_EntryBox")))
			{
				UrgentComplaintEntry->SetVisibility(ESlateVisibility::Visible);
				Canvas->PlaceWidgetAtElement(UrgentComplaintEntry, El, 715, FMargin(2.f));
			}
		}
		PlaceTextOnElement(EnsureLabel(Tree, UrgentButtonLabels, 0),
			Manager->FindElementUnder(TEXT("UrgentAssistance_PageTwo"),
				TEXT("UrgentAssistance_CancelButton")),
			TEXT("Cancel"), 9, DlgGold, 712, true);
		PlaceTextOnElement(EnsureLabel(Tree, UrgentButtonLabels, 1),
			Manager->FindElementUnder(TEXT("UrgentAssistance_PageTwo"),
				TEXT("UrgentAssistance_PageTwo_ContinueButton")),
			TEXT("Continue"), 9, DlgGold, 712, true);
	}
	else
	{
		PlaceTextOnElement(EnsureLabel(Tree, UrgentTextLabels, 1),
			Manager->FindElementUnder(TEXT("UrgentAssistance_PageThree"),
				TEXT("UrgentAssistance_PageThree_Text")),
			UrgentDone, 9, DlgWhite, 711);
		PlaceTextOnElement(EnsureLabel(Tree, UrgentButtonLabels, 0),
			Manager->FindElementUnder(TEXT("UrgentAssistance_PageThree"),
				TEXT("UrgentAssistance_PageThree_DoneButton")),
			TEXT("Done"), 9, DlgGold, 712, true);
		if (UrgentButtonLabels.IsValidIndex(1) && UrgentButtonLabels[1])
		{
			UrgentButtonLabels[1]->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UACEUIGameplayBinder::RequestBookPageIfNeeded(int32 PageIndex)
{
	if (!Client)
	{
		return;
	}
	const FACEBookInfo Info = Client->GetBookInfo();
	if (!Info.bOpen || Info.BookGuid == 0 || PageIndex < 0)
	{
		return;
	}
	// BookDataResponse leaves page text null — always fetch the visible page.
	Client->SendBookPageData(Info.BookGuid, PageIndex);
}

TSharedPtr<FACEUIElement> UACEUIGameplayBinder::GetBookScrollbar() const
{
	if (!Manager) return nullptr;
	// The book has its own parchment scrollbar template. Resolve the parent
	// with arrow children, not the nested draggable Scrollbar widget.
	TFunction<TSharedPtr<FACEUIElement>(TSharedPtr<FACEUIElement>)> Find;
	Find = [&](TSharedPtr<FACEUIElement> Node) -> TSharedPtr<FACEUIElement>
	{
		if (!Node) return nullptr;
		for (const auto& Child : Node->Children)
			if (Child && Child->ElementName == TEXT("ScrollBar_Up")) return Node;
		for (const auto& Child : Node->Children)
			if (auto Found = Find(Child)) return Found;
		return nullptr;
	};
	return Find(Manager->FindElementByName(TEXT("BookPanel_Field")));
}

void UACEUIGameplayBinder::RefreshBookOverlays()
{
	if (ActivePanelPage != TEXT("BookPanel_Field") || !Manager || !Canvas || !Canvas->WidgetTree
		|| !Client)
	{
		if (BookPageTextLabel) { BookPageTextLabel->SetVisibility(ESlateVisibility::Collapsed); }
		if (BookScroll) { BookScroll->SetVisibility(ESlateVisibility::Collapsed); }
		if (BookTitleLabel) { BookTitleLabel->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}
	EnsureOverlays();
	UWidgetTree* Tree = Canvas->WidgetTree;
	if (!BookPageTextLabel)
	{
		BookPageTextLabel = Tree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		BookPageTextLabel->SetVisibility(ESlateVisibility::Collapsed);
		BookPageTextLabel->SetAutoWrapText(true);
	}
	if (!BookTitleLabel)
	{
		BookTitleLabel = Tree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		BookTitleLabel->SetVisibility(ESlateVisibility::Collapsed);
	}

	const FACEBookInfo Info = Client->GetBookInfo();
	FString Title = TEXT("Book");
	if (!Info.AuthorName.IsEmpty())
	{
		Title = FString::Printf(TEXT("Book — %s"), *Info.AuthorName);
	}
	if (Info.NumPages > 0 || Info.MaxPages > 0)
	{
		const int32 Total = Info.NumPages > 0 ? Info.NumPages : Info.MaxPages;
		Title += FString::Printf(TEXT("  (%d / %d)"), Info.CurrentPage + 1, Total);
	}
	PlaceTextOnElement(BookTitleLabel,
		Manager->FindElementUnder(TEXT("BookPanel_Field"), TEXT("TitleText")),
		Title, 10, DlgGold, 720, true);

	FString Body;
	if (!Info.bOpen)
	{
		Body = TEXT("Double-click a book in your inventory to read it.");
	}
	else if (Info.Pages.IsValidIndex(Info.CurrentPage) && !Info.Pages[Info.CurrentPage].IsEmpty())
	{
		Body = Info.Pages[Info.CurrentPage];
	}
	else if (!Info.Inscription.IsEmpty() && Info.CurrentPage == 0)
	{
		Body = Info.Inscription;
	}
	else
	{
		Body = TEXT("(Loading page…)");
	}
	const auto TextEl = Manager->FindElementUnder(TEXT("BookPanel_Field"), TEXT("BookPageText"));
	if (!TextEl) return;
	if (!BookScroll)
	{
		BookScroll = Tree->ConstructWidget<UScrollBox>();
		BookScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
		BookScroll->SetClipping(EWidgetClipping::ClipToBounds);
		BookScroll->SetAnimateWheelScrolling(false);
		BookScroll->AddChild(BookPageTextLabel);
	}
	if (ScrolledBookGuid != Info.BookGuid || ScrolledBookPage != Info.CurrentPage)
	{
		BookScroll->SetScrollOffset(0.f);
		ScrolledBookGuid = Info.BookGuid; ScrolledBookPage = Info.CurrentPage;
	}
	BookPageTextLabel->SetText(FText::FromString(Body));
	BookPageTextLabel->SetColorAndOpacity(TextEl->TextColor.Get(FLinearColor::Black));
	BookPageTextLabel->SetFont(ACEUIFontStyles::MakeSlateFont(ACEUIFontStyles::ResolveForElement(TextEl,9)));
	BookPageTextLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (auto* Retail = Cast<UACERetailTextBlock>(BookPageTextLabel))
		Retail->SetRetailElement(Canvas->GetResourceResolver(), TextEl, Canvas->GetLastScale2D(), TextEl->Width, false);
	BookScroll->SetVisibility(ESlateVisibility::Visible);
	Canvas->PlaceWidgetAtElement(BookScroll, TextEl, 721);
	const float Max = BookScroll->GetScrollOffsetOfEnd();
	SyncDatScrollbar(GetBookScrollbar(), Max > 0.f ? BookScroll->GetScrollOffset()/Max : 0.f,
		TextEl->Height/FMath::Max(1.f, Max + TextEl->Height));
}

namespace
{
	// GameBoardGrid::GetPieceDID / UI_MiniGame_PieceIconArray.
	// Our board codes are P,N,B,R,Q,K; the DAT array is P,B,N,R,Q,K.
	constexpr int32 ChessIconOffsets[] = {0, 0, 2, 1, 3, 4, 5};
}

void UACEUIGameplayBinder::ResetChessBoard()
{
	FMemory::Memzero(ChessBoard, sizeof(ChessBoard));
	// White back rank at y=0 (ACE ChessPieceCoord: Rank = Y + 1).
	static const int8 BackRank[8] = { 4, 2, 3, 5, 6, 3, 2, 4 };
	for (int32 X = 0; X < 8; ++X)
	{
		ChessBoard[0 * 8 + X] = BackRank[X];
		ChessBoard[1 * 8 + X] = 1;
		ChessBoard[6 * 8 + X] = -1;
		ChessBoard[7 * 8 + X] = -BackRank[X];
	}
	ChessSelX = ChessSelY = -1;
	ChessPendingFromX = ChessPendingFromY = ChessPendingToX = ChessPendingToY = -1;
}

void UACEUIGameplayBinder::ApplyChessMove(int32 FromX, int32 FromY, int32 ToX, int32 ToY)
{
	if (FromX < 0 || FromX > 7 || FromY < 0 || FromY > 7
		|| ToX < 0 || ToX > 7 || ToY < 0 || ToY > 7)
	{
		return;
	}
	const int8 Piece = ChessBoard[FromY * 8 + FromX];
	const int8 Dst = ChessBoard[ToY * 8 + ToX];
	// En passant: pawn moves diagonally onto an empty square — remove the passed pawn.
	if (FMath::Abs(Piece) == 1 && FromX != ToX && Dst == 0)
	{
		ChessBoard[FromY * 8 + ToX] = 0;
	}
	ChessBoard[ToY * 8 + ToX] = Piece;
	ChessBoard[FromY * 8 + FromX] = 0;
	// Castling: king moves two files — bring the rook across.
	if (FMath::Abs(Piece) == 6 && FMath::Abs(ToX - FromX) == 2)
	{
		const int32 RookFromX = ToX > FromX ? 7 : 0;
		const int32 RookToX = ToX > FromX ? ToX - 1 : ToX + 1;
		ChessBoard[ToY * 8 + RookToX] = ChessBoard[ToY * 8 + RookFromX];
		ChessBoard[ToY * 8 + RookFromX] = 0;
	}
	// Promotion (ACE auto-queens).
	if (Piece == 1 && ToY == 7)
	{
		ChessBoard[ToY * 8 + ToX] = 5;
	}
	else if (Piece == -1 && ToY == 0)
	{
		ChessBoard[ToY * 8 + ToX] = -5;
	}
	ChessTurnColor = ChessTurnColor == 0 ? 1 : 0;
}

void UACEUIGameplayBinder::HandleChessEvent(const FACEChessEvent& Event)
{
	switch (static_cast<uint32>(Event.EventType))
	{
	case ACEGameEvent::ChessJoinGameResponse:
		if (Event.Value < 0)
		{
			AppendLocalChatLine(TEXT("You cannot join that game."), ACEChatMessageType::System);
			break;
		}
		bChessActive = true;
		ChessBoardGuid = Event.BoardGuid;
		ChessMyColor = Event.Value;
		ChessTurnColor = 0;
		ResetChessBoard();
		AppendLocalChatLine(FString::Printf(TEXT("You join the chess game as %s."),
			Event.Value == 0 ? TEXT("White") : TEXT("Black")), ACEChatMessageType::System);
		ShowPanelPage(TEXT("MiniGamePanel_Field"));
		break;
	case ACEGameEvent::ChessStartGame:
		bChessActive = true;
		ChessBoardGuid = Event.BoardGuid;
		ChessTurnColor = Event.Value;
		ResetChessBoard();
		AppendLocalChatLine(TEXT("The chess game begins!"), ACEChatMessageType::System);
		break;
	case ACEGameEvent::ChessMoveResponse:
		if (Event.Value > 0)
		{
			// OKMove* — commit the pending move to the local board mirror.
			if (ChessPendingFromX >= 0)
			{
				ApplyChessMove(ChessPendingFromX, ChessPendingFromY,
					ChessPendingToX, ChessPendingToY);
			}
			if (Event.Value & 0x0800)
			{
				AppendLocalChatLine(TEXT("Checkmate!"), ACEChatMessageType::System);
			}
			else if (Event.Value & 0x0400)
			{
				AppendLocalChatLine(TEXT("Check!"), ACEChatMessageType::System);
			}
		}
		else
		{
			const TCHAR* Reason = TEXT("That move is invalid.");
			switch (Event.Value)
			{
			case -3: Reason = TEXT("It's not your turn."); break;
			case -100: Reason = TEXT("The selected piece cannot move that direction."); break;
			case -101: Reason = TEXT("The selected piece cannot move that far."); break;
			case -102: Reason = TEXT("You tried to move an empty square."); break;
			case -103: Reason = TEXT("The selected piece is not yours."); break;
			default: break;
			}
			AppendLocalChatLine(Reason, ACEChatMessageType::ChatError);
		}
		ChessPendingFromX = ChessPendingFromY = ChessPendingToX = ChessPendingToY = -1;
		break;
	case ACEGameEvent::ChessOpponentTurn:
		if (Event.MoveType == 5) // FromTo
		{
			ApplyChessMove(Event.FromX, Event.FromY, Event.ToX, Event.ToY);
		}
		else if (Event.MoveType == 1) // Pass
		{
			AppendLocalChatLine(TEXT("Your opponent passes."), ACEChatMessageType::System);
			ChessTurnColor = ChessTurnColor == 0 ? 1 : 0;
		}
		break;
	case ACEGameEvent::ChessOpponentStalemate:
		AppendLocalChatLine(Event.Value != 0
			? TEXT("Your opponent offers a stalemate. Click Stalemate to accept.")
			: TEXT("Your opponent retracts the stalemate offer."), ACEChatMessageType::System);
		break;
	case ACEGameEvent::ChessGameOver:
		if (Event.Value < 0)
		{
			AppendLocalChatLine(TEXT("The chess game ends in a draw."), ACEChatMessageType::System);
		}
		else
		{
			AppendLocalChatLine(Event.Value == ChessMyColor
				? TEXT("You win the chess game!")
				: TEXT("You lose the chess game."), ACEChatMessageType::System);
		}
		bChessActive = false;
		ChessMyColor = -1;
		ChessSelX = ChessSelY = -1;
		break;
	default:
		break;
	}
	RefreshMiniGameOverlays();
}

bool UACEUIGameplayBinder::TryHandleChessBoardClick(FVector2D CanvasLocalPos)
{
	if (!bChessActive || ActivePanelPage != TEXT("MiniGamePanel_Field") || !Manager || !Canvas)
	{
		return false;
	}
	TSharedPtr<FACEUIElement> BoardEl = Manager->FindElementUnder(
		TEXT("MiniGamePanel_Field"), TEXT("MiniGame_PieceListBox"));
	if (!BoardEl.IsValid() || !BoardEl->bVisible)
	{
		return false;
	}
	const FIntPoint Origin = BoardEl->GetScreenOrigin();
	const float SX = FMath::Max(KINDA_SMALL_NUMBER, Canvas->GetLastScaleX());
	const float SY = FMath::Max(KINDA_SMALL_NUMBER, Canvas->GetLastScaleY());
	const int32 RelX = FMath::FloorToInt(CanvasLocalPos.X / SX) - Origin.X;
	const int32 RelY = FMath::FloorToInt(CanvasLocalPos.Y / SY) - Origin.Y;
	if (RelX < 0 || RelY < 0 || RelX >= BoardEl->Width || RelY >= BoardEl->Height)
	{
		return false;
	}
	const int32 Col = FMath::Clamp(RelX * 8 / FMath::Max(1, BoardEl->Width), 0, 7);
	const int32 RowFromTop = FMath::Clamp(RelY * 8 / FMath::Max(1, BoardEl->Height), 0, 7);
	// Own color at the bottom: white ranks ascend upward, black view is mirrored.
	const int32 X = ChessMyColor == 1 ? 7 - Col : Col;
	const int32 Y = ChessMyColor == 1 ? RowFromTop : 7 - RowFromTop;
	const int8 Piece = ChessBoard[Y * 8 + X];
	const bool bMine = Piece != 0 && ((Piece > 0) == (ChessMyColor == 0));
	if (ChessSelX < 0)
	{
		if (bMine)
		{
			ChessSelX = X;
			ChessSelY = Y;
			RefreshMiniGameOverlays();
		}
		return true;
	}
	if (X == ChessSelX && Y == ChessSelY)
	{
		// Clicking the selection again clears it.
		ChessSelX = ChessSelY = -1;
		RefreshMiniGameOverlays();
		return true;
	}
	if (bMine)
	{
		// Re-select a different piece.
		ChessSelX = X;
		ChessSelY = Y;
		RefreshMiniGameOverlays();
		return true;
	}
	if (Client)
	{
		ChessPendingFromX = ChessSelX;
		ChessPendingFromY = ChessSelY;
		ChessPendingToX = X;
		ChessPendingToY = Y;
		Client->SendChessMove(ChessSelX, ChessSelY, X, Y);
	}
	ChessSelX = ChessSelY = -1;
	RefreshMiniGameOverlays();
	return true;
}

void UACEUIGameplayBinder::RefreshMiniGameOverlays()
{
	if (ActivePanelPage != TEXT("MiniGamePanel_Field") || !Manager || !Canvas || !Canvas->WidgetTree)
	{
		for (UTextBlock* T : MiniGameLabels) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
		for (UBorder* T : ChessSelectionIcons) { if (T) T->SetVisibility(ESlateVisibility::Collapsed); }
		for (UBorder* T : ChessSquareIcons) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
		return;
	}
	EnsureOverlays();
	UWidgetTree* Tree = Canvas->WidgetTree;
	PlaceTextOnElement(EnsureLabel(Tree, MiniGameLabels, 0),
		Manager->FindElementUnder(TEXT("MiniGamePanel_Field"), TEXT("MiniGame_TitleText")),
		TEXT("Game Center"), 10, DlgGold, 730, true);
	PlaceTextOnElement(EnsureLabel(Tree, MiniGameLabels, 1),
		Manager->FindElementUnder(TEXT("MiniGamePanel_Field"), TEXT("MiniGame_Resign")),
		TEXT("Resign"), 9, DlgGold, 731, true);
	// Retail hides Pass for chess.
	if (auto Pass = Manager->FindElementUnder(TEXT("MiniGamePanel_Field"), TEXT("MiniGame_Pass")))
		Pass->bVisible = false;
	if (MiniGameLabels.IsValidIndex(2) && MiniGameLabels[2]) MiniGameLabels[2]->SetVisibility(ESlateVisibility::Collapsed);
	PlaceTextOnElement(EnsureLabel(Tree, MiniGameLabels, 3),
		Manager->FindElementUnder(TEXT("MiniGamePanel_Field"), TEXT("MiniGame_Stalemate")),
		TEXT("Stalemate"), 9, DlgGold, 731, true);

	TSharedPtr<FACEUIElement> BoardEl = Manager->FindElementUnder(
		TEXT("MiniGamePanel_Field"), TEXT("MiniGame_PieceListBox"));
	UTextBlock* Hint = EnsureLabel(Tree, MiniGameLabels, 4);
	if (!bChessActive || !BoardEl.IsValid())
	{
		PlaceTextOnElement(Hint, BoardEl,
			TEXT("Double-click a chessboard in the world to join a game.\n")
			TEXT("Board pieces will appear here once joined."),
			8, DlgDim, 732);
		for (UBorder* T : ChessSelectionIcons) { if (T) T->SetVisibility(ESlateVisibility::Collapsed); }
		for (UBorder* T : ChessSquareIcons) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
		return;
	}
	if (Hint)
	{
		Hint->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (MiniGameLabels.IsValidIndex(5) && MiniGameLabels[5]) MiniGameLabels[5]->SetVisibility(ESlateVisibility::Collapsed);
	for (UBorder* T : ChessSelectionIcons) if (T) T->SetVisibility(ESlateVisibility::Collapsed);
	while (ChessSquareIcons.Num() < 64) EnsureIconBorder(ChessSquareIcons, ChessSquareIcons.Num());
	const FIntPoint Origin = BoardEl->GetScreenOrigin();
	const float SX = FMath::Max(KINDA_SMALL_NUMBER, Canvas->GetLastScaleX());
	const float SY = FMath::Max(KINDA_SMALL_NUMBER, Canvas->GetLastScaleY());
	const float CellW = static_cast<float>(BoardEl->Width) / 8.f;
	const float CellH = static_cast<float>(BoardEl->Height) / 8.f;
	for (int32 RowFromTop = 0; RowFromTop < 8; ++RowFromTop)
	{
		for (int32 Col = 0; Col < 8; ++Col)
		{
			const int32 X = ChessMyColor == 1 ? 7 - Col : Col;
			const int32 Y = ChessMyColor == 1 ? RowFromTop : 7 - RowFromTop;
			UBorder* T = ChessSquareIcons[RowFromTop * 8 + Col];
			const int8 Piece = ChessBoard[Y * 8 + X];
			if (Piece == 0)
			{
				T->SetVisibility(ESlateVisibility::Collapsed);
				continue;
			}
			const int32 Kind = FMath::Clamp<int32>(FMath::Abs(Piece), 1, 6);
			SetIconDid(T, 0x06004CFD + ChessIconOffsets[Kind] + (Piece < 0 ? 6 : 0));
			T->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (T->GetParent() != Canvas->GetElementLayer())
			{
				Canvas->GetElementLayer()->AddChild(T);
			}
			if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(T->Slot))
			{
				Slot->SetAnchors(FAnchors(0.f, 0.f));
				Slot->SetAlignment(FVector2D(0.f, 0.f));
				Slot->SetAutoSize(false);
				Slot->SetPosition(FVector2D(
					(static_cast<float>(Origin.X) + Col * CellW) * SX,
					(static_cast<float>(Origin.Y) + RowFromTop * CellH) * SY));
				Slot->SetSize(FVector2D(CellW * SX, CellH * SY));
				Canvas->SetOverlayOrder(T, Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field")), 733);
				if (X == ChessSelX && Y == ChessSelY)
				{
					UBorder* Selected = EnsureIconBorder(ChessSelectionIcons, 0);
					SetIconDid(Selected, 0x06004D09);
					Selected->SetVisibility(ESlateVisibility::HitTestInvisible);
					if (Selected->GetParent() != Canvas->GetElementLayer()) Canvas->GetElementLayer()->AddChild(Selected);
					if (auto* SelectedSlot = Cast<UCanvasPanelSlot>(Selected->Slot))
					{
						SelectedSlot->SetPosition(Slot->GetPosition()); SelectedSlot->SetSize(Slot->GetSize());
						Canvas->SetOverlayOrder(Selected, Manager->FindElementByName(TEXT("RootGameplay_FloatyPanel_Field")), 734);
					}
				}

			}
		}
	}
}

bool UACEUIGameplayBinder::HandleDialogNamedClick(const FString& Name)
{
	// —— Abuse ——
	if (Name == TEXT("Abuse_CancelButton") || Name == TEXT("Abuse_PageThree_DoneButton"))
	{
		HidePanel();
		return true;
	}
	if (Name == TEXT("Abuse_PageOne_OKButton"))
	{
		SyncAbusePanelPage(2);
		return true;
	}
	if (Name == TEXT("Abuse_PageTwo_ContinueButton"))
	{
		const FString CharName = AbuseNameEntry
			? AbuseNameEntry->GetText().ToString().TrimStartAndEnd() : FString();
		const FString Complaint = AbuseComplaintEntry
			? AbuseComplaintEntry->GetText().ToString().TrimStartAndEnd() : FString();
		if (CharName.IsEmpty())
		{
			PostInventorySystemMessage(TEXT("Enter the character name to report."));
			return true;
		}
		if (Complaint.IsEmpty())
		{
			PostInventorySystemMessage(TEXT("Enter a description of the problem."));
			return true;
		}
		if (Client)
		{
			Client->SendAbuseLogRequest(CharName, 0, Complaint);
		}
		SyncAbusePanelPage(3);
		return true;
	}

	// —— Urgent Assistance ——
	if (Name == TEXT("UrgentAssistance_CancelButton")
		|| Name == TEXT("UrgentAssistance_PageThree_DoneButton"))
	{
		HidePanel();
		return true;
	}
	if (Name == TEXT("UrgentAssistance_PageOne_OKButton"))
	{
		SyncUrgentPanelPage(2);
		return true;
	}
	if (Name == TEXT("UrgentAssistance_PageTwo_ContinueButton"))
	{
		const FString Complaint = UrgentComplaintEntry
			? UrgentComplaintEntry->GetText().ToString().TrimStartAndEnd() : FString();
		if (Complaint.IsEmpty())
		{
			PostInventorySystemMessage(TEXT("Please describe your problem."));
			return true;
		}
		if (Client)
		{
			// Empty character name + status bit marks this as an assistance request.
			Client->SendAbuseLogRequest(FString(), 1, Complaint);
		}
		SyncUrgentPanelPage(3);
		return true;
	}

	// —— Book ——
	if (Name == TEXT("CloseBookPanelButton"))
	{
		if (Client) { Client->ClearBook(); }
		HidePanel();
		return true;
	}
	if (Name == TEXT("BookPreviousButton") && Client)
	{
		const FACEBookInfo Info = Client->GetBookInfo();
		if (Info.bOpen && Info.CurrentPage > 0)
		{
			Client->SendBookPageData(Info.BookGuid, Info.CurrentPage - 1);
		}
		RefreshBookOverlays();
		return true;
	}
	if (Name == TEXT("BookNextButton") && Client)
	{
		const FACEBookInfo Info = Client->GetBookInfo();
		const int32 Total = Info.NumPages > 0 ? Info.NumPages : Info.MaxPages;
		if (Info.bOpen && Info.CurrentPage + 1 < Total)
		{
			Client->SendBookPageData(Info.BookGuid, Info.CurrentPage + 1);
		}
		RefreshBookOverlays();
		return true;
	}

	// —— MiniGame / Chess ——
	if (Name == TEXT("MiniGame_ClosePanelButton"))
	{
		HidePanel();
		return true;
	}
	if (Name == TEXT("MiniGame_Resign"))
	{
		if (Client) { Client->SendChessQuit(); }
		bChessActive = false;
		ChessMyColor = -1;
		ChessSelX = ChessSelY = -1;
		RefreshMiniGameOverlays();
		PostInventorySystemMessage(TEXT("You resign the chess game."));
		return true;
	}
	if (Name == TEXT("MiniGame_Pass"))
	{
		if (Client) { Client->SendChessMovePass(); }
		if (bChessActive && ChessTurnColor == ChessMyColor)
		{
			ChessTurnColor = ChessTurnColor == 0 ? 1 : 0;
			RefreshMiniGameOverlays();
		}
		PostInventorySystemMessage(TEXT("You pass your chess turn."));
		return true;
	}
	if (Name == TEXT("MiniGame_Stalemate"))
	{
		if (Client) { Client->SendChessStalemate(true); }
		PostInventorySystemMessage(TEXT("You offer a stalemate."));
		return true;
	}
	return false;
}
