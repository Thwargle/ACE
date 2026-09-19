#include "ACELoginHUD.h"
#include "ACEPlayerController.h"
#include "ACEClientBuild.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "CanvasItem.h"

extern RENDERCORE_API FTexture* GWhiteTexture;

namespace
{
	void DrawFilledRect(UCanvas* Canvas, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem TileItem(Position, GWhiteTexture, Size, Color);
		TileItem.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(TileItem);
	}
}

void AACELoginHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const AACEPlayerController* AcePC = Cast<AACEPlayerController>(PlayerOwner);
	const bool bUmgLoginVisible = AcePC && AcePC->IsLoginUIVisible();
	const bool bSessionDisconnected = !AcePC || AcePC->IsSessionDisconnected();

	// In-world hover blurbs are drawn by UACEHoverTooltipWidget (UMG, ZOrder 10050) so they
	// sit above Slate chrome. Canvas HUD only keeps the login backup overlay.
	if (!bSessionDisconnected)
	{
		return;
	}

	if (bUmgLoginVisible && !bForceAlwaysShow)
	{
		return;
	}

	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;

	DrawFilledRect(Canvas, FVector2D(0.f, 0.f), FVector2D(W, H), FLinearColor(0.f, 0.f, 0.f, 1.f));

	const FVector2D PanelSize(480.f, 140.f);
	const FVector2D PanelPos((W - PanelSize.X) * 0.5f, (H - PanelSize.Y) * 0.5f);
	DrawFilledRect(Canvas, PanelPos, PanelSize, FLinearColor(0.09f, 0.10f, 0.13f, 0.92f));

	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (Font)
	{
		float TitleW = 0.f, TitleH = 0.f;
		const FString TitleStr = ACEClientBuild::ProductName(AcePC && AcePC->IsVRActive());
		Canvas->TextSize(Font, TitleStr, TitleW, TitleH);
		Canvas->SetDrawColor(FColor(230, 230, 235));
		Canvas->DrawText(Font, TitleStr, PanelPos.X + (PanelSize.X - TitleW) * 0.5f, PanelPos.Y + 24.f);

		const FString SubStr = bUmgLoginVisible
			? TEXT("Loading...")
			: TEXT("Waiting for viewport...");
		float SubW = 0.f, SubH = 0.f;
		Canvas->TextSize(Font, SubStr, SubW, SubH);
		Canvas->SetDrawColor(FColor(160, 165, 175));
		Canvas->DrawText(Font, SubStr, PanelPos.X + (PanelSize.X - SubW) * 0.5f, PanelPos.Y + 24.f + TitleH + 12.f);
	}
}
