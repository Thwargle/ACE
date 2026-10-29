#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ACEMobileSettings.generated.h"

USTRUCT()
struct FACETouchControl
{
    GENERATED_BODY()
    UPROPERTY(Config) FName Id;
    UPROPERTY(Config) FName Action;
    UPROPERTY(Config) FVector2D Position = FVector2D(.5,.5);
    UPROPERTY(Config) float Size = 64;
    UPROPERTY(Config) bool bVisible = true;
};

/** Phone configuration is deliberately separate from desktop and Quest settings. */
UCLASS(Config=ACEMobile)
class ACECLIENT_API UACEMobileSettings : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config) bool bEnabled = false;
    UPROPERTY(Config) TArray<FACETouchControl> Controls;
    UPROPERTY(Config) float LookSensitivity = .22f;
    UPROPERTY(Config) float ControlOpacity = .8f;
    UPROPERTY(Config) bool bLeftHanded = false;
    UPROPERTY(Config) bool bPinnedChat = false;
    UPROPERTY(Config) TArray<FString> HiddenHUDPlugins;
    UPROPERTY(Config) TArray<FString> ExtraHUDPlugins;
    void ResetLayout();
    void Sanitize();
    static bool IsEnabled();
    static void ApplyTextureBudget();
};

namespace ACEMobile
{
    ACECLIENT_API int32 TexturePoolMB(uint64 PhysicalBytes);
    struct FButtonContent
    {
        FSlateRect Icon,Label;
        float Radius=12,Border=1.25f,Font=12,Padding=6;
    };
    ACECLIENT_API FButtonContent ButtonContent(FVector2D Size);
    ACECLIENT_API FSlateRect FitImage(FVector2D ImageSize,const FSlateRect& Bounds);
    ACECLIENT_API FSlateRect TurnKeyRect(float Width,int32 Side);
    ACECLIENT_API FSlateRect DPadKeyRect(float Width,FVector2D Direction);
    // Coordinates are in a 540-unit high safe area. Every target remains at
    // least 56 units wide; inventory pages scroll instead of shrinking icons.
    ACECLIENT_API FVector2D LogicalSize(FVector2D Available);
    ACECLIENT_API FSlateRect ControlRect(const FACETouchControl& Control, FVector2D Size);
    ACECLIENT_API bool HitControl(const FACETouchControl& Control, FVector2D Size, FVector2D Point);
    ACECLIENT_API bool CanPlace(const TArray<FACETouchControl>& Controls, int32 Index,
        FVector2D Position, float Diameter, FVector2D Size, float Top=100);
    ACECLIENT_API void FitLayout(TArray<FACETouchControl>& Controls, FVector2D Size, float Top=100);
    ACECLIENT_API FVector2D StickVector(FVector2D Delta, float Radius);
    ACECLIENT_API FVector2D MovementAxes(FVector2D Delta, float Radius);
    ACECLIENT_API int32 WheelIndex(FVector2D Delta, int32 Count, float Inner=55, float Outer=230);
    ACECLIENT_API const TArray<FName>& Actions();
    ACECLIENT_API FString ActionLabel(FName Action);
}
