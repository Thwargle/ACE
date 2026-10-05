#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "ACEPluginPanel.generated.h"

UCLASS()
class ACECLIENT_API UACEPluginPanel : public UWidget
{
    GENERATED_BODY()
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};
