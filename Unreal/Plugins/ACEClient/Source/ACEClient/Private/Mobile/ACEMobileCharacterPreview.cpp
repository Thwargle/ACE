#include "Mobile/ACEMobileWidget.h"
#include "ACEDatSubsystem.h"
#include "ACECharacterAppearanceComponent.h"
#include "UI/ACECaptureImage.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"

void UACEMobileWidget::ReleaseCharacterPreview()
{
    if(CreationActor)CreationActor->Destroy();
    CreationActor=nullptr;CreationCapture=nullptr;CreationTarget=nullptr;CreationImage=nullptr;
}
TSharedRef<SWidget> UACEMobileWidget::CharacterPreview()
{
    if(!Creation || !GetWorld())return SNullWidget::NullWidget;
    if(!CreationActor)
    {
        FActorSpawnParameters P;P.ObjectFlags|=RF_Transient;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        CreationActor=GetWorld()->SpawnActor<AActor>(AActor::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,P);
        if(!CreationActor)return SNullWidget::NullWidget;
        auto* Root=NewObject<USceneComponent>(CreationActor);CreationActor->SetRootComponent(Root);Root->RegisterComponent();
        CreationActor->SetActorLocation(FVector(-60000,-60000,-60000));
        auto* Appearance=NewObject<UACECharacterAppearanceComponent>(CreationActor);Appearance->RegisterComponent();
        CreationTarget=NewObject<UTextureRenderTarget2D>(this);CreationTarget->ClearColor=FLinearColor(0,0,0,1);
        CreationTarget->InitCustomFormat(300,400,PF_FloatRGBA,true);CreationTarget->UpdateResourceImmediate(true);
        CreationCapture=NewObject<USceneCaptureComponent2D>(CreationActor);CreationCapture->SetupAttachment(Root);CreationCapture->RegisterComponent();
        CreationCapture->TextureTarget=CreationTarget;CreationCapture->CaptureSource=ESceneCaptureSource::SCS_SceneColorHDR;
        CreationCapture->bCaptureEveryFrame=false;CreationCapture->bCaptureOnMovement=false;
        CreationCapture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
        CreationCapture->ShowFlags.SetAtmosphere(false);CreationCapture->ShowFlags.SetFog(false);CreationCapture->ShowFlags.SetMotionBlur(false);
        CreationCapture->ShowFlags.SetEyeAdaptation(false);CreationCapture->ShowFlags.SetBloom(false);CreationCapture->ShowFlags.SetAntiAliasing(false);
        CreationCapture->PostProcessSettings.bOverride_AutoExposureMethod=true;CreationCapture->PostProcessSettings.AutoExposureMethod=AEM_Manual;
        CreationCapture->bAlwaysPersistRenderingState=true;
        CreationCapture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure=true;CreationCapture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure=false;
        CreationCapture->PostProcessSettings.bOverride_ToneCurveAmount=true;CreationCapture->PostProcessSettings.ToneCurveAmount=0.f;
        CreationCapture->PostProcessSettings.bOverride_BlueCorrection=true;CreationCapture->PostProcessSettings.BlueCorrection=0.f;
        CreationCapture->PostProcessSettings.bOverride_ExpandGamut=true;CreationCapture->PostProcessSettings.ExpandGamut=0.f;
        CreationImage=NewObject<UACECaptureImage>(this);CreationImage->SetBrushResourceObject(CreationTarget);
    }
    auto* Dat=GetGameInstance()->GetSubsystem<UACEDatSubsystem>();
    auto* Appearance=CreationActor->FindComponentByClass<UACECharacterAppearanceComponent>();FACEWorldObject Object;
    if(Dat && Appearance && Creation->BuildAppearance(Object))
    {
        Object.Scale=1.f;Appearance->ApplyWorldObject(Object,100,false);
        const auto Heritage=Creation->Selection.Heritage;
        Appearance->SetPreviewAnimation(Creation->MappedAsset(0x25000010,Heritage==12?0x10000011:Heritage==13?0x10000013:0x10000005),false);
        Appearance->SetComponentTickEnabled(false);
        if(auto* MeshRoot=Appearance->GetMeshRoot())MeshRoot->SetRelativeRotation(FRotator(0,CreationYaw,0));
        CreationCapture->ClearShowOnlyComponents();TArray<UPrimitiveComponent*> Parts;CreationActor->GetComponents(Parts);
        for(auto* Part:Parts)
        {
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetLightingChannels(false,false,true);Part->SetVisibleInSceneCaptureOnly(true);
            for(int32 I=0;I<Part->GetNumMaterials();++I)Part->SetMaterial(I,Dat->CreateExaminationMaterial(Part->GetMaterial(I),CreationActor));
            CreationCapture->ShowOnlyComponent(Part);
        }
        CreationCapture->SetRelativeLocation(Heritage==12?FVector(0,-380,115):Heritage==13?FVector(0,-570,165):FVector(0,-270,95));
        CreationCapture->SetRelativeRotation(FRotator(0,90,0));CreationCapture->FOVAngle=45;
        // Refresh immediately; the panel also recaptures at 10 Hz while visible
        // so newly streamed render resources cannot leave a stale first image.
        CreationCapture->CaptureScene();
    }
    return SNew(SBox).WidthOverride(225).HeightOverride(300)[CreationImage->TakeWidget()];
}
