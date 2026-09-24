#include "UI/ACEUIGameplayBinder.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEPlayerController.h"
#include "VR/ACEVRComponent.h"

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
	const int32 Clamped = FMath::Clamp(SelectedCombatSpellSlot, -1, FMath::Max(0, Bar.Num()-1));
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
	SyncSpellTabSelection();
	SyncSpellcastTabChrome();
	RefreshSpellHotbarOverlays();
}

void UACEUIGameplayBinder::StepCombatSpellSelection(int32 Direction, bool bFirst, bool bLast)
{
	if (!Client) return;
	SyncSpellTabSelection();
	const auto Bar = Client->GetSpellBar(Client->GetActiveSpellBar());
	if (Bar.IsEmpty()) return;
	const int32 Next = bFirst ? 0 : bLast ? Bar.Num()-1 : SelectedCombatSpellSlot < 0
		? (Direction > 0 ? 0 : Bar.Num()-1) : (SelectedCombatSpellSlot + Direction + Bar.Num()) % Bar.Num();
	SelectCombatSpellSlot(Next);
}
