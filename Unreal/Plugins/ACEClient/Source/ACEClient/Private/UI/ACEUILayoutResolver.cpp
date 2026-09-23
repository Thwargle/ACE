#include "UI/ACEUILayoutResolver.h"
#include "UI/ACEUIElementManager.h"
#include "UI/ACEUIElement.h"
#include "UI/ACEUITypes.h"
#include "ACEDatSubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "Async/Async.h"
#include "Async/InheritedContext.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

struct FACEPreparedUILayout
{
    struct FNode
    {
        TSharedPtr<FJsonObject> Json;
        TSharedPtr<FACEUIElement> Parent;
        TSharedPtr<FACEUIElement> Finalize;
    };
    uint32 Id = 0;
    TFuture<TSharedPtr<FJsonObject>> Json;
    TArray<FNode> Pending;
    TArray<TSharedPtr<FACEUIElement>> Roots;
    bool bStarted = false;
    bool bFailed = false;
};

namespace
{
	uint32 GNextUIInstanceId = 1;

	bool ParseHexU32(const FString& Text, uint32& Out)
	{
		FString S = Text.TrimStartAndEnd();
		if (S.StartsWith(TEXT("0x"), ESearchCase::IgnoreCase))
		{
			S = S.Mid(2);
		}
		if (S.IsEmpty())
		{
			return false;
		}
		Out = static_cast<uint32>(FCString::Strtoui64(*S, nullptr, 16));
		return true;
	}

	void ApplyPanelTabs(const TSharedPtr<FJsonObject>& Obj, const TSharedPtr<FACEUIElement>& Out)
	{
		// UIElement_Panel registers the tab IDs from UICore_Panel_pages.
		// Retail tabs can be Text elements: the normal label factory must not
		// disable controls that the owning panel explicitly registers.
		const TSharedPtr<FJsonObject>* PanelProps = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Pages = nullptr;
		if (Obj->TryGetObjectField(TEXT("properties"), PanelProps)
			&& (*PanelProps)->TryGetArrayField(TEXT("UICore_Panel_pages"), Pages))
		{
			for (const auto& Page : *Pages)
			{
				const auto Entry = Page->AsObject();
				double TabId = 0;
				if (!Entry || !Entry->TryGetNumberField(TEXT("0x00000030"), TabId)) continue;
				for (const auto& Child : Out->Children)
					if (Child->ElementId == static_cast<uint32>(TabId))
					{
						Child->bPanelTab = true;
						Child->bActivatable = true;
					}
			}
		}
	}

