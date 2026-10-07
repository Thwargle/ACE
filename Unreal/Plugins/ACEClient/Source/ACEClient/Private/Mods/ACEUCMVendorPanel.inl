        void VendorPanel()
        {
            Heading(TEXT("Vendor restocking"),TEXT("Open a vendor, add stock below, and set the total quantity to keep in inventory. Saved lists match this server, vendor and item across logins. With UCM running, visiting that vendor buys only the missing quantity. Route Use points can visit vendors automatically."));
            Add(Toggle(TEXT("vendor_restock"),TEXT("Automatically buy saved supplies"),TEXT("Redeems available trade notes when pyreals are short, splitting stacks as needed. Retained notes and notes in trade are excluded. Stops if funds, space or stock are insufficient.")));
            const TArray<TSharedPtr<FJsonValue>>* Saved=nullptr;
            if(P()->TryGetArrayField(TEXT("vendor_rules"),Saved))for(int32 Index=0;Index<Saved->Num();++Index)
            {
                auto Rule=(*Saved)[Index]->AsObject();auto Row=SNew(SVerticalBox);
                Row->AddSlot().AutoHeight()[Text(Str(Rule,TEXT("item_name")),18)];
                Row->AddSlot().AutoHeight()[Text(Str(Rule,TEXT("vendor_name"))+TEXT(" / ")+Str(Rule,TEXT("server")),14,Muted)];
                Row->AddSlot().AutoHeight()[SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().FillWidth(1)[Text(TEXT("Keep in inventory"))]
                    +SHorizontalBox::Slot().AutoWidth()[SNew(SSpinBox<int32>).MinValue(1).MaxValue(100000).Value(int32(Num(Rule,TEXT("quantity"),1)))
                        .OnValueCommitted_Lambda([this,Index](int32 N,ETextCommit::Type){const TArray<TSharedPtr<FJsonValue>>* Rules=nullptr;if(P()->TryGetArrayField(TEXT("vendor_rules"),Rules)&&Rules->IsValidIndex(Index)){(*Rules)[Index]->AsObject()->SetNumberField(TEXT("quantity"),N);Save();}})]
                    +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[Button(TEXT("Remove"),[this,Index](){auto Rules=P()->GetArrayField(TEXT("vendor_rules"));if(Rules.IsValidIndex(Index))Rules.RemoveAt(Index);P()->SetArrayField(TEXT("vendor_rules"),Rules);Save();Rebuild();})]];
                Add(Tile(Row));
            }
            Add(Button(TEXT("Refresh vendor stock"),[this](){Rebuild();}));
            auto* C=Host->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();FACEWorldObject Vendor;
            if(!C||!C->GetOpenVendorGuid()||!C->GetWorldObject(C->GetOpenVendorGuid(),Vendor)){Add(Text(TEXT("Open a vendor in game, then refresh to choose supplies."),15,Muted));return;}
            Heading(Vendor.Name,TEXT("Click an item to add it. Lists are saved with this UCM setup and with saved loot profiles."));
            for(const auto& Item:C->GetVendorMerchandise())
            {
                const FString ItemName=ACERetailObjectNames::Name(Item);
                Add(Button(TEXT("Add ")+ItemName,[this,Vendor,Item,ItemName,Server=C->GetServerName()]()
                {
                    const TArray<TSharedPtr<FJsonValue>>* Old=nullptr;TArray<TSharedPtr<FJsonValue>> Rules;
                    if(P()->TryGetArrayField(TEXT("vendor_rules"),Old))Rules=*Old;
                    for(const auto& V:Rules){auto R=V->AsObject();if(Num(R,TEXT("vendor_wcid"))==Vendor.WeenieClassId&&Str(R,TEXT("vendor_name"))==Vendor.Name&&Str(R,TEXT("server"))==Server&&Num(R,TEXT("item_wcid"))==Item.WeenieClassId&&Str(R,TEXT("item_name"))==ItemName)return;}
                    if(Rules.Num()>=512)return;
                    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("server"),Server);R->SetStringField(TEXT("vendor_name"),Vendor.Name);R->SetNumberField(TEXT("vendor_wcid"),Vendor.WeenieClassId);
                    R->SetStringField(TEXT("item_name"),ItemName);R->SetNumberField(TEXT("item_wcid"),Item.WeenieClassId);R->SetNumberField(TEXT("quantity"),1);
                    Rules.Add(MakeShared<FJsonValueObject>(R));P()->SetArrayField(TEXT("vendor_rules"),Rules);Save();Rebuild();
                }));
            }
        }
