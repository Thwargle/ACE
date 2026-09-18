#include "ACELoginWidget.h"
#include "UI/ACERetailTextBlock.h"
#include "ACELoginSettings.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEDatSubsystem.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/ScrollBox.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Widgets/SWidget.h"

void UACECharacterRowButton::HandleClicked()
{
	if (UACELoginWidget* Owner = OwnerWidget.Get())
	{
		Owner->SelectCharacterIndex(CharacterIndex);
	}
}

namespace
{
	// Subdued, modern palette — no neon diagnostic colors.
	const FLinearColor ACEBackdropColor(0.02f, 0.02f, 0.03f, 1.f);
	const FLinearColor ACEPanelColor(0.09f, 0.10f, 0.13f, 0.97f);
	const FLinearColor ACERowColor(0.14f, 0.15f, 0.19f, 1.f);
	const FLinearColor ACERowSelectedColor(0.20f, 0.38f, 0.62f, 1.f);
	const FLinearColor ACEAccentColor(0.25f, 0.5f, 0.85f, 1.f);
	const FLinearColor ACETextColor(0.88f, 0.89f, 0.92f, 1.f);
	const FLinearColor ACELabelColor(0.62f, 0.64f, 0.7f, 1.f);
}

TSharedRef<SWidget> UACELoginWidget::RebuildWidget()
{
	// Must run BEFORE Super::RebuildWidget() — see header comment. This is what actually fixes
	// the "CachedGeometry=1280x720 but DesiredSize=0,0 and nothing paints" symptom: without this,
	// the first-ever RebuildWidget() call captures WidgetTree->RootWidget while it's still null
	// (EnsureDefaultLayout() used to only run from NativeConstruct(), which fires AFTER
	// RebuildWidget()), permanently locking this widget's real content to an invisible SSpacer.
	EnsureDefaultLayout();
	return Super::RebuildWidget();
}

