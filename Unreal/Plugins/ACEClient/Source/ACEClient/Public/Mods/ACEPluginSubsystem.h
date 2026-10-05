#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "ACETypes.h"
#include "ACEPluginSubsystem.generated.h"

class FACEPluginVM;
class SWidget;
class AACEPlayerController;
struct FACEWaypointLandmark { FVector2D Coordinates; FString Name, Type; };
struct FACEPluginRouteSegment { FVector Start,End; FLinearColor Color; float Thickness; };

struct FACEClientPlugin
{
    FString Id, Name, Version, Description, Directory, Status = TEXT("Stopped"), ProfileName = TEXT("Default");
    TArray<FString> Permissions;
    TSharedPtr<FJsonObject> Manifest, Profile;
    TSharedPtr<FACEPluginVM> VM;
    bool Enabled = false;
    bool Running = false;
    bool CanResumeMeta = false, ResumeMetaPending = false;
    uint32 VMSourceHash = 0, VMProfileHash = 0;
    TWeakPtr<class FACESession> VMSession;
    double NextAction = 0;
    double NextDecision = 0;
    int32 Player = 0;
    FString Server,MetaState;
};

/** Versioned, opt-in script plugins. No native injection or server protocol extensions. */
UCLASS()
class ACECLIENT_API UACEPluginSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
    friend class FACEPluginHostTest;
    friend class FACEPluginEquipmentTest;
    friend class FACEPluginRequestsTest;
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    void Discover();
    TArray<TSharedPtr<FACEClientPlugin>> Plugins;
    FString Notice;
    bool Start(const FString& Id);
    void StopAll(const FString& Reason, bool PreserveMeta = false);
    void Stop(const FString& Id, const FString& Reason = TEXT("Stopped"), bool PreserveMeta = false);
    void SetEnabled(const FString& Id, bool Enabled);
    bool SaveProfile(const FString& Id, const FString& Name, const FString& Json, bool bStopRunning = true);
    bool LoadProfile(const FString& Id, const FString& Name);
    TSharedPtr<FJsonObject> InspectLegacyProfile(const FString& Path);
    bool SaveLegacyLoot(const FString& Name,const TSharedPtr<FJsonObject>& Document);
    bool ImportLegacyProfile(const FString& Path,const FString& Name);
    bool SaveImportedProfile(const FString& Name,const TSharedPtr<FJsonObject>& Profile,const FString& Source);
    bool RunUCMCommand(const FString& Arguments);
    FString GetProfileFolder() const;
    bool SetProfileFolder(const FString& Folder);
    TArray<FString> GetProfileFiles(const FString& Extension) const;
    bool SelectProfileFile(const FString& Filename);
    bool ClearProfileFile(const FString& Extension);
    bool SaveLootProfile(const FString& Name);
    bool LoadLootProfile(const FString& Name);
    FString ProfileJson(const FString& Id) const;
    FString UserDirectory() const;
    TSharedRef<SWidget> MakePanel(const FString& PluginId = FString());
    void TogglePluginWindow(const FString& Id);
    bool IsPluginWindowOpen(const FString& Id) const;
    FVector2D GetPluginWindowPosition(const FString& Id, const FVector2D& Default) const;
    void SavePluginWindowPosition(const FString& Id, const FVector2D& Position);
    void TogglePanel();
    TSharedRef<SWidget> MakeUCMPanel(const FString& InitialPage=FString());
    TSharedRef<SWidget> MakeWaypointPanel(bool bMap=false, bool bArrowOnly=false);
    TSharedRef<SWidget> MakeWaypointMapIcon();
    TSharedRef<SWidget> MakeWaypointDungeonOverlay();
    bool IsWaypointMapUnlocked() const;
    bool IsWaypointEnabled() const;
    bool IsWaypointUnlocked() const;
    FACEPosition WaypointPlayerPosition() const;
    bool SetWaypoint(const FString& Coordinates);
    void SetWaypoint(FVector2D Coordinates, uint32 DungeonLandblock=0);
    bool GetWaypoint(FVector2D& Coordinates, uint32& DungeonLandblock) const;
    bool WaypointOption(const FString& Key, bool Default=true) const;
    void SetWaypointOption(const FString& Key,bool Value);
    void ClearWaypoint();
    bool ImportWaypointLocations(const FString& Path);
    const TArray<FACEWaypointLandmark>& GetWaypointLocations();
    uint64 WaypointLocationsRevision=0;
    void RecordRouteAction(const FString& Type, float Charge = .5f);
    void RefreshAutomationData();
    const TMap<int32, FACEAppraisalInfo>& GetPluginAppraisals() const { return Appraisals; }
    uint64 DataRevision = 0;
    UFUNCTION() void ObserveAppraisal(const FACEAppraisalInfo& Info);
    void ObserveUseDone(uint32 Error);
    void ExtendSnapshot(const TSharedPtr<FJsonObject>& Out);
    bool HasClearCombatSight(const FACEWorldObject& Target) const;
    /** Plugin inspection/buffing includes both carried and equipped gear. */
    bool IsOwnedPluginItem(const FACEWorldObject& Item) const;
    bool ExecuteInventory(FACEClientPlugin& P, const TSharedPtr<FJsonObject>& Intent, const FString& Action);
    void DrawRoute();
    bool IsDrivingMovement() const { return !MovementOwner.IsEmpty()||!FastCastOwner.IsEmpty(); }
    void ObservePlayerTell(const FString& Text,const FString& Sender,int32 SenderId);
    TArray<TSharedPtr<FJsonValue>> BuffCommands() const;
    FString BuffRequestStatus;
    int32 QueuedBuffRequests() const { return BuffRequests.Num(); }
    void ClearBuffRequests();
    bool RequestForceBuff();
    void CancelForceBuff();
    bool IsForceBuffRequested() const { return ForceBuffRequest!=0; }

    void RecordWaypoint(const FString& Id);
    void AddBuff(const FString& Id, int32 SpellId);
    TSharedPtr<FJsonObject> Snapshot();
    /** Called before the normal prediction/network pipeline; never teleports the pawn. */
    void ApplyMovement(AACEPlayerController* Controller, float& Forward, float& Right, float& Turn,
        bool Manual, bool Blocked, bool VR, const FVector& Facing);