	bool ParseElementNode(UACEUIElementManager* Manager, const TSharedPtr<FJsonObject>& Obj,
		TSharedPtr<FACEUIElement>& Out, bool bRecurseChildren = true)
	{
		if (!Obj.IsValid())
		{
			return false;
		}
		FString HexStr;
		uint32 ElementId = 0;
		uint32 ElementType = ACEUI::ElementType::Field;
		if (Obj->TryGetStringField(TEXT("elementId"), HexStr))
		{
			ParseHexU32(HexStr, ElementId);
		}
		if (Obj->TryGetStringField(TEXT("type"), HexStr))
		{
			ParseHexU32(HexStr, ElementType);
		}
		// Per-type factory supplies retail defaults (activatable / focus) for this ElementDesc.
		Out = Manager ? Manager->CreateElementByType(ElementType, ElementId) : nullptr;
		if (!Out.IsValid())
		{
			Out = MakeShared<FACEUIElement>();
			Out->ElementId = ElementId;
			Out->Type = ElementType;
		}
		Out->InstanceId = GNextUIInstanceId++;
		if (Obj->TryGetStringField(TEXT("baseLayout"), HexStr))
		{
			ParseHexU32(HexStr, Out->BaseLayout);
		}
		if (Obj->TryGetStringField(TEXT("baseElement"), HexStr))
		{
			ParseHexU32(HexStr, Out->BaseElement);
		}
		Obj->TryGetStringField(TEXT("elementName"), Out->ElementName);

		double Num = 0;
		if (Obj->TryGetNumberField(TEXT("x"), Num))
		{
			Out->X = static_cast<int32>(Num);
		}
		if (Obj->TryGetNumberField(TEXT("y"), Num))
		{
			Out->Y = static_cast<int32>(Num);
		}
		if (Obj->TryGetNumberField(TEXT("width"), Num))
		{
			Out->Width = static_cast<int32>(Num);
		}
		if (Obj->TryGetNumberField(TEXT("height"), Num))
		{
			Out->Height = static_cast<int32>(Num);
		}
		if (Obj->TryGetNumberField(TEXT("zLevel"), Num))
		{
			Out->ZLevel = static_cast<uint32>(Num);
		}
		if (Obj->TryGetNumberField(TEXT("drawMode"), Num))
		{
			Out->DrawMode = static_cast<uint32>(Num);
		}
		if (Obj->TryGetNumberField(TEXT("leftEdge"), Num))
		{
			Out->LeftEdge = static_cast<uint8>(Num);
		}
		if (Obj->TryGetNumberField(TEXT("topEdge"), Num))
		{
			Out->TopEdge = static_cast<uint8>(Num);
		}
		if (Obj->TryGetNumberField(TEXT("rightEdge"), Num))
		{
			Out->RightEdge = static_cast<uint8>(Num);
		}
		if (Obj->TryGetNumberField(TEXT("bottomEdge"), Num))
		{
			Out->BottomEdge = static_cast<uint8>(Num);
		}
		if (Obj->TryGetStringField(TEXT("imageFile"), HexStr) && !HexStr.IsEmpty())
		{
			ParseHexU32(HexStr, Out->ImageFileId);
		}
		if (Obj->TryGetStringField(TEXT("alphaFile"), HexStr) && !HexStr.IsEmpty())
		{
			ParseHexU32(HexStr, Out->AlphaFileId);
		}

		Out->DebugName = Out->ElementName.IsEmpty()
			? FString::Printf(TEXT("El_0x%08X"), Out->ElementId)
			: Out->ElementName;

		if (Obj->TryGetStringField(TEXT("defaultState"), HexStr)) ParseHexU32(HexStr, Out->DefaultState);
		if (Obj->TryGetNumberField(TEXT("fontHeight"), Num)) Out->FontHeight = static_cast<int32>(Num);
		Obj->TryGetBoolField(TEXT("passToChildren"), Out->bPassStateToChildren);
		const TSharedPtr<FJsonObject>* Props = nullptr;
		auto ReadTextColor = [&](const TSharedPtr<FJsonObject>& Properties, TOptional<FLinearColor>& Color)
		{
			const TArray<TSharedPtr<FJsonValue>>* Colors = nullptr;
			uint32 ARGB = 0;
			if (Properties->TryGetArrayField(TEXT("UICore_Text_font_colors"), Colors) && Colors->Num() > 0
				&& ParseHexU32((*Colors)[0]->AsString(), ARGB))
				Color = FLinearColor(FColor((ARGB >> 16) & 255, (ARGB >> 8) & 255, ARGB & 255, ARGB >> 24));
		};
		if (Obj->TryGetObjectField(TEXT("properties"), Props))
		{
            auto ReadLimit = [&](const TCHAR* Key, int32& Limit)
            {
                double Value;
                if ((*Props)->TryGetNumberField(Key,Value) && Value > 0) Limit=static_cast<int32>(Value);
            };
            ReadLimit(TEXT("UICore_Element_min_width"),Out->MinWidth);
            ReadLimit(TEXT("UICore_Element_min_height"),Out->MinHeight);
            ReadLimit(TEXT("UICore_Element_max_width"),Out->MaxWidth);
            ReadLimit(TEXT("UICore_Element_max_height"),Out->MaxHeight);
            (*Props)->TryGetBoolField(TEXT("UICore_Resizebar_border_left"),Out->bResizeLeft);
            (*Props)->TryGetBoolField(TEXT("UICore_Resizebar_border_right"),Out->bResizeRight);
            (*Props)->TryGetBoolField(TEXT("UICore_Resizebar_border_top"),Out->bResizeTop);
            (*Props)->TryGetBoolField(TEXT("UICore_Resizebar_border_bottom"),Out->bResizeBottom);
			(*Props)->TryGetBoolField(TEXT("UICore_Button_boolean_button"), Out->bBooleanButton);
			ReadTextColor(*Props, Out->TextColor);
			(*Props)->TryGetBoolField(TEXT("UICore_Text_outline"), Out->bTextOutline);
            const TSharedPtr<FJsonObject>* Entry=nullptr;
            if((*Props)->TryGetObjectField(TEXT("UICore_Text_entry"),Entry)) ParseHexU32((*Entry)->GetStringField(TEXT("id")),Out->TextEntryId);
            if((*Props)->TryGetObjectField(TEXT("UICore_Element_tooltip_entry"),Entry)) ParseHexU32((*Entry)->GetStringField(TEXT("id")),Out->TooltipEntryId);
			(*Props)->TryGetBoolField(TEXT("UICore_Text_one_line"), Out->bTextOneLine);
			uint32 Outline = 0;
			FString OutlineHex;
			if ((*Props)->TryGetStringField(TEXT("UICore_Text_font_outline_color"), OutlineHex) && ParseHexU32(OutlineHex, Outline))
				Out->TextOutlineColor = FLinearColor(FColor((Outline >> 16) & 255, (Outline >> 8) & 255, Outline & 255, Outline >> 24));
			bool bHidden = false;
			if ((*Props)->TryGetBoolField(TEXT("UICore_Element_hide"), bHidden)) Out->bDefaultHidden = bHidden;
			(*Props)->TryGetBoolField(TEXT("UICore_Element_activatable"), Out->bActivatable);
			(*Props)->TryGetBoolField(TEXT("UICore_Element_DrawAfterChildren"), Out->bDrawAfterChildren);
			(*Props)->TryGetBoolField(TEXT("UICore_Button_rollover"), Out->bRollover);
			(*Props)->TryGetBoolField(TEXT("UICore_Button_ghosted"), Out->bGhosted);
			(*Props)->TryGetBoolField(TEXT("UICore_Button_highlighted"), Out->bHighlighted);
			const TArray<TSharedPtr<FJsonValue>>* Fonts = nullptr;
			if ((*Props)->TryGetArrayField(TEXT("UICore_Text_fonts"), Fonts) && Fonts->Num() > 0)
			{
				ParseHexU32((*Fonts)[0]->AsString(), Out->FontId);
				Out->bHasTextLayout = true;
			}
			auto ReadMargin = [&](const TCHAR* Name, float& Value)
			{
				double Margin = 0;
				if ((*Props)->TryGetNumberField(Name, Margin)) Value = static_cast<float>(Margin);
			};
			ReadMargin(TEXT("UICore_Text_left_margin"), Out->TextMargins.Left);
			ReadMargin(TEXT("UICore_Text_right_margin"), Out->TextMargins.Right);
			ReadMargin(TEXT("UICore_Text_top_margin"), Out->TextMargins.Top);
			ReadMargin(TEXT("UICore_Text_bottom_margin"), Out->TextMargins.Bottom);
			if ((*Props)->TryGetNumberField(TEXT("UICore_Text_horizontal_justification"), Num))
				Out->TextHorizontalJustification = static_cast<uint32>(Num);
			if ((*Props)->TryGetNumberField(TEXT("UICore_Text_vertical_justification"), Num))
				Out->TextVerticalJustification = static_cast<uint32>(Num);
		}
		const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
		if (Obj->TryGetArrayField(TEXT("states"), States))
		{
			for (const TSharedPtr<FJsonValue>& Value : *States)
			{
				const TSharedPtr<FJsonObject> State = Value->AsObject();
				uint32 StateId = 0;
				if (!State || !State->TryGetStringField(TEXT("stateId"), HexStr) || !ParseHexU32(HexStr, StateId)) continue;
				FACEUIStateMedia Media;
				if (State->TryGetStringField(TEXT("imageFile"), HexStr)) ParseHexU32(HexStr, Media.ImageFileId);
				if (State->TryGetStringField(TEXT("alphaFile"), HexStr)) ParseHexU32(HexStr, Media.AlphaFileId);
				if (State->TryGetNumberField(TEXT("drawMode"), Num)) Media.DrawMode = static_cast<uint32>(Num);
				State->TryGetBoolField(TEXT("hasMedia"), Media.bHasMedia);
				State->TryGetBoolField(TEXT("passToChildren"), Media.bPassToChildren);
				if (State->TryGetObjectField(TEXT("properties"), Props))
				{
					ReadTextColor(*Props, Media.TextColor);
					const TArray<TSharedPtr<FJsonValue>>* StateFonts = nullptr;
					if ((*Props)->TryGetArrayField(TEXT("UICore_Text_fonts"), StateFonts) && StateFonts->Num() > 0)
						ParseHexU32((*StateFonts)[0]->AsString(), Media.FontId);
					bool bOutline = false;
					if ((*Props)->TryGetBoolField(TEXT("UICore_Text_outline"), bOutline)) Media.bTextOutline = bOutline;
					bool bHidden = false;
					if ((*Props)->TryGetBoolField(TEXT("UICore_Element_hide"), bHidden)) Media.bHidden = bHidden;
				}
				Out->States.Add(StateId, Media);
			}
		}

		Out->ResolvePaintState(false, false, false);
		const TArray<TSharedPtr<FJsonValue>>* ChildrenArr = nullptr;
		if (bRecurseChildren && Obj->TryGetArrayField(TEXT("children"), ChildrenArr))
		{
			for (const TSharedPtr<FJsonValue>& Val : *ChildrenArr)
			{
				TSharedPtr<FJsonObject> ChildObj = Val->AsObject();
				TSharedPtr<FACEUIElement> Child;
				if (ParseElementNode(Manager, ChildObj, Child))
				{
					Out->AddChild(Child);
				}
			}
		}
		ApplyPanelTabs(Obj, Out);
		return true;
	}

