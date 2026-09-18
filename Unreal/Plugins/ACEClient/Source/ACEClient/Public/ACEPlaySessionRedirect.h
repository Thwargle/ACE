#pragma once

#include "CoreMinimal.h"
#include "ACETypes.h"

/** PIE login redirect: WC maps play on LoginMapPath, then travel to the baked world on Enter World. */
namespace ACEPlaySessionRedirect
{
	extern ACECLIENT_API FString GPendingWcTravelMapAfterLogin;
	extern ACECLIENT_API const FString LoginMapPath;
	/** Baked WC world — ClientTravel target after login when PIE did not start on Dereth. */
	extern ACECLIENT_API const FString WorldMapPath;

	struct FPendingEnteredWorldAfterTravel
	{
		int32 PlayerGuid = 0;
		FACEPosition SpawnPosition;
	};

	extern ACECLIENT_API TOptional<FPendingEnteredWorldAfterTravel> GPendingEnteredWorldAfterTravel;
}
