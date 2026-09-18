#pragma once
#include "Commandlets/Commandlet.h"
#include "ACEPrepareRuntimeMaterialsCommandlet.generated.h"

/** Uses the runtime material factories to prepare shader assets for the cooker. */
UCLASS()
class UACEPrepareRuntimeMaterialsCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UACEPrepareRuntimeMaterialsCommandlet();
	virtual int32 Main(const FString& Params) override;
	static bool PrepareMaterials();
};
