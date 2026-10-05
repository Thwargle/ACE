#include "ACEHoverTooltipWidget.h"
#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUILayoutResolver.h"
#include "UI/ACERetailTextBlock.h"
#include "UI/ACEUIResourceResolver.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "ACEClientSubsystem.h"
#include "ACEAppraisalFormatting.h"
#include "ACEDatSubsystem.h"
#include "Components/Border.h"

bool UACEUIGameplayBinder::InspectSpellAt(FVector2D Point)
{
    if (!Canvas || !Client || !Manager) return false;
    const FVector2D Absolute = Canvas->GetCachedGeometry().LocalToAbsolute(Point);
    int32 Spell = 0;
    HitTestSpellbookRow(Absolute, Spell);
    for (int32 I=0; !Spell && I<SpellBarIcons.Num() && I<SpellBarSpellIds.Num(); ++I)
        if (Canvas->IsWidgetExposedAt(SpellBarIcons[I], Absolute)) Spell = SpellBarSpellIds[I];
    if (!Spell && BuiltInSpellIconBorders.Num() && Canvas->IsWidgetExposedAt(BuiltInSpellIconBorders[0], Absolute)) Spell = BuiltInSpellId;
    auto* Dat = Canvas->GetResourceResolver() ? Canvas->GetResourceResolver()->GetDatSubsystem() : nullptr;
    FString Name; uint32 Icon=0;
    if (!Spell || !Dat || !Dat->TryGetSpellInfo(Spell, Name, Icon)) return false;
    ExaminedSpellId = Spell; bExaminationDismissed = false;
    if (ExamScroll) ExamScroll->SetScrollOffset(0.f);
    ShowExamination(true); RefreshExaminationOverlay();
    return true;
}

