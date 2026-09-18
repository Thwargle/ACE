#pragma once

#include "Commandlets/Commandlet.h"
#include "ACEWorldBakeBuildWorldCommandlet.generated.h"

UCLASS()
class UACEWorldBakeBuildWorldCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UACEWorldBakeBuildWorldCommandlet();

	virtual int32 Main(const FString& Params) override;
};
