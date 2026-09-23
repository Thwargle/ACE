#pragma once
#include "CoreMinimal.h"
#include "Framework/Commands/InputChord.h"
class APlayerController;
namespace ACEInputBindings
{
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
struct FImportResult
{
 bool bSuccess=false;
 int32 BindingCount=0;
 FString Error;
 TArray<FString> Skipped;
};
// Import changes the edit draft only; OK/Save commits it and Cancel discards it.
ACECLIENT_API FImportResult ImportRetailKeymap(const FString& Text);
ACECLIENT_API FImportResult ImportRetailKeymapFile(const FString& Path);
ACECLIENT_API bool ExportRetailKeymapFile(const FString& Path, FString& Error);
ACECLIENT_API const TArray<FAction>& Actions();
ACECLIENT_API bool Down(const APlayerController* PC, FKey DefaultKey);
ACECLIENT_API bool Pressed(const APlayerController* PC, FKey DefaultKey);
ACECLIENT_API bool Matches(FKey ActionKey, const FInputChord& Input);
ACECLIENT_API void BeginEdit();
ACECLIENT_API void Defaults();
ACECLIENT_API void Revert();
ACECLIENT_API void Cancel();
ACECLIENT_API void Commit();
ACECLIENT_API void Reload();
ACECLIENT_API FInputChord Get(FKey DefaultKey, int32 Slot);
ACECLIENT_API void Set(FKey DefaultKey, int32 Slot, FInputChord Chord);
ACECLIENT_API bool IsEditing();
}
