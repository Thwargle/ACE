#pragma once
#include "Components/Image.h"
#include "ACECaptureImage.generated.h"

/** Composites SceneCapture's premultiplied RGB / inverse-opacity render target. */
UCLASS()
class ACECLIENT_API UACECaptureImage : public UImage
{
    GENERATED_BODY()
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};
