#pragma once
#include "CoreMinimal.h"
#include "Framework/Commands/InputChord.h"
class APlayerController;
namespace ACEInputBindings
{
struct FAction { FKey Key; const TCHAR* Label; const TCHAR* Page; };
ACECLIENT_API const TArray<FAction>& Actions();
ACECLIENT_API bool Down(const APlayerController* PC, FKey DefaultKey);
ACECLIENT_API bool Pressed(const APlayerController* PC, FKey DefaultKey);
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