void UACELoginWidget::EnsureDefaultLayout()
{
	if (HostBox || !WidgetTree)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("ACE: UACELoginWidget::EnsureDefaultLayout — building default Slate layout"));

	// Make sure the widget itself can receive focus/input and is never accidentally
	// Collapsed/Hidden by inherited defaults.
	SetIsFocusable(true);
	SetVisibility(ESlateVisibility::Visible);

	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = Canvas;
	// SelfHitTestInvisible (not just Visible) so the root itself never blocks hit-testing to its
	// children, while still participating fully in layout/paint. Explicitly Visible here would
	// also work for painting, but SelfHitTestInvisible is correct for a pure layout container.
	Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Opaque backdrop — credentials screen must not show the level, sky, or default pawn.
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	{
		FSlateBrush BackdropBrush;
		BackdropBrush.DrawAs = ESlateBrushDrawType::Box;
		BackdropBrush.TintColor = FSlateColor(ACEBackdropColor);
		Backdrop->SetBrush(BackdropBrush);
		Backdrop->SetBrushColor(ACEBackdropColor);
		Backdrop->SetVisibility(ESlateVisibility::Visible);
	}
	if (UCanvasPanelSlot* BackdropSlot = Canvas->AddChildToCanvas(Backdrop))
	{
		BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackdropSlot->SetOffsets(FMargin(0.f));
	}
	BackdropBorder = Backdrop;
	if (bUseDatIntroBackdrop)
	{
		Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
	}

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("LoginPanel"));
	Panel->SetPadding(FMargin(28.f));
	{
		FSlateBrush PanelBrush;
		PanelBrush.DrawAs = ESlateBrushDrawType::Box;
		PanelBrush.TintColor = FSlateColor(ACEPanelColor);
		Panel->SetBrush(PanelBrush);
		Panel->SetBrushColor(ACEPanelColor);
		Panel->SetVisibility(ESlateVisibility::Visible);
	}

	// AutoSize (rather than a fixed footprint) so the card hugs its actual content instead of
	// leaving dead space or clipping/overflowing it — safe now that EnsureDefaultLayout() runs
	// before the first RebuildWidget() capture (see header comment), so content desired-size is
	// always real by the time layout happens.
	if (UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		PanelSlot->SetAutoSize(true);
	}

	// Fixed comfortable width; height follows content (title/fields/status/character list).
	USizeBox* PanelSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelSizeBox"));
	PanelSizeBox->SetWidthOverride(440.f);
	PanelSizeBox->SetVisibility(ESlateVisibility::Visible);
	Panel->AddChild(PanelSizeBox);

	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RootBox"));
	Root->SetVisibility(ESlateVisibility::Visible);
	PanelSizeBox->AddChild(Root);

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass(), TEXT("Title"));
	Title->SetText(FText::FromString(TEXT("Login")));
	Title->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Title->SetVisibility(ESlateVisibility::Visible);
	{
		FSlateFontInfo TitleFont = Title->GetFont();
		TitleFont.Size = 26;
		Title->SetFont(TitleFont);
	}
	if (UVerticalBoxSlot* TitleSlot = Root->AddChildToVerticalBox(Title))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));
	}

	auto AddLabel = [&](const FString& Label)
	{
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		T->SetText(FText::FromString(Label));
		T->SetColorAndOpacity(FSlateColor(ACELabelColor));
		T->SetVisibility(ESlateVisibility::Visible);
		{
			FSlateFontInfo LabelFont = T->GetFont();
			LabelFont.Size = 13;
			T->SetFont(LabelFont);
		}
		if (UVerticalBoxSlot* LabelSlot = Root->AddChildToVerticalBox(T))
		{
			LabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
		}
	};

	auto AddTextBox = [&](TObjectPtr<UEditableTextBox>& OutBox, const FString& Name, bool bPassword = false)
	{
		OutBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), *Name);
		if (bPassword)
		{
			OutBox->SetIsPassword(true);
		}
		OutBox->SetVisibility(ESlateVisibility::Visible);
		if (UVerticalBoxSlot* FieldSlot = Root->AddChildToVerticalBox(OutBox))
		{
			FieldSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
		}
	};

	AddLabel(TEXT("Server"));
	AddTextBox(HostBox, TEXT("HostBox"));

	AddLabel(TEXT("Port"));
	AddTextBox(PortBox, TEXT("PortBox"));

	AddLabel(TEXT("Username"));
	AddTextBox(AccountBox, TEXT("AccountBox"));

	AddLabel(TEXT("Password"));
	AddTextBox(PasswordBox, TEXT("PasswordBox"), true);

	auto StyleButton = [](UButton* Button, const FLinearColor& BaseColor)
	{
		FButtonStyle Style = Button->GetStyle();
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.TintColor = FSlateColor(BaseColor);
		Style.Normal = Brush;
		Style.Hovered = Brush;
		Style.Hovered.TintColor = FSlateColor(BaseColor * 1.15f);
		Style.Pressed = Brush;
		Style.Pressed.TintColor = FSlateColor(BaseColor * 0.85f);
		Style.Disabled = Brush;
		Style.Disabled.TintColor = FSlateColor(FLinearColor(0.25f, 0.25f, 0.28f, 1.f));
		Button->SetStyle(Style);
	};

	UHorizontalBox* ButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ButtonRow"));
	ButtonRow->SetVisibility(ESlateVisibility::Visible);
	if (UVerticalBoxSlot* ButtonRowSlot = Root->AddChildToVerticalBox(ButtonRow))
	{
		ButtonRowSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 16.f));
	}

	auto MakeButton = [&](const FString& Name, const FString& Label, const FLinearColor& Color) -> UButton*
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), *Name);
		Button->SetVisibility(ESlateVisibility::Visible);
		StyleButton(Button, Color);
		USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		SizeBox->SetMinDesiredHeight(36.f);
		SizeBox->SetVisibility(ESlateVisibility::Visible);
		UTextBlock* LabelText = WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		LabelText->SetText(FText::FromString(Label));
		LabelText->SetJustification(ETextJustify::Center);
		LabelText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		LabelText->SetVisibility(ESlateVisibility::Visible);
		SizeBox->AddChild(LabelText);
		Button->AddChild(SizeBox);
		if (UHorizontalBoxSlot* HSlot = ButtonRow->AddChildToHorizontalBox(Button))
		{
			HSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
			HSlot->SetVerticalAlignment(VAlign_Fill);
		}
		return Button;
	};

	LoginButton = MakeButton(TEXT("LoginButton"), TEXT("Login"), ACEAccentColor);
	// Character select is the retail DAT screen after auth — keep Enter World off this card.
	EnterWorldButton = MakeButton(TEXT("EnterWorldButton"), TEXT("Enter World"), FLinearColor(0.2f, 0.55f, 0.3f, 1.f));
	EnterWorldButton->SetVisibility(ESlateVisibility::Collapsed);
	if (UHorizontalBoxSlot* LastSlot = Cast<UHorizontalBoxSlot>(EnterWorldButton->Slot))
	{
		LastSlot->SetPadding(FMargin(0.f));
	}

	// Status text lives in its own fixed-height, wrapping/scrollable area so long messages never
	// overflow or stretch the card — matches the "Status text in its own area" requirement.
	UScrollBox* StatusScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("StatusScroll"));
	StatusScroll->SetVisibility(ESlateVisibility::Visible);
	StatusScroll->SetOrientation(Orient_Vertical);
	StatusScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
	{
		USizeBox* StatusSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		StatusSizeBox->SetMaxDesiredHeight(64.f);
		StatusSizeBox->SetVisibility(ESlateVisibility::Visible);
		StatusText = WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass(), TEXT("StatusText"));
		StatusText->SetColorAndOpacity(FSlateColor(ACETextColor));
		StatusText->SetAutoWrapText(true);
		StatusText->SetVisibility(ESlateVisibility::Visible);
		StatusSizeBox->AddChild(StatusText);
		StatusScroll->AddChild(StatusSizeBox);
	}
	if (UVerticalBoxSlot* StatusSlot = Root->AddChildToVerticalBox(StatusScroll))
	{
		StatusSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	}

	CharacterListLabel = WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass(), TEXT("CharacterListLabel"));
	CharacterListLabel->SetColorAndOpacity(FSlateColor(ACELabelColor));
	CharacterListLabel->SetVisibility(ESlateVisibility::Collapsed);
	{
		FSlateFontInfo LabelFont = CharacterListLabel->GetFont();
		LabelFont.Size = 13;
		CharacterListLabel->SetFont(LabelFont);
	}
	if (UVerticalBoxSlot* CharacterListLabelSlot = Root->AddChildToVerticalBox(CharacterListLabel))
	{
		CharacterListLabelSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
	}

	// Clickable character rows, capped and scrollable so a long character list never grows the
	// card past a reasonable height.
	CharacterListSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CharacterListSizeBox"));
	CharacterListSizeBox->SetMaxDesiredHeight(220.f);
	CharacterListSizeBox->SetVisibility(ESlateVisibility::Collapsed);

	CharacterListBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("CharacterListBox"));
	CharacterListBox->SetVisibility(ESlateVisibility::Visible);
	CharacterListBox->SetOrientation(Orient_Vertical);
	CharacterListSizeBox->AddChild(CharacterListBox);

	Root->AddChildToVerticalBox(CharacterListSizeBox);

	int32 WidgetCount = 0;
	WidgetTree->ForEachWidget([&WidgetCount](UWidget*) { ++WidgetCount; });
	UE_LOG(LogTemp, Warning,
		TEXT("ACE: UACELoginWidget::EnsureDefaultLayout — built RootWidget=%s WidgetCount=%d BackdropBrush(DrawAs=%d TintA=%.2f) PanelBrush(DrawAs=%d TintA=%.2f)"),
		*WidgetTree->RootWidget->GetClass()->GetName(),
		WidgetCount,
		static_cast<int32>(Backdrop->Background.DrawAs.GetValue()),
		Backdrop->Background.TintColor.GetSpecifiedColor().A,
		static_cast<int32>(Panel->Background.DrawAs.GetValue()),
		Panel->Background.TintColor.GetSpecifiedColor().A);

	// Make sure nothing cached from before this rebuild (e.g. a stale invalidation box from the
	// previous SSpacer-only tree) lingers around now that real content exists.
	InvalidateLayoutAndVolatility();
}

