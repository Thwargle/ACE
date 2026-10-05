TSharedPtr<FJsonObject> LegacyRule;
int32 LegacyRuleIndex=INDEX_NONE,LegacyPage=0;
FString LegacyFilter;
const TMap<int32,FString>& LegacySchemas()const
{
    static const TMap<int32,FString> S={
        {0,TEXT("Spell name|Pattern")},{1,TEXT("String property|Pattern|String property ID")},
        {2,TEXT("Integer at most|Value|Integer property ID")},{3,TEXT("Integer at least|Value|Integer property ID")},
        {4,TEXT("Decimal at most|Value|Decimal property ID")},{5,TEXT("Decimal at least|Value|Decimal property ID")},
        {6,TEXT("Damage percent|Minimum")},{7,TEXT("Object class|Class ID")},{8,TEXT("Spell count|Minimum count")},
        {9,TEXT("Spell matching count|Include pattern|Exclude pattern|Minimum count")},{10,TEXT("Minimum damage|Minimum")},
        {11,TEXT("Integer flags|Flag mask|Integer property ID")},{12,TEXT("Integer equals|Value|Integer property ID")},{13,TEXT("Integer not equal|Value|Integer property ID")},
        {14,TEXT("Similar color|Red|Green|Blue|Hue tolerance|Saturation/value tolerance")},
        {15,TEXT("Armor color|Red|Green|Blue|Hue tolerance|Saturation/value tolerance|Armor group")},
        {16,TEXT("Slot color|Red|Green|Blue|Hue tolerance|Saturation/value tolerance|Slot")},
        {17,TEXT("Exact palette|Slot|Palette")},{1000,TEXT("Character skill|Minimum|Skill ID")},{1001,TEXT("Main pack space|Minimum free slots")},
        {1002,TEXT("Character minimum level|Minimum")},{1003,TEXT("Character maximum level|Maximum")},{1004,TEXT("Character base skill|Skill ID|Minimum|Maximum")},
        {2000,TEXT("Buffed median damage|Minimum")},{2001,TEXT("Buffed missile damage|Minimum")},{2003,TEXT("Buffed integer|Minimum|Property ID")},
        {2005,TEXT("Buffed decimal|Minimum|Property ID")},{2006,TEXT("Buffed tinkered damage|Minimum")},{2007,TEXT("Total ratings|Minimum")},
        {2008,TEXT("Tinkered melee target|Damage over time|Melee defense|Attack")},{9999,TEXT("Disable rule|Disabled (true / false)")}};
    return S;
}
void LegacyLootPanel()
{
    if(!LegacyDocument||Str(LegacyDocument,TEXT("format"))!=TEXT("utl")){Page=TEXT("Import");ImportPanel();return;}
    Heading(TEXT("UTL loot editor"),TEXT("Edit VT Classic files without running their rules. All requirement types, rule order, expressions and extra blocks are retained. Saving creates a separate .utl copy; UCM still requires a successful compatibility check before import."));
    auto Rules=LegacyDocument->GetArrayField(TEXT("rules"));
    if(LegacyRule)
    {
        auto Input=[&](const TCHAR* Key,const FString& Caption)
        {
            Add(Text(Caption,15,Muted));Add(SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(FText::FromString(Str(LegacyRule,Key))).OnTextChanged_Lambda([this,Key](const FText& V){LegacyRule->SetStringField(Key,V.ToString());}));
        };
        Input(TEXT("label"),TEXT("Rule name"));Input(TEXT("expression"),TEXT("VT expression (optional; preserved, not executed by UCM)"));
        const TArray<FString> Actions={TEXT("No loot"),TEXT("Keep"),TEXT("Salvage"),TEXT("Sell"),TEXT("Read"),TEXT("User 1"),TEXT("User 2"),TEXT("User 3"),TEXT("User 4"),TEXT("User 5"),TEXT("Keep up to")};
        auto Menu=SNew(SVerticalBox);for(int I=0;I<Actions.Num();++I)Menu->AddSlot().AutoHeight()[Button(Actions[I],[this,I](){LegacyRule->SetNumberField(TEXT("action_id"),I);FSlateApplication::Get().DismissAllMenus();Rebuild();})];
        const int Action=int(Num(LegacyRule,TEXT("action_id")));
        Add(SNew(SComboButton).ButtonStyle(&Style()).ButtonContent()[Text(Actions.IsValidIndex(Action)?Actions[Action]:FString::Printf(TEXT("Custom action %d"),Action))].MenuContent()[Menu]);
        if(Action==10)Add(SNew(SSpinBox<int32>).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).MinValue(1).MaxValue(100000).Value(int(Num(LegacyRule,TEXT("amount"),1))).OnValueChanged_Lambda([this](int V){LegacyRule->SetNumberField(TEXT("amount"),V);}));
        Add(Text(TEXT("Priority (stored by VT; file order controls first-match evaluation)"),14,Muted));
        Add(SNew(SSpinBox<int32>).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).MinValue(-100000).MaxValue(100000).Value(int(Num(LegacyRule,TEXT("priority")))).OnValueChanged_Lambda([this](int V){LegacyRule->SetNumberField(TEXT("priority"),V);}));
        auto Reqs=LegacyRule->GetArrayField(TEXT("requirements"));
        for(int I=0;I<Reqs.Num();++I)
        {
            auto Req=Reqs[I]->AsObject();const int Type=int(Num(Req,TEXT("type")));const FString* Schema=LegacySchemas().Find(Type);
            TArray<FString> Fields,Values;if(Schema)Schema->ParseIntoArray(Fields,TEXT("|"));
            Str(Req,TEXT("body")).Replace(TEXT("\r"),TEXT("")).ParseIntoArray(Values,TEXT("\n"),false);if(Values.Num()&&Values.Last().IsEmpty())Values.Pop();
            Heading(Schema?Fields[0]:FString::Printf(TEXT("Unknown requirement %d"),Type),TEXT(""));
            if(Schema&&Values.Num()==Fields.Num()-1)
            {
                for(int J=0;J<Values.Num();++J)
                {
                    Add(Text(Fields[J+1],14,Muted));
                    Add(SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(FText::FromString(Values[J])).OnTextChanged_Lambda([Req,J](const FText& V)
                    {
                        TArray<FString> Updated;Str(Req,TEXT("body")).Replace(TEXT("\r"),TEXT("")).ParseIntoArray(Updated,TEXT("\n"),false);
                        if(Updated.Num()&&Updated.Last().IsEmpty())Updated.Pop();if(Updated.IsValidIndex(J)){Updated[J]=V.ToString();Req->SetStringField(TEXT("body"),FString::Join(Updated,TEXT("\r\n"))+TEXT("\r\n"));}
                    }));
                }
            }
            else Add(SNew(SBox).HeightOverride(100)[SNew(SMultiLineEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(FText::FromString(Str(Req,TEXT("body")))).OnTextChanged_Lambda([Req](const FText& V){Req->SetStringField(TEXT("body"),V.ToString());})]);
            Add(Button(TEXT("Remove requirement"),[this,I,Reqs]()mutable{Reqs.RemoveAt(I);LegacyRule->SetArrayField(TEXT("requirements"),Reqs);Rebuild();}));
        }
        auto Requirements=SNew(SVerticalBox);TArray<int32> Types;LegacySchemas().GetKeys(Types);Types.Sort();
        for(int Type:Types){TArray<FString> Fields;LegacySchemas()[Type].ParseIntoArray(Fields,TEXT("|"));Requirements->AddSlot().AutoHeight()[Button(Fields[0],[this,Type,Fields]()
        {
            auto Req=MakeShared<FJsonObject>();Req->SetNumberField(TEXT("type"),Type);FString RequirementBody;
            for(int I=1;I<Fields.Num();++I)RequirementBody+=(Type==9999?TEXT("true"):Fields[I].Contains(TEXT("attern"))?TEXT(""):TEXT("0"))+FString(TEXT("\r\n"));
            Req->SetStringField(TEXT("body"),RequirementBody);auto List=LegacyRule->GetArrayField(TEXT("requirements"));List.Add(MakeShared<FJsonValueObject>(Req));LegacyRule->SetArrayField(TEXT("requirements"),List);FSlateApplication::Get().DismissAllMenus();Rebuild();
        })];}
        Add(SNew(SComboButton).ButtonStyle(&Style()).ButtonContent()[Text(TEXT("+ Add requirement"))].MenuContent()[SNew(SBox).MaxDesiredHeight(320)[SNew(SScrollBox)+SScrollBox::Slot()[Requirements]]]);
        Add(Button(TEXT("Save rule draft"),[this,Rules]()mutable{if(LegacyRuleIndex==INDEX_NONE)Rules.Add(MakeShared<FJsonValueObject>(LegacyRule));else if(Rules.IsValidIndex(LegacyRuleIndex))Rules[LegacyRuleIndex]=MakeShared<FJsonValueObject>(LegacyRule);LegacyDocument->SetArrayField(TEXT("rules"),Rules);LegacyRule.Reset();Rebuild();}));
        Add(Button(TEXT("Cancel rule changes"),[this](){LegacyRule.Reset();Rebuild();}));return;
    }
    Add(SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).HintText(FText::FromString(TEXT("Filter rule names"))).Text(FText::FromString(LegacyFilter)).OnTextCommitted_Lambda([this](const FText& V,ETextCommit::Type){LegacyFilter=V.ToString();LegacyPage=0;Rebuild();}));
    Add(Button(TEXT("+ New rule"),[this](){LegacyRule=MakeShared<FJsonObject>();LegacyRule->SetStringField(TEXT("label"),TEXT("New rule"));LegacyRule->SetNumberField(TEXT("action_id"),1);LegacyRule->SetNumberField(TEXT("amount"),1);LegacyRule->SetArrayField(TEXT("requirements"),{});LegacyRuleIndex=INDEX_NONE;Rebuild();}));
    TArray<int32> Visible;for(int I=0;I<Rules.Num();++I)if(LegacyFilter.IsEmpty()||Str(Rules[I]->AsObject(),TEXT("label")).Contains(LegacyFilter))Visible.Add(I);
    LegacyPage=FMath::Clamp(LegacyPage,0,FMath::Max(0,(Visible.Num()-1)/25));
    Add(Text(FString::Printf(TEXT("%d rules • %d matching • page %d"),Rules.Num(),Visible.Num(),LegacyPage+1),15,Muted));
    auto Paging=SNew(SHorizontalBox);Paging->AddSlot().FillWidth(1)[Button(TEXT("Previous"),[this](){LegacyPage=FMath::Max(0,LegacyPage-1);Rebuild();})];Paging->AddSlot().FillWidth(1)[Button(TEXT("Next"),[this](){++LegacyPage;Rebuild();})];Add(Paging);
    for(int Row=LegacyPage*25;Row<FMath::Min(Visible.Num(),(LegacyPage+1)*25);++Row)
    {
        const int I=Visible[Row];auto R=Rules[I]->AsObject();auto RuleCard=SNew(SVerticalBox);RuleCard->AddSlot().AutoHeight()[Text(FString::Printf(TEXT("%d. %s"),I+1,*Str(R,TEXT("label"))),16)];
        auto Buttons=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5,5));
        Buttons->AddSlot()[Button(TEXT("Edit"),[this,R,I](){FString Json;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Json));FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),LegacyRule);LegacyRuleIndex=I;Rebuild();})];
        for(int Direction:{-1,1})if(Rules.IsValidIndex(I+Direction))Buttons->AddSlot()[Button(Direction<0?TEXT("Up"):TEXT("Down"),[this,Rules,I,Direction]()mutable{Rules.Swap(I,I+Direction);LegacyDocument->SetArrayField(TEXT("rules"),Rules);Rebuild();})];
        Buttons->AddSlot()[Button(TEXT("Remove"),[this,Rules,I]()mutable{Rules.RemoveAt(I);LegacyDocument->SetArrayField(TEXT("rules"),Rules);Rebuild();})];RuleCard->AddSlot().AutoHeight()[Buttons];Add(Tile(RuleCard));
    }
    Add(SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(FText::FromString(LegacyName)).HintText(FText::FromString(TEXT("Output filename"))).OnTextChanged_Lambda([this](const FText& V){LegacyName=V.ToString();}));
    Add(Button(TEXT("Save UTL copy"),[this](){Host->SaveLegacyLoot(LegacyName,LegacyDocument);}));
    Add(Button(TEXT("Check compatibility of saved copy"),[this](){if(Host->SaveLegacyLoot(LegacyName,LegacyDocument)){Page=TEXT("Import");PreviewLegacy(Host->UserDirectory()/TEXT("LegacyLootProfiles")/(LegacyName+TEXT(".utl")));}}));
    Add(Text(TEXT("Extra blocks (including salvage combination ranges) remain intact in saved copies."),14,Muted));
}
