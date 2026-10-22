#include "ACEDisplayView.h"
#include "SceneViewExtension.h"
#include "SceneView.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "HAL/IConsoleManager.h"

namespace
{
TAutoConsoleVariable<float> Brightness(TEXT("ace.Display.Brightness"), 1.f,
    TEXT("Final scene brightness multiplier (0.2 to 2)."), ECVF_RenderThreadSafe);
TAutoConsoleVariable<float> Contrast(TEXT("ace.Display.Contrast"), 1.f,
    TEXT("Scene contrast around middle grey (0.5 to 2)."), ECVF_RenderThreadSafe);

float ValidValue(float Value, float Min, float Max)
{
    return FMath::IsFinite(Value) ? FMath::Clamp(Value, Min, Max) : 1.f;
}
}

class FACEDisplayPS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FACEDisplayPS);
    SHADER_USE_PARAMETER_STRUCT(FACEDisplayPS, FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
        SHADER_PARAMETER(FVector4f, SceneUV)
        SHADER_PARAMETER(FVector4f, OutputRect)
        SHADER_PARAMETER(float, Brightness)
        SHADER_PARAMETER(float, Contrast)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FACEDisplayPS, "/ACERetail/DisplaySettings.usf", "MainPS", SF_Pixel);

namespace
{
class FACEDisplayView final : public FSceneViewExtensionBase
{
public:
    FACEDisplayView(const FAutoRegister& Auto, UGameInstance* Instance, bool IncludeCaptures)
        : FSceneViewExtensionBase(Auto), GameInstance(Instance), bIncludeSceneCaptures(IncludeCaptures) {}

    // Retail's additive effects must be composed before adjusting the image.
    virtual int32 GetPriority() const override { return -100; }
    virtual void SetupViewFamily(FSceneViewFamily&) override {}
    virtual void BeginRenderViewFamily(FSceneViewFamily&) override {}
    virtual void SetupView(FSceneViewFamily& Family, FSceneView& View) override
    {
        if (!Eligible(View) || !Family.bIsHDR) return;
        // HDR has a different transfer function. Use native scene-linear grading
        // there rather than clamping an HDR output into the SDR display range.
        const float Gain = ValidValue(Brightness.GetValueOnGameThread(), .2f, 2.f);
        const float Amount = ValidValue(Contrast.GetValueOnGameThread(), .5f, 2.f);
        View.FinalPostProcessSettings.ColorGain.W *= Gain;
        View.FinalPostProcessSettings.ColorContrast.W *= Amount;
    }

    virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& View,
        FPostProcessingPassDelegateArray& Callbacks, bool) override
    {
        if (Pass != EPostProcessingPass::Tonemap || !Eligible(View) || View.Family->bIsHDR) return;
        if (FMath::IsNearlyEqual(Brightness.GetValueOnRenderThread(), 1.f)
            && FMath::IsNearlyEqual(Contrast.GetValueOnRenderThread(), 1.f)) return;
        Callbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FACEDisplayView::Adjust));
    }

protected:
    virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override
    {
        const UWorld* World = Context.GetWorld();
        return GameInstance.IsValid() && World && World->GetGameInstance() == GameInstance.Get();
    }

private:
    TWeakObjectPtr<UGameInstance> GameInstance;
    bool bIncludeSceneCaptures;

    bool Eligible(const FSceneView& View) const
    {
        return (!View.bIsSceneCapture || (bIncludeSceneCaptures && View.Family->SceneCaptureSource == SCS_FinalColorLDR))
            && !View.bIsReflectionCapture && !View.Family->UseDebugViewPS();
    }

    FScreenPassTexture Adjust(FRDGBuilder& Graph, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
    {
        const auto Scene = FScreenPassTexture::CopyFromSlice(Graph, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
        auto Output = Inputs.OverrideOutput;
        if (!Output.IsValid()) Output = FScreenPassRenderTarget::CreateFromInput(Graph, Scene,
            ERenderTargetLoadAction::ENoAction, TEXT("ACE.DisplaySettings"));
        auto* P = Graph.AllocParameters<FACEDisplayPS::FParameters>();
        P->SceneTexture = Scene.Texture;
        P->LinearSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
        const FVector2f Size(Scene.Texture->Desc.Extent);
        P->SceneUV = FVector4f(Scene.ViewRect.Width()/Size.X, Scene.ViewRect.Height()/Size.Y,
            Scene.ViewRect.Min.X/Size.X, Scene.ViewRect.Min.Y/Size.Y);
        P->OutputRect = FVector4f(Output.ViewRect.Min.X, Output.ViewRect.Min.Y,
            1.f/Output.ViewRect.Width(), 1.f/Output.ViewRect.Height());
        P->Brightness = ValidValue(Brightness.GetValueOnRenderThread(), .2f, 2.f);
        P->Contrast = ValidValue(Contrast.GetValueOnRenderThread(), .5f, 2.f);
        P->RenderTargets[0] = Output.GetRenderTargetBinding();
        TShaderMapRef<FACEDisplayPS> Shader(GetGlobalShaderMap(View.GetFeatureLevel()));
        AddDrawScreenPass(Graph, RDG_EVENT_NAME("ACE Brightness / Contrast"), View,
            FScreenPassTextureViewport(Output), FScreenPassTextureViewport(Scene), Shader, P);
        return Output;
    }
};
}

TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> CreateACEDisplayView(UGameInstance* GameInstance, bool IncludeCaptures)
{
    if (GameInstance) return FSceneViewExtensions::NewExtension<FACEDisplayView>(GameInstance, IncludeCaptures);
    return {};
}
