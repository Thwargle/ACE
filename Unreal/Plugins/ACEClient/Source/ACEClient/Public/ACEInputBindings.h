#pragma once
#include "CoreMinimal.h"
#include "Framework/Commands/InputChord.h"
class APlayerController;
namespace ACEInputBindings
{
constexpr int32 BindingSlots = 4; // Three retail mappings plus a separate controller mapping.
ACECLIENT_API FKey StickDirectionKey(FKey Axis, float Value);
ACECLIENT_API float Value(const APlayerController* PC, FKey ActionKey);
// Key is a stable action identifier (also the legacy settings key), not
// necessarily its current/default physical key.
struct FAction
{
 FKey Key; const TCHAR* Label; const TCHAR* Page; TArray<FInputChord> DefaultBindings; int32 Context;
 FAction(FKey Id,const TCHAR* Name,const TCHAR* Category,TArray<FInputChord> Bindings,int32 Mode=0)
  :Key(Id),Label(Name),Page(Category),DefaultBindings(MoveTemp(Bindings)),Context(Mode){}
};
ACECLIENT_API FKey Action(const TCHAR* Name);
ACECLIENT_API FKey Shortcut(int32 Slot);
ACECLIENT_API FKey SpellSlot(int32 Slot);
ACECLIENT_API void SetCombatContext(int32 Mode);
// DoNothing is an explicit input override, not an absent binding.
struct FBlockedBinding { FString Group; FInputChord Chord; int32 Context=0; };
ACECLIENT_API void ReplaceBlockedBindings(const TSet<FString>& Groups, const TArray<FBlockedBinding>& Bindings);
ACECLIENT_API const TArray<FBlockedBinding>& GetBlockedBindings();
ACECLIENT_API FKey ApplicationsKey();
ACECLIENT_API FKey NumpadEnterKey();
struct FImportResult
{
 bool bSuccess=false;
 int32 BindingCount=0;
 FString Error;
 TArray<FString> Skipped;
 TArray<FString> UnchangedContexts;
};
// Import changes the edit draft only; OK/Save commits it and Cancel discards it.
ACECLIENT_API FImportResult ImportRetailKeymap(const FString& Text);
ACECLIENT_API FImportResult ImportRetailKeymapFile(const FString& Path);
ACECLIENT_API bool ExportRetailKeymapFile(const FString& Path, FString& Error);
ACECLIENT_API const TArray<FAction>& Actions();
ACECLIENT_API bool Down(const APlayerController* PC, FKey DefaultKey);
ACECLIENT_API bool Pressed(const APlayerController* PC, FKey DefaultKey);
// Plugin editors use Slate text controls outside the native chat widget.
ACECLIENT_API bool IsTextEntryFocused();
// Retail CommandList: newest held direction wins; releasing it restores the
// previously held direction. Preserve physical event order across remaps.
ACECLIENT_API float MovementAxis(const APlayerController* PC, FKey Positive, FKey Negative,
 const TMap<FKey,uint64>& PressOrder,FKey AdditionalPositive=FKey(),FKey AdditionalNegative=FKey());
// Controller buttons can be held as modifiers in addition to Shift/Ctrl/Alt/Cmd.
// Supply the controller when matching live input (including immediate UI actions).
ACECLIENT_API bool Matches(FKey ActionKey, const FInputChord& Input, const APlayerController* PC=nullptr);
ACECLIENT_API void BeginEdit();
ACECLIENT_API void Defaults();
ACECLIENT_API void Revert();
ACECLIENT_API void Cancel();
ACECLIENT_API void Commit();
ACECLIENT_API void Reload();
ACECLIENT_API FInputChord Get(FKey DefaultKey, int32 Slot);
ACECLIENT_API TArray<FKey> GetControllerModifiers(FKey ActionKey, int32 Slot);
ACECLIENT_API void Set(FKey DefaultKey, int32 Slot, FInputChord Chord, const TArray<FKey>& ControllerModifiers={});
ACECLIENT_API bool IsEditing();
ACECLIENT_API FString GetKeymapFileName();
ACECLIENT_API void SetKeymapFileName(const FString& Name);
}