private:
    bool Tick(float DeltaTime);
    void Execute(FACEClientPlugin& Plugin, const TSharedPtr<FJsonObject>& Intent);
    void SaveSettings();
    TSharedPtr<FACEClientPlugin> Find(const FString& Id) const;
    FTSTicker::FDelegateHandle Ticker;
    TSharedPtr<SWidget> DesktopPanel;
    TSharedPtr<class SACEPluginDesktop> DesktopDock;
    void UpdateDesktopDock();
    void RemoveDesktopDock();
    FString MovementOwner;
    FString FastCastOwner;
    bool FastCastStarted = false;
    FString UseApproachOwner;
    uint32 ForceBuffSerial=0,ForceBuffRequest=0;
    bool bForceBuffOnly=false;
    bool bUCMCommandOnly=false;
    TArray<TSharedPtr<FJsonValue>> UCMCommands;
    TOptional<float> FaceHeading;
    FACEPosition MoveTarget;
    float MoveArrivalRadius = 70.f;
    FVector LastMovePosition = FVector::ZeroVector;
    double MoveExpires = 0, LastProgress = 0;
    TSharedPtr<FJsonObject> Settings;
    FString StorageRoot;
    TArray<FACEWaypointLandmark> WaypointLocations;
    bool bWaypointLocationsLoaded=false;
    TMap<int32, FACEAppraisalInfo> Appraisals;
    TSharedPtr<FJsonObject> CachedSpeciesNames;
    TMap<int32, double> CorpseFirstSeen;
    TMap<int32, double> AppraisalRequests;
    TArray<TSharedPtr<FJsonValue>> CachedInventory;
    uint64 InventoryRevision = MAX_uint64, InventoryAppraisalRevision = MAX_uint64;
    uint32 InventoryEligibility = 0;
    int32 DataPlayer = 0;
    FString DataServer;
    TWeakPtr<class FACESession> ObservedSession;
    FDelegateHandle UseDoneHandle;
    FDelegateHandle TellHandle, MetaChatHandle, CombatFeedbackHandle;
    void ObserveCombatFeedback(const FString& Name,int32 Damage,bool Incoming,bool Critical);
    void RecordCombatOutcome(bool Hit);
    void AddCombatOutcome(int32 Target,bool Hit,bool Unhittable=false);
    void ObserveOffensiveSpellChat(const FString& Text,const FString& Sender,int32 Type);
    void ExpireOffensiveCasts(double Now);
    struct FOffensiveCast
    {
        int32 Spell=0,Target=0;
        FString Owner,SpellName,TargetName;
        double Sent=0,Started=0;
    };
    TArray<FOffensiveCast> OffensiveCasts;
    int32 PhysicalAttackTarget=0;
    double PhysicalAttackUntil=0;
    FString PhysicalAttackOwner;
    uint32 CombatOutcomeSerial=0;
    TArray<TSharedPtr<FJsonValue>> CombatOutcomes;
    void ObserveMetaChat(const FString& Text,const FString& Sender,int32 Type);
    TArray<TSharedPtr<FJsonValue>> MetaChatEvents,MetaPortalEvents;
    uint32 MetaChatSerial=0,MetaPortalSerial=0;
    bool MetaPortalSpace=false;
    TSharedPtr<FJsonObject> RuntimeRoute;
    TArray<TSharedPtr<FJsonValue>> BuffRequests;
    TMap<int32,double> LastBuffRequest;
    uint32 BuffRequestSerial=0;
    double LastAppraisalRequest = 0;
    double NextAppraisalScan = 0;
    int32 PendingSpell = 0, PendingSpellTarget = 0;
    bool PendingManaRefresh = false;
    double PendingSpellAt = 0;
    TArray<TSharedPtr<FJsonValue>> ItemBuffs;
    TArray<TSharedPtr<FJsonValue>> Debuffs, PendingDebuffCasts;
    uint32 DebuffRevision=0;
    int32 LastDebuffTarget=0,LastDebuffSpell=0;
    uint32 LastActionError = 0, ActionSerial = 0;
    int32 LastCompletedSpell = 0;
    UPROPERTY(Transient) TObjectPtr<class UProceduralMeshComponent> RouteMesh;
    UPROPERTY(Transient) TObjectPtr<AActor> RouteActor;
    TArray<FACEPluginRouteSegment> RouteSegments;
    int32 RoutePoint=1;
    uint32 RouteSignature=0;
    double RouteRebuiltAt=0;
    double NextRouteCheck=0;

    TArray<int32> CachedSpellIds, CachedSchoolSkills;
    TArray<TSharedPtr<FJsonValue>> CachedSpells;
    TArray<TSharedPtr<FJsonValue>> CachedKnownSpellValues;
    uint64 CachedSpellRevision = MAX_uint64;
};
