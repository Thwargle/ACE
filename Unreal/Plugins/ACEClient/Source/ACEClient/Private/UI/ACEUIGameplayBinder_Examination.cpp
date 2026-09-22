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
#include "Protocol/ACECharacterTitleNames.inl"
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
    for (const TCHAR* Field : {TEXT("SpellManaText"), TEXT("SpellDurationText"), TEXT("SpellRangeText"), TEXT("SpellFormulaText"), TEXT("SpellFormulaIconList")})
        if (auto Element=Manager->FindElementUnder(TEXT("SpellExamineUI"),Field)) Element->bVisible=false;
    const auto Body=Manager->FindElementUnder(TEXT("SpellExamineUI"), TEXT("SpellDisplayText"));
    if (!Body) return;
    if (!ExamBody) ExamBody=Canvas->WidgetTree->ConstructWidget<UTextBlock>(UACERetailTextBlock::StaticClass());
    ExamBody->SetText(FText::FromString(Details));
    ExamBody->SetColorAndOpacity(FLinearColor::White); ExamBody->SetAutoWrapText(true);
    ExamBody->SetVisibility(ESlateVisibility::HitTestInvisible);
    if (auto* Retail=Cast<UACERetailTextBlock>(ExamBody))
        Retail->SetRetailElement(Canvas->GetResourceResolver(), Body, Canvas->GetLastScale2D(), Body->Width, false);
    if (!ExamScroll) ExamScroll=Canvas->WidgetTree->ConstructWidget<UScrollBox>();
    ExamScroll->SetClipping(EWidgetClipping::ClipToBounds);
    ExamScroll->SetScrollBarVisibility(ESlateVisibility::Visible);
    if (ExamBody->GetParent()!=ExamScroll) ExamScroll->AddChild(ExamBody);
    ExamScroll->SetVisibility(ESlateVisibility::Visible);
    Canvas->PlaceWidgetAtElement(ExamScroll, Body, 100001);
    if (auto Bar=Manager->FindElementUnder(TEXT("SpellExamineUI"),TEXT("SpellDisplayTextScrollbar"))) Bar->bVisible=false;
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
    FString TypeName;
    if (LastAppraisal.CreatureType && Canvas->GetResourceResolver())
        TypeName = Canvas->GetResourceResolver()->ResolveEnumString(0x10000005, LastAppraisal.CreatureType);
    // AppraisalSystem::InqCreatureDisplayName replaces enum separators for display.
    TypeName.ReplaceInline(TEXT("_"), TEXT(" "));
    if (bCharacter)
    {
        auto* Resources = Canvas->GetResourceResolver();
        FString Gender = Resources ? Resources->ResolveEnumString(0x10000001, LastAppraisal.IntProperties.FindRef(113)) : FString();
        FString Heritage = Resources ? Resources->ResolveEnumString(0x10000002, LastAppraisal.IntProperties.FindRef(188)) : FString();
        if (!LastAppraisal.IntProperties.FindRef(113)) Gender.Reset();
        if (!LastAppraisal.IntProperties.FindRef(188)) Heritage = TypeName;
        Gender.ReplaceInline(TEXT("_"), TEXT(" ")); Heritage.ReplaceInline(TEXT("_"), TEXT(" "));
        const FString GenderHeritage = (Gender + TEXT(" ") + Heritage).TrimStartAndEnd();
        FString Profession = LastAppraisal.StringProperties.FindRef(5);
        if (const int32* Title = LastAppraisal.IntProperties.Find(261))
        {
            const FString TitleName = GetCharacterTitleName(*Title);
            if (TitleName != TEXT("(Unknown Title)")) Profession = TitleName;
        }
        const int32 PK = LastAppraisal.IntProperties.FindRef(134);
        FACEWorldObject Subject;
        const bool bHaveSubject = Client && Client->GetWorldObject(LastAppraisal.ObjectGuid, Subject);
        const bool bPK = bHaveSubject ? (Subject.ObjectDescriptionFlags & ACEObjectDescFlag::PlayerKiller) != 0 : (PK & ACEPlayerKillerStatus::PK) != 0;
        const bool bPKLite = bHaveSubject ? (Subject.ObjectDescriptionFlags & ACEObjectDescFlag::PkLiteStatus) != 0 : (PK & ACEPlayerKillerStatus::PKLite) != 0;
        PlaceTextOnElement(ExamCreatureHeadings[3], Find(TEXT("HeritageText")), GenderHeritage, 9, FLinearColor::White, 100001);
        PlaceTextOnElement(ExamCreatureHeadings[4], Find(TEXT("ProfessionText")), Profession, 9, FLinearColor::White, 100001);
        PlaceTextOnElement(ExamCreatureHeadings[5], Find(TEXT("PlayerKillerText")), bPK ? TEXT("Player Killer")
            : bPKLite ? TEXT("Player Killer Lite") : TEXT("Non-Player Killer"), 9, FLinearColor::White, 100001);
    }
    else PlaceTextOnElement(ExamCreatureHeadings[2], Find(TEXT("CreatureName")), TypeName, 9, FLinearColor::White, 100001);
    PlaceTextOnElement(ExamCreatureHeadings[6], Find(TEXT("AllegianceNameText")), bCharacter && LastAppraisal.IntProperties.FindRef(30) > 0
        ? LastAppraisal.StringProperties.FindRef(47) : FString(), 9, FLinearColor::White, 100001);
    auto List = Manager->FindElementUnder(TEXT("BasicCreatureExamineUI"), TEXT("BasicCreatureExam_Attributes"));
    if (!List) return;
    List->bVisible = true;
    // BasicCreatureExamineUI constructs six AttributeInfoRegions followed by
    // Health (including percent), Stamina and Mana Attribute2ndInfoRegions.
    // All use the actual 2100006B/10000166 list entry, including label/value
    // alignment, glyph atlas, outline and twenty-pixel row height.
    static const TCHAR* Names[] = {TEXT("Strength"), TEXT("Endurance"), TEXT("Coordination"),
        TEXT("Quickness"), TEXT("Focus"), TEXT("Self"), TEXT("Health"), TEXT("Stamina"), TEXT("Mana")};
    const int32 Values[] = {LastAppraisal.Strength, LastAppraisal.Endurance, LastAppraisal.Coordination,
        LastAppraisal.Quickness, LastAppraisal.Focus, LastAppraisal.Self, LastAppraisal.Health,
        LastAppraisal.Stamina, LastAppraisal.Mana};
    const int32 Maxima[] = {LastAppraisal.MaxHealth, LastAppraisal.MaxStamina, LastAppraisal.MaxMana};
    const int32 Masks[] = {1, 2, 8, 4, 16, 32, 64, 128, 256};
    while (ExamAttributeRows.Num() < UE_ARRAY_COUNT(Names))
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
    for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I)
    {
        const auto Row = ExamAttributeRows[I];
        FString Value = Values[I] > 0 ? FString::FromInt(Values[I]) : TEXT("???");
        if (I >= 6)
        {
            const int32 Max = Maxima[I - 6];
            Value = TEXT("???");
            if (Max > 0)
            {
                const int32 Percent = FMath::RoundToInt(100.0 * Values[I] / Max);
                if (LastAppraisal.bSuccess)
                    Value = I == 6 ? FString::Printf(TEXT("%d/%d (%d %%)"), Values[I], Max, Percent)
                        : FString::Printf(TEXT("%d/%d"), Values[I], Max);
                else if (I == 6) Value = FString::Printf(TEXT("%d %%"), Percent);
            }
        }
        FLinearColor Color = FLinearColor::White;
        if (!LastAppraisal.bSuccess) Color = FLinearColor::Yellow;
        else if (LastAppraisal.AttributeHighlights & Masks[I])
            Color = (LastAppraisal.AttributeColors & Masks[I]) ? FLinearColor::Green : FLinearColor::Red;
        for (const auto& Child : Row->Children)
        {
            if (Child->ElementName == TEXT("InfoRegion_Label"))
                PlaceTextOnElement(ExamAttributeLabels[I], Child, Names[I], 9, FLinearColor::White, 100001);
            else if (Child->ElementName == TEXT("InfoRegion_Value"))
            {
                Child->TextColor = Color;
                PlaceTextOnElement(ExamAttributeValues[I], Child, Value, 9, Color, 100001);
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
            Text->SetToolTipText(Text->GetText());
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
