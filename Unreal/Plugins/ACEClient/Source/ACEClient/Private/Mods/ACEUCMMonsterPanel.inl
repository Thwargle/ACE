// Included inside SUCMPanel. The matrix uses the same fields as imported VT
// rules; only new native rules opt into inheritance from the live default row.
int32 MonsterPage=0;
float MonsterFontSize=0;
TSharedPtr<FJsonObject> MatrixDefault;
TSharedPtr<SScrollBox> MonsterTableScroll;
TSharedPtr<FJsonObject> MonsterDefaults() const
{
    const TArray<TSharedPtr<FJsonValue>>* Rules=nullptr;
    if(P()->TryGetArrayField(TEXT("monsters"),Rules))for(const auto& V:*Rules)
        if(V->Type==EJson::Object&&Bool(V->AsObject(),TEXT("default")))return V->AsObject();
    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("name"),TEXT("<DEFAULT>"));R->SetBoolField(TEXT("default"),true);return R;
}
TSharedPtr<FJsonObject> EffectiveMonster(const TSharedPtr<FJsonObject>& Rule) const
{
    auto R=MakeShared<FJsonObject>();
    if(Bool(Rule,TEXT("inherit_default")))R->Values=(MatrixDefault?MatrixDefault:MonsterDefaults())->Values;
    for(const auto& Pair:Rule->Values)R->Values.Add(Pair.Key,Pair.Value);
    if(Bool(R,TEXT("ignore"))&&Str(R,TEXT("combat"))!=TEXT("ignore")&&R->HasField(TEXT("bolt"))&&!Bool(R,TEXT("bolt"))&&R->HasField(TEXT("streak"))&&!Bool(R,TEXT("streak"))&&R->HasField(TEXT("ring"))&&!Bool(R,TEXT("ring")))R->SetBoolField(TEXT("ignore"),false);
    return R;
}
void SaveMonster(const TSharedPtr<FJsonObject>& Rule)
{
    if(Bool(Rule,TEXT("default")))
    {
        auto Rules=P()->GetArrayField(TEXT("monsters"));
        bool Found=false;for(const auto& V:Rules)if(V->AsObject()==Rule)Found=true;
        if(!Found){Rules.Insert(MakeShared<FJsonValueObject>(Rule),0);P()->SetArrayField(TEXT("monsters"),Rules);if(ExpandedMonster!=INDEX_NONE)++ExpandedMonster;}
    }
    Save();
}
bool MonsterFlag(const TSharedPtr<FJsonObject>& Rule,const FString& Key) const
{
    bool Value;
    if(Rule->TryGetBoolField(Key,Value))return Value;
    if(Bool(Rule,TEXT("inherit_default"))&&(MatrixDefault?MatrixDefault:MonsterDefaults())->TryGetBoolField(Key,Value))return Value;
    return Bool(P(),*Key,Key==TEXT("bolt")||Key==TEXT("streak"));
}
TSharedRef<SWidget> MonsterSwitch(const TSharedPtr<FJsonObject>& Rule,const FString& Key,const FString& Caption,bool Details=false)
{
    return SNew(SACEPluginButton).Tag(FName(*(FString(Details?TEXT("UCMMobDetail."):TEXT("UCMMonster."))+Str(Rule,TEXT("name"))+TEXT(".")+Key)))
        .ButtonStyle(&Style()).ContentPadding(FMargin(5,7)).IsFocusable(false)
        .ToolTip(FACEPluginStyle::Tooltip(TAttribute<FText>::CreateLambda([this,Rule,Key,Caption](){return FText::FromString(Caption+TEXT("\n")+(MonsterFlag(Rule,Key)?TEXT("On"):TEXT("Off"))
            +(!Rule->HasField(Key)&&Bool(Rule,TEXT("inherit_default"))?TEXT(" (inherited from Default)"):TEXT(""))+TEXT(". Click to toggle for this row."));})))
        .OnClicked_Lambda([this,Rule,Key](){Rule->SetBoolField(Key,!MonsterFlag(Rule,Key));
            // Old imports inferred Ignore from these switches. An explicit
            // Ignore mode remains an independent choice in the Mode menu.
            if(Str(Rule,TEXT("combat"))!=TEXT("ignore"))Rule->SetBoolField(TEXT("ignore"),false);
            SaveMonster(Rule);return FReply::Handled();})
        [SNew(SACEPluginText).Font(FACEPluginStyle::NativeFont("Regular",15)).Justification(ETextJustify::Center)
            .Text_Lambda([this,Rule,Key](){return FText::FromString(MonsterFlag(Rule,Key)?TEXT("On"):TEXT("—"));})
            .ColorAndOpacity_Lambda([this,Rule,Key](){return MonsterFlag(Rule,Key)?Accent:Muted;})];
}
TSharedRef<SWidget> MonsterChoice(const TSharedPtr<FJsonObject>& Rule,const FString& Key,const TArray<TPair<FString,FString>>& Options,bool Numeric=false,bool Details=false)
{
    auto Label=[this,Rule,Key,Options,Numeric](){auto E=EffectiveMonster(Rule);
        FString Value=Numeric?FString::FromInt(int32(Num(E,*Key,Key==TEXT("pet_damage_type")?101:Key==TEXT("damage_type")?Num(P(),TEXT("damage_type")):0))):Str(E,*Key,TEXT("auto"));
        if(Key==TEXT("combat")&&Bool(E,TEXT("ignore")))Value=TEXT("ignore");
        for(const auto& O:Options)if(O.Key==Value)return O.Value;return Value;};
    return SNew(SACEPluginComboButton).Tag(FName(*(FString(Details?TEXT("UCMMobDetail."):TEXT("UCMMonster."))+Str(Rule,TEXT("name"))+TEXT(".")+Key)))
        .ComboButtonStyle(&FACEPluginStyle::Current()->Combo).ButtonContent()[LiveText(Label)]
        .OnGetMenuContent_Lambda([this,Rule,Key,Options,Numeric](){auto List=SNew(SVerticalBox);
            if(Bool(Rule,TEXT("inherit_default")))List->AddSlot().AutoHeight()[MenuButton(TEXT("Use default"),[this,Rule,Key](){Rule->RemoveField(Key);if(Key==TEXT("combat"))Rule->RemoveField(TEXT("ignore"));FSlateApplication::Get().DismissAllMenus();SaveMonster(Rule);})];
            for(const auto& O:Options)List->AddSlot().AutoHeight()[MenuButton(O.Value,[this,Rule,Key,O,Numeric](){
                if(Numeric)Rule->SetNumberField(Key,FCString::Atoi(*O.Key));else Rule->SetStringField(Key,O.Key);
                if(Key==TEXT("combat"))Rule->SetBoolField(TEXT("ignore"),O.Key==TEXT("ignore"));
                FSlateApplication::Get().DismissAllMenus();SaveMonster(Rule);})];
            return SNew(SBox).MaxDesiredHeight(400)[SNew(SScrollBox)+SScrollBox::Slot()[List]];});
}
struct FMonsterMode {const TCHAR* Key;const TCHAR* Short;const TCHAR* Help;};
static const TArray<FMonsterMode>& MonsterModes()
{
    static const TArray<FMonsterMode> Modes={{TEXT("bolt"),TEXT("A"),TEXT("A — Ordinary attacks\nWeapon attacks and bolt / arc spells.")},
        {TEXT("ring"),TEXT("R"),TEXT("R — Ring spells\nArea spells for nearby groups; uses the ring distance and target count in Combat.")},
        {TEXT("streak"),TEXT("S"),TEXT("S — Streak spells\nAllow streak attack spells.")},
        {TEXT("debuff_imperil"),TEXT("I"),TEXT("I — Imperil\nLower the target's armor before attacking.")},
        {TEXT("debuff_vuln"),TEXT("V"),TEXT("V — Vulnerability\nLower resistance to the selected attack element.")},
        {TEXT("debuff_yield"),TEXT("Y"),TEXT("Y — Yield\nLower the target's magic defense.")},
        {TEXT("debuff_fester"),TEXT("F"),TEXT("F — Fester\nReduce the target's stamina regeneration.")},
        {TEXT("debuff_broadside"),TEXT("B"),TEXT("B — Broadside of a Barn\nLower the target's missile defense.")},
        {TEXT("debuff_gravity"),TEXT("G"),TEXT("G — Gravity Well\nSlow the target's movement.")},
        {TEXT("debuff_weakening"),TEXT("W"),TEXT("W — Weakening Curse\nLower the target's damage rating.")},
        {TEXT("debuff_festering"),TEXT("FC"),TEXT("FC — Festering Curse\nReduce the target's healing.")},
        {TEXT("debuff_corruption"),TEXT("Cp"),TEXT("Cp — Corruption\nApply nether damage over time.")},
        {TEXT("debuff_destructive"),TEXT("DC"),TEXT("DC — Destructive Curse\nApply nether damage over time.")},
        {TEXT("debuff_corrosion"),TEXT("Cs"),TEXT("Cs — Corrosion\nApply nether damage over time.")}};
    return Modes;
}
static const TArray<TPair<FString,FString>>& MonsterOptions(const FString& Key)
{
    if(Key==TEXT("combat")){static const TArray<TPair<FString,FString>> Options={{TEXT("auto"),TEXT("Auto")},{TEXT("melee"),TEXT("Melee")},{TEXT("missile"),TEXT("Missile")},{TEXT("magic"),TEXT("Magic")},{TEXT("off"),TEXT("No attacks")},{TEXT("ignore"),TEXT("Ignore")}};return Options;}
    if(Key==TEXT("damage_type")){static const TArray<TPair<FString,FString>> Options={{TEXT("0"),TEXT("Auto")},{TEXT("1"),TEXT("Slash")},{TEXT("2"),TEXT("Pierce")},{TEXT("4"),TEXT("Blunt")},{TEXT("8"),TEXT("Cold")},{TEXT("16"),TEXT("Fire")},{TEXT("32"),TEXT("Acid")},{TEXT("64"),TEXT("Electric")},{TEXT("1024"),TEXT("Nether")}};return Options;}
    if(Key==TEXT("pet_damage_type")){static const TArray<TPair<FString,FString>> Options={{TEXT("101"),TEXT("Auto")},{TEXT("98"),TEXT("None")},{TEXT("8"),TEXT("Attack")},{TEXT("2"),TEXT("Slash")},{TEXT("0"),TEXT("Pierce")},{TEXT("1"),TEXT("Blunt")},{TEXT("5"),TEXT("Cold")},{TEXT("6"),TEXT("Fire")},{TEXT("3"),TEXT("Acid")},{TEXT("4"),TEXT("Electric")}};return Options;}
    static const TArray<TPair<FString,FString>> Vulnerabilities={{TEXT("0"),TEXT("None")},{TEXT("1"),TEXT("Slash")},{TEXT("2"),TEXT("Pierce")},{TEXT("4"),TEXT("Blunt")},{TEXT("8"),TEXT("Cold")},{TEXT("16"),TEXT("Fire")},{TEXT("32"),TEXT("Acid")},{TEXT("64"),TEXT("Electric")}};
    return Vulnerabilities;
}
void MonsterDetailFlags(const TSharedPtr<FJsonObject>& Rule)
{
    const float Scale=FMath::Max(1.f,FACEPluginStyle::ScaleFont(FACEPluginStyle::NativeFont("Regular",16)).Size/15.f);
    for(const auto& Mode:MonsterModes())
    {
        if(FString(Mode.Key)==TEXT("bolt"))Add(Text(TEXT("Attacks"),18,Accent));
        if(FString(Mode.Key)==TEXT("debuff_imperil"))Add(Text(TEXT("Debuffs"),18,Accent));
        FString Label,Help;FString(Mode.Help).Split(TEXT("\n"),&Label,&Help);
        auto Row=SNew(SHorizontalBox);
        Row->AddSlot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,8,0)
            [SNew(SBox).ToolTip(FACEPluginStyle::Tooltip(FText::FromString(Mode.Help)))[Text(Label,16)]];
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(42*Scale).MinDesiredHeight(34)[MonsterSwitch(Rule,Mode.Key,Mode.Help,true)]];
        if(Bool(Rule,TEXT("inherit_default")))Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6,0,0,0)
            [SNew(SACEPluginButton).Tag(FName(*(TEXT("UCMMobDefault.")+Str(Rule,TEXT("name"))+TEXT(".")+Mode.Key)))
                .ButtonStyle(&Style()).ContentPadding(FMargin(5,7)).IsFocusable(false)
                .IsEnabled_Lambda([Rule,Key=FString(Mode.Key)](){return Rule->HasField(Key);})
                .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(TEXT("Remove this override and follow the Default row."))))
                .OnClicked_Lambda([this,Rule,Key=FString(Mode.Key)](){Rule->RemoveField(Key);SaveMonster(Rule);return FReply::Handled();})[Text(TEXT("Default"),14)]];
        Add(Row);
    }
}
void MonsterMatrix()
{
    SectionSelector(TEXT("Monsters"));SectionBody.Reset();
    auto Actions=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6,6));
    Actions->AddSlot()[Button(TEXT("Add selected mob"),[this](){AddSelectedMonster();})];
    Add(Actions);
    Add(SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[SAssignNew(MonsterName,SACEPluginEditableTextBox)
        .Style(&FACEPluginStyle::Current()->Edit).HintText(FText::FromString(TEXT("Mob name begins with…")))
        .OnTextCommitted_Lambda([this](const FText& V,ETextCommit::Type How){if(How==ETextCommit::OnEnter)AddMonster(V.ToString());})]
        +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[Button(TEXT("Add"),[this](){AddMonster(MonsterName->GetText().ToString());})]);
    Add(SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[SNew(SACEPluginEditableTextBox).Style(&FACEPluginStyle::Current()->Edit)
        .HintText(FText::FromString(TEXT("Find a mob…"))).Text(FText::FromString(MonsterSearch))
        .OnTextChanged_Lambda([this](const FText& V){MonsterSearch=V.ToString();})
        .OnTextCommitted_Lambda([this](const FText&,ETextCommit::Type How){if(How==ETextCommit::OnEnter){MonsterFilter=MonsterSearch.TrimStartAndEnd();MonsterPage=0;Rebuild();}})]
        +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[Button(TEXT("Search"),[this](){MonsterFilter=MonsterSearch.TrimStartAndEnd();MonsterPage=0;Rebuild();})]
        +SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Clear search"),[this](){MonsterSearch.Empty();MonsterFilter.Empty();MonsterPage=0;Rebuild();})]);
    const auto& Modes=MonsterModes();
    MonsterFontSize=FACEPluginStyle::ScaleFont(FACEPluginStyle::NativeFont("Regular",16)).Size;
    const float Scale=FMath::Max(1.f,MonsterFontSize/15.f);
    const auto Column=[Scale](TSharedRef<SHorizontalBox> Row,float Width,TSharedRef<SWidget> W){Row->AddSlot().AutoWidth().Padding(2,2).VAlign(VAlign_Center)[SNew(SBox).WidthOverride(Width*Scale).MinDesiredHeight(34)[W]];};
    auto Table=SNew(SVerticalBox);auto Header=SNew(SHorizontalBox);auto Names=SNew(SVerticalBox);
    const auto HeadingCell=[](const TCHAR* Key,const TCHAR* Label,const TCHAR* Help){return SNew(SBox)
        .Tag(FName(*(FString(TEXT("UCMMonster.Header."))+Key))).Visibility(EVisibility::Visible).HAlign(HAlign_Center).VAlign(VAlign_Center)
        .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(Help)))[Text(Label,15,Accent)];};
    Column(Header,115,HeadingCell(TEXT("combat"),TEXT("Mode"),TEXT("Combat mode\nAuto chooses a usable skill and weapon. No attacks allows pets only. Ignore excludes both attacks and pets. Activity switches in Overview / Micro must be enabled.")));
    for(const auto& M:Modes)Column(Header,42,HeadingCell(M.Key,M.Short,M.Help));
    Column(Header,105,HeadingCell(TEXT("damage_type"),TEXT("Damage"),TEXT("Attack damage type\nAuto chooses an element for this mob; choose an element to override it.")));
    Column(Header,105,HeadingCell(TEXT("pet_damage_type"),TEXT("Pet"),TEXT("Pet damage type\nAuto chooses an eligible essence from the Equipment Pool. None disables summoning for this row. Summoning must be enabled in Overview / Micro.")));Column(Header,80,Text(TEXT("")));
    Table->AddSlot().AutoHeight()[Header];
    // Names stay outside the horizontal scroller. Match each control row's
    // measured height so names remain aligned at every plugin text scale.
    Names->AddSlot().AutoHeight()[SNew(SBox).HeightOverride_Lambda([Header](){return Header->GetDesiredSize().Y;})
        .Padding(6,2).VAlign(VAlign_Center)[Text(TEXT("Name"),15,Accent,false)]];
    TArray<TSharedPtr<FJsonObject>> Rows;MatrixDefault=MonsterDefaults();Rows.Add(MatrixDefault);
    TArray<int32> Matches;const auto Rules=P()->GetArrayField(TEXT("monsters"));
    for(int32 I=0;I<Rules.Num();++I)if(!Bool(Rules[I]->AsObject(),TEXT("default"))&&(MonsterFilter.IsEmpty()||Str(Rules[I]->AsObject(),TEXT("name")).Contains(MonsterFilter,ESearchCase::IgnoreCase)))Matches.Add(I);
    MonsterPage=FMath::Clamp(MonsterPage,0,FMath::Max(0,(Matches.Num()-1)/25));
    for(int32 I=MonsterPage*25;I<FMath::Min(Matches.Num(),(MonsterPage+1)*25);++I)Rows.Add(Rules[Matches[I]]->AsObject());
    for(const auto& Rule:Rows)
    {
        auto Row=SNew(SHorizontalBox);const bool Default=Bool(Rule,TEXT("default"));
        Column(Row,115,MonsterChoice(Rule,TEXT("combat"),MonsterOptions(TEXT("combat"))));
        for(const auto& M:Modes)Column(Row,42,MonsterSwitch(Rule,M.Key,M.Help));
        Column(Row,105,MonsterChoice(Rule,TEXT("damage_type"),MonsterOptions(TEXT("damage_type")),true));
        Column(Row,105,MonsterChoice(Rule,TEXT("pet_damage_type"),MonsterOptions(TEXT("pet_damage_type")),true));
        Column(Row,80,SNew(SACEPluginButton).Tag(FName(*(TEXT("UCMMonster.")+Str(Rule,TEXT("name"))+TEXT(".details")))).ButtonStyle(&Style()).ContentPadding(FMargin(5,7))
            .OnClicked_Lambda([this,Rule](){if(Bool(Rule,TEXT("default")))SaveMonster(Rule);const auto Current=P()->GetArrayField(TEXT("monsters"));for(int32 I=0;I<Current.Num();++I)if(Current[I]->AsObject()==Rule)ExpandedMonster=ExpandedMonster==I?INDEX_NONE:I;Rebuild();return FReply::Handled();})[Text(TEXT("Details"),14)]);
        Table->AddSlot().AutoHeight()[SNew(SBorder).BorderImage(&FACEPluginStyle::Current()->TitleBackground).Padding(0)[Row]];
        Names->AddSlot().AutoHeight()[SNew(SBox).HeightOverride_Lambda([Row](){return Row->GetDesiredSize().Y;})
            [SNew(SBorder).Tag(FName(*(TEXT("UCMMonster.Name.")+Str(Rule,TEXT("name"))))).BorderImage(&FACEPluginStyle::Current()->TitleBackground)
                .Padding(FMargin(6,2)).VAlign(VAlign_Center).Clipping(EWidgetClipping::ClipToBounds)
                .ToolTip(FACEPluginStyle::Tooltip(FText::FromString(Default?TEXT("Default settings for mobs without their own rule."):Str(Rule,TEXT("name")))))
                [SNew(SACEPluginText).Text(FText::FromString(Default?TEXT("Default"):Str(Rule,TEXT("name"))))
                    .Font(FACEPluginStyle::NativeFont("Regular",15)).ColorAndOpacity(Default?Accent:Ink).AutoWrapText(false).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]];
    }
    Add(SNew(SHorizontalBox)+SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
        [SNew(SBox).WidthOverride_Lambda([this,Scale](){return FMath::Clamp(BodyScroll->GetCachedGeometry().GetLocalSize().X*.4f,120.f,220.f*Scale);})[Names]]
        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Top)
        [SAssignNew(MonsterTableScroll,SScrollBox).Orientation(Orient_Horizontal).ScrollBarAlwaysVisible(false)+SScrollBox::Slot()[Table]]);
    if(Matches.Num()>25)
    {
        auto Pages=SNew(SHorizontalBox);Pages->AddSlot().AutoWidth()[Button(TEXT("Previous"),[this](){MonsterPage=FMath::Max(0,MonsterPage-1);Rebuild();})];
        Pages->AddSlot().FillWidth(1).Padding(8,0)[Text(FString::Printf(TEXT("Page %d / %d · %d mobs"),MonsterPage+1,(Matches.Num()+24)/25,Matches.Num()),14)];
        Pages->AddSlot().AutoWidth()[Button(TEXT("Next"),[this](){++MonsterPage;Rebuild();})];Add(Pages);
    }
    if(Rules.IsValidIndex(ExpandedMonster))
    {
        auto Rule=Rules[ExpandedMonster]->AsObject();const FString Name=Bool(Rule,TEXT("default"))?TEXT("Default"):Str(Rule,TEXT("name"));CollapsedSections.Add(TEXT("Monsters/")+Name,false);
        Heading(Name,TEXT("Changes save immediately to the selected Mobs profile."));MonsterDetails(Rule);
        if(!Bool(Rule,TEXT("default")))
        {
            Add(Button(TEXT("Use default settings"),[this,Rule](){const auto Original=Rule->Values;Rule->Values.Empty();
                for(const TCHAR* Key:{TEXT("name"),TEXT("exact"),TEXT("expression")})if(const auto* V=Original.Find(Key))Rule->Values.Add(Key,*V);
                Rule->SetBoolField(TEXT("inherit_default"),true);SaveMonster(Rule);Rebuild();}));
            Add(Button(TEXT("Remove mob"),[this,Rule](){auto Copy=P()->GetArrayField(TEXT("monsters"));Copy.RemoveAll([Rule](const auto& V){return V->AsObject()==Rule;});P()->SetArrayField(TEXT("monsters"),Copy);ExpandedMonster=INDEX_NONE;Save();Rebuild();}));
        }
    }
}
