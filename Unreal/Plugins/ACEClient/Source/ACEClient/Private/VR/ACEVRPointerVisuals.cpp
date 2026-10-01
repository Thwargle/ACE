#include "VR/ACEVRPointerVisuals.h"
#include "Components/WidgetComponent.h"
#include "Materials/Material.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionMultiply.h"
#endif

UMaterialInterface* ACEVRPointerVisuals::GetMaterial(bool Overlay)
{
	static TWeakObjectPtr<UMaterialInterface> Cached[2];
	const int32 Index = Overlay ? 1 : 0;
	if (Cached[Index].IsValid()) return Cached[Index].Get();
	const TCHAR* Name = Overlay ? TEXT("M_ACEVRPointerOverlay_v1") : TEXT("M_ACEVRPointerWorld_v1");
#if WITH_EDITOR
	auto* Material = NewObject<UMaterial>(GetTransientPackage(), Name, RF_Transient);
	Material->MaterialDomain = MD_Surface;
	Material->SetShadingModel(MSM_Unlit);
	Material->BlendMode = BLEND_AlphaComposite;
	Material->TwoSided = true;
	Material->bDisableDepthTest = Overlay;
	Material->bUseTranslucencyVertexFog = false;
	Material->bEnableResponsiveAA = true;
	Material->TranslucencyPass = MTP_AfterDOF;
	auto* Color = NewObject<UMaterialExpressionVertexColor>(Material);
	auto* Premultiply = NewObject<UMaterialExpressionMultiply>(Material);
	Premultiply->A.Expression = Color;
	Premultiply->B.Expression = Color; Premultiply->B.OutputIndex = 4;
	auto* Data = Material->GetEditorOnlyData();
	Data->ExpressionCollection.AddExpression(Color);
	Data->ExpressionCollection.AddExpression(Premultiply);
	Data->EmissiveColor.Expression = Premultiply;
	Data->Opacity.Expression = Color; Data->Opacity.OutputIndex = 4;
	Material->SetUsageByFlag(MATUSAGE_StaticMesh, true);
	Material->PostEditChange();
	Cached[Index] = Material;
#else
	Cached[Index] = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/ACE/RuntimeMaterials/%s.%s"), Name, Name));
#endif
	return Cached[Index].Get();
}

bool ACEVRPointerVisuals::IsInteractivePanel(const UWidgetComponent* Panel)
{
	return Panel && Panel->IsVisible() && Panel->bRenderInMainPass
		&& Panel->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
}
