#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VR/ACEVRWidgetComponent.h"
#include "VR/ACEVRWidgetInteraction.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Widgets/Layout/SBorder.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Materials/Material.h"
#if WITH_EDITOR
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionMultiply.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRWidgetFilteringTest,"ACE.VR.WidgetFiltering",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEVRWidgetFilteringTest::RunTest(const FString&)
{
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);
 auto* Owner=World->SpawnActor<AActor>();
 auto* Panel=NewObject<UACEVRWidgetComponent>(Owner);Owner->SetRootComponent(Panel);
 Panel->SetWidgetSpace(EWidgetSpace::World);Panel->SetTwoSided(true);
 Panel->SetBlendMode(EWidgetBlendMode::Transparent);Panel->SetDrawSize(FVector2D(400,500));
 Panel->SetManuallyRedraw(true);Panel->SetRedrawTime(0);Panel->SetTickWhenOffscreen(true);
 Panel->RegisterComponent();
 TSharedPtr<SBorder> Content;
 Panel->SetSlateWidget(SAssignNew(Content,SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
  .BorderBackgroundColor(FLinearColor::Red));
 for(int Pass=0;Pass<3;++Pass)
 {
  // A second draw at the same clock time, and then a resize, must also refresh
  // lower mips. Reading the RHI mip catches allocation-only/old-content bugs.
  if(Pass==1)Content->SetBorderBackgroundColor(FLinearColor::Green);
  if(Pass==2){Panel->SetDrawSize(FVector2D(480,240));Content->SetBorderBackgroundColor(FLinearColor::Blue);}
  Panel->RequestRedraw();Panel->TickComponent(.016f,LEVELTICK_All,nullptr);FlushRenderingCommands();
  auto* Target=Panel->GetRenderTarget();
  if(!TestNotNull(TEXT("Floating HUD renders a texture"),Target))break;
  int32 MipCount=0;TArray<FColor> Pixels;
  auto* Resource=Target->GameThread_GetRenderTargetResource();
  ENQUEUE_RENDER_COMMAND(ReadHUDMip)([Resource,&MipCount,&Pixels](FRHICommandListImmediate& Cmd)
  {
   const auto& Texture=Resource->GetRenderTargetTexture();MipCount=Texture->GetNumMips();
   if(MipCount>2)
   {
    FReadSurfaceDataFlags Flags;Flags.SetMip(2);
    Cmd.ReadSurfaceData(Texture,FIntRect(0,0,Texture->GetSizeX()>>2,Texture->GetSizeY()>>2),Pixels,Flags);
   }
  });
  FlushRenderingCommands();
  TestEqual(TEXT("Non-power-of-two HUD allocates its entire mip chain"),MipCount,FMath::FloorLog2(FMath::Max(Target->SizeX,Target->SizeY))+1);
  const FColor Pixel=Pixels.IsEmpty()?FColor::Black:Pixels[Pixels.Num()/2];
  TestTrue(TEXT("Lower HUD mip contains the current draw, including redraw and resize"),
   Pass==0 ? Pixel.R>200 && Pixel.G<10 : Pass==1 ? Pixel.G>200 && Pixel.B<10 : Pixel.B>200 && Pixel.R<10);
 }
#if WITH_EDITOR
 if(auto* Material=Panel->GetMaterial(0)->GetMaterial())
  for(UMaterialExpression* Expression:Material->GetExpressions())
   if(auto* Sample=Cast<UMaterialExpressionTextureSampleParameter2D>(Expression);Sample && Sample->ParameterName==TEXT("SlateUI"))
   {
    TestEqual(TEXT("HUD uses its trilinear texture sampler independent of world quality"),int32(Sample->SamplerSource),int32(SSM_FromTextureAsset));
    TestEqual(TEXT("HUD selects mips from its projected size"),int32(Sample->MipValueMode),int32(TMVM_None));
    TestFalse(TEXT("View sharpening does not reintroduce HUD shimmer"),Sample->AutomaticViewMipBias);
   }
#endif
 Owner->Destroy();World->DestroyWorld(false);
 return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEVRWidgetLayeringTest,"ACE.VR.WidgetLayering",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACEVRWidgetLayeringTest::RunTest(const FString&)
{
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);
 auto* Owner=World->SpawnActor<AActor>();
 auto* Root=NewObject<USceneComponent>(Owner);Owner->SetRootComponent(Root);Root->RegisterComponent();
 auto MakePanel=[&](float Distance)
 {
  auto* Panel=NewObject<UWidgetComponent>(Owner);Panel->SetupAttachment(Root);
  Panel->SetWidgetSpace(EWidgetSpace::World);Panel->SetDrawSize(FVector2D(100,100));Panel->SetTwoSided(true);
  Panel->SetBlendMode(EWidgetBlendMode::Transparent);Panel->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  Panel->RegisterComponent();Panel->SetWorldLocation(FVector(Distance,0,0));
  return Panel;
 };
 auto* Near=MakePanel(60);auto* Far=MakePanel(100);
 UACEVRWidgetComponent::SetPersonalLayer(Near,10);UACEVRWidgetComponent::SetPersonalLayer(Far,20);
 auto* Pointer=NewObject<UACEVRWidgetInteraction>(Owner);Pointer->SetupAttachment(Root);Pointer->RegisterComponent();
 Pointer->InteractionDistance=200;Pointer->SetWorldLocationAndRotation(FVector::ZeroVector,FRotator::ZeroRotator);
 TestTrue(TEXT("Menu layers exceed spell and weather particle priorities"),Near->TranslucencySortPriority>2500);
 TestTrue(TEXT("Comfort curtain remains above all personal windows"),Far->TranslucencySortPriority<32000);
 auto Trace=Pointer->PerformTrace();
 TestTrue(TEXT("Farther foreground window receives the controller ray"),Trace.bWasHit && Trace.HitWidgetComponent==Far);
 UACEVRWidgetComponent::SetPersonalLayer(Near,30);Trace=Pointer->PerformTrace();
 TestTrue(TEXT("Promoting a clicked panel also changes ray priority"),Trace.bWasHit && Trace.HitWidgetComponent==Near);
 Near->SetVisibility(false);Trace=Pointer->PerformTrace();
 TestTrue(TEXT("Hidden foreground window cannot block the visible window"),Trace.bWasHit && Trace.HitWidgetComponent==Far);
 Near->SetVisibility(true);Near->SetCollisionEnabled(ECollisionEnabled::NoCollision);Trace=Pointer->PerformTrace();
 TestTrue(TEXT("Decorative foreground panels never intercept input"),Trace.bWasHit && Trace.HitWidgetComponent==Far);
 Near->SetCollisionEnabled(ECollisionEnabled::QueryOnly);UACEVRWidgetComponent::SetPersonalLayer(Far,30);Trace=Pointer->PerformTrace();
 TestTrue(TEXT("Equal layers use the nearest surface"),Trace.bWasHit && Trace.HitWidgetComponent==Near);
 Near->SetRenderInMainPass(false);Trace=Pointer->PerformTrace();
 TestTrue(TEXT("Offscreen source canvases cannot cover a visible pointer target"),Trace.bWasHit && Trace.HitWidgetComponent==Far);
 Near->SetRenderInMainPass(true);
 auto* Mid=Near->GetMaterialInstance();
 for(int32 I=0;I<90;++I)UACEVRWidgetComponent::SetPersonalLayer(Near,30);
 TestTrue(TEXT("Per-frame layer assignment retains the material instance"),Near->GetMaterialInstance()==Mid);
 auto* Material=Near->GetMaterial(0)?Near->GetMaterial(0)->GetMaterial():nullptr;
 if(TestNotNull(TEXT("Personal windows have a cooked overlay material"),Material))
 {
  TestTrue(TEXT("World geometry cannot clip personal menu pixels"),Material->bDisableDepthTest);
  TestEqual(TEXT("Menus cover additive effects on the mobile unlit path"),int32(Material->BlendMode),int32(BLEND_AlphaComposite));
  TestFalse(TEXT("World fog cannot wash out personal UI"),Material->bUseTranslucencyVertexFog);
  TestEqual(TEXT("UI shares the world-effects and comfort-curtain translucency pass"),int32(Material->TranslucencyPass),int32(MTP_AfterDOF));
#if WITH_EDITOR
  auto* Data=Material->GetEditorOnlyData();
  auto* Premultiply=Cast<UMaterialExpressionMultiply>(Data->EmissiveColor.Expression);
  TestTrue(TEXT("Premultiplied UI preserves authored alpha holes for the mirror/world"),Premultiply && Premultiply->A.Expression && Premultiply->B.Expression==Data->Opacity.Expression);
#else
  TestTrue(TEXT("Packaged widgets load the prepared overlay shader"),Material->GetPathName().Contains(TEXT("/Game/ACE/RuntimeMaterials/M_ACEVRWidgetFiltered_v2")));
#endif
 }
 Owner->Destroy();World->DestroyWorld(false);
 return !HasAnyErrors();
}
#endif
