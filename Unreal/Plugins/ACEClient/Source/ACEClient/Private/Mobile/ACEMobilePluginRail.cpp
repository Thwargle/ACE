#include "Mobile/ACEMobileWidget.h"
#include "Mods/ACEPluginSubsystem.h"
#include "Mods/ACEPluginPresentation.h"
#include "Engine/GameInstance.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Brushes/SlateRoundedBoxBrush.h"

void UACEMobileWidget::RefreshPluginRail()
{
    if(!PluginRailHost)return;
    auto* Host=GetGameInstance()->GetSubsystem<UACEPluginSubsystem>();if(!Host)return;
    TArray<FString> Enabled;
    for(const auto& Id:Host->PluginDisplayOrder())
        for(const auto& Plugin:Host->Plugins)if(Plugin->Id==Id && Plugin->Enabled){Enabled.Add(Id);break;}
    if(Enabled==RailPluginIds && RailSize.Equals(ViewSize))return;
    RailPluginIds=Enabled;RailSize=ViewSize;
    static const FButtonStyle Style=FButtonStyle()
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor(.018f,.029f,.043f,.85f),12,FLinearColor(.45f,.56f,.60f,.65f),1))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.04f,.17f,.20f,.95f),12,FLinearColor(.26f,.83f,.76f),2))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.04f,.17f,.20f),12,FLinearColor::White,2));
    auto List=SNew(SScrollBox).ScrollBarThickness(FVector2D(4,4));
    for(const auto& Id:Enabled)for(const auto& Plugin:Host->Plugins)if(Plugin->Id==Id)
    {
        List->AddSlot().Padding(0,0,4,8)[SNew(SBox).WidthOverride(56).HeightOverride(56)
            [SNew(SButton).Tag(FName(*(TEXT("MobilePlugin:")+Id))).ButtonStyle(&Style).IsFocusable(false)
                .TouchMethod(EButtonTouchMethod::PreciseTap).ContentPadding(8).ToolTipText(FText::FromString(Plugin->DisplayTitle()))
                .OnClicked_Lambda([Weak=TWeakObjectPtr<UACEMobileWidget>(this),Id](){if(Weak.IsValid())Weak->OpenPanel(FName(*(TEXT("Plugin:")+Id)));return FReply::Handled();})
                [SNew(SACEPluginBadge).Plugin(Plugin)]]];
        break;
    }
    // Use the same logical safe area as the controls, so fold/unfold and UI
    // scaling cannot move plugin hit targets over the movement dial.
    PluginRailHost->SetContent(SNew(SScaleBox).Stretch(EStretch::ScaleToFit).Visibility(EVisibility::SelfHitTestInvisible)
        [SNew(SBox).WidthOverride(ViewSize.X).HeightOverride(ViewSize.Y).Visibility(EVisibility::SelfHitTestInvisible)
            [SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
                +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8,100,0,0)
                    [SNew(SBox).WidthOverride(60).HeightOverride(ViewSize.Y-148)[List]]]]);
}