	FString ResolvedLayoutPath(uint32 LayoutId)
	{
		const FString FileName = FString::Printf(TEXT("0x%08X.json"), LayoutId);
		TArray<FString> Candidates;

		if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ACEClient")))
		{
			Candidates.Add(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Docs/UI/Resolved"), FileName));
		}
		Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins/ACEClient/Docs/UI/Resolved"), FileName));
		Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/UI/Resolved"), FileName));

		for (const FString& Path : Candidates)
		{
			if (FPaths::FileExists(Path))
			{
				return Path;
			}
		}
		return Candidates.Num() > 0 ? Candidates[0] : FString();
	}

	/**
	 * Retail classic_gameplay (0x21000005) starts with only the always-on chrome visible.
	 * LayoutDesc includes every floaty; gm*UI shows the rest on demand.
	 *
	 * Geometry under RootGameplay_Field (800×600):
	 *   Radar 680,0 120×140 | SideVitals 0,0 460×26 | Indicators 0,0 150×30
	 *   Toolbar 490,500 310×100 | MainChat 0,500 410×100
	 * Hidden until needed: Panel, Examination, Combat/Env, Chat1–4, PowerBar,
	 * Keyboard, Admin, SmartBox, FloatyVitals (alternate to SideVitals).
	 */
	void ApplyClassicGameplayDefaultVisibility(const TSharedPtr<FACEUIElement>& Root)
	{
		if (!Root.IsValid())
		{
			return;
		}

		// World view shows through — do not paint the full-screen RootGameplay_Field backdrop.
		if (Root->Width >= ACEUI::ReferenceWidth && Root->Height >= ACEUI::ReferenceHeight
			&& Root->ImageFileId != 0)
		{
			Root->ImageFileId = 0;
		}

		auto IsAlwaysOnChrome = [](const FString& Name) -> bool
		{
			return Name == TEXT("RootGameplay_Radar_Field")
				|| Name == TEXT("RootGameplay_FloatySideVitals_Field")
				|| Name == TEXT("RootGameplay_FloatyIndicators_Field")
				|| Name == TEXT("RootGameplay_FloatyToolbar_Field")
				|| Name == TEXT("RootGameplay_FloatyMainChat_Field");
		};

		for (const TSharedPtr<FACEUIElement>& Child : Root->Children)
		{
			if (!Child.IsValid())
			{
				continue;
			}
			Child->bVisible = IsAlwaysOnChrome(Child->ElementName);
			if (Child->ElementName == TEXT("RootGameplay_FloatyToolbar_Field"))
			{
				// Start expanded, but retain retail's 100..132 height limits so the
				// bottom resize grip can reveal or conceal the second shortcut row.
				Child->Y -= 32;
				Child->Height = Child->AuthoredHeight = 132;
				UACEUIElementManager::ApplyFloatyResizeLayout(Child);
			}

		}

		/**
		 * SideVitals is authored at (0,0) but its edge anchors are L=3/R=3 (center):
		 * retail UpdateForParentSizeChange centers it at (Vw−460)/2, clear of the
		 * 150px indicator strip. ApplyEdgeAnchoredLayout handles this — no X hack.
		 */
		TSharedPtr<FACEUIElement> Examination;
		TSharedPtr<FACEUIElement> Panel;
		for (const TSharedPtr<FACEUIElement>& Child : Root->Children)
		{
			if (!Child.IsValid())
			{
				continue;
			}
			if (Child->ElementName == TEXT("RootGameplay_FloatyExamination_Field"))
			{
				Examination = Child;
			}
			else if (Child->ElementName == TEXT("RootGameplay_FloatyPanel_Field"))
			{
				Panel = Child;
				Panel->Y -= 32; // Keep the inventory above the full two-row toolbar.
			}
		}
		// MainChat is z=600; raise exam/panel so they are not buried under chat where they overlap.
		if (Examination.IsValid() && Examination->ZLevel < 700)
		{
			Examination->ZLevel = 700;
		}
		if (Panel.IsValid() && Panel->ZLevel < 800)
		{
			Panel->ZLevel = 800;
		}
		Root->SortChildrenByZ();

		// PanelPages children all start visible in LayoutDesc — only one page may show at a time.
		for (const TSharedPtr<FACEUIElement>& Child : Root->Children)
		{
			if (!Child.IsValid() || Child->ElementName != TEXT("RootGameplay_FloatyPanel_Field"))
			{
				continue;
			}
			for (const TSharedPtr<FACEUIElement>& PChild : Child->Children)
			{
				if (!PChild.IsValid() || PChild->ElementName != TEXT("PanelPages"))
				{
					continue;
				}
				for (const TSharedPtr<FACEUIElement>& Page : PChild->Children)
				{
					if (Page.IsValid())
					{
						Page->bVisible = false;
					}
				}
			}
			break;
		}
		// Examination bodies are stacked in LayoutDesc — show none until appraisal.
		if (Examination.IsValid())
		{
			for (const TSharedPtr<FACEUIElement>& C : Examination->Children)
			{
				if (!C.IsValid())
				{
					continue;
				}
				if (C->ElementName == TEXT("BasicCreatureExamineUI")
					|| C->ElementName == TEXT("ItemExamineUI")
					|| C->ElementName == TEXT("SpellExamineUI"))
				{
					C->bVisible = false;
				}
			}
		}
	}

	void ApplyLayoutDefaultVisibility(uint32 LayoutId, UACEUIElementManager* Manager)
	{
		if (!Manager)
		{
			return;
		}
		if (LayoutId == ACEUI::LayoutId::CharacterManagement)
		{
			// Template root is only used to stamp list rows — hide the stray 0,0 instance.
			Manager->SetElementVisibleByName(TEXT("CharacterSlotTemplate"), false);
			return;
		}
		if (LayoutId != ACEUI::LayoutId::ClassicGameplay)
		{
			return;
		}
		const TSharedPtr<FACEUIElement> Synthetic = Manager->GetSyntheticRoot();
		if (!Synthetic.IsValid())
		{
			return;
		}
		for (const TSharedPtr<FACEUIElement>& Root : Synthetic->Children)
		{
			ApplyClassicGameplayDefaultVisibility(Root);
		}
	}
}

void UACEUILayoutResolver::Initialize(UACEDatSubsystem* InDat, UACEUIElementManager* InManager)
{
	Dat = InDat;
	Manager = InManager;
	bReady = true;
}

void UACEUILayoutResolver::Shutdown()
{
	PreparedLayout.Reset();
	Dat = nullptr;
	Manager = nullptr;
	bReady = false;
}

TSharedPtr<FACEUIElement> UACEUILayoutResolver::LoadTemplate(uint32 LayoutId, uint32 ElementId)
{
	FString Json;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Json, *ResolvedLayoutPath(LayoutId))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root)) return nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Roots = nullptr;
	if (!Root->TryGetArrayField(TEXT("roots"), Roots)) return nullptr;
	TFunction<TSharedPtr<FACEUIElement>(const TSharedPtr<FJsonObject>&)> Find;
	Find = [&](const TSharedPtr<FJsonObject>& Node) -> TSharedPtr<FACEUIElement>
	{
		if (!Node) return nullptr;
		FString Hex;
		uint32 Id = 0;
		if (Node->TryGetStringField(TEXT("elementId"), Hex) && ParseHexU32(Hex, Id) && Id == ElementId)
		{
			TSharedPtr<FACEUIElement> Element;
			ParseElementNode(nullptr, Node, Element);
			return Element;
		}
		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (Node->TryGetArrayField(TEXT("children"), Children))
			for (const auto& Child : *Children) if (auto Result = Find(Child->AsObject())) return Result;
		return nullptr;
	};
	for (const auto& Node : *Roots) if (auto Result = Find(Node->AsObject())) return Result;
	return nullptr;
}