void UACEUIGameplayBinder::RefreshSpellExamination()
{
    if (!Canvas || !Canvas->WidgetTree || !Manager || !Canvas->GetResourceResolver()) return;
    auto* Dat=Canvas->GetResourceResolver()->GetDatSubsystem();
    FString Name, Details; uint32 Icon=0;
    if (!Dat || !Dat->TryGetSpellInfo(ExaminedSpellId, Name, Icon)) return;
    Dat->TryGetSpellExamination(ExaminedSpellId, Details);
    while (ExamSpellLabels.Num()<2) ExamSpellLabels.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
    PlaceTextOnElement(ExamSpellLabels[0], TEXT("DisplayedNameText"), Name, 10, FLinearColor::White, 100001);
    FString School, RemainingDetails;
    Details.Split(TEXT("\n"), &School, &RemainingDetails);
    PlaceTextOnElement(ExamSpellLabels[1], TEXT("MagicSchoolText"), School, 10, FLinearColor::White, 100001);
    if (!ExamIconBorder) ExamIconBorder=Canvas->WidgetTree->ConstructWidget<UBorder>();
    SetIconDid(ExamIconBorder, Icon);
    ExamIconBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
    Canvas->PlaceWidgetAtElement(ExamIconBorder, Manager->FindElementUnder(TEXT("SpellExamineUI"), TEXT("SpellIcon")), 100001);
    // Details and ingredients share a responsive scroll area. Retire the
    // original fixed-height subdivisions instead of drawing them through text.
    for (const TCHAR* Field : {TEXT("SpellManaText"), TEXT("SpellDurationText"), TEXT("SpellRangeText"), TEXT("SpellFormulaText"), TEXT("SpellFormulaIconList"),
        TEXT("SpellExamBackground_Divider_Middle"),TEXT("SpellExamBackground_Divider_Lower")})
        if (auto Element=Manager->FindElementUnder(TEXT("SpellExamineUI"),Field)) Element->bVisible=false;
    const auto Body=Manager->FindElementUnder(TEXT("SpellExamineUI"), TEXT("SpellDisplayText"));
    if (!Body) return;
    const auto Panel=Manager->FindElementByName(TEXT("SpellExamineUI"));
    const auto Divider=Manager->FindElementUnder(TEXT("SpellExamineUI"),TEXT("SpellExamBackground_Divider"));
    if(Panel && Divider)
    {
        Body->Y=Divider->Y+Divider->Height+6;
        Body->Height=FMath::Max(32,Panel->Height-Body->Y-4);
        if(auto Background=Manager->FindElementUnder(TEXT("SpellExamineUI"),TEXT("SpellExamBackground")))Background->Height=Panel->Height;
        if(auto Scrollbar=Manager->FindElementUnder(TEXT("SpellExamineUI"),TEXT("SpellDisplayTextScrollbar")))
        {Scrollbar->Y=Body->Y;Scrollbar->Height=Body->Height;}
    }
    if (!ExamBody) ExamBody=Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
    ExamBody->SetText(FText::FromString(RemainingDetails));
    ExamBody->SetColorAndOpacity(FLinearColor::White); ExamBody->SetAutoWrapText(true);
    ExamBody->SetVisibility(ESlateVisibility::HitTestInvisible);
    if (auto* Retail=Cast<UACERetailTextBlock>(ExamBody))
	{
        Retail->SetRetailElement(Canvas->GetResourceResolver(), Body, Canvas->GetLastScale2D(), Body->Width, false);
		Retail->SetTextColors({});
	}
    if (!ExamScroll) ExamScroll=Canvas->WidgetTree->ConstructWidget<UScrollBox>();
    ExamScroll->SetClipping(EWidgetClipping::ClipToBounds);
    ExamScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
    ExamScroll->SetAnimateWheelScrolling(false);
    if (ExamBody->GetParent()!=ExamScroll) ExamScroll->AddChild(ExamBody);
    ExamScroll->SetVisibility(ESlateVisibility::Visible);
    Canvas->PlaceWidgetAtElement(ExamScroll, Body, 100001);
    const float MaxOffset = ExamScroll->GetScrollOffsetOfEnd();
    const auto Bar = Manager->FindElementUnder(TEXT("SpellExamineUI"),TEXT("SpellDisplayTextScrollbar"));
    if (Bar) Bar->bVisible = MaxOffset > .5f;
    SyncDatScrollbar(Bar, MaxOffset > 0.f ? ExamScroll->GetScrollOffset()/MaxOffset : 0.f,
        Body->Height/FMath::Max(1.f, MaxOffset+Body->Height));
}