void UACELoginWidget::NativeConstruct()
{
	UE_LOG(LogTemp, Warning, TEXT("ACE: UACELoginWidget::NativeConstruct"));
	EnsureDefaultLayout();
	Super::NativeConstruct();

	if (UGameInstance* GI = GetGameInstance())
	{
		Client = GI->GetSubsystem<UACEClientSubsystem>();
		if (Client)
		{
			// Slate can reconstruct this widget when it moves from the viewport into a VR panel.
			Client->OnSessionStateChanged.AddUniqueDynamic(this, &UACELoginWidget::HandleState);
			Client->OnCharacterList.AddUniqueDynamic(this, &UACELoginWidget::HandleCharacters);
			Client->OnLogMessage.AddUniqueDynamic(this, &UACELoginWidget::HandleLog);
		}
	}

	if (!bLoginSettingsLoaded)
	{
		FACELoginSettings Saved;
		Saved.Host = DefaultHost;
#if PLATFORM_ANDROID
		// A standalone headset reaches the development server over the LAN.
		if (Saved.Host.IsEmpty() || Saved.Host == TEXT("localhost") || Saved.Host == TEXT("127.0.0.1"))
			Saved.Host = TEXT("10.0.0.26");
#endif
		Saved.Port = FString::FromInt(DefaultPort);
		Saved.Account = DefaultAccount;
		Saved.Password = DefaultPassword;
		ACELoginSettings::Load(Saved);
		if (HostBox && HostBox->GetText().IsEmpty()) HostBox->SetText(FText::FromString(Saved.Host));
		if (PortBox && PortBox->GetText().IsEmpty()) PortBox->SetText(FText::FromString(Saved.Port));
		if (AccountBox && AccountBox->GetText().IsEmpty()) AccountBox->SetText(FText::FromString(Saved.Account));
		if (PasswordBox && PasswordBox->GetText().IsEmpty()) PasswordBox->SetText(FText::FromString(Saved.Password));
		bLoginSettingsLoaded = true;
	}
	for (UEditableTextBox* Box : {HostBox.Get(), PortBox.Get(), AccountBox.Get(), PasswordBox.Get()})
	{
		if (!Box) continue;
		Box->OnTextChanged.AddUniqueDynamic(this, &UACELoginWidget::OnLoginEntryChanged);
		Box->OnTextCommitted.AddUniqueDynamic(this, &UACELoginWidget::OnLoginEntryCommitted);
	}

	if (LoginButton)
	{
		LoginButton->OnClicked.AddUniqueDynamic(this, &UACELoginWidget::OnLoginClicked);
	}
	if (EnterWorldButton)
	{
		EnterWorldButton->OnClicked.AddUniqueDynamic(this, &UACELoginWidget::OnEnterWorldClicked);
		EnterWorldButton->SetIsEnabled(false);
	}
	SetStatus(TEXT("Ready"));

	// Force the widget tree to compute its layout immediately rather than waiting for the next
	// Slate tick — otherwise GetDesiredSize()/GetCachedGeometry() can still report stale (0,0)
	// values for a frame or two after construction, which is misleading for diagnostics even
	// though it doesn't by itself affect the outer viewport slot's actual on-screen size.
	ForceLayoutPrepass();
}

