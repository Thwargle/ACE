#include "ACERetailParticleView.h"
#include "SceneViewExtension.h"
#include "SceneView.h"
#include "StereoRendering.h"
#include "SceneInterface.h"
#include "Engine/World.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"

class FACERetailParticleCompositePS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FACERetailParticleCompositePS);
    SHADER_USE_PARAMETER_STRUCT(FACERetailParticleCompositePS, FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneTexture)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ParticleTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
        SHADER_PARAMETER(FVector4f, SceneUV)
        SHADER_PARAMETER(FVector4f, ParticleUV)
        SHADER_PARAMETER(FVector4f, OutputRect)
        SHADER_PARAMETER(float, ParticleGamma)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FACERetailParticleCompositePS, "/ACERetail/ParticleComposite.usf", "MainPS", SF_Pixel);

namespace
{
class FACERetailParticleView final : public FWorldSceneViewExtension
{
public:
    FACERetailParticleView(const FAutoRegister& Auto, UWorld* World) : FWorldSceneViewExtension(Auto, World) {}

    virtual void SetupView(FSceneViewFamily& Family, FSceneView& View) override
    {
        View.FinalPostProcessSettings.UserFlags &= ~ACERetailParticleViewFlag;
        // Mobile LDR already blends in display space. Preserve its renderer, VR
        // panels, HDR outputs and raw scene captures instead of treating their
        // different buffers as SDR. Never change the world's tone mapping.
        const auto* Separate = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SeparateTranslucency"));
        const bool SDR = !Family.bIsHDR && (!View.bIsSceneCapture || Family.SceneCaptureSource == SCS_FinalColorLDR);
        if (Family.GetFeatureLevel() >= ERHIFeatureLevel::SM5 && SDR && !IStereoRendering::IsStereoEyeView(View)
            && Family.EngineShowFlags.PostProcessing && !Family.EngineShowFlags.PathTracing
            && Family.EngineShowFlags.SeparateTranslucency && !Family.UseDebugViewPS()
            && Separate && Separate->GetInt() != 0)
        {
            View.FinalPostProcessSettings.UserFlags |= ACERetailParticleViewFlag;
            // Do not let DOF precompose the reserved AfterDOF particle buffer.
            View.FinalPostProcessSettings.DepthOfFieldFocalDistance = 0.f;
        }
    }

    virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& View,
        FPostProcessingPassDelegateArray& Callbacks, bool) override
    {
        if ((View.FinalPostProcessSettings.UserFlags & ACERetailParticleViewFlag) == 0) return;
        if (Pass == EPostProcessingPass::TranslucencyAfterDOF)
            Callbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FACERetailParticleView::HoldParticles));
        if (Pass == EPostProcessingPass::Tonemap)
            Callbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FACERetailParticleView::Composite));
    }

    virtual void PostRenderView_RenderThread(FRDGBuilder&, FSceneView& View) override { Layers.Remove(&View); }

private:
    // RDG resources are retained only between passes of the same view/graph.
    TMap<const FSceneView*, FScreenPassTexture> Layers;

    FScreenPassTexture HoldParticles(FRDGBuilder& Graph, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
    {
        const auto Layer = FScreenPassTexture::CopyFromSlice(Graph, Inputs.GetInput(EPostProcessMaterialInput::SeparateTranslucency));
        Layers.Add(&View, Layer);
        auto Desc = Layer.Texture->Desc;
        Desc.ClearValue = FClearValueBinding(FLinearColor(0, 0, 0, 1));
        auto* Empty = Graph.CreateTexture(Desc, TEXT("ACE.Particles.DeferredComposite"));
        AddClearRenderTargetPass(Graph, Empty, FLinearColor(0, 0, 0, 1));
        return FScreenPassTexture(Empty, Layer.ViewRect);
    }

    FScreenPassTexture Composite(FRDGBuilder& Graph, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
    {
        FScreenPassTexture Layer;
        if (!Layers.RemoveAndCopyValue(&View, Layer)) return Inputs.ReturnUntouchedSceneColorForPostProcessing(Graph);
        const auto Scene = FScreenPassTexture::CopyFromSlice(Graph, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
        auto Output = Inputs.OverrideOutput;
        if (!Output.IsValid()) Output = FScreenPassRenderTarget::CreateFromInput(Graph, Scene, ERenderTargetLoadAction::ENoAction, TEXT("ACE.Particles.RetailComposite"));
        auto* P = Graph.AllocParameters<FACERetailParticleCompositePS::FParameters>();
        P->SceneTexture = Scene.Texture; P->ParticleTexture = Layer.Texture;
        P->LinearSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
        const auto UV = [](const FScreenPassTexture& T)
        {
            const FVector2f Size(T.Texture->Desc.Extent);
            return FVector4f(T.ViewRect.Width()/Size.X, T.ViewRect.Height()/Size.Y,
                T.ViewRect.Min.X/Size.X, T.ViewRect.Min.Y/Size.Y);
        };
        P->SceneUV = UV(Scene); P->ParticleUV = UV(Layer);
        P->OutputRect = FVector4f(Output.ViewRect.Min.X, Output.ViewRect.Min.Y, 1.f/Output.ViewRect.Width(), 1.f/Output.ViewRect.Height());
        const auto* Gamma = IConsoleManager::Get().FindConsoleVariable(TEXT("r.TonemapperGamma"));
        P->ParticleGamma = Gamma && Gamma->GetFloat() > 0.f ? 2.2f/Gamma->GetFloat() : 1.f;
        P->RenderTargets[0] = Output.GetRenderTargetBinding();
        TShaderMapRef<FACERetailParticleCompositePS> Shader(GetGlobalShaderMap(View.GetFeatureLevel()));
        AddDrawScreenPass(Graph, RDG_EVENT_NAME("ACE Retail Particle Colors"), View,
            FScreenPassTextureViewport(Output), FScreenPassTextureViewport(Scene), Shader, P);
        return Output;
    }
};
}

TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> CreateACERetailParticleView(UWorld* World)
{
    if (World) return FSceneViewExtensions::NewExtension<FACERetailParticleView>(World);
    return {};
}

class FACERetailRenderModule : public IModuleInterface
{
    virtual void StartupModule() override
    {
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("ACEClient"));
        check(Plugin);
        AddShaderSourceDirectoryMapping(TEXT("/ACERetail"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
    }
};
IMPLEMENT_MODULE(FACERetailRenderModule, ACERetailRender)