void UACEUIGameplayBinder::RefreshCreatureExamination()
{
    if (!Manager || !Canvas || !Canvas->WidgetTree) return;
    const auto Root = Manager->FindElementUnder(TEXT("RootGameplay_FloatyExamination_Field"), TEXT("BasicCreatureExamineUI"));
    if (!Root) return;
    const bool bCharacter = ACEAppraisalFormatting::UsesCharacterExamination(LastAppraisal);
    auto Find = [&](const TCHAR* Name) { return Manager->FindElementUnder(TEXT("BasicCreatureExamineUI"), Name); };
    if (auto El = Find(TEXT("CreatureExam_Attributes"))) El->bVisible = !bCharacter;
    if (auto El = Find(TEXT("CharacterExam_Attributes"))) El->bVisible = bCharacter;
    while (ExamCreatureHeadings.Num() < 7)
        ExamCreatureHeadings.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
    for (UTextBlock* Text : ExamCreatureHeadings) Text->SetVisibility(ESlateVisibility::Collapsed);
    // 23000001/0990A1E2 and 07100DAC supply these English labels in the DAT.
    PlaceTextOnElement(ExamCreatureHeadings[0], Find(TEXT("LevelInfo_Character")), TEXT("Character"), 8, FLinearColor::White, 100001);
    PlaceTextOnElement(ExamCreatureHeadings[1], Find(TEXT("LevelInfo_Level")), TEXT("Level"), 8, FLinearColor::White, 100001);
    FACEWorldObject Subject;
    const bool bHaveSubject = Client && Client->GetWorldObject(LastAppraisal.ObjectGuid, Subject);
    const auto Headings = ACEAppraisalFormatting::CreatureHeadings(LastAppraisal, Canvas->GetResourceResolver(), bHaveSubject ? &Subject : nullptr);
    if (bCharacter)
    {
        PlaceTextOnElement(ExamCreatureHeadings[3], Find(TEXT("HeritageText")), Headings.Heritage, 9, FLinearColor::White, 100001);
        PlaceTextOnElement(ExamCreatureHeadings[4], Find(TEXT("ProfessionText")), Headings.Profession, 9, FLinearColor::White, 100001);
        PlaceTextOnElement(ExamCreatureHeadings[5], Find(TEXT("PlayerKillerText")), Headings.PlayerKiller, 9, FLinearColor::White, 100001);
    }
    else PlaceTextOnElement(ExamCreatureHeadings[2], Find(TEXT("CreatureName")), Headings.Type, 9, FLinearColor::White, 100001);
    PlaceTextOnElement(ExamCreatureHeadings[6], Find(TEXT("AllegianceNameText")), Headings.Allegiance, 9, FLinearColor::White, 100001);
    auto List = Manager->FindElementUnder(TEXT("BasicCreatureExamineUI"), TEXT("BasicCreatureExam_Attributes"));
    if (!List) return;
    List->bVisible = true;
    // BasicCreatureExamineUI constructs six AttributeInfoRegions followed by
    // Health (including percent), Stamina and Mana Attribute2ndInfoRegions.
    // All use the actual 2100006B/10000166 list entry, including label/value
    // alignment, glyph atlas, outline and twenty-pixel row height.
    const auto Stats = ACEAppraisalFormatting::CreatureStatLines(LastAppraisal);
    while (ExamAttributeRows.Num() < Stats.Num())
    {
        auto Row = UACEUILayoutResolver::LoadTemplate(0x2100006B, 0x10000166);
        if (!Row) return;
        Row->Y = ExamAttributeRows.Num() * Row->Height;
        Row->SetElementName(FString::Printf(TEXT("ExamAttribute_%d"), ExamAttributeRows.Num()));
        List->AddChild(Row);
        ExamAttributeRows.Add(Row);
        ExamAttributeLabels.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
        ExamAttributeValues.Add(Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass()));
    }
    for (int32 I = 0; I < Stats.Num(); ++I)
    {
        const auto Row = ExamAttributeRows[I];
        const auto& Stat = Stats[I];
        for (const auto& Child : Row->Children)
        {
            if (Child->ElementName == TEXT("InfoRegion_Label"))
                PlaceTextOnElement(ExamAttributeLabels[I], Child, Stat.Label, 9, FLinearColor::White, 100001);
            else if (Child->ElementName == TEXT("InfoRegion_Value"))
            {
                Child->TextColor = Stat.Color;
                PlaceTextOnElement(ExamAttributeValues[I], Child, Stat.Value, 9, Stat.Color, 100001);
            }
        }
    }
    if (!ExamLevel) ExamLevel = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
    PlaceTextOnElement(ExamLevel, Find(TEXT("LevelInfo_Value")), LastAppraisal.Level > 0
        ? FString::FromInt(LastAppraisal.Level) : TEXT("???"), 12, FLinearColor::White, 100001);

    // Use BasicCreatureExam_ExtraInfo and its two-column row from 2100001C.
    // The row atlas, margins, alignment and twenty-pixel cadence are retail data.
    const auto DetailRegion = Find(TEXT("BasicCreatureExam_ExtraInfo"));
    if (!DetailRegion) return;
    const auto Lines = ACEAppraisalFormatting::CreatureDetailLines(LastAppraisal);
    if (!ExamCreatureDetailsScroll)
    {
        ExamCreatureDetailsScroll = Canvas->WidgetTree->ConstructWidget<UScrollBox>();
        ExamCreatureDetailsScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
        ExamCreatureDetailsScroll->SetClipping(EWidgetClipping::ClipToBounds);
        ExamCreatureDetailsScroll->SetAnimateWheelScrolling(false);
        ExamCreatureDetailsSize = Canvas->WidgetTree->ConstructWidget<USizeBox>();
        ExamCreatureDetailsCanvas = Canvas->WidgetTree->ConstructWidget<UCanvasPanel>();
        ExamCreatureDetailsSize->AddChild(ExamCreatureDetailsCanvas);
        ExamCreatureDetailsScroll->AddChild(ExamCreatureDetailsSize);
    }
    while (ExamMiscRows.Num() < Lines.Num())
    {
        auto Row = UACEUILayoutResolver::LoadTemplate(0x2100001C, 0x10000337);
        if (!Row) break;
        ExamMiscRows.Add(Row);
        auto* Label = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
        auto* Value = Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
        ExamMiscLabels.Add(Label); ExamMiscValues.Add(Value);
        ExamCreatureDetailsCanvas->AddChild(Label); ExamCreatureDetailsCanvas->AddChild(Value);
    }
    const FVector2D Scale = Canvas->GetLastScale2D();
    for (int32 I = 0; I < ExamMiscRows.Num(); ++I)
    {
        if (!Lines.IsValidIndex(I))
        {
            ExamMiscLabels[I]->SetVisibility(ESlateVisibility::Collapsed);
            ExamMiscValues[I]->SetVisibility(ESlateVisibility::Collapsed);
            continue;
        }
        for (const auto& Cell : ExamMiscRows[I]->Children)
        {
            const bool bLabel = Cell->ElementId == 0x1000012A;
            UTextBlock* Text = bLabel ? ExamMiscLabels[I].Get() : ExamMiscValues[I].Get();
            Text->SetText(FText::FromString(bLabel ? Lines[I].Label : Lines[I].Value));
            Cell->TextColor = Lines[I].Color;
            if (!bLabel) Cell->Width = FMath::Max(1, DetailRegion->Width - Cell->X - 2);
            Cast<UACERetailTextBlock>(Text)->SetRetailElement(Canvas->GetResourceResolver(), Cell, Scale, Cell->Width, false);
            Text->SetColorAndOpacity(Lines[I].Color);
            Text->SetJustification(bLabel ? ETextJustify::Left : ETextJustify::Right);
            Text->SetMargin(FMargin(Cell->TextMargins.Left * Scale.X, Cell->TextMargins.Top * Scale.Y,
                Cell->TextMargins.Right * Scale.X, Cell->TextMargins.Bottom * Scale.Y));
            Text->SetAutoWrapText(false);
            UACEHoverTooltipWidget::SetWidgetTooltip(Text, Text->GetText());
            Text->SetVisibility(ESlateVisibility::Visible);
            auto* Slot = CastChecked<UCanvasPanelSlot>(Text->Slot);
            Slot->SetPosition(FVector2D(Cell->X * Scale.X, (I * 20 + Cell->Y) * Scale.Y));
            Slot->SetSize(FVector2D(Cell->Width * Scale.X, Cell->Height * Scale.Y));
        }
    }
    ExamCreatureDetailsSize->SetWidthOverride(DetailRegion->Width * Scale.X);
    ExamCreatureDetailsSize->SetHeightOverride(Lines.Num() * 20 * Scale.Y);
    if (ExamCreatureScrolledGuid != LastAppraisal.ObjectGuid)
    {
        ExamCreatureDetailsScroll->SetScrollOffset(0.f);
        ExamCreatureScrolledGuid = LastAppraisal.ObjectGuid;
    }
    ExamCreatureDetailsScroll->SetVisibility(ESlateVisibility::Visible);
    Canvas->PlaceWidgetAtElement(ExamCreatureDetailsScroll, DetailRegion, 100001);
}