void UACELoginWidget::NativeDestruct()
{
	SaveLoginEntries();
	if (Client)
	{
		Client->OnSessionStateChanged.RemoveDynamic(this, &UACELoginWidget::HandleState);
		Client->OnCharacterList.RemoveDynamic(this, &UACELoginWidget::HandleCharacters);
		Client->OnLogMessage.RemoveDynamic(this, &UACELoginWidget::HandleLog);
	}
	Super::NativeDestruct();
}

void UACELoginWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bLoginEntriesDirty && FPlatformTime::Seconds() - LastLoginEditTime >= .4) SaveLoginEntries();

	// Safety net: if we're on screen but the allotted geometry is still zero-sized (e.g. this
	// tick raced ahead of AACEPlayerController::ApplyViewportSizeToLoginWidget), force one extra
	// layout prepass rather than silently sitting invisible.
	if (!bForcedZeroSizePrepass && IsInViewport() && MyGeometry.GetLocalSize().IsNearlyZero())
	{
		bForcedZeroSizePrepass = true;
		UE_LOG(LogTemp, Warning, TEXT("ACE: UACELoginWidget::NativeTick — geometry still zero-sized while in viewport, forcing layout prepass"));
		ForceLayoutPrepass();
	}
}

void UACELoginWidget::OnLoginEntryChanged(const FText& Text)
{
	bLoginEntriesDirty = true;
	LastLoginEditTime = FPlatformTime::Seconds();
}

