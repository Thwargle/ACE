#include "ACEPlaySessionRedirect.h"

namespace ACEPlaySessionRedirect
{
	FString GPendingWcTravelMapAfterLogin;
	const FString LoginMapPath = TEXT("/Engine/Maps/Templates/Template_Default");
	const FString WorldMapPath = TEXT("/Game/Maps/Dereth/Dereth");
	TOptional<FPendingEnteredWorldAfterTravel> GPendingEnteredWorldAfterTravel;
}
