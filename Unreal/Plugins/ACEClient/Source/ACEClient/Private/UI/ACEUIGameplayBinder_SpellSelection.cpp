#include "UI/ACEUIGameplayBinder.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEPlayerController.h"
#include "VR/ACEVRComponent.h"
#include "ACEDatSubsystem.h"
#include "ACESpellbookFilters.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIResourceResolver.h"
#include "UI/ACEUIElementManager.h"

void UACEUIGameplayBinder::HandleSpellLearned(int32 SpellId)
{
	if (!Client || !Manager || !Canvas || SpellId <= 0) return;
	const auto Session = Client->GetSession();
	if (!Session || Session->GetState() != EACESessionState::InWorld || !Session->GetKnownSpells().Contains(SpellId)) return;
	if (PlayerController)
		if (auto* VR = PlayerController->GetVRComponent(); VR && VR->RevealLearnedSpellInMenu(SpellId)) return;
	// Reveal the learned spell even if its school/level was unchecked. Retain
	// all other filter choices instead of resetting the player's book filters.
	if (auto* Dat = Canvas->GetResourceResolver() ? Canvas->GetResourceResolver()->GetDatSubsystem() : nullptr)
	{
		uint32 School = 0, Level = 0;
		if (Dat->TryGetSpellSchoolAndLevel(SpellId, School, Level))
		{
			const uint32 OldSchools = SpellbookSchoolFilterMask, OldLevels = SpellbookLevelFilterMask;
			if (School >= 1 && School <= 5) SpellbookSchoolFilterMask |= 1u << (School - 1);
			if (Level >= 1 && Level <= 8) SpellbookLevelFilterMask |= 1u << (Level - 1);
			if (OldSchools != SpellbookSchoolFilterMask || OldLevels != SpellbookLevelFilterMask)
				Session->SendSpellbookFilters(ACESpellbookFilters::ToWire(
					SpellbookSchoolFilterMask, SpellbookLevelFilterMask, Session->GetSpellbookFilters()));
		}
	}
	SelectedSpellbookId = SpellId;
	ShowPanelPage(TEXT("SpellManagementPanel_Field"));
	SyncSpellPanelTab(TEXT("SpellbookPage"));
	bRevealLearnedSpell = true;
	RefreshSpellbookOverlays();
	SyncSpellbookScrollbar();
	if (PlayerController)
		if (auto* VR = PlayerController->GetVRComponent(); VR && VR->IsActive())
			VR->OpenRetailPanel(NAME_None);
}

void UACEUIGameplayBinder::SyncSpellTabSelection()
{
	if (!Client) return;
	const auto Session = Client->GetSession();
	if (!Session) return;
	const int32 Tab = FMath::Clamp(Client->GetActiveSpellBar(), 0, 7);
	const bool bChangedTab = SelectionSpellTab != Tab;
	if (bChangedTab)
	{
		if (SelectionSpellTab != INDEX_NONE)
		{
			auto& Old = SpellTabSelections[SelectionSpellTab];
			Old.Slot = SelectedCombatSpellSlot;
			Old.Offset = SpellHotbarScrollOffset;
		}
		SelectionSpellTab = Tab;
		const auto& Saved = SpellTabSelections[Tab];
		SelectedCombatSpellSlot = Saved.Slot;
		SpellHotbarScrollOffset = Saved.Offset;
		bRevealSelectedSpell = true;
		LastSpellClickSlot = INDEX_NONE;
		LastSpellClickId = 0;
		LastSpellClickTime = 0;
	}
	const auto& Bar = Session->GetSpellBar(Tab);
	auto& Saved = SpellTabSelections[Tab];
	// Keep the same spell through edits/reordering while this tab was hidden.
	if (SelectedCombatSpellSlot == Saved.Slot && Saved.SpellId)
	{
		const int32 Found = Bar.Find(Saved.SpellId);
		if (Found != INDEX_NONE && Found != SelectedCombatSpellSlot)
		{
			SelectedCombatSpellSlot = Found;
			bRevealSelectedSpell = true;
		}
	}
	int32 Clamped = FMath::Clamp(SelectedCombatSpellSlot, -1, FMath::Max(0, Bar.Num()-1));
	if (Clamped >= 0 && (!Bar.IsValidIndex(Clamped) || Bar[Clamped] == 0))
	{
		// PlayerModule can pad favorites to 56 entries. Empty storage is not
		// a selectable spell (including after deleting the selected favorite).
		Clamped = Bar.IndexOfByPredicate([](int32 Spell) { return Spell != 0; });
		if (Clamped == INDEX_NONE) Clamped = 0;
	}
	if (Clamped != Saved.Slot) bRevealSelectedSpell = true;
	SelectedCombatSpellSlot = Clamped;
	const int32 Spell = Bar.IsValidIndex(Clamped) ? Bar[Clamped] : 0;
	const bool bChangedSelection = Saved.Slot != Clamped || Saved.SpellId != Spell;
	Saved.Slot = Clamped;
	Saved.SpellId = Spell;
	Saved.Offset = SpellHotbarScrollOffset;
	if ((bChangedTab || bChangedSelection) && PlayerController)
		if (auto* VR = PlayerController->GetVRComponent())
			VR->RestoreSpellBarSelection(Clamped < 0 ? BuiltInSpellId : Spell);
}

void UACEUIGameplayBinder::SelectCombatSpellSlot(int32 Slot)
{
	SyncSpellTabSelection();
	SelectedCombatSpellSlot = Slot;
	// Explicit selection supersedes identity retained from before an edit.
	if (SelectionSpellTab != INDEX_NONE)
	{
		SpellTabSelections[SelectionSpellTab].SpellId = 0;
	}
	bRevealSelectedSpell = true;
	SyncSpellTabSelection();
	// Re-selecting the innate spell must also replace a spellbook-only VR selection.
	if (Slot < 0 && PlayerController)
		if (auto* VR = PlayerController->GetVRComponent()) VR->RestoreSpellBarSelection(BuiltInSpellId);
}

void UACEUIGameplayBinder::SetCombatSpellBar(int32 Tab)
{
	if (!Client || Tab < 0 || Tab >= 8) return;
	SyncSpellTabSelection();
	Client->SetActiveSpellBar(Tab);
	// Tab selection is presentation only; reapplying magic stance here sends
	// ChangeCombatMode and interrupts the server's current spell.
	if (Manager) for (int32 I = 0; I < 8; ++I)
		Manager->SetElementVisibleByName(FString::Printf(TEXT("Spellcasting_Bank%d"), I + 1),
			CombatMode == int32(ACECombatMode::Magic) && I == Tab);
	SyncSpellTabSelection();
	SyncSpellcastTabChrome();
	RefreshSpellHotbarOverlays();
}

void UACEUIGameplayBinder::StepCombatSpellSelection(int32 Direction, bool bFirst, bool bLast)
{
	if (!Client) return;
	SyncSpellTabSelection();
	const auto Bar = Client->GetSpellBar(Client->GetActiveSpellBar());
	TArray<int32, TInlineAllocator<64>> Spells;
	for (int32 I = 0; I < Bar.Num(); ++I) if (Bar[I] != 0) Spells.Add(I);
	if (Spells.IsEmpty()) return;
	const int32 Current = Spells.Find(SelectedCombatSpellSlot);
	const int32 Next = bFirst ? 0 : bLast ? Spells.Num()-1 : Current == INDEX_NONE
		? (Direction > 0 ? 0 : Spells.Num()-1) : (Current + Direction + Spells.Num()) % Spells.Num();
	SelectCombatSpellSlot(Spells[Next]);
}
