#include "UI/ACEUIGameplayBinder.h"
#include "UI/ACEUICanvasWidget.h"
#include "UI/ACEUIResourceResolver.h"
#include "ACEClientSubsystem.h"
#include "ACEDatSubsystem.h"
#include "ACEPlayerController.h"
#include "ACESession.h"
#include "ACEClientBuild.h"
#include "ACEFrameDiagnostics.h"
#include "ACERuntimeOptions.h"
#include "UI/ACEVideoSettingsWidget.h"
#include "ACEWorldEntityActor.h"
#include "ACECharacterAppearanceComponent.h"
#include "Components/SceneComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Dat/ACEOutdoorPortalPlan.h"
#include "Dat/ACECellTransit.h"
#include "PrimitiveSceneProxy.h"
#include "RenderingThread.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "HAL/FileManager.h"

bool UACEUIGameplayBinder::TryDispatchMiscCommand(const FString& Cmd, const FString& Args)
{
    if (!Client) return false;
    if (TryDispatchUILayoutCommand(Cmd, Args)) return true;
    auto Error=[this](const FString& Text) { AppendLocalChatLine(Text,ACEChatMessageType::ChatError); };
    auto Print=[this](const FString& Text) { AppendLocalChatLine(Text,ACEChatMessageType::System); };
    if (Cmd==TEXT("fillcomps")) { FillVendorComponents(Args); return true; }
    if (Cmd==TEXT("acueperf"))
    {
        for(const FString& Line:ACEFrameDiagnostics::Describe(Client->GetWorld(),FApp::GetDeltaTime()*1000))
        {
            Print(Line);
            UE_LOG(LogTemp,Display,TEXT("ACE performance report: %s"),*Line);
        }
        return true;
    }
    if (Cmd==TEXT("acueobject"))
    {
        // Local-only and on demand: retain the full report in the log as well as
        // chat, before a reset/relog destroys evidence of an invisible object.
        auto Report=[&](const FString& Text)
        {
            Print(Text);
            UE_LOG(LogTemp,Display,TEXT("ACE object report: %s"),*Text);
        };
        FACEWorldObject Object;
        if (!Client->GetWorldObject(Client->GetSelectedObject().Guid, Object))
        {
            Error(TEXT("Select a world object, then type /acueobject to display its placement details."));
            return true;
        }
        Report(FString::Printf(TEXT("Object report %s: %s [%08X / WCID %d]"),
            ACEClientBuild::Version, *Object.Name, uint32(Object.Guid), Object.WeenieClassId));
        Report(FString::Printf(TEXT("Setup %08X Motion %08X Placement %d Scale %.4f"),
            uint32(Object.SetupId), uint32(Object.MotionTableId), Object.PlacementId, Object.Scale));
        Report(FString::Printf(TEXT("Type %08X Desc %08X Physics %08X Parent %08X"),
            uint32(Object.ItemType), uint32(Object.ObjectDescriptionFlags), uint32(Object.PhysicsState), uint32(Object.ParentGuid)));
        const FACEPosition& P = Object.Position;
        Report(FString::Printf(TEXT("Received %08X [%.4f %.4f %.4f] Contact %d/%d"),
            uint32(P.CellId), P.Location.X, P.Location.Y, P.Location.Z, P.bHasContactState, P.bIsGrounded));
        Report(FString::Printf(TEXT("Rotation %.4f %.4f %.4f %.4f Velocity %.4f %.4f %.4f"),
            P.RotationW, P.RotationXYZ.X, P.RotationXYZ.Y, P.RotationXYZ.Z, Object.Velocity.X, Object.Velocity.Y, Object.Velocity.Z));
        // An explicit diagnostic can scan actors once; no work is added to frame updates.
        UWorld* World = Client->GetWorld();
        FVector Eye=FVector::ZeroVector; FConvexVolume View;
        const bool HasView=ACEOutdoorPortalPlan::TryGetPlayerViewFrustum(World,Eye,View);
        if (PlayerController && PlayerController->PlayerCameraManager)
        {
            int32 Width=0,Height=0; PlayerController->GetViewportSize(Width,Height);
            Report(FString::Printf(TEXT("View %dx%d FOV %.2f Projection available %d"),
                Width,Height,PlayerController->PlayerCameraManager->GetFOVAngle(),HasView));
        }
        if (World) for (TActorIterator<AACEWorldEntityActor> It(World); It; ++It)
        {
            if (It->GetACEGuid() != Object.Guid) continue;
            const float Units = FMath::Max(It->WorldScale, SMALL_NUMBER);
            if (HasView)
            {
                FACEPosition Player=Client->GetPlayerPosition();
                if (PlayerController) PlayerController->TryGetLocallyPredictedPosition(Player);
                auto* Dat=World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UACEDatSubsystem>() : nullptr;
                uint32 ViewCell=Player.CellId;
                const bool Resolved=Dat && ACECellTransit::ResolveViewerCellId(*Dat,Player.CellId,Player.ToUnrealLocation(Units),Eye,Units,ViewCell);
                const bool Inside=Resolved && ACECellTransit::IsIndoorCell(ViewCell) && Dat->IsPointInsideEnvCell(ViewCell,Eye,Units);
                const uint32 Block=uint32(P.CellId);
                const FVector EyeLocal=FACEPosition::AceVectorToUnreal(Eye,1.f/Units)-FVector((Block>>24)*192.,((Block>>16)&255)*192.,0);
                Report(FString::Printf(TEXT("Camera (object block local) [%s] Player %08X [%s] View cell %08X Resolved %d Inside %d"),
                    *EyeLocal.ToString(),uint32(Player.CellId),*Player.Location.ToString(),ViewCell,Resolved,Inside));
                if (PlayerController && PlayerController->PlayerCameraManager)
                    Report(FString::Printf(TEXT("Camera rotation [%s] Near clip %.3f AC units"),
                        *PlayerController->PlayerCameraManager->GetCameraRotation().ToString(),GNearClippingPlane/Units));
                if (APawn* Pawn=PlayerController ? PlayerController->GetPawn() : nullptr)
                    if (auto* Boom=Pawn->FindComponentByClass<USpringArmComponent>())
                        Report(FString::Printf(TEXT("Camera boom requested %.3f actual %.3f probe %.3f collision %d"),
                            Boom->TargetArmLength/Units,FVector::Distance(Boom->GetComponentLocation()+Boom->TargetOffset,Eye)/Units,
                            Boom->ProbeSize/Units,Boom->bDoCollisionTest));
            }
            const FVector Delta = (It->GetActorLocation() - P.ToUnrealLocation(Units)) / Units;
            const FVector Scale = It->GetActorScale3D();
            Report(FString::Printf(TEXT("Actor delta (UE axes/AC units) [%.4f %.4f %.4f] Scale [%.4f %.4f %.4f]"),
                Delta.X, Delta.Y, Delta.Z, Scale.X, Scale.Y, Scale.Z));
            Report(FString::Printf(TEXT("Actor cell %08X Admitted %d Hidden %d Suppressed %d Physics %08X"),
                uint32(It->GetLastAceCellId()),It->IsCellVisible(),It->IsHidden(),It->IsMeshSuppressed(),uint32(It->PhysicsState)));
            if (auto* Appearance = It->Appearance.Get())
            {
                Report(FString::Printf(TEXT("Parts %d Animated %d Visible %d Mesh %d"),
                    Appearance->GetPartCount(), Appearance->IsComponentTickEnabled(), It->IsCellVisible(), It->bUsingDatMesh));
                FBox Bounds(ForceInit);
                if (Appearance->GetVisualWorldBounds(Bounds))
                {
                    Report(FString::Printf(TEXT("Bounds offset [%s] Extent [%s] (AC units) In frustum %d Distance %.2f"),
                        *((Bounds.GetCenter()-It->GetActorLocation())/Units).ToString(),*(Bounds.GetExtent()/Units).ToString(),
                        HasView ? int32(View.IntersectBox(Bounds.GetCenter(),Bounds.GetExtent())) : -1,
                        HasView ? FVector::Distance(Eye,Bounds.GetCenter())/Units : -1.));
                    if(HasView) for(int32 Sample=0;Sample<2;++Sample)
                    {
                        const FVector Point=Bounds.GetCenter()+FVector(0,0,Sample?Bounds.GetExtent().Z*.75:0);
                        FCollisionQueryParams Query(SCENE_QUERY_STAT(ACEObjectReport),true,*It);
                        if(PlayerController && PlayerController->GetPawn())Query.AddIgnoredActor(PlayerController->GetPawn());
                        FHitResult Hit;
                        const bool Blocked=World->LineTraceSingleByChannel(Hit,Eye,Point,ECC_Camera,Query);
                        // Collision evidence only: invisible PhysicsBSP can span
                        // a drawn opening. Do not label this as GPU visibility.
                        Report(FString::Printf(TEXT("Camera collision ray %s: %s component=%s distance=%.3f"),
                            Sample?TEXT("upper"):TEXT("center"),Blocked?*GetNameSafe(Hit.GetActor()):TEXT("clear"),
                            *GetNameSafe(Hit.GetComponent()),Blocked?Hit.Distance/Units:0));
                    }
                }
                if (auto* Part = Appearance->GetPartMesh(0))
                {
                    const FVector Local = Part->GetRelativeLocation() / Units;
                    Report(FString::Printf(TEXT("Part 0 local [%.4f %.4f %.4f] Height above received origin %.4f"),
                        Local.X, Local.Y, Local.Z, (Part->GetComponentLocation().Z - P.ToUnrealLocation(Units).Z) / Units));
                }
                // Bound chat/log volume even for creatures with many body parts.
                struct FRenderPart
                {
                    FPrimitiveSceneProxy* Proxy=nullptr;
                    bool Drawn=false,Occlusion=false;
                    FVector Origin=FVector::ZeroVector,BoundsOrigin=FVector::ZeroVector,Extent=FVector::ZeroVector;
                };
                TArray<FRenderPart> RenderParts;
                RenderParts.SetNum(FMath::Min(Appearance->GetPartCount(),8));
                for(int32 Index=0;Index<RenderParts.Num();++Index)
                    if(auto* Part=Cast<UPrimitiveComponent>(Appearance->GetPartMesh(Index)))RenderParts[Index].Proxy=Part->SceneProxy;
                // Read the resident render state without rebuilding it: resetting
                // graphics first can erase precisely the failure being reported.
                ENQUEUE_RENDER_COMMAND(ACEObjectReportRenderState)([&RenderParts](FRHICommandListImmediate&)
                {
                    for(auto& Part:RenderParts) if(auto* Proxy=Part.Proxy)
                    {
                        Part.Drawn=Proxy->IsDrawnInGame();Part.Occlusion=Proxy->CanBeOccluded();
                        Part.Origin=Proxy->GetLocalToWorld().GetOrigin();
                        Part.BoundsOrigin=Proxy->GetBounds().Origin;Part.Extent=Proxy->GetBounds().BoxExtent;
                    }
                });
                FlushRenderingCommands();
                for (int32 Index=0;Index<FMath::Min(Appearance->GetPartCount(),8);++Index)
                {
                    auto* Part=Cast<UProceduralMeshComponent>(Appearance->GetPartMesh(Index));
                    if (!Part) continue;
                    Report(FString::Printf(TEXT("Part %d Registered %d Visible %d Hidden %d Main %d Capture-only %d Owner-no-see %d Proxy %d Dirty %d"),
                        Index,Part->IsRegistered(),Part->IsVisible(),Part->bHiddenInGame,Part->bRenderInMainPass,
                        Part->bVisibleInSceneCaptureOnly,Part->bOwnerNoSee,Part->SceneProxy!=nullptr,Part->IsRenderStateDirty()));
                    const auto& Render=RenderParts[Index];
                    if(Render.Proxy)
                        Report(FString::Printf(TEXT("Part %d Render drawn %d Occlusion %d Transform delta [%s] Bounds offset [%s] Extent [%s]"),
                            Index,Render.Drawn,Render.Occlusion,*((Render.Origin-Part->GetComponentLocation())/Units).ToString(),
                            *((Render.BoundsOrigin-It->GetActorLocation())/Units).ToString(),*(Render.Extent/Units).ToString()));
                    Report(FString::Printf(TEXT("Part %d Draw distance %.3f/%.3f Detail %d Recently rendered %d Material %s"),
                        Index,Part->MinDrawDistance/Units,Part->CachedMaxDrawDistance/Units,int32(Part->DetailMode),
                        Part->WasRecentlyRendered(.2f),*GetNameSafe(Part->GetMaterial(0))));
                    const int32 Level=HasView ? Part->GetDistanceLevel(Eye) : INDEX_NONE;
                    int32 VisibleSections=0,Triangles=0,Vertices=0; float MinimumOpacity=1.f; bool HasOpacity=false;
                    for (int32 Section=0;Section<Part->GetNumSections();++Section)
                    {
                        const auto* Geometry=Part->GetProcMeshSection(Section);
                        if (!Geometry || !Geometry->bSectionVisible || Geometry->ProcIndexBuffer.IsEmpty()) continue;
                        ++VisibleSections;
                        Triangles+=Geometry->GetRenderIndexCount()/3;Vertices+=Geometry->ProcVertexBuffer.Num();
                        float Opacity=1.f;
                        if (auto* Material=Part->GetMaterial(Section); Material && Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OpacityMul")),Opacity))
                        { MinimumOpacity=FMath::Min(MinimumOpacity,Opacity); HasOpacity=true; }
                    }
                    Report(FString::Printf(TEXT("Part %d Sections %d/%d LOD %d LOD sections %d Minimum opacity %.3f Triangles %d Vertices %d"),
                        Index,VisibleSections,Part->GetNumSections(),Level,
                        Level==INDEX_NONE ? Part->GetNumSections() : Part->GetDistanceLevels()[Level].SectionCount,
                        HasOpacity ? MinimumOpacity : -1.f,Triangles,Vertices));
                }
            }
            return true;
        }
        Report(TEXT("No rendered world actor for this selection (inventory, hidden, or still loading)."));
        return true;
    }
    if (Cmd==TEXT("age") || Cmd==TEXT("birth") || Cmd==TEXT("pkl") || Cmd==TEXT("pklite"))
    {
        if (!Args.IsEmpty()) Error(FString::Printf(TEXT("Usage: /%s"),*Cmd));
        else if (auto S=Client->GetSession(); S.IsValid())
        {
            if (Cmd==TEXT("age") || Cmd==TEXT("birth"))
                S->SendGameActionString(Cmd==TEXT("age") ? ACEGameAction::QueryAge : ACEGameAction::QueryBirth,FString());
            else S->SendSimpleGameAction(ACEGameAction::EnterPkLite);
        }
        return true;
    }
    if (Cmd==TEXT("day"))
    {
        Client->SendSetSingleCharacterOption(0x05,!Client->IsCharacterOptionSet(0x05));
        Print(Client->IsCharacterOptionSet(0x05) ? TEXT("Always daylight outdoors enabled.") : TEXT("Always daylight outdoors disabled."));
        return true;
    }
    if (Cmd==TEXT("fps") || Cmd==TEXT("framerate"))
    {
        ACERuntimeOptions::Set(TEXT("ShowFrameRate"),ACERuntimeOptions::Get(TEXT("ShowFrameRate"))>.5f ? 0.f : 1.f);
        ACERuntimeOptions::Apply();
        if (auto* Video=Cast<UACEVideoSettingsWidget>(VideoSettings)) Video->RefreshFrameRateOption();
        return true;
    }
    if (Cmd==TEXT("version"))
    {
        Print(FString::Printf(TEXT("%s %s (%s, Unreal Engine %s)"),
            ACEClientBuild::ProductName(PlayerController && PlayerController->IsVRActive()), ACEClientBuild::Version,
            PLATFORM_ANDROID ? TEXT("Quest") : PLATFORM_LINUX ? TEXT("Linux") : TEXT("Windows"), *FEngineVersion::Current().ToString()));
        return true;
    }
    if (Cmd==TEXT("loc"))
    {
        const FACEPosition P=Client->GetPlayerPosition();
        Print(FString::Printf(TEXT("Your location is: 0x%08X [%.6f %.6f %.6f] %.6f %.6f %.6f %.6f"),
            uint32(P.CellId),P.Location.X,P.Location.Y,P.Location.Z,P.RotationW,P.RotationXYZ.X,P.RotationXYZ.Y,P.RotationXYZ.Z));
        return true;
    }
    if (Cmd==TEXT("endurance"))
    {
        Print(TEXT("Strength and Endurance improve health regeneration (up to about 110% in addition to regeneration spells). Endurance reduces attacking stamina cost by up to 50%, with a minimum cost of one.\n"
            "With the corresponding Melee or Missile Defense skill trained or specialized, Endurance gives up to a 75% chance to avoid the stamina cost of a successful evasion.\n"
            "Strength and Endurance also provide up to 50% resistance to drain/harm and natural resistance to the seven damage types. Natural resistance and Life protections do not stack; the stronger protection applies.\n"
            "Character Information shows your resistance and regeneration categories: Poor, Mediocre, Hardy, Resilient, and Indomitable."));
        return true;
    }
    if (Cmd==TEXT("loadfile"))
    {
        if (Args.IsEmpty()) { Error(TEXT("You must provide a file name.")); return true; }
        FString Path=Args; Path.TrimQuotesInline();
        if (FPaths::IsRelative(Path)) Path=FPaths::Combine(FPaths::ProjectDir(),Path);
        Path=FPaths::ConvertRelativePathToFull(Path); FPaths::CollapseRelativeDirectories(Path);
        if (ActiveChatFiles.Contains(Path)) { Error(TEXT("A command file cannot load itself recursively.")); return true; }
        TArray<FString> Lines;
        if (!FFileHelper::LoadFileToStringArray(Lines,*Path)) { Error(FString::Printf(TEXT("Cannot open file %s"),*Path)); return true; }
        if (ActiveChatFiles.Num()>=16) { Error(TEXT("Command file nesting limit reached.")); return true; }
        ActiveChatFiles.Add(Path);
        const int32 Source=ChatSourceWindow;
        for (FString Line:Lines)
        {
            Line.ReplaceInline(TEXT("%DATE%"),*FDateTime::Now().ToFormattedString(TEXT("%Y-%m-%d-%a")));
            if (Line.TrimStartAndEnd().IsEmpty()) continue;
            Print(Line);
            TrySendChatFromEntry(&Line,Source);
        }
        ActiveChatFiles.Remove(Path);
        return true;
    }
    return false;
}

void UACEUIGameplayBinder::FillVendorComponents(const FString& Args)
{
    auto Error=[this](const FString& Text) { AppendLocalChatLine(Text,ACEChatMessageType::ChatError); };
    auto Print=[this](const FString& Text) { AppendLocalChatLine(Text,ACEChatMessageType::System); };
    TArray<FString> Words; Args.ToLower().ParseIntoArrayWS(Words);
    if (Words.Num()==1 && Words[0]==TEXT("clear"))
    {
        // Retail's bulk clear marker is rejected by older ACE servers. Individual
        // removals use the same protocol and persist correctly on those servers too.
        const auto Desired=Client->GetDesiredComponents();
        for (const auto& Pair:Desired) Client->SendSetDesiredComponentLevel(Pair.Key,0);
        RefreshComponentOverlays(); Print(TEXT("Component refill quantities cleared.")); return;
    }
    int32 Category=0;
    int64 Budget=0;
    const TCHAR* Categories[]={TEXT(""),TEXT("scarabs"),TEXT("herbs"),TEXT("powders"),TEXT("potions"),TEXT("talismans"),TEXT("tapers"),TEXT("peas")};
    bool bValid=Words.Num()<=2;
    for (const auto& Word:Words)
    {
        if (Word.IsNumeric() && !Budget)
        {
            int64 Value=0;
            if (!LexTryParseString(Value,*Word) || Value<=0 || Value>MAX_int32) bValid=false;
            else Budget=Value;
        }
        else
        {
            int32 Match=0;
            for (int32 I=1;I<UE_ARRAY_COUNT(Categories);++I)
                if (Word==Categories[I] || Word==FString(Categories[I]).LeftChop(1)) Match=I;
            if (!Match || Category) bValid=false;
            Category=Match;
        }
    }
    if (!bValid) { Error(TEXT("Usage: /fillcomps [scarabs|herbs|powders|potions|talismans|tapers|peas] [maximum cost], or /fillcomps clear")); return; }
    if (!OpenVendorGuid) { Error(TEXT("You must be using a vendor to fill your components.")); return; }
    auto* Dat=Canvas && Canvas->GetResourceResolver() ? Canvas->GetResourceResolver()->GetDatSubsystem() : nullptr;
    if (!Dat) { Error(TEXT("Spell component data is unavailable.")); return; }
    TMap<int32,int32> Carried;
    auto Tally=[&](const TArray<FACEWorldObject>& Items) { for (const auto& Item:Items) Carried.FindOrAdd(Item.WeenieClassId)+=FMath::Max(1,Item.StackSize); };
    Tally(Client->GetPackItems(Client->GetPlayerGuid()));
    for (const auto& Pack:Client->GetPlayerPacks()) Tally(Client->GetPackItems(Pack.Guid));
    auto Stock=Client->GetVendorMerchandise();
    if (Stock.IsEmpty()) Stock=Client->GetPackItems(OpenVendorGuid);
    const auto Desired=Client->GetDesiredComponents();
    int64 Cost=0; int32 Added=0;
    TArray<FString> Missing;
    bool bReachedBudget=false;
    for (const auto& Component:Dat->GetSpellComponents())
    {
        if (Category && uint32(Category)!=Component.Type) continue;
        const int32 Want=Desired.FindRef(Component.Wcid);
        int32 Need=FMath::Max(0,Want-Carried.FindRef(Component.Wcid));
        for (const auto& Item:Stock) if (Item.WeenieClassId==int32(Component.Wcid))
            for (const auto& Pair:VendorBuyCart) if (Pair.Value==Item.Guid) Need-=Pair.Key;
        if (Need<=0) continue;
        for (const auto& Item:Stock)
        {
            if (Item.WeenieClassId!=int32(Component.Wcid)) continue;
            auto* Existing=VendorBuyCart.FindByPredicate([&](const auto& Pair) { return Pair.Value==Item.Guid; });
            const int32 Already=Existing ? Existing->Key : 0;
            const int32 Qty=Item.VendorQuantityAvailable<0 ? Need : FMath::Min(Need,FMath::Max(0,Item.VendorQuantityAvailable-Already));
            if (!Qty) continue;
            const int64 Price=FMath::Max<int64>(1,FMath::CeilToInt64(double(Item.Value)*Client->GetVendorSellRate()*Qty/FMath::Max(1,Item.StackSize)-.1));
            // A refill must fit the limit as a whole; preserve the existing cart.
            if (Budget && Cost+Price>Budget) { bReachedBudget=true; break; }
            if (Existing) Existing->Key+=Qty; else VendorBuyCart.Emplace(Qty,Item.Guid);
            Cost+=Price; Added+=Qty; Need-=Qty;
            if (!Need) break;
        }
        if (bReachedBudget) break;
        if (Need>0) Missing.Add(Component.Name);
    }
    ActiveVendorPage=1;
    SyncVendorPageVisibility(); RefreshVendorOverlays(); RefreshVendorTabLabels(); RefreshVendorButtonLabels(); RefreshVendorInfoTexts();
    if (bReachedBudget) Print(TEXT("Buying aborted; max price reached."));
    if (!Missing.IsEmpty()) Print(TEXT("There was not enough: ")+FString::Join(Missing,TEXT(", ")));
    if (Added) Print(FString::Printf(TEXT("Added %d components to the buy list. Review the list and choose Buy All."),Added));
    else if (Missing.IsEmpty() && !bReachedBudget) Print(TEXT("Your carried components and buy list already meet the refill quantities."));
}
