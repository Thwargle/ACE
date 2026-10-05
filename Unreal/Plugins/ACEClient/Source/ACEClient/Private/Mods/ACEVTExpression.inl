// Small, bounded expression compiler. Its output is interpreted, never eval'd.
struct VTExpression
{
    const FString& Text;int At=0,Nodes=0;FString Error;
    void Space(){while(At<Text.Len()&&FChar::IsWhitespace(Text[At]))++At;}
    bool Take(const TCHAR* Token){Space();const int N=FCString::Strlen(Token);if(Text.Mid(At,N)==Token){At+=N;return true;}return false;}
    J Node(const FString& Op){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("op"),Op);if(++Nodes>512)Error=TEXT("Expression exceeds 512 nodes");return O;}
    J Atom(int Depth)
    {
        Space();if(Depth>20||At>=Text.Len()){Error=TEXT("Invalid or excessively nested expression");return Node(TEXT("invalid"));}
        if(Take(TEXT("("))){auto O=Expr(1,Depth+1);if(!Take(TEXT(")")))Error=TEXT("Missing )");return O;}
        if(Take(TEXT("!"))){auto O=Node(TEXT("not"));O->SetObjectField(TEXT("left"),Atom(Depth+1));return O;}
        if(Take(TEXT("-"))){auto O=Node(TEXT("negative"));O->SetObjectField(TEXT("left"),Atom(Depth+1));return O;}
        if(Take(TEXT("`")))
        {
            FString Literal;bool Closed=false;
            while(At<Text.Len())
            {
                if(Text[At]=='`'){++At;if(At<Text.Len()&&Text[At]=='`'){Literal+='`';++At;continue;}Closed=true;break;}
                Literal+=Text[At++];
            }
            if(!Closed||Literal.IsEmpty())Error=TEXT("Invalid quoted expression string");
            auto O=Node(TEXT("literal"));O->SetStringField(TEXT("value"),Literal);return O;
        }
        FString Word;bool Escaped=false;
        while(At<Text.Len())
        {
            const TCHAR C=Text[At];
            if(C=='\\'){Escaped=true;++At;if(At>=Text.Len()){Error=TEXT("Incomplete escape");break;}Word+=Text[At++];continue;}
            if(FString(TEXT("[](),+*/%<>=!&|-#;^")).Contains(FString::Chr(C)))break;
            Word+=C;++At;
        }
        Word.TrimStartAndEndInline();
        if(Word.IsEmpty()){Error=TEXT("Expected expression value");return Node(TEXT("invalid"));}
        if(Take(TEXT("[")))
        {
            auto O=Node(TEXT("call"));const FString Name=Word.ToLower();O->SetStringField(TEXT("name"),Name);A Args;
            if(!Take(TEXT("]")))do{Args.Add(Obj(Expr(1,Depth+1)));if(!Error.IsEmpty())break;if(Take(TEXT("]")))break;if(!Take(TEXT(","))){Error=TEXT("Expected , or ]");break;}}while(Args.Num()<16);
            O->SetArrayField(TEXT("args"),Args);
            const TMap<FString,int> Arity={{TEXT("getobjectinternaltype"),1},{TEXT("istrue"),1},{TEXT("iif"),3},{TEXT("randint"),2},{TEXT("strlen"),1},{TEXT("cnumber"),1},{TEXT("floor"),1},{TEXT("ceiling"),1},{TEXT("round"),1},{TEXT("abs"),1},{TEXT("touchvar"),1},{TEXT("getcharquadprop"),1},{TEXT("getchardoubleprop"),1},{TEXT("getcharboolprop"),1},{TEXT("getcharintprop"),1},{TEXT("getcharvital_base"),1},{TEXT("getcharvital_current"),1},{TEXT("getcharvital_buffedmax"),1},{TEXT("getcharskill_traininglevel"),1},{TEXT("getcharskill_base"),1},{TEXT("getcharskill_buffed"),1},{TEXT("getplayerlandcell"),0},{TEXT("coordinategetns"),1},{TEXT("coordinategetwe"),1},{TEXT("coordinategetz"),1},{TEXT("coordinateparse"),1},{TEXT("coordinatedistanceflat"),2},{TEXT("wobjectgetobjectclass"),1},{TEXT("wobjectgettemplatetype"),1},{TEXT("wobjectfindininventorybytemplatetype"),1},{TEXT("wobjectgetselection"),0},{TEXT("wobjectgetplayer"),0},{TEXT("actiontryselect"),1},{TEXT("actiontryequipanywand"),0},{TEXT("actiontrycastbyid"),1},{TEXT("actiontrycastbyidontarget"),2},{TEXT("getcancastspell_hunt"),1},{TEXT("getcancastspell_buff"),1},{TEXT("actiontrygiveitem"),2},{TEXT("getfellowids"),0},{TEXT("getfellownames"),0},{TEXT("getfellowshipcanrecruit"),0},{TEXT("getfellowshipcount"),0},{TEXT("getfellowshipisfull"),0},{TEXT("getfellowshipisleader"),0},{TEXT("getfellowshipisopen"),0},{TEXT("getfellowshipleaderid"),0},{TEXT("getfellowshiplocked"),0},{TEXT("getfellowshipname"),0},{TEXT("getfellowshipstatus"),0},{TEXT("getworldname"),0},{TEXT("listadd"),2},{TEXT("listclear"),1},{TEXT("listcontains"),2},{TEXT("listcopy"),1},{TEXT("listcount"),1},{TEXT("listcreate"),-1},{TEXT("listgetitem"),2},{TEXT("listindexof"),2},{TEXT("listinsert"),3},{TEXT("listlastindexof"),2},{TEXT("listremove"),2},{TEXT("listremoveat"),2},{TEXT("listreverse"),1},{TEXT("wobjectfindininventorybynamerx"),1},{TEXT("getisspellknown"),1},{TEXT("wobjectfindnearestbynameandobjectclass"),2},{TEXT("getvar"),1},{TEXT("setvar"),2},{TEXT("testvar"),1},{TEXT("clearvar"),1},{TEXT("clearallvars"),0},{TEXT("stopwatchcreate"),0},{TEXT("stopwatchstart"),1},{TEXT("stopwatchstop"),1},{TEXT("stopwatchelapsedseconds"),1},{TEXT("isfalse"),1},{TEXT("cstr"),1},{TEXT("cstrf"),2},{TEXT("wobjectfindnearestbyobjectclass"),1},{TEXT("wobjectfindnearestmonster"),0},{TEXT("wobjectfindnearestdoor"),0},{TEXT("wobjectfindininventorybyname"),1},{TEXT("wobjectgetisdooropen"),1},{TEXT("wobjectgetname"),1},{TEXT("wobjectgetphysicscoordinates"),1},{TEXT("getplayercoordinates"),0},{TEXT("coordinatedistancewithz"),2},{TEXT("coordinatetostring"),1},{TEXT("getcharstringprop"),1},{TEXT("actiontryuseitem"),1},{TEXT("actiontryapplyitem"),2}};
            if(!Arity.Contains(Name))Error=TEXT("Expression function needs an adapter: ")+Name;
            else if(Arity[Name]>=0&&Args.Num()!=Arity[Name])Error=TEXT("Invalid argument count: ")+Name;
            else if(Name==TEXT("wobjectfindnearestbynameandobjectclass")||Name==TEXT("wobjectfindininventorybynamerx"))
            {
                const auto Pattern=Args[Name==TEXT("wobjectfindininventorybynamerx")?0:1]->AsObject();FString Op,Value;
                if(Pattern->TryGetStringField(TEXT("op"),Op)&&Op==TEXT("literal")&&Pattern->TryGetStringField(TEXT("value"),Value)&&!ACEVTProfile::IsSupportedPattern(Value))Error=TEXT("Unsupported object-name regex");
            }
            return O;
        }
        auto O=Node(TEXT("literal"));double Num=0;
        if(!Escaped&&LexTryParseString(Num,*Word)&&FMath::IsFinite(Num))O->SetNumberField(TEXT("value"),Num);
        else if(!Escaped&&(Word==TEXT("true")||Word==TEXT("false")))O->SetBoolField(TEXT("value"),Word==TEXT("true"));
        else O->SetStringField(TEXT("value"),Word);return O;
    }
    J Expr(int Min,int Depth)
    {
        auto Left=Atom(Depth);while(Error.IsEmpty())
        {
            Space();FString Op;int Prec=0;
            for(const auto& Pair:TArray<TPair<FString,int>>{{TEXT(";"),1},{TEXT("||"),2},{TEXT("&&"),2},{TEXT("^"),2},{TEXT("=="),3},{TEXT("!="),3},{TEXT("<="),3},{TEXT(">="),3},{TEXT("<"),3},{TEXT(">"),3},{TEXT("#"),4},{TEXT("+"),5},{TEXT("-"),5},{TEXT("*"),6},{TEXT("/"),6},{TEXT("%"),6}})
                if(Text.Mid(At,Pair.Key.Len())==Pair.Key){Op=Pair.Key;Prec=Pair.Value;break;}
            if(Prec<Min||Op.IsEmpty())break;At+=Op.Len();auto O=Node(Op);O->SetObjectField(TEXT("left"),Left);O->SetObjectField(TEXT("right"),Expr(Prec+1,Depth+1));Left=O;
        }
        return Left;
    }
};