bool UACEUILayoutResolver::PrepareLayout(uint32 LayoutId, double BudgetSeconds)
{
    check(IsInGameThread());
    TRACE_CPUPROFILER_EVENT_SCOPE(ACE_PrepareUILayout);
    if (!Manager) return false;
    if (!PreparedLayout || PreparedLayout->Id != LayoutId)
    {
        PreparedLayout = MakeShared<FACEPreparedUILayout>();
        PreparedLayout->Id = LayoutId;
        const FString Path = ResolvedLayoutPath(LayoutId);
        auto Context = MakeShared<UE::FInheritedContextBase, ESPMode::ThreadSafe>();
        Context->CaptureInheritedContext();
        // File I/O and JSON parsing must not join a game-thread asset-load wait.
        // This worker owns only detached JSON, never UI elements or UObjects.
        PreparedLayout->Json = Async(EAsyncExecution::ThreadPool, [Path, Context]()
        {
            auto Scope = Context->RestoreInheritedContext();
            TRACE_CPUPROFILER_EVENT_SCOPE(ACE_ReadUILayout);
            FString Text;
            TSharedPtr<FJsonObject> Root;
            if (!FFileHelper::LoadFileToString(Text, *Path)
                || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)) return TSharedPtr<FJsonObject>();
            return Root;
        });
        return false;
    }
    auto& Prepared = *PreparedLayout;
    if (!Prepared.bStarted)
    {
        if (!Prepared.Json.IsReady()) return false;
        const auto Root = Prepared.Json.Get();
        const TArray<TSharedPtr<FJsonValue>>* Roots = nullptr;
        Prepared.bStarted = true;
        Prepared.bFailed = !Root || !Root->TryGetArrayField(TEXT("roots"), Roots);
        if (!Prepared.bFailed)
            for (int32 I = Roots->Num()-1; I >= 0; --I)
                Prepared.Pending.Add({(*Roots)[I]->AsObject(), nullptr, nullptr});
    }
    // A failed preload lets LoadLayout report the normal error; do not hold
    // portal space indefinitely waiting for a task that has already failed.
    if (Prepared.bFailed) return true;
    const double Deadline = FPlatformTime::Seconds() + FMath::Max(0.0, BudgetSeconds);
    int32 Count = 0;
    while (!Prepared.Pending.IsEmpty() && Count < 128
        && (Count == 0 || FPlatformTime::Seconds() < Deadline))
    {
        auto Node = Prepared.Pending.Pop(EAllowShrinking::No);
        ++Count;
        if (!Node.Json) continue;
        if (Node.Finalize)
        {
            ApplyPanelTabs(Node.Json, Node.Finalize);
            continue;
        }
        TSharedPtr<FACEUIElement> Element;
        if (!ParseElementNode(Manager, Node.Json, Element, false)) continue;
        if (Node.Parent) Node.Parent->AddChild(Element);
        else Prepared.Roots.Add(Element);
        Prepared.Pending.Add({Node.Json, nullptr, Element});
        const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
        if (Node.Json->TryGetArrayField(TEXT("children"), Children))
            for (int32 I = Children->Num()-1; I >= 0; --I)
                Prepared.Pending.Add({(*Children)[I]->AsObject(), Element, nullptr});
    }
    return Prepared.Pending.IsEmpty();
}

