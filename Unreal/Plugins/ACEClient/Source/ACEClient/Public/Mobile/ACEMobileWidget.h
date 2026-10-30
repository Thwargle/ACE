#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Mobile/ACEMobileSettings.h"
#include "ACECharacterCreation.h"
#include "ACEMobileWidget.generated.h"

class AACEPlayerController;
class UACEClientSubsystem;
class UACEVRMenu;
class SBox;
class SEditableTextBox;
class SVerticalBox;
class SScrollBox;
class UACECaptureImage;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

USTRUCT()
struct FACEMobileActionVisual
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UTexture2D> Icon;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> Background;
    FString Label, Detail, Badge;
    int32 SpellId=0, ObjectId=0;
    uint32 ResourceId=0;
    bool Available=true;
};

/** Touch owns each finger independently; no synthetic mouse/key presses. */
UCLASS()
class ACECLIENT_API UACEMobileWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void InitializeMobile(AACEPlayerController* Controller);
    void UpdateMobile();
    void CancelTouches();
    void CancelFinger(int32 Finger);
    void OpenPanel(FName Page);
    void ClosePanel();
    bool IsHUDPluginVisible(const FString& Id) const;
    void ToggleHUDPlugin(const FString& Id);
    void Action(FName Name);
    bool IsBlockingMovement() const;
    bool IsJumpHeld() const;
    bool IsWalkHeld() const;
    FVector2D Movement() const;
    float Turning() const;
    FVector2D ConsumeLook();
    void SurfaceResized(FVector2D Size);
    FReply TouchBegin(int32 Finger,FVector2D Point,FVector2D Absolute);
    FReply TouchMove(int32 Finger,FVector2D Point);
    FReply TouchEnd(int32 Finger,FVector2D Point,FVector2D Absolute);
    int32 Paint(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer) const;
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool Children) override;
    virtual void NativeDestruct() override;
    UPROPERTY(Transient) TObjectPtr<UACEVRMenu> Menu;
    UPROPERTY(Transient) TObjectPtr<AACEPlayerController> PC;
    UPROPERTY(Transient) TObjectPtr<UACEClientSubsystem> Client;
    UPROPERTY(Transient) TObjectPtr<UACEMobileSettings> Settings;
    UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> WheelIcons;
    UPROPERTY(Transient) TMap<FName,FACEMobileActionVisual> ActionVisuals;
    UPROPERTY(Transient) TMap<uint32,TObjectPtr<UTexture2D>> VitalTextures;
    FVector2D ViewSize={960,540};
    TArray<FACETouchControl> Layout;
    FName Panel;
    bool bEditing=false,bWheel=false;
    int32 SelectedControl=INDEX_NONE,SelectedSpell=0,WheelPage=0;
private:
    friend class FACEMobileTouchTest;
    friend class FACEMobilePersistenceTest;
    // Last fitted/committed layout, used to distinguish edits from temporary
    // relocation or hidden overflow on a different screen or around HUD panels.
    TArray<FACETouchControl> FittedLayout;
    struct FFinger {FName Action;int32 Control=INDEX_NONE;FVector2D Start=FVector2D::ZeroVector,Point=FVector2D::ZeroVector;bool Moved=false;};
    TMap<int32,FFinger> Fingers;
    FVector2D LookDelta=FVector2D::ZeroVector;
    TSharedPtr<SBox> PanelHost;
    TSharedPtr<SBox> PluginRailHost;
    TSharedPtr<SBox> HUDHost;
    TArray<FString> HUDPluginIds;
    FString HUDSignature;
    float ControlsTop=100;
    void RefreshHUDPanels();
    bool WantsHUDPlugin(const FString& Id) const;
    void SetPinnedChat(bool Pinned);
    TSharedRef<SWidget> MakePinnedChat();
    void FitControls();
    TArray<FString> RailPluginIds;
    FVector2D RailSize=FVector2D::ZeroVector;
    void RefreshPluginRail();
    TSharedPtr<SEditableTextBox> ChatEntry;
    TSharedPtr<SVerticalBox> ChatLines;
    TSharedPtr<SScrollBox> ChatScroll;
    TSharedPtr<SVerticalBox> PinnedChatLines;
    TSharedPtr<SScrollBox> PinnedChatScroll;
    TSharedPtr<SEditableTextBox> PinnedChatEntry;
    FString PinnedChatDraft;
    void SendPinnedChat();
    bool bChatDirty=true;
    bool bBookDirty=false;
    FString ConfirmationText;
    UFUNCTION() void BookChanged();
    void ResolveConfirmation(bool Accept);
    UFUNCTION() void ChatReceived(const FString& Text,const FString& Sender,int32 Type);
    void RefreshChat();
    TArray<int32> WheelSpells;
    int32 LastVendor=0,LastContainer=0,LastTrade=0;
    int32 LastSession=-1,LastRosterSize=0;
    FDelegateHandle BackgroundHandle,DeactivateHandle;
    FDelegateHandle CharacterCreatedHandle;
    TUniquePtr<FACECharacterCreation> Creation;
    FString CreationMessage;
    int32 CreationTab=0;
    double LastCreationCapture=0;
    float CreationYaw=180.f;
    void BuildCharacterCreation(TSharedRef<SVerticalBox> Body);
    void StartCharacterCreation();
    TSharedRef<SWidget> CharacterPreview();
    void ReleaseCharacterPreview();
    UPROPERTY(Transient) TObjectPtr<AActor> CreationActor;
    UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> CreationCapture;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> CreationTarget;
    UPROPERTY(Transient) TObjectPtr<UACECaptureImage> CreationImage;
    void BuildPanel();
    void RefreshWheel();
    void RefreshPresentation();
    double NextPresentationRefresh=0;
    TSharedRef<SWidget> MenuTile(FName Page,TFunction<void()> Click);
    void SaveLayout();
    void Rebind(FName Action);
    FName HeaderAt(FVector2D Point) const;
};
