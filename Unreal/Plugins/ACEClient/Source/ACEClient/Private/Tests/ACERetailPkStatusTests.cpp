#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ACESession.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACERetailPkStatusTest,"ACE.RetailParity.PkStatus",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FACERetailPkStatusTest::RunTest(const FString&)
{
 FACESession S;S.PlayerGuid=1;S.State=EACESessionState::InWorld;
 for(int32 Guid : {1,2})
 {
  FACEWorldObject Obj;Obj.Guid=Guid;Obj.bIsPlayer=true;Obj.bIsSelf=Guid==1;
  Obj.ObjectDescriptionFlags=ACEObjectDescFlag::Attackable|ACEObjectDescFlag::Admin;
  S.WorldObjects.Add(Guid,Obj);
 }
 int32 Changes=0,Vitals=0,Selection=0,Creates=0;
 S.OnPkStatusUpdated.AddLambda([&](int32,int32){++Changes;});
 S.OnVitalsUpdated.AddLambda([&](const FACEPlayerVitals&){++Vitals;});
 S.OnObjectCreated.AddLambda([&](const FACEWorldObject&){++Creates;});
 S.SelectObject(1);
 S.OnSelectionChanged.AddLambda([&](const FACESelectedObject&){++Selection;});
 const uint32 SelectionSerial=S.SelectedObject.SelectionSerial;
 auto Public=[&](int32 Guid,int32 Status)
 {
  FACEBinaryWriter W;W.WriteUInt8(1);W.WriteUInt32(Guid);W.WriteUInt32(134);W.WriteInt32(Status);
  FACEBinaryReader R(W.GetData());S.HandlePublicUpdatePropertyInt(R);
 };
 Public(1,0x40);
 TestEqual(TEXT("Server PKL acknowledgement updates self stats"),S.PlayerVitals.PlayerKillerStatus,0x40);
 TestEqual(TEXT("Character information quality stays current"),S.PlayerVitals.StatQualityInts.FindRef(134),0x40);
 TestTrue(TEXT("Self descriptor used by inspection, radar and selection becomes PKL"),(S.WorldObjects[1].ObjectDescriptionFlags&ACEObjectDescFlag::PkLiteStatus)!=0);
 TestEqual(TEXT("Selected status update preserves selection serial"),S.SelectedObject.SelectionSerial,SelectionSerial);
 TestEqual(TEXT("Selected status update refreshes UI"),Selection,1);
 Public(2,4);
 TestTrue(TEXT("Remote PK acknowledgement updates local collision category"),S.WorldObjects[2].IsPkOrPkLite());
 TestEqual(TEXT("Remote status cannot change self stats"),S.PlayerVitals.PlayerKillerStatus,0x40);
 Public(2,0x40);
 TestFalse(TEXT("PK to PKL clears old PK flag"),(S.WorldObjects[2].ObjectDescriptionFlags&ACEObjectDescFlag::PlayerKiller)!=0);
 Public(2,0x20);
 TestFalse(TEXT("Free PK status walks through players"),S.WorldObjects[2].IsPkOrPkLite());
 TestTrue(TEXT("Free PK descriptor flag matches retail"),(S.WorldObjects[2].ObjectDescriptionFlags&ACEObjectDescFlag::FreePkStatus)!=0);
 FACEBinaryWriter W;W.WriteUInt8(2);W.WriteUInt32(134);W.WriteInt32(1);
 FACEBinaryReader R(W.GetData());S.HandlePrivateUpdatePropertyInt(R);
 TestFalse(TEXT("Private NPK update clears self descriptor"),S.WorldObjects[1].IsPkOrPkLite());
 TestEqual(TEXT("Unrelated description flags survive PK transitions"),S.WorldObjects[1].ObjectDescriptionFlags,ACEObjectDescFlag::Attackable|ACEObjectDescFlag::Admin);
 TestEqual(TEXT("Both private and public self status notify stats"),Vitals,2);
 TestEqual(TEXT("Every acknowledgement reaches actor status listeners"),Changes,5);
 TestEqual(TEXT("Status never respawns/reinitializes an actor"),Creates,0);
 S.WorldObjects.Remove(1);Public(1,0x40);
 TestEqual(TEXT("Self status before ObjectCreate is retained"),S.PlayerVitals.PlayerKillerStatus,0x40);
 FACEBinaryWriter Short;Short.WriteUInt8(1);Short.WriteUInt32(1);Short.WriteUInt32(134);
 FACEBinaryReader ShortR(Short.GetData());S.HandlePublicUpdatePropertyInt(ShortR);
 TestEqual(TEXT("Truncated status never changes state"),Changes,6);
 return !HasAnyErrors();
}
#endif
