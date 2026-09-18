#pragma once
#include "CoreMinimal.h"
#include "Input/Events.h"
#include "UObject/Object.h"
#include "ACECharacterCreation.h"
#include "ACEUICharGenBinder.generated.h"
class UACEClientSubsystem;
class UACEUICanvasWidget;
class UACEUIElementManager;
class AACEPlayerController;
class UACERetailTextBlock;
class UImage;
class UTextureRenderTarget2D;
class USceneCaptureComponent2D;
struct FACEUIElement;

/** gmCharGenMainUI and its six pages, using the original 800x600 LayoutDesc. */
UCLASS()
class ACECLIENT_API UACEUICharGenBinder : public UObject
{
    GENERATED_BODY()
public:
    bool Initialize(UACEClientSubsystem* InClient,UACEUICanvasWidget* InCanvas,AACEPlayerController* InController);
    void Shutdown();
    void Tick(float Delta);
    bool MouseDown(FVector2D Position);
    void MouseMove(FVector2D Position);
    void MouseUp();
    bool MouseWheel(FVector2D Position,float Delta);
    bool KeyDown(const FKeyEvent& Event);
    bool KeyChar(const FCharacterEvent& Event);
    void Activate(TSharedPtr<FACEUIElement> Element);
    void SetPage(int32 InPage);
    FACECharacterCreation Model;
    int32 GetPage() const {return Page;}
private:
    friend class FACERetailCharacterCreationScreenTest;
    void Refresh();
    void RefreshPreview(float Delta);
    void RefreshSkills();
    void RefreshSummary();
    void RefreshName();
    void RefreshColors();
    void RefreshTooltip(float Delta);
    void ScrollText(TSharedPtr<FACEUIElement> Element,const FString& Text);
    void InsertName(const FString& Text);
    int32 NamePosition(FVector2D Position) const;
    void ShowDialog(int32 Action,const FString& Text);
    void CloseDialog(bool Accept);
    void RandomizePage();
    void Spin(int32 Field,int32 Direction);
    void Submit();
    TSharedPtr<FACEUIElement> Find(const TCHAR* Name) const;
    TSharedPtr<FACEUIElement> Under(const TCHAR* Parent,const TCHAR* Name) const;
    TSharedPtr<FACEUIElement> Child(TSharedPtr<FACEUIElement> Parent,uint32 Id) const;
    void Label(TSharedPtr<FACEUIElement> Element,const FString& Text);
    void Label(const TCHAR* Name,const FString& Text);
    void StaticLabels(TSharedPtr<FACEUIElement> Element);
    void Show(const TCHAR* Name,bool Visible);
    void Selected(const TCHAR* Name,bool Value);
    FVector2D Relative(FVector2D Position) const;
    UPROPERTY() TObjectPtr<UACEClientSubsystem> Client;
    UPROPERTY() TObjectPtr<UACEUICanvasWidget> Canvas;
    UPROPERTY() TObjectPtr<UACEUIElementManager> Manager;
    UPROPERTY() TObjectPtr<AACEPlayerController> Controller;
    UPROPERTY() TMap<uint32,TObjectPtr<UACERetailTextBlock>> Labels;
    TArray<TSharedPtr<FACEUIElement>> SkillRows, SummaryRows;
    TArray<int32> DisplaySkills;
    UPROPERTY() TObjectPtr<AActor> Preview;
    UPROPERTY() TObjectPtr<AActor> PreviewEnvironment;
    UPROPERTY() TMap<uint32,TObjectPtr<UImage>> ColorImages;
    UPROPERTY() TMap<uint64,TObjectPtr<UTexture2D>> ColorTextures;
    UPROPERTY() TObjectPtr<USceneCaptureComponent2D> Capture;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Target;
    UPROPERTY() TObjectPtr<UImage> PreviewImage;
    UPROPERTY() TObjectPtr<UImage> NameSelectionImage;
    UPROPERTY() TObjectPtr<UImage> NameCaretImage;
    FDelegateHandle ActivatedHandle, CreatedHandle;
    int32 Page=1, SkillScroll=0, SummaryScroll=0, DescriptionScroll=0, SelectedSkill=0, ColorField=0;
    int32 DragAttribute=-1,NameCaret=0,NameAnchor=0,RotateDirection=0,DescriptionMaxScroll=0;
    int32 SummaryMaxScroll=0,NameDisplayStart=0;
    TSharedPtr<FACEUIElement> DragScrollbar, Dialog, Tooltip;
    uint32 TooltipSource=0;
    float TooltipClock=0;
    int32 DialogAction=0;
    FString DialogText;
    FVector PreviewCamera=FVector(0,-55,165);
    FVector PreviewCameraStart=PreviewCamera,PreviewCameraTarget=PreviewCamera;
    float PreviewCameraTime=.6f;
    uint32 PreviewEnvironmentId=0;
    uint32 PreviewAnimationId=0;
    bool bPreviewWasAnimated=false;
    bool bClothes=false,bDirty=true,bPreviewDirty=true,bNameFocus=false,bSelectName=false,bReturn=false,bConfirmCredits=false,bHelp=false;
    bool bGradientDrag=false,bPreviewDrag=false,bNameDrag=false;
    float PreviewYaw=180.f,Zoom=0.f; FVector2D LastMouse;
    FString Message;
    float PreviewClock=0;
};
