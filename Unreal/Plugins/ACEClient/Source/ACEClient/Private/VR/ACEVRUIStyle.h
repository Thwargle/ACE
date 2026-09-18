#pragma once
#include "CoreMinimal.h"
#include "Widgets/SWidget.h"

class UACEClientSubsystem;
namespace ACEVRUIStyle
{
inline const FLinearColor Gold = FLinearColor(FColor(216,175,91));
inline const FLinearColor TextColor = FLinearColor(FColor(255,242,204));
inline const FLinearColor Background = FLinearColor(FColor(9,13,20,250));
TSharedRef<SWidget> Text(UACEClientSubsystem* Client, const FString& Value, float Height, float Width, FLinearColor Color = TextColor);
TSharedRef<SWidget> Frame(TSharedRef<SWidget> Body, float Padding = 12.f);
}
