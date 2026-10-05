// SUCMPanel members. The same editor is rendered on desktop and VR.
TSharedPtr<FJsonObject> LootDraft;
int32 LootEditIndex=INDEX_NONE;
FString LootProfileName,LootSearch;
bool EditingSalvage=false;
int32 SalvageMaterial=0;
TSharedPtr<FJsonObject> SalvageDraft;
int32 LootPage=0;
TSharedPtr<FJsonObject> ConditionDraft;
int32 ConditionIndex=INDEX_NONE;

void LootText(const TCHAR* Key,const FString& Caption)
{
    Add(Text(Caption,15,Muted));
    Add(SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",16))
        .Text(FText::FromString(Str(LootDraft,Key)))
        .OnTextChanged_Lambda([this,Key](const FText& V){LootDraft->SetStringField(Key,V.ToString());}));
}
void LootChoice(const TCHAR* Key,const FString& Caption,const TArray<TPair<FString,FString>>& Options,bool Numeric=false)
{
    Add(Text(Caption,15,Muted));
    auto Menu=SNew(SVerticalBox);
    for(const auto& Option:Options)Menu->AddSlot().AutoHeight()[Button(Option.Value,[this,Key,Option,Numeric]()
    {
        if(Numeric)LootDraft->SetNumberField(Key,FCString::Atod(*Option.Key));else LootDraft->SetStringField(Key,Option.Key);
        FSlateApplication::Get().DismissAllMenus();
    })];
    Add(SNew(SComboButton).ButtonStyle(&Style()).ContentPadding(FMargin(12,9))
        .ButtonContent()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).ColorAndOpacity(Ink)
            .Text_Lambda([this,Key,Options,Numeric]()
            {
                const FString Value=Numeric?FString::Printf(TEXT("%.0f"),Num(LootDraft,Key)):Str(LootDraft,Key);
                for(const auto& O:Options)if(O.Key==Value)return FText::FromString(O.Value);
                return FText::FromString(Value.IsEmpty()?TEXT("Choose"):Value);
            })]
        .MenuContent()[SNew(SBox).MaxDesiredHeight(320)[SNew(SScrollBox)+SScrollBox::Slot()[Menu]]]);
}
void LootRange(const FString& Field,const FString& Caption,double Maximum)
{
    Add(Text(Caption+TEXT(" — minimum / maximum (0 = no limit)"),15,Muted));
    auto Row=SNew(SHorizontalBox);
    for(const FString Prefix:{TEXT("min_"),TEXT("max_")})
    {
        const FString Key=Prefix+Field;
        Row->AddSlot().FillWidth(1).Padding(0,0,8,0)[SNew(SSpinBox<double>).MinValue(0).MaxValue(Maximum)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Value(Num(LootDraft,*Key))
            .OnValueChanged_Lambda([this,Key](double V){LootDraft->SetNumberField(Key,V);})];
    }
    Add(Row);
}
TArray<TSharedPtr<FJsonValue>> CurrentLootRules() const
{
    const TArray<TSharedPtr<FJsonValue>>* Rules=nullptr;
    return P()->TryGetArrayField(TEXT("loot_rules"),Rules)?*Rules:TArray<TSharedPtr<FJsonValue>>{};
}
void WriteLootRules(const TArray<TSharedPtr<FJsonValue>>& Rules)
{
    auto Updated=MakeShared<FJsonObject>();Updated->Values=P()->Values;
    Updated->SetArrayField(TEXT("loot_rules"),Rules);
    Updated->SetBoolField(TEXT("loot_modified"),true);
    FString Json;FJsonSerializer::Serialize(Updated,TJsonWriterFactory<>::Create(&Json));
    if(Host->SaveProfile(Plugin->Id,Plugin->ProfileName,Json)) {LootDraft.Reset();Rebuild();}
}
void LootPanel()
{
    if(EditingSalvage){SalvagePanel();return;}
    TArray<TSharedPtr<FJsonValue>> Rules;const TArray<TSharedPtr<FJsonValue>>* Stored=nullptr;
    if(P()->TryGetArrayField(TEXT("loot_rules"),Stored))Rules=*Stored;
    if(LootDraft)
    {
        Heading(LootEditIndex==INDEX_NONE?TEXT("New loot rule"):TEXT("Edit loot rule"),TEXT("All conditions must match. Rules run from top to bottom; the first match decides. Names include material prefixes, such as Copper Ring. Saving stops UCM so the new rules start together."));
        LootText(TEXT("label"),TEXT("Rule name"));
        LootChoice(TEXT("action"),TEXT("When this rule matches"),{{TEXT("keep"),TEXT("Keep in inventory")},{TEXT("skip"),TEXT("Skip / leave on corpse")},{TEXT("salvage"),TEXT("Loot, then salvage with Ust (destroys item)")},{TEXT("sell"),TEXT("Loot, then sell at the next open vendor")},{TEXT("read"),TEXT("Loot and read a spell scroll")}});
        LootText(TEXT("name"),TEXT("Item name (blank = any)"));
        LootChoice(TEXT("name_mode"),TEXT("Name matching — case insensitive"),{{TEXT("prefix"),TEXT("Begins with")},{TEXT("exact"),TEXT("Exact name")},{TEXT("contains"),TEXT("Contains text")}});
        TArray<TPair<FString,FString>> Materials={{TEXT("0"),TEXT("Any material")}};
        TArray<TPair<FString,FString>> Named;
        for(int32 Id=1;Id<=77;++Id)Named.Add({FString::FromInt(Id),ACERetailObjectNames::GetMaterialTypeName(Id)});
        Named.Sort([](const auto& A,const auto& B){return A.Value<B.Value;});Materials.Append(Named);
        LootChoice(TEXT("material"),TEXT("Salvage material"),Materials,true);
        LootChoice(TEXT("type"),TEXT("Item type"),{{TEXT("0"),TEXT("Any type")},{TEXT("1"),TEXT("Melee weapon")},{TEXT("256"),TEXT("Missile weapon / ammunition")},{TEXT("32768"),TEXT("Casting device")},{TEXT("2"),TEXT("Armor")},{TEXT("4"),TEXT("Clothing")},{TEXT("8"),TEXT("Jewelry")},{TEXT("32"),TEXT("Food")},{TEXT("4096"),TEXT("Spell component")},{TEXT("1073741824"),TEXT("Salvage bag")}},true);
        LootRange(TEXT("workmanship"),TEXT("Salvage workmanship"),1000);
        LootRange(TEXT("value"),TEXT("Item / stack value (pyreals)"),MAX_int32);
        LootRange(TEXT("burden"),TEXT("Item / stack burden"),MAX_int32);
        LootRange(TEXT("rating"),TEXT("Damage rating (requires appraisal)"),10000);
        Add(Text(TEXT("Keep up to per item type, including equipped items (0 = unlimited)"),15,Muted));
        Add(SNew(SSpinBox<int32>).MinValue(0).MaxValue(100000).Value(int32(Num(LootDraft,TEXT("keep_up_to"))))
            .OnValueChanged_Lambda([this](int32 V){LootDraft->SetNumberField(TEXT("keep_up_to"),V);}));
        LootConditions();
        Add(Button(TEXT("Use selected item's name and material"),[this]()
        {
            auto* C=Host->GetGameInstance()->GetSubsystem<UACEClientSubsystem>();FACEWorldObject Item;
            if(!C->GetWorldObject(C->GetSelectedObject().Guid,Item)){Host->Notice=TEXT("Select an item first.");return;}
            LootDraft->SetStringField(TEXT("name"),ACERetailObjectNames::Name(Item));LootDraft->SetStringField(TEXT("name_mode"),TEXT("exact"));
            LootDraft->SetNumberField(TEXT("material"),Item.MaterialType);Rebuild();
        }));
        auto Actions=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8,8));
        Actions->AddSlot()[Button(TEXT("Save rule"),[this]()
        {
            auto Rules=CurrentLootRules();
            if(LootEditIndex==INDEX_NONE){if(Rules.Num()>=2048){Host->Notice=TEXT("Maximum 2048 loot rules.");return;}Rules.Add(MakeShared<FJsonValueObject>(LootDraft));}
            else if(Rules.IsValidIndex(LootEditIndex))Rules[LootEditIndex]=MakeShared<FJsonValueObject>(LootDraft);
            WriteLootRules(Rules);
        })];
        Actions->AddSlot()[Button(TEXT("Cancel"),[this](){LootDraft.Reset();Rebuild();})];Add(Actions);return;
    }
    Heading(TEXT("Loot profiles"),TEXT("Match drops by material, workmanship, name, value and more. The first matching rule wins. Keep preserves an item; Salvage destroys it, Sell queues it for a vendor, and Read consumes a spell scroll."));
    Add(Tile(SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",16)).ColorAndOpacity(Accent).AutoWrapText(true)
        .Text_Lambda([this]()
        {
            const FString Source=Str(P(),TEXT("utl_source")),Saved=Str(P(),TEXT("loot_profile"));
            FString Name=!Source.IsEmpty()?FPaths::GetCleanFilename(Source):Saved;
            if(Name.IsEmpty())Name=CurrentLootRules().IsEmpty()?TEXT("None"):TEXT("Custom rules in ")+Plugin->ProfileName;
            return FText::FromString(TEXT("Active loot: ")+Name+(Bool(P(),TEXT("loot_modified"))?TEXT(" (edited in this setup)"):TEXT("")));
        })));
    Add(Toggle(TEXT("looting"),TEXT("Loot corpses"),TEXT("Uses normal corpse permissions and inventory transfers.")));
    Slider(TEXT("loot_range"),TEXT("Corpse search range"),1,60,15,Accent,TEXT(" m"));
    Add(Toggle(TEXT("salvage_combine"),TEXT("Combine partial salvage bags"),TEXT("Uses imported workmanship groups and value thresholds; consumes matching bags with an Ust.")));
    Add(Button(TEXT("Edit salvage combining groups"),[this](){EditingSalvage=true;SalvageMaterial=0;SalvageDraft.Reset();Rebuild();}));
    Add(Toggle(TEXT("loot_priority"),TEXT("Loot before combat"),TEXT("Finish nearby corpses before selecting another combat target.")));
    Add(Toggle(TEXT("loot_only_rare"),TEXT("Only rare corpses"),TEXT("Appraises corpses and checks the server's rare-generation description.")));
    Add(Toggle(TEXT("loot_fellow_corpses"),TEXT("Loot fellowship corpses"),TEXT("Honors the killer's share-loot setting and waits for reserved corpses.")));
    Add(Toggle(TEXT("loot_all_corpses"),TEXT("Loot other corpses"),TEXT("Waits 100 seconds after seeing a corpse. Another player's rare corpse remains excluded.")));
    Add(Toggle(TEXT("read_unknown_scrolls"),TEXT("Learn unknown spells"),TEXT("Collect and read scrolls your current skills can learn, before applying loot rules. Known spells and carried duplicates follow normal rules.")));
    Add(Toggle(TEXT("autocram"),TEXT("Move loose items into packs"),TEXT("Keeps main-pack slots free using owned packs.")));
    Add(Toggle(TEXT("autostack"),TEXT("Combine matching stacks"),TEXT("Uses the normal stack-merge action.")));
    Add(Button(TEXT("+ New rule"),[this](){LootDraft=MakeShared<FJsonObject>();LootDraft->SetStringField(TEXT("action"),TEXT("keep"));LootDraft->SetStringField(TEXT("name_mode"),TEXT("prefix"));LootEditIndex=INDEX_NONE;ConditionDraft.Reset();Rebuild();}));
    if(Rules.IsEmpty())Add(Text(TEXT("No rules yet. Add a material + workmanship rule for salvage, or a name / value rule for other drops."),16,Muted));
    Add(SNew(SEditableTextBox).HintText(FText::FromString(TEXT("Filter rule names"))).Text(FText::FromString(LootSearch))
        .OnTextCommitted_Lambda([this](const FText& V,ETextCommit::Type){LootSearch=V.ToString();LootPage=0;Rebuild();}));
    TArray<int32> Visible;for(int I=0;I<Rules.Num();++I)if(LootSearch.IsEmpty()||Str(Rules[I]->AsObject(),TEXT("label")).Contains(LootSearch))Visible.Add(I);
    LootPage=FMath::Clamp(LootPage,0,FMath::Max(0,(Visible.Num()-1)/25));
    auto Paging=SNew(SHorizontalBox);
    Paging->AddSlot().AutoWidth()[Button(TEXT("Previous rules"),[this](){LootPage=FMath::Max(0,LootPage-1);Rebuild();})];
    Paging->AddSlot().FillWidth(1)[Text(FString::Printf(TEXT("%d matching rules • page %d / %d"),Visible.Num(),LootPage+1,FMath::Max(1,(Visible.Num()+24)/25)),15,Muted)];
    Paging->AddSlot().AutoWidth()[Button(TEXT("Next rules"),[this](){++LootPage;Rebuild();})];Add(Paging);
    for(int32 Row=LootPage*25;Row<FMath::Min(Visible.Num(),(LootPage+1)*25);++Row)
    {
        const int32 Index=Visible[Row];
        const auto R=Rules[Index]->AsObject();const bool Enabled=Bool(R,TEXT("enabled"),true);
        FString Summary=FString::Printf(TEXT("%d. %s — %s%s"),Index+1,*Str(R,TEXT("label"),TEXT("Loot rule")),*Str(R,TEXT("action"),TEXT("keep")),Enabled?TEXT(""):TEXT(" (disabled)"));
        if(!Str(R,TEXT("name")).IsEmpty())Summary+=TEXT("\nName ")+Str(R,TEXT("name_mode"),TEXT("prefix"))+TEXT(": ")+Str(R,TEXT("name"));
        if(Num(R,TEXT("material")))Summary+=TEXT("\nMaterial: ")+FString(ACERetailObjectNames::GetMaterialTypeName(uint32(Num(R,TEXT("material")))));
        if(Num(R,TEXT("type")))Summary+=FString::Printf(TEXT(" • type %.0f"),Num(R,TEXT("type")));
        for(const TCHAR* Field:{TEXT("workmanship"),TEXT("value"),TEXT("burden"),TEXT("rating")})
        {
            const double Lo=Num(R,*(FString(TEXT("min_"))+Field)),Hi=Num(R,*(FString(TEXT("max_"))+Field));
            if(Lo>0||Hi>0)Summary+=FString::Printf(TEXT("\n%s: %g – %s"),Field,Lo,Hi>0?*FString::Printf(TEXT("%g"),Hi):TEXT("any"));
        }
        if(Num(R,TEXT("keep_up_to")))Summary+=FString::Printf(TEXT("\nKeep up to %.0f per item type"),Num(R,TEXT("keep_up_to")));
        const TArray<TSharedPtr<FJsonValue>>* Conditions=nullptr;if(R->TryGetArrayField(TEXT("conditions"),Conditions)&&Conditions->Num())Summary+=FString::Printf(TEXT("\n%d property / spell conditions"),Conditions->Num());
        auto CardBody=SNew(SVerticalBox);CardBody->AddSlot().AutoHeight()[Text(Summary,16,Enabled?Ink:Muted)];
        auto Actions=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6,6));
        Actions->AddSlot()[Button(TEXT("Edit"),[this,R,Index](){LootDraft=MakeShared<FJsonObject>();LootDraft->Values=R->Values;LootEditIndex=Index;ConditionDraft.Reset();Rebuild();})];
        Actions->AddSlot()[Button(Enabled?TEXT("Disable"):TEXT("Enable"),[this,R,Index,Enabled]() {auto Rules=CurrentLootRules();if(!Rules.IsValidIndex(Index))return;auto Copy=MakeShared<FJsonObject>();Copy->Values=R->Values;Copy->SetBoolField(TEXT("enabled"),!Enabled);Rules[Index]=MakeShared<FJsonValueObject>(Copy);WriteLootRules(Rules);})];
        if(Index>0)Actions->AddSlot()[Button(TEXT("Up"),[this,Index]() {auto Rules=CurrentLootRules();if(!Rules.IsValidIndex(Index))return;Rules.Swap(Index,Index-1);WriteLootRules(Rules);})];
        if(Index+1<Rules.Num())Actions->AddSlot()[Button(TEXT("Down"),[this,Index]() {auto Rules=CurrentLootRules();if(!Rules.IsValidIndex(Index+1))return;Rules.Swap(Index,Index+1);WriteLootRules(Rules);})];
        Actions->AddSlot()[Button(TEXT("Remove"),[this,Index]() {auto Rules=CurrentLootRules();if(!Rules.IsValidIndex(Index))return;Rules.RemoveAt(Index);WriteLootRules(Rules);})];
        CardBody->AddSlot().AutoHeight().Padding(0,8)[Actions];Add(Tile(CardBody));
    }
    Heading(TEXT("Save / load loot rules"),TEXT("Reuse rules between hunts without changing buffs, combat or routes. Loading stops UCM; press Start when ready. Saved files are in Saved/ClientPlugins/LootProfiles."));
    Add(SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).HintText(FText::FromString(TEXT("Profile name, e.g. Copper_W7")))
        .Text(FText::FromString(LootProfileName)).OnTextChanged_Lambda([this](const FText& V){LootProfileName=V.ToString();}));
    Add(Button(TEXT("Save loot profile"),[this](){if(Host->SaveLootProfile(LootProfileName))Rebuild();}));
    TArray<FString> Files;IFileManager::Get().FindFiles(Files,*(Host->UserDirectory()/TEXT("LootProfiles/*.json")),true,false);Files.Sort();
    for(const auto& File:Files){const FString Name=FPaths::GetBaseFilename(File);Add(Button(TEXT("Load ")+Name,[this,Name](){if(Host->LoadLootProfile(Name)){LootProfileName=Name;Rebuild();}}));}
}