void UACELoginWidget::OnLoginEntryCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	SaveLoginEntries();
}

void UACELoginWidget::SaveLoginEntries()
{
	if (!bLoginSettingsLoaded || !bLoginEntriesDirty) return;
	FACELoginSettings Entries;
	Entries.Host = HostBox ? HostBox->GetText().ToString() : DefaultHost;
	Entries.Port = PortBox ? PortBox->GetText().ToString() : FString::FromInt(DefaultPort);
	Entries.Account = AccountBox ? AccountBox->GetText().ToString() : DefaultAccount;
	Entries.Password = PasswordBox ? PasswordBox->GetText().ToString() : DefaultPassword;
	if (!ACELoginSettings::Save(Entries))
		UE_LOG(LogTemp, Warning, TEXT("ACE: Could not save local login settings"));
	bLoginEntriesDirty = false;
}

void UACELoginWidget::OnLoginClicked()
{
	DoLogin();
}

void UACELoginWidget::OnEnterWorldClicked()
{
	DoEnterSelectedCharacter();
}

void UACELoginWidget::DoLogin()
{
	SaveLoginEntries();
	if (!Client)
	{
		return;
	}
	const FString Host = HostBox ? HostBox->GetText().ToString() : DefaultHost;
	FString PortString = PortBox ? PortBox->GetText().ToString() : FString();
	PortString.ReplaceInline(TEXT(","), TEXT(""), ESearchCase::CaseSensitive);
	const int32 Port = PortBox ? FCString::Atoi(*PortString) : DefaultPort;
	const FString Account = AccountBox ? AccountBox->GetText().ToString() : DefaultAccount;
	const FString Password = PasswordBox ? PasswordBox->GetText().ToString() : DefaultPassword;

	// Kick DAT indexing on a worker if needed — never block Connect on portal/cell B-tree walks
	// (that was freezing the UI with low CPU while the disk was busy).
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UACEDatSubsystem* Dat = GI->GetSubsystem<UACEDatSubsystem>())
			{
				Dat->BeginBackgroundLoad(true);
			}
		}
	}

	SetStatus(TEXT("Connecting..."));
	Client->Login(Host, Port, Account, Password);
}

void UACELoginWidget::SelectCharacterIndex(int32 Index)
{
	if (!CachedCharacters.IsValidIndex(Index))
	{
		return;
	}
	SelectedCharacterIndex = Index;
	SelectedCharacterId = CachedCharacters[Index].CharacterId;
	SetStatus(FString::Printf(TEXT("Selected %s"), *CachedCharacters[Index].Name));
	UpdateCharacterRowHighlight();
}

void UACELoginWidget::DoEnterSelectedCharacter()
{
	if (!Client)
	{
		return;
	}
	if (SelectedCharacterId == 0 && CachedCharacters.Num() > 0)
	{
		SelectedCharacterId = CachedCharacters[0].CharacterId;
	}
	if (SelectedCharacterId == 0)
	{
		SetStatus(TEXT("No character selected"));
		return;
	}
	Client->EnterWorld(SelectedCharacterId);
}

void UACELoginWidget::HandleState(EACESessionState NewState)
{
	switch (NewState)
	{
	case EACESessionState::Connecting: SetStatus(TEXT("Connecting...")); break;
	case EACESessionState::AwaitConnectRequest: SetStatus(TEXT("Awaiting ConnectRequest")); break;
	case EACESessionState::AwaitCharacterList: SetStatus(TEXT("Waiting for the server's character list...")); break;
	case EACESessionState::CharacterSelect:
		// The controller removes this card only once the DAT screen is ready.
		// Do not overwrite a loading failure set by its character-list handler.
		break;
	case EACESessionState::EnteringWorld: SetStatus(TEXT("Entering world...")); break;
	case EACESessionState::InWorld:
		SetStatus(TEXT("In world"));
		SetVisibility(ESlateVisibility::Collapsed);
		break;
	case EACESessionState::Disconnected:
	case EACESessionState::Failed:
	{
		const auto Session = Client ? Client->GetSession() : nullptr;
		SetStatus(NewState == EACESessionState::Failed
			? (Session && !Session->GetConnectionError().IsEmpty() ? Session->GetConnectionError() : TEXT("Connection failed (see log)"))
			: TEXT("Disconnected"));
		SetVisibility(ESlateVisibility::Visible);
		break;
	}
	default: break;
	}
}

