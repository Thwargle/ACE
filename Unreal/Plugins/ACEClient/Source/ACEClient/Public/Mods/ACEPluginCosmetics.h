#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Widgets/Input/SComboBox.h"
#include "ACEPluginCosmetics.generated.h"

class UProceduralMeshComponent;
class UPrimitiveComponent;
class AACEWorldEntityActor;

UCLASS()
class ACECLIENT_API UACEPluginCosmetics : public UObject
{
    GENERATED_BODY()
    friend class FACELocalCosmeticsRenderTest;
    friend class FACELocalCosmeticsOutfitTest;
    friend class FACEPluginCosmeticsTest;
    friend class FACEPluginCosmeticsIntegrationTest;
    friend class FACEPluginCosmeticEffectsTest;
public:
    UACEPluginCosmetics(const FObjectInitializer& ObjectInitializer);
    bool Initialize(class UACEPluginSubsystem* InHost, const struct FACEClientPlugin& Plugin, FString& Error);
    void Shutdown();
    virtual UWorld* GetWorld() const override;
    class UGameInstance* GetGameInstance() const;
    void RunTool(const TArray<FString>& Arguments, bool bLoadAfter = false);
    void Disable();
    void SetFit(const FVector& Offset, const FRotator& Rotation, float Scale);
    FString Status = TEXT("Enable this plugin to select a local cosmetic.");
    TSharedRef<class SWidget> MakePanel();
private:
    void RegisterLuaService();
    TSharedPtr<FJsonObject> ServiceSnapshot() const;
    bool ServiceExecute(const TSharedPtr<FJsonObject>& Intent, FString& Error);
    void QueueRequest(const FString& Operation);
    void CancelTool();
    void UpdateInstallations(const FString& Json);
    void UpdateCatalog(const FString& Json);
    void SelectSlot(const FString& Id);
    void SelectCatalogItem(const TSharedPtr<FJsonObject>& Item);
    TSharedRef<class SWidget> MakePaperdollPanel();
    TSharedRef<class SWidget> MakeCatalogIcon(TFunction<TSharedPtr<FJsonObject>()> Item, float Size);
    TSharedRef<class SWidget> MakeCatalogRow(TSharedPtr<FJsonObject> Item);
    FString CatalogFilter;
    FString SelectedSlot=TEXT("weapon");
    TArray<TSharedPtr<FJsonObject>> Catalog, SlotOptions;
    TMap<FString,TSharedPtr<FJsonObject>> SlotSelections;
    TWeakPtr<SComboBox<TSharedPtr<FJsonObject>>> ItemCombo;
    TSharedPtr<TArray<TSharedPtr<FJsonObject>>> ComboOptions;
    FString CatalogStatus=TEXT("Scan local items to populate the slot dropdowns.");
    bool bPaperdoll=false;
    UPROPERTY(Transient) TMap<FString,TObjectPtr<UACEPluginCosmetics>> SlotRenderers;
    UACEPluginCosmetics* RendererForSlot(const FString& Id) const;
    bool LoadSingle(const FString& Json);
    UPROPERTY() TObjectPtr<UACEPluginSubsystem> Host;
    FString PluginId, PluginName, PluginDirectory, HelperPath, CacheRoot, SourceLabel;
    TSharedPtr<FJsonObject> Configuration;
    TMap<FString,FString> Inputs;
    TArray<TSharedPtr<FJsonObject>> Fields, Presets;
    static bool LocalFile(const FString& Path);
    TArray<TSharedPtr<FJsonObject>> Installations;
    TWeakPtr<class SVerticalBox> InstallationList;
    TSharedPtr<FJsonObject> PendingRequest;
    uint32 RequestSerial=0;
    FString ToolCommand;
    bool Tick(float Dt);
    bool LoadVerified(const FString& Json);
    void RestoreOriginal();
    bool Attach(AACEWorldEntityActor* Item);
    bool AttachBody(class APawn* Pawn);
    bool AttachSkinnedBody(class APawn* Pawn);
    void UpdateSkin();
    bool ReplaceCoveredParts(class APawn* Pawn);
    void HideOriginal(UPrimitiveComponent* Part);
    struct FSkinBinding { int32 Part=0; FVector Pivot, End; FTransform ToPart; };
    struct FSkinWeight { int32 Bone=0; float Weight=0; };
    TArray<FSkinBinding> SkinBindings;
    TArray<FSkinWeight> SkinWeights;
    TArray<FVector> SkinnedVertices,SkinnedNormals;
    TArray<FTransform> LastSkinPose;
    UPROPERTY(Transient) TArray<TObjectPtr<UProceduralMeshComponent>> BareParts;
    static bool IsBodySlot(const FString& Id);
    bool MatchesLocalItem(const AACEWorldEntityActor* Item, const class APawn* Pawn, int32 Guid) const;
    void PollTool();
    void Tell(const FString& Message);
    struct FSection { TArray<int32> Indices; int32 Blend=0; int32 BodyPart=-1; };
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UVs;
    TArray<FSection> Sections;
    UPROPERTY() TArray<TObjectPtr<class UTexture2D>> Textures;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Cosmetic;
    UPROPERTY(Transient) TArray<TObjectPtr<UProceduralMeshComponent>> BodyCosmetics;
    TWeakObjectPtr<class APawn> BodyPawn;
    TWeakObjectPtr<AACEWorldEntityActor> ReplacedItem;
    TWeakObjectPtr<class UACEScriptComponent> HiddenItemParticles;
    struct FHiddenPart { TWeakObjectPtr<UPrimitiveComponent> Part; bool MainPass=true, Shadow=true; };
    TArray<FHiddenPart> HiddenParts;
    FTSTicker::FDelegateHandle Ticker;
    FProcHandle ToolProcess;
    void* PipeRead=nullptr;
    void* PipeWrite=nullptr;
    FString ToolOutput, BuildInfo, BuildInfoSnapshot, Slot;
    bool bEnableAfterTool=false, bEnabled=false;
    double NextProgressRead=0, ToolStarted=0, NextSearch=0, NextInstallCheck=0;
    int32 PlayerGuid=0;
    uint64 AppearanceRevision=0;
    FTransform Fit=FTransform::Identity;
};
