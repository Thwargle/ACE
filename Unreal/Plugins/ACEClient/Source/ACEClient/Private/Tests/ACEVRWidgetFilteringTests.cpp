#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "VR/ACEVRWidgetComponent.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Widgets/Layout/SBorder.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
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
#endif
