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
	const FLinearColor ACERowColor(0.14f, 0.15f, 0.19f, 1.f);
	const FLinearColor ACERowSelectedColor(0.20f, 0.38f, 0.62f, 1.f);
	const FLinearColor ACETextColor(0.88f, 0.89f, 0.92f, 1.f);
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