void LootConditions()
{
    struct FCalculatedType{FString Key;TArray<FString> Value;};
    const TMap<int32,FCalculatedType> Calculated={
        {7,{TEXT("Object class"),{TEXT("Decal object class ID")}}},{10,{TEXT("Minimum weapon damage"),{TEXT("Minimum damage")}}},
        {17,{TEXT("Exact palette"),{TEXT("Palette slot (starts at 0)"),TEXT("Palette ID")}}},
        {1000,{TEXT("Current character skill"),{TEXT("Minimum skill"),TEXT("Skill ID")}}},
        {1001,{TEXT("Main pack space"),{TEXT("Minimum empty slots")}}},
        {1002,{TEXT("Minimum character level"),{TEXT("Level")}}},{1003,{TEXT("Maximum character level"),{TEXT("Level")}}},
        {1004,{TEXT("Base character skill range"),{TEXT("Skill ID"),TEXT("Minimum"),TEXT("Maximum")}}},
        {2000,{TEXT("Buffed average melee damage"),{TEXT("Minimum average damage")}}},
        {2001,{TEXT("Buffed missile damage"),{TEXT("Minimum missile damage score")}}},
        {2003,{TEXT("Buffed integer property"),{TEXT("Minimum value"),TEXT("Property ID")}}},
        {2005,{TEXT("Buffed decimal property"),{TEXT("Minimum value"),TEXT("Property ID")}}},
        {2006,{TEXT("Potential tinkered melee damage"),{TEXT("Minimum damage score")}}},
        {2007,{TEXT("Total ratings"),{TEXT("Minimum combined ratings")}}},
        {2008,{TEXT("Potential tinkered melee targets"),{TEXT("Minimum damage score"),TEXT("Minimum melee defense multiplier"),TEXT("Minimum attack multiplier")}}}
    };
    Heading(TEXT("Property, character and spell conditions"),TEXT("All conditions must match. Properties use server appraisal data or supported Decal aliases. Regex supports groups, alternatives, repetition and named captures, with bounded match time and a 2,048-character limit."));
    const TArray<TSharedPtr<FJsonValue>>* Stored=nullptr;TArray<TSharedPtr<FJsonValue>> Conditions;
    if(LootDraft->TryGetArrayField(TEXT("conditions"),Stored))Conditions=*Stored;
    for(int I=0;I<Conditions.Num();++I)
    {
        auto C=Conditions[I]->AsObject();auto Row=SNew(SHorizontalBox);
        const FString Summary=Str(C,TEXT("field"))+TEXT(" ")+FString::Printf(TEXT("%.0f %s %g "),Num(C,TEXT("key")),*Str(C,TEXT("op")),Num(C,TEXT("value")))+Str(C,TEXT("pattern"));
        Row->AddSlot().FillWidth(1)[Text(Summary,15,Muted)];
        Row->AddSlot().AutoWidth()[Button(TEXT("Edit"),[this,C,I](){ConditionDraft=MakeShared<FJsonObject>();ConditionDraft->Values=C->Values;ConditionIndex=I;Rebuild();})];
        Row->AddSlot().AutoWidth()[Button(TEXT("Remove"),[this,I,Conditions]()mutable{Conditions.RemoveAt(I);LootDraft->SetArrayField(TEXT("conditions"),Conditions);ConditionDraft.Reset();Rebuild();})];Add(Row);
    }
    if(!ConditionDraft)
    {
        Add(Button(TEXT("+ Add condition"),[this](){ConditionDraft=MakeShared<FJsonObject>();ConditionDraft->SetStringField(TEXT("field"),TEXT("int"));ConditionDraft->SetStringField(TEXT("op"),TEXT("ge"));ConditionDraft->SetNumberField(TEXT("key"),19);ConditionIndex=INDEX_NONE;Rebuild();}));return;
    }
    auto Fields=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5,5));
    for(const FString Field:{TEXT("int"),TEXT("float"),TEXT("string"),TEXT("spells"),TEXT("legacy")})Fields->AddSlot()[Button((Field==TEXT("legacy")?TEXT("Calculated / character"):Field)+(Str(ConditionDraft,TEXT("field"))==Field?TEXT(" •"):TEXT("")),[this,Field](){ConditionDraft->SetStringField(TEXT("field"),Field);if(Field==TEXT("legacy")){ConditionDraft->SetNumberField(TEXT("key"),1002);ConditionDraft->SetArrayField(TEXT("args"),{MakeShared<FJsonValueNumber>(1)});}Rebuild();})];Add(Fields);
    const FString Field=Str(ConditionDraft,TEXT("field"));
    if(Field==TEXT("legacy"))
    {
        const int Key=int(Num(ConditionDraft,TEXT("key")));const auto* Definition=Calculated.Find(Key);
        auto Menu=SNew(SVerticalBox);TArray<int> Keys;Calculated.GetKeys(Keys);Keys.Sort();
        for(int Type:Keys){const auto Choice=Calculated[Type];Menu->AddSlot().AutoHeight()[Button(Choice.Key,[this,Type,Choice](){ConditionDraft->SetNumberField(TEXT("key"),Type);TArray<TSharedPtr<FJsonValue>> Args;for(const auto& Label:Choice.Value)Args.Add(MakeShared<FJsonValueNumber>(0));ConditionDraft->SetArrayField(TEXT("args"),Args);FSlateApplication::Get().DismissAllMenus();Rebuild();})];}
        Add(SNew(SComboButton).ButtonStyle(&Style()).ButtonContent()[Text(Definition?Definition->Key:TEXT("Retired legacy condition"),16)].MenuContent()[SNew(SBox).MaxDesiredHeight(320)[SNew(SScrollBox)+SScrollBox::Slot()[Menu]]]);
        if(Definition)for(int Index=0;Index<Definition->Value.Num();++Index)
        {
            const auto& Args=ConditionDraft->GetArrayField(TEXT("args"));Add(Text(Definition->Value[Index],15,Muted));
            Add(SNew(SSpinBox<double>).MinValue(0).MaxValue(4294967295.).Value(Args.IsValidIndex(Index)?Args[Index]->AsNumber():0)
                .OnValueChanged_Lambda([this,Index](double V){auto Values=ConditionDraft->GetArrayField(TEXT("args"));while(Values.Num()<=Index)Values.Add(MakeShared<FJsonValueNumber>(0));Values[Index]=MakeShared<FJsonValueNumber>(V);ConditionDraft->SetArrayField(TEXT("args"),Values);}));
        }
    }
    else if(Field!=TEXT("spells"))
    {
        Add(Text(TEXT("Appraisal property ID (e.g. Int 131 material, 105 workmanship, 28 armor, 265 set; Float 29 melee defense; String 1 name)"),14,Muted));
        Add(SNew(SSpinBox<int32>).MinValue(1).MaxValue(MAX_int32).Value(int(Num(ConditionDraft,TEXT("key"),19))).OnValueChanged_Lambda([this](int V){ConditionDraft->SetNumberField(TEXT("key"),V);}));
    }
    if(Field==TEXT("string")||Field==TEXT("spells"))
    {
        Add(Text(TEXT("Match text / alternatives"),15));
        Add(SNew(SEditableTextBox).Text(FText::FromString(Str(ConditionDraft,TEXT("pattern")))).OnTextChanged_Lambda([this](const FText& V){ConditionDraft->SetStringField(TEXT("pattern"),V.ToString());}));
        if(Field==TEXT("spells"))
        {
            Add(Text(TEXT("Exclude matching spell names (blank = none)"),15));
            Add(SNew(SEditableTextBox).Text(FText::FromString(Str(ConditionDraft,TEXT("exclude")))).OnTextChanged_Lambda([this](const FText& V){ConditionDraft->SetStringField(TEXT("exclude"),V.ToString());}));
            Add(Text(TEXT("Minimum matching spells"),15));
            Add(SNew(SSpinBox<int32>).MinValue(0).MaxValue(1024).Value(int(Num(ConditionDraft,TEXT("value"),1))).OnValueChanged_Lambda([this](int V){ConditionDraft->SetNumberField(TEXT("value"),V);}));
            if(!ConditionDraft->HasField(TEXT("value")))ConditionDraft->SetNumberField(TEXT("value"),1);
        }
    }
    else if(Field!=TEXT("legacy"))
    {
        auto Ops=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5,5));
        for(const FString Op:{TEXT("ge"),TEXT("le"),TEXT("eq"),TEXT("ne"),TEXT("bits")})Ops->AddSlot()[Button(Op+(Str(ConditionDraft,TEXT("op"))==Op?TEXT(" •"):TEXT("")),[this,Op](){ConditionDraft->SetStringField(TEXT("op"),Op);Rebuild();})];Add(Ops);
        Add(Text(TEXT("ge = at least • le = at most • eq = equal • ne = unequal • bits = any matching flag"),14,Muted));
        Add(SNew(SSpinBox<double>).MinValue(-2147483648.).MaxValue(4294967295.).Value(Num(ConditionDraft,TEXT("value"))).OnValueChanged_Lambda([this](double V){ConditionDraft->SetNumberField(TEXT("value"),V);}));
        Add(Button(Bool(ConditionDraft,TEXT("missing_zero"))?TEXT("Missing value: use 0 (VT)"):TEXT("Missing value: does not match"),[this](){ConditionDraft->SetBoolField(TEXT("missing_zero"),!Bool(ConditionDraft,TEXT("missing_zero")));Rebuild();}));
    }
    Add(Button(TEXT("Accept condition"),[this,Conditions]()mutable
    {
        if(!ACEVTProfile::IsSupportedPattern(Str(ConditionDraft,TEXT("pattern")))||!ACEVTProfile::IsSupportedPattern(Str(ConditionDraft,TEXT("exclude")))){Host->Notice=TEXT("Invalid or unsupported regular expression (maximum 2,048 characters and 32 capture groups).");return;}
        if(ConditionIndex==INDEX_NONE){if(Conditions.Num()>=ACEVTProfile::MaxLootConditions){Host->Notice=FString::Printf(TEXT("Maximum %d conditions per rule."),ACEVTProfile::MaxLootConditions);return;}Conditions.Add(MakeShared<FJsonValueObject>(ConditionDraft));}
        else if(Conditions.IsValidIndex(ConditionIndex))Conditions[ConditionIndex]=MakeShared<FJsonValueObject>(ConditionDraft);
        LootDraft->SetArrayField(TEXT("conditions"),Conditions);ConditionDraft.Reset();Rebuild();
    }));
    Add(Button(TEXT("Cancel condition"),[this](){ConditionDraft.Reset();Rebuild();}));
}

