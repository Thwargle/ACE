#include "VR/ACEVRWidgetComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/Material.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionMultiply.h"
#endif

UMaterialInterface* UACEVRWidgetComponent::GetFilteredMaterial()
{
	static TWeakObjectPtr<UMaterialInterface> Cached;
	if (Cached.IsValid()) return Cached.Get();
#if WITH_EDITOR
	auto* Source=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent"));
	if (!Source) return nullptr;
	auto* Material=DuplicateObject<UMaterial>(Source->GetMaterial(),GetTransientPackage(),TEXT("M_ACEVRWidgetFiltered_v3"));
	Material->SetFlags(RF_Transient);Material->ClearFlags(RF_Public|RF_Standalone);
	Material->BlendMode=BLEND_AlphaComposite;Material->TwoSided=true;
	Material->bDisableDepthTest=true;Material->bUseTranslucencyVertexFog=false;
	Material->TranslucencyPass=MTP_AfterDOF;
	// Desktop temporal reconstruction needs the panel's velocity, not the wall
	// behind its depth-disabled quad. Responsive coverage limits glyph trails.
	// Quest's MSAA path does not use temporal history; its fix is the UI sampler.
	Material->bOutputTranslucentVelocity=true;
	Material->bEnableResponsiveAA=true;
	// Explicit premultiplied coverage also works on mobile Substrate's unlit
	// path, where legacy translucent shading can behave additively. Preserve
	// transparent pixels so the mirror and world remain visible outside panels.
	auto* Data=Material->GetEditorOnlyData();
	auto* Premultiply=NewObject<UMaterialExpressionMultiply>(Material);
	Premultiply->A=static_cast<const FExpressionInput&>(Data->EmissiveColor);
	Premultiply->B=static_cast<const FExpressionInput&>(Data->Opacity);
	Data->ExpressionCollection.AddExpression(Premultiply);
	Data->EmissiveColor.Expression=Premultiply;Data->EmissiveColor.OutputIndex=0;
	for (UMaterialExpression* Expression:Material->GetExpressions())
		if (auto* Sample=Cast<UMaterialExpressionTextureSampleParameter2D>(Expression);Sample && Sample->ParameterName==TEXT("SlateUI"))
		{
			// Honor the dedicated UI sampler, including its bounded anisotropy
			// and linear mip blending; never inherit the world's point mip filter.
			Sample->SamplerSource=SSM_FromTextureAsset;
			Sample->AutomaticViewMipBias=false;
		}
	Material->PostEditChange();Cached=Material;
#else
	Cached=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/ACE/RuntimeMaterials/M_ACEVRWidgetFiltered_v3.M_ACEVRWidgetFiltered_v3"));
#endif
	return Cached.Get();
}

void UACEVRWidgetComponent::SetPersonalLayer(UWidgetComponent* Panel,int32 Order)
{
	if(!Panel)return;
	if(auto* Material=GetFilteredMaterial();Material && Panel->GetMaterial(0)!=Material)
		Panel->SetMaterial(0,Material);
	const int32 Priority=PersonalLayerBase+FMath::Clamp(Order,0,9999);
	if(Panel->TranslucencySortPriority!=Priority)Panel->SetTranslucentSortPriority(Priority);
}

void UACEVRWidgetComponent::OnRegister()
{
	if (auto* Material=GetFilteredMaterial()) SetMaterial(0,Material);
	Super::OnRegister();
}

void UACEVRWidgetComponent::UpdateRenderTarget(FIntPoint DesiredRenderTargetSize)
{
	Super::UpdateRenderTarget(DesiredRenderTargetSize);
	if (RenderTarget && !RenderTarget->bAutoGenerateMips)
	{
		RenderTarget->bAutoGenerateMips = true;
		RenderTarget->LODGroup = TEXTUREGROUP_Project01;
		RenderTarget->Filter = TF_Default;
		RenderTarget->AddressX = RenderTarget->AddressY = TA_Clamp;
		RenderTarget->MipsSamplerFilter = TF_Bilinear;
		// The superclass created a single-mip resource. Reallocate it once;
		// later size changes retain bAutoGenerateMips and allocate the full chain.
		RenderTarget->UpdateResource();
		RenderTarget->UpdateResourceImmediate();
	}
}

void UACEVRWidgetComponent::DrawWidgetToRenderTarget(float DeltaTime)
{
	// Immediate Slate rendering keeps the mip pass ordered after this draw.
	// Retained/deferred rendering would generate mips from the previous contents.
	bUseInvalidationInWorldSpace = false;
	// TickComponent already gated the draw by visibility/redraw cadence. The
	// superclass clears this only after a successful draw, even at paused time.
	bRedrawRequested = true;
	Super::DrawWidgetToRenderTarget(DeltaTime);
	if (RenderTarget && !bRedrawRequested)
		RenderTarget->UpdateResourceImmediate(false);
}
