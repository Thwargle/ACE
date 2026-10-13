#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/Texture2D.h"
#include "Widgets/SCompoundWidget.h"

struct FACEClientPlugin;
namespace ACEPluginPresentation
{
    bool Valid(const TSharedPtr<FJsonObject>& Object,FString& Error);
    bool ValidAsset(const FString& Asset);
}

/** One bounded cache shared by a plugin's toolbar, manager and Lua image widgets. */
class FACEPluginImageCache
{
public:
    UTexture2D* Load(const FString& Directory,const FString& Asset);
private:
    TMap<FString,TStrongObjectPtr<UTexture2D>> Images;
    int64 ImageBytes=0;
};

class SACEPluginBadge : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SACEPluginBadge){} SLATE_ARGUMENT(TSharedPtr<FACEClientPlugin>,Plugin) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual void Tick(const FGeometry&,double,float) override;
private:
    void Refresh();
    TSharedPtr<FACEClientPlugin> Plugin;
    FString Asset;
    bool Initialized=false;
    FSlateBrush Brush;
};