void SalvagePanel()
{
    Heading(TEXT("Salvage combining"),TEXT("Bags combine only with the same material and workmanship group. Material overrides replace the default groups. A value target delays full bags until their combined value reaches the target; zero uses workmanship only. Saving stops UCM."));
    const TSharedPtr<FJsonObject>* Stored=nullptr;
    TSharedPtr<FJsonObject> Policy=P()->TryGetObjectField(TEXT("salvage_policy"),Stored)?*Stored:nullptr;
    if(!SalvageDraft)
    {
        SalvageDraft=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Ranges;
        const TArray<TSharedPtr<FJsonValue>>* Source=nullptr;
        if(SalvageMaterial&&Policy&&Policy->HasTypedField<EJson::Object>(TEXT("materials")))Policy->GetObjectField(TEXT("materials"))->TryGetArrayField(FString::FromInt(SalvageMaterial),Source);
        if(!Source&&Policy)Policy->TryGetArrayField(TEXT("default"),Source);
        if(Source)for(const auto& V:*Source){auto Copy=MakeShared<FJsonObject>();Copy->Values=V->AsObject()->Values;Ranges.Add(MakeShared<FJsonValueObject>(Copy));}
        else for(const auto& Pair:TArray<TPair<double,double>>{{1,6},{7,8},{9,9},{10,10}}){auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("min"),Pair.Key);R->SetNumberField(TEXT("max"),Pair.Value);Ranges.Add(MakeShared<FJsonValueObject>(R));}
        SalvageDraft->SetArrayField(TEXT("ranges"),Ranges);
        double Target=0;if(SalvageMaterial&&Policy&&Policy->HasTypedField<EJson::Object>(TEXT("values")))Policy->GetObjectField(TEXT("values"))->TryGetNumberField(FString::FromInt(SalvageMaterial),Target);
        SalvageDraft->SetNumberField(TEXT("target"),Target);
    }
    auto Menu=SNew(SVerticalBox);
    for(int32 Id=0;Id<=77;++Id)Menu->AddSlot().AutoHeight()[Button(Id?ACERetailObjectNames::GetMaterialTypeName(Id):TEXT("Default groups"),[this,Id](){SalvageMaterial=Id;SalvageDraft.Reset();FSlateApplication::Get().DismissAllMenus();Rebuild();})];
    Add(SNew(SComboButton).ButtonStyle(&Style()).ContentPadding(FMargin(12,9)).ButtonContent()[Text(SalvageMaterial?ACERetailObjectNames::GetMaterialTypeName(SalvageMaterial):TEXT("Default groups"),16)]
        .MenuContent()[SNew(SBox).MaxDesiredHeight(320)[SNew(SScrollBox)+SScrollBox::Slot()[Menu]]]);
    Add(Text(TEXT("Workmanship groups: minimum / maximum"),16,Muted));
    const auto Ranges=SalvageDraft->GetArrayField(TEXT("ranges"));
    for(int32 Index=0;Index<Ranges.Num();++Index)
    {
        auto Row=SNew(SHorizontalBox);auto Range=Ranges[Index]->AsObject();
        for(const FString Key:{TEXT("min"),TEXT("max")})Row->AddSlot().FillWidth(1).Padding(0,0,8,0)[SNew(SSpinBox<double>).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).MinValue(0).MaxValue(1000).Value(Num(Range,*Key))
            .OnValueChanged_Lambda([Range,Key](double V){Range->SetNumberField(Key,V);})];
        Row->AddSlot().AutoWidth()[Button(TEXT("Remove"),[this,Index](){auto R=SalvageDraft->GetArrayField(TEXT("ranges"));R.RemoveAt(Index);SalvageDraft->SetArrayField(TEXT("ranges"),R);Rebuild();})];Add(Row);
    }
    Add(Button(TEXT("+ Workmanship group"),[this](){auto R=SalvageDraft->GetArrayField(TEXT("ranges"));if(R.Num()>=64)return;auto Next=MakeShared<FJsonObject>();const double Start=R.Num()?Num(R.Last()->AsObject(),TEXT("max"))+1:1;Next->SetNumberField(TEXT("min"),Start);Next->SetNumberField(TEXT("max"),Start);R.Add(MakeShared<FJsonValueObject>(Next));SalvageDraft->SetArrayField(TEXT("ranges"),R);Rebuild();}));
    if(SalvageMaterial)
    {
        Add(Text(TEXT("Minimum combined value for a full bag (pyreals; 0 = disabled)"),15,Muted));
        Add(SNew(SSpinBox<int32>).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).MinValue(0).MaxValue(MAX_int32).Value(int32(Num(SalvageDraft,TEXT("target"))))
            .OnValueChanged_Lambda([this](int32 V){SalvageDraft->SetNumberField(TEXT("target"),V);}));
    }
    auto Save=[this,Policy](bool Remove)
    {
        auto Updated=MakeShared<FJsonObject>();Updated->Values=P()->Values;
        auto Copy=MakeShared<FJsonObject>();if(Policy)Copy->Values=Policy->Values;
        if(!Copy->HasField(TEXT("default"))){TArray<TSharedPtr<FJsonValue>> Defaults;for(const auto& Pair:TArray<TPair<double,double>>{{1,6},{7,8},{9,9},{10,10}}){auto R=MakeShared<FJsonObject>();R->SetNumberField(TEXT("min"),Pair.Key);R->SetNumberField(TEXT("max"),Pair.Value);Defaults.Add(MakeShared<FJsonValueObject>(R));}Copy->SetArrayField(TEXT("default"),Defaults);}
        if(SalvageMaterial)
        {
            auto Materials=MakeShared<FJsonObject>(),Values=MakeShared<FJsonObject>();
            if(Copy->HasTypedField<EJson::Object>(TEXT("materials")))Materials->Values=Copy->GetObjectField(TEXT("materials"))->Values;
            if(Copy->HasTypedField<EJson::Object>(TEXT("values")))Values->Values=Copy->GetObjectField(TEXT("values"))->Values;
            const FString Key=FString::FromInt(SalvageMaterial);
            if(Remove){Materials->RemoveField(Key);Values->RemoveField(Key);}
            else{Materials->SetArrayField(Key,SalvageDraft->GetArrayField(TEXT("ranges")));Values->SetNumberField(Key,Num(SalvageDraft,TEXT("target")));}
            Copy->SetObjectField(TEXT("materials"),Materials);Copy->SetObjectField(TEXT("values"),Values);
        }
        else Copy->SetArrayField(TEXT("default"),SalvageDraft->GetArrayField(TEXT("ranges")));
        Updated->SetObjectField(TEXT("salvage_policy"),Copy);FString Json;FJsonSerializer::Serialize(Updated,TJsonWriterFactory<>::Create(&Json));
        if(Host->SaveProfile(Plugin->Id,Plugin->ProfileName,Json)){SalvageDraft.Reset();Rebuild();}
        else Host->Notice=TEXT("Groups must have ascending, non-overlapping ranges with minimum no greater than maximum.");
    };
    Add(Button(TEXT("Save groups"),[Save](){Save(false);}));
    if(SalvageMaterial)Add(Button(TEXT("Use default groups for this material"),[Save](){Save(true);}));
    Add(Button(TEXT("Back to loot rules"),[this](){EditingSalvage=false;SalvageDraft.Reset();Rebuild();}));
}
