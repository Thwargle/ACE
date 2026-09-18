#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ACELoginHUD.generated.h"

/**
 * Canvas HUD for ACE Viewer:
 * - While disconnected: backup login chrome if UMG login is not yet visible.
 * - While in-world: retail-style object hover blurbs (dark box, gold border) bottom-left of cursor.
 */
UCLASS()
class ACEVIEWER_API AACELoginHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ACE")
	bool bForceAlwaysShow = false;
};