bool UACEUILayoutResolver::LoadLayout(uint32 LayoutId)
{
	if (!Manager)
	{
		return false;
	}

    if (PreparedLayout && PreparedLayout->Id == LayoutId && PreparedLayout->bStarted
        && !PreparedLayout->bFailed && PreparedLayout->Pending.IsEmpty())
    {
        auto Roots = MoveTemp(PreparedLayout->Roots);
        PreparedLayout.Reset();
        Manager->ClearRoots();
        for (const auto& Root : Roots) Manager->AddRoot(Root);
        ApplyLayoutDefaultVisibility(LayoutId, Manager);
        Manager->SyncLockedChromeVisibility();
        Manager->LoadFloatyLayout();
        UE_LOG(LogTemp, Log, TEXT("ACE UILayoutResolver: installed prepared 0x%08X (%d roots)"), LayoutId, Roots.Num());
        return !Roots.IsEmpty();
    }

	const FString Path = ResolvedLayoutPath(LayoutId);
	if (Path.IsEmpty() || !FPaths::FileExists(Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("ACE UILayoutResolver: missing resolved layout 0x%08X (%s)"),
			LayoutId, *Path);
		return false;
	}

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("ACE UILayoutResolver: failed to read %s"), *Path);
		return false;
	}

	TSharedPtr<FJsonObject> RootObj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("ACE UILayoutResolver: invalid JSON %s"), *Path);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* RootsArr = nullptr;
	if (!RootObj->TryGetArrayField(TEXT("roots"), RootsArr))
	{
		UE_LOG(LogTemp, Warning, TEXT("ACE UILayoutResolver: no roots in %s"), *Path);
		return false;
	}

	Manager->ClearRoots();
	int32 Loaded = 0;
	for (const TSharedPtr<FJsonValue>& Val : *RootsArr)
	{
		TSharedPtr<FJsonObject> ElObj = Val->AsObject();
		TSharedPtr<FACEUIElement> RootEl;
		if (ParseElementNode(Manager, ElObj, RootEl))
		{
			Manager->AddRoot(RootEl);
			++Loaded;
		}
	}

	ApplyLayoutDefaultVisibility(LayoutId, Manager);
	Manager->SyncLockedChromeVisibility();
	Manager->LoadFloatyLayout();

	UE_LOG(LogTemp, Log, TEXT("ACE UILayoutResolver: loaded 0x%08X from %s (%d roots)"),
		LayoutId, *Path, Loaded);
	return Loaded > 0;
}
