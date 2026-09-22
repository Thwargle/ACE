#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ACEVideoSettingsWidget.generated.h"
class UComboBoxString;
class UCheckBox;
class USlider;
class UScrollBox;
/** An option is a user widget so its Slate SObjectWidget keeps the DAT label alive during GC. */
UCLASS()
class ACECLIENT_API UACERetailOptionWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 FString Option;
 virtual TSharedRef<SWidget> RebuildWidget() override;
};
UCLASS()
class ACECLIENT_API UACEVideoSettingsWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 UFUNCTION() void ApplyVideo();
 void ApplyInterfaceOptions();
 UFUNCTION() void ResetVideo();
 UFUNCTION() void DefaultsVideo();
 UFUNCTION() UWidget* GenerateOption(FString Option);
 UFUNCTION() void ChangeMouseTurnSpeed(float Value);
 UFUNCTION() void ResetMouseTurnSpeed();
 float GetScrollOffset() const;
 float GetScrollEnd() const;
 void SetScrollOffset(float Offset);
private:
 int32 InitialQuality = 0;
 UPROPERTY() TObjectPtr<UScrollBox> Scroll;
 UPROPERTY() TArray<TObjectPtr<UComboBoxString>> QualityLevels;
 UPROPERTY() TMap<FName,TObjectPtr<USlider>> ValueSliders;
 UPROPERTY() TMap<int32,TObjectPtr<UCheckBox>> CharacterChecks;
 UPROPERTY() TObjectPtr<UCheckBox> ActiveSoundOnly;
 UPROPERTY() TObjectPtr<UComboBoxString> Filtering;
 UPROPERTY() TObjectPtr<UComboBoxString> Resolution;
 UPROPERTY() TObjectPtr<UComboBoxString> WindowMode;
 UPROPERTY() TObjectPtr<UComboBoxString> Quality;
 UPROPERTY() TObjectPtr<UComboBoxString> FrameLimit;
 UPROPERTY() TObjectPtr<UCheckBox> VSync;
 UPROPERTY() TObjectPtr<UCheckBox> ShowFrameRate;
 UPROPERTY() TObjectPtr<USlider> MouseTurnSpeed;
 UPROPERTY() TObjectPtr<UCheckBox> InvertMouseX;
 UPROPERTY() TObjectPtr<UCheckBox> InvertMouseY;
 UPROPERTY() TObjectPtr<UComboBoxString> DesktopScale;
};