void UACELoginWidget::HandleCharacters(const TArray<FACECharacterInfo>& Characters, const FString& /*ServerName*/)
{
	// Cache only — retail character select owns the list UI after login.
	CachedCharacters = Characters;
	SelectedCharacterIndex = INDEX_NONE;
	SelectedCharacterId = 0;
	if (Characters.Num() > 0)
	{
		SelectedCharacterIndex = 0;
		SelectedCharacterId = Characters[0].CharacterId;
	}
	if (CharacterListLabel)
	{
		CharacterListLabel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (CharacterListSizeBox)
	{
		CharacterListSizeBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (EnterWorldButton)
	{
		EnterWorldButton->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UACELoginWidget::RebuildCharacterRows()
{
	if (!CharacterListBox || !WidgetTree)
	{
		return;
	}

	CharacterListBox->ClearChildren();
	CharacterRowButtons.Reset();

	for (int32 i = 0; i < CachedCharacters.Num(); ++i)
	{
		const FACECharacterInfo& Info = CachedCharacters[i];

		UACECharacterRowButton* Row = WidgetTree->ConstructWidget<UACECharacterRowButton>(
			UACECharacterRowButton::StaticClass(), *FString::Printf(TEXT("CharacterRow%d"), i));
		Row->CharacterIndex = i;
		Row->OwnerWidget = this;
		Row->SetVisibility(ESlateVisibility::Visible);
		Row->OnClicked.AddDynamic(Row, &UACECharacterRowButton::HandleClicked);

		USizeBox* RowSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		RowSizeBox->SetMinDesiredHeight(40.f);
		RowSizeBox->SetVisibility(ESlateVisibility::Visible);

		UTextBlock* RowText = WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
		RowText->SetText(FText::FromString(Info.Name));
		RowText->SetColorAndOpacity(FSlateColor(ACETextColor));
		RowText->SetVisibility(ESlateVisibility::Visible);
		RowSizeBox->AddChild(RowText);
		if (USizeBoxSlot* PadSlot = Cast<USizeBoxSlot>(RowText->Slot))
		{
			PadSlot->SetPadding(FMargin(12.f, 0.f));
			PadSlot->SetHorizontalAlignment(HAlign_Left);
			PadSlot->SetVerticalAlignment(VAlign_Center);
		}

		Row->AddChild(RowSizeBox);

		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(CharacterListBox->AddChild(Row)))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		}

		CharacterRowButtons.Add(Row);
	}

	UpdateCharacterRowHighlight();
}

void UACELoginWidget::UpdateCharacterRowHighlight()
{
	for (int32 i = 0; i < CharacterRowButtons.Num(); ++i)
	{
		UACECharacterRowButton* Row = CharacterRowButtons[i];
		if (!Row)
		{
			continue;
		}
		const FLinearColor RowColor = (i == SelectedCharacterIndex) ? ACERowSelectedColor : ACERowColor;
		FButtonStyle Style = Row->GetStyle();
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.TintColor = FSlateColor(RowColor);
		Style.Normal = Brush;
		Style.Hovered = Brush;
		Style.Hovered.TintColor = FSlateColor(RowColor * 1.2f);
		Style.Pressed = Brush;
		Style.Pressed.TintColor = FSlateColor(RowColor * 0.85f);
		Row->SetStyle(Style);
	}
}

void UACELoginWidget::HandleLog(const FString& Message)
{
	SetStatus(Message);
}

void UACELoginWidget::SetUseDatIntroBackdrop(bool bUse)
{
	bUseDatIntroBackdrop = bUse;
	if (!BackdropBorder)
	{
		return;
	}
	if (bUse)
	{
		// Fully transparent but still hit-tests so clicks don't fall through to the world.
		BackdropBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		BackdropBorder->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		BackdropBorder->SetBrushColor(ACEBackdropColor);
		BackdropBorder->SetVisibility(ESlateVisibility::Visible);
	}
}

void UACELoginWidget::SetStatus(const FString& Msg)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Msg));
	}
}
