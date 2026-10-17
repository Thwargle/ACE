#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ACEClientSubsystem.h"
#include "ACESession.h"
#include "ACEPlayerController.h"
#include "ACETerrainPresenterComponent.h"
#include "UI/ACEUIGameplayBinder.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACEDungeonInterfacesTest,"ACE.RetailParity.DungeonInterfaces",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FACEDungeonInterfacesTest::RunTest(const FString&)
{
 const auto Values=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 auto* GI=NewObject<UGameInstance>(GEngine);GI->InitializeStandalone();
 GI->GetWorldContext()->SetCurrentWorld(World);World->SetGameInstance(GI);
 ON_SCOPE_EXIT {GI->Shutdown();World->DestroyWorld(false);};
 World->SetGameMode(FURL(nullptr,TEXT("/Game/Test?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
 auto* GM=World->GetAuthGameMode();if(!TestNotNull(TEXT("Test game mode"),GM))return false;
 auto* Terrain=NewObject<UACETerrainPresenterComponent>(GM);GM->AddInstanceComponent(Terrain);
 auto* Client=GI->GetSubsystem<UACEClientSubsystem>();auto S=Client->GetSession();
 S->State=EACESessionState::InWorld;S->PlayerGuid=1;S->PlayerPosition.CellId=0x01E30100;
 S->PlayerPosition.Location=FVector(50,50,0);
 auto* PC=World->SpawnActor<AACEPlayerController>();PC->Client=Client;
 auto* Binder=NewObject<UACEUIGameplayBinder>(GI);Binder->Client=Client;Binder->PlayerController=PC;
 auto Add=[&](int Id,int Type,double Distance,bool Player=false)
 {
  FACEWorldObject O;O.Guid=Id;O.ItemType=Type;O.bIsPlayer=Player;O.bHasPosition=true;
  O.Position=S->PlayerPosition;O.Position.Location.X+=Distance;
  O.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable;O.RadarBehavior=2;
  S->WorldObjects.Add(Id,O);
 };
 Add(2,ACEItemType::Creature,-10);Add(3,ACEItemType::Creature,24.9);Add(4,ACEItemType::Creature,25.1);
 TestFalse(TEXT("Fixture creatures are outside the rendered cell set"),Client->IsWorldObjectVisible(S->WorldObjects[2]));
 PC->CycleNearbyTarget(true,0);TestEqual(TEXT("Closest monster works behind the camera in a culled room"),Client->GetSelectedObject().Guid,2);
 PC->CycleNearbyTarget(true,1);TestEqual(TEXT("Next monster reaches the indoor radar rim"),Client->GetSelectedObject().Guid,3);
 PC->CycleNearbyTarget(true,1);TestEqual(TEXT("Beyond-radar monster is excluded and cycling wraps"),Client->GetSelectedObject().Guid,2);
 Add(5,ACEItemType::Misc,-3);PC->CycleNearbyTarget(false,0);
 TestEqual(TEXT("Item hotkeys use awareness rather than rendered visibility"),Client->GetSelectedObject().Guid,5);
 Add(6,ACEItemType::Creature,-12,true);Binder->CycleKeyboardSelection(TEXT("Player"),0);
 TestEqual(TEXT("Player hotkeys work in a culled room"),Client->GetSelectedObject().Guid,6);

 const int Types[]={ACEItemType::Armor,ACEItemType::Writable,ACEItemType::Clothing,ACEItemType::Container,
  ACEItemType::Food,ACEItemType::Gem,ACEItemType::Jewelry,ACEItemType::Key,ACEItemType::Misc,
  0x100000,ACEItemType::SpellComponents,ACEItemType::PromissoryNote,ACEItemType::MeleeWeapon,
  ACEItemType::ManaStone,ACEItemType::Caster,0x800000,0x400000,0x1000000};
 const TCHAR* Names[]={TEXT("Armor"),TEXT("Books, Paper"),TEXT("Clothing"),TEXT("Containers"),TEXT("Food"),TEXT("Gems"),
  TEXT("Jewelry"),TEXT("Keys, Tools"),TEXT("Miscellaneous"),TEXT("Services"),TEXT("Spell Components"),TEXT("Trade Notes"),
  TEXT("Weapons"),TEXT("Mana Stones"),TEXT("Magic Items"),TEXT("Alchemical Items"),TEXT("Cooking Items"),TEXT("Fletching Items")};
 Binder->OpenVendorGuid=50;
 for(int I=0;I<UE_ARRAY_COUNT(Types);++I)
 {
  FACEWorldObject O;O.Guid=100+I;O.ItemType=Types[I];S->VendorMerchandise={O};
  Binder->RebuildVendorVisibleFilters();
  TestTrue(TEXT("Only represented retail vendor categories appear"),Binder->VendorVisibleFilterIndices==TArray<int32>{I});
  TestEqual(TEXT("Retail vendor category label"),Binder->VendorFilterLabel(),FString(Names[I]));
  TestTrue(TEXT("Selected category includes its matching item"),Binder->VendorItemMatchesFilter(O,I));
 }
 FACEWorldObject Wand;Wand.ItemType=ACEItemType::Caster;
 TestFalse(TEXT("Casting devices are Magic Items, not Weapons"),Binder->VendorItemMatchesFilter(Wand,12));
 return !HasAnyErrors();
}
#endif
