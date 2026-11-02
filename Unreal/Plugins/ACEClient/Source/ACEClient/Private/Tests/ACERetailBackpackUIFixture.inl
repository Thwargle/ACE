// Included in the DAT-screen loopback fixture. Exercise actual drags back into
// a chest, not just pickup requests, and compare the icon feedback to the wire.
TFunction<void()> CheckBackpackReturn = [&]()
{
    const int32 PackGuid=61003;
    const auto SavedNested=Session.WorldObjects[NestedPack.Guid];
    Session.WorldObjects[NestedPack.Guid].ContainersCapacity=0;
    Session.WorldObjects[Chest.Guid].ContainersCapacity=2;
    Gameplay->SelectedPackGuid=Player.Guid;Gameplay->PackScrollOffset=0;
    Gameplay->RefreshInventoryOverlays();Gameplay->OpenLootSelectedPackGuid=Chest.Guid;
    Gameplay->RefreshExternalContainerOverlays();
    TestTrue(TEXT("A chest with a spare pack slot accepts a backpack"),Gameplay->CanMoveInventoryAmountToContainer(PackGuid,Chest.Guid,1));
    TestFalse(TEXT("An ordinary backpack cannot contain another pack"),Gameplay->CanMoveInventoryAmountToContainer(PackGuid,NestedPack.Guid,1));
    Session.WorldObjects[Chest.Guid].ContainersCapacity=1;
    TestFalse(TEXT("A full chest rejects another pack"),Gameplay->CanMoveInventoryAmountToContainer(PackGuid,Chest.Guid,1));
    TestTrue(TEXT("A full chest still allows its existing pack to be reordered"),Gameplay->CanMoveInventoryAmountToContainer(NestedPack.Guid,Chest.Guid,1));
    Session.WorldObjects[Chest.Guid].ContainersCapacity=-1;
    TestTrue(TEXT("Unlimited chest pack capacity remains supported"),Gameplay->CanMoveInventoryAmountToContainer(PackGuid,Chest.Guid,1));
    Session.WorldObjects[Chest.Guid].ContainersCapacity=2;
    TestFalse(TEXT("A bag cannot move into itself"),Gameplay->CanMoveInventoryAmountToContainer(PackGuid,PackGuid,1));
    const auto Parent=Session.WorldObjects[Chest.Guid].ContainerId;
    Session.WorldObjects[Chest.Guid].ContainerId=PackGuid;
    TestFalse(TEXT("A bag cannot move into its own descendant"),Gameplay->CanMoveInventoryAmountToContainer(PackGuid,Chest.Guid,1));
    Session.WorldObjects[Chest.Guid].ContainerId=Parent;
    for(float Scale:{1.f,1.5f}) for(int32 TargetIndex:{0,1,2})
    {
        Gameplay->SelectedPackGuid=Player.Guid;Gameplay->RefreshInventoryOverlays();
        Gameplay->OpenLootSelectedPackGuid=Chest.Guid;Gameplay->RefreshExternalContainerOverlays();
        CaptureScreen(TEXT("GameplayChestPackReturn"),Scale,FVector2D(43,27));
        const int32 SourceIndex=Gameplay->PackSlotGuids.IndexOfByKey(PackGuid);
        if(!TestTrue(TEXT("Return fixture has owned source and chest destination"),SourceIndex>0 && Gameplay->ExtPackSlots.IsValidIndex(TargetIndex)))continue;
        const auto& Geo=Canvas->GetCachedGeometry();
        const auto& Source=Gameplay->PackSlots[SourceIndex]->GetCachedGeometry();
        const auto& Target=Gameplay->ExtPackSlots[TargetIndex]->GetCachedGeometry();
        const auto From=Source.LocalToAbsolute(Source.GetLocalSize()*.5),To=Target.LocalToAbsolute(Target.GetLocalSize()*.5);
        Session.PendingInventoryTransfers.Reset();Session.CachedC2SPackets.Reset();
        Canvas->NativeOnMouseButtonDown(Geo,FPointerEvent(0,From,From,{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
        Canvas->NativeOnMouseMove(Geo,FPointerEvent(0,To,From,{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()));
        const uint32 Feedback=TargetIndex==0 ? 0x060011F7 : 0x060011F9;
        TestTrue(TEXT("Chest root, occupied and empty slots preview the actual legal drop"),Gameplay->PaperDollDragTargetIcon
            && Gameplay->PaperDollDragTargetIcon->IsVisible()
            && Gameplay->PaperDollDragTargetIcon->Background.GetResourceObject()==Resources->ResolveIconTexture(Feedback));
        Canvas->NativeOnMouseButtonUp(Geo,FPointerEvent(0,To,To,{},EKeys::LeftMouseButton,0,FModifierKeysState()));
        LastWire(ACEGameAction::PutItemInContainer,{uint32(PackGuid),uint32(Chest.Guid),TargetIndex==2 ? 1u : 0u});
        TestEqual(TEXT("Return transfer leaves ownership untouched until confirmation"),Session.WorldObjects[PackGuid].ContainerId,Player.Guid);
    }
    Session.WorldObjects[NestedPack.Guid]=SavedNested;
    Session.PendingInventoryTransfers.Reset();
};
CheckBackpackReturn();
