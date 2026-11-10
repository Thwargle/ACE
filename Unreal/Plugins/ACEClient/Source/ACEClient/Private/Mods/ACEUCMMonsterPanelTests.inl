// Part of ACE.Plugins.ImportAndSettingsUI, using its isolated profile storage.
{
    TestTrue(TEXT("Create isolated Monsters section"),Host->NewUCMSection(TEXT("Monsters"),TEXT("Matrix")));
    Panel->Page=TEXT("Monsters");Panel->Rebuild();
    auto FindMonsterButton=[&](const FString& Name,const FString& Key)
    {
        TSharedPtr<SButton> Found;const FName Tag(*(TEXT("UCMMonster.")+Name+TEXT(".")+Key));
        TFunction<void(TSharedRef<SWidget>)> Visit=[&](TSharedRef<SWidget> W){
            if(W->GetType()==TEXT("SACEPluginButton")&&W->GetTag()==Tag)Found=StaticCastSharedRef<SButton>(W);
            auto* C=W->GetChildren();for(int32 I=0;I<C->Num();++I)Visit(C->GetChildAt(I));};Visit(Panel);return Found;
    };
    auto Click=[&](const FString& Name,const FString& Key){auto B=FindMonsterButton(Name,Key);
        if(TestTrue(*FString::Printf(TEXT("Quick control exists: %s / %s"),*Name,*Key),B.IsValid()))B->SimulateClick();};
    auto FindMonsterName=[&](const FString& Name)
    {
        TSharedPtr<SWidget> Found;const FName Tag(*(TEXT("UCMMonster.Name.")+Name));
        TFunction<void(TSharedRef<SWidget>)> Visit=[&](TSharedRef<SWidget> W){if(W->GetTag()==Tag)Found=W;
            auto* C=W->GetChildren();for(int32 I=0;I<C->Num();++I)Visit(C->GetChildAt(I));};Visit(Panel);return Found;
    };
    TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>,FName)> FindMobWidget;
    FindMobWidget=[&](TSharedRef<SWidget> W,FName Tag)->TSharedPtr<SWidget>{if(W->GetTag()==Tag)return W;
        auto* C=W->GetChildren();for(int32 I=0;I<C->Num();++I)if(auto Found=FindMobWidget(C->GetChildAt(I),Tag))return Found;return nullptr;};
    const auto ProfileLabel=[&](){auto W=FindMobWidget(Panel,TEXT("UCMProfileLabel.Monsters"));return W?StaticCastSharedPtr<STextBlock>(W)->GetText().ToString():FString();};
    TestEqual(TEXT("Custom mob profile displays its own name without a prefix"),ProfileLabel(),FString(TEXT("Matrix")));
    TestEqual(TEXT("Legacy section has the new visible name"),ACEUCMProfiles::DisplaySection(TEXT("Monsters")),FString(TEXT("Mobs")));
    TestEqual(TEXT("Opening the matrix does not write a default rule"),Plugin->Profile->GetArrayField(TEXT("monsters")).Num(),0);
    const TArray<FString> DebuffKeys={TEXT("debuff_fester"),TEXT("debuff_broadside"),TEXT("debuff_gravity"),TEXT("debuff_weakening"),
        TEXT("debuff_festering"),TEXT("debuff_corruption"),TEXT("debuff_destructive"),TEXT("debuff_corrosion")};
    for(const auto& Key:DebuffKeys)TestTrue(*FString(TEXT("Mode available on first opening: ")+Key),FindMonsterButton(TEXT("<DEFAULT>"),Key).IsValid());
    // Legacy Character values remain readable without another editor competing
    // with the default row. Editing a row must not change activity switches.
    Plugin->Profile->SetBoolField(TEXT("debuff_fester"),true);
    Plugin->Profile->SetStringField(TEXT("combat"),TEXT("off"));Plugin->Profile->SetBoolField(TEXT("summon_pets"),false);
    TestTrue(TEXT("Default reads existing Character fallback"),Panel->MonsterFlag(Panel->MonsterDefaults(),TEXT("debuff_fester")));
    auto FirstButton=FindMonsterButton(TEXT("<DEFAULT>"),TEXT("bolt"));Panel->SlatePrepass();
    const auto FirstChild=Panel->Body->GetChildren()->GetChildAt(0);
    Click(TEXT("<DEFAULT>"),TEXT("bolt"));Panel->SlatePrepass();
    TestTrue(TEXT("Saving a chart setting preserves button instance"),FindMonsterButton(TEXT("<DEFAULT>"),TEXT("bolt"))==FirstButton);
    TestTrue(TEXT("Saving settings does not rebuild tab"),Panel->Body->GetChildren()->GetChildAt(0)==FirstChild);
    Click(TEXT("<DEFAULT>"),TEXT("streak"));
    TestEqual(TEXT("First edit creates one saved default"),Plugin->Profile->GetArrayField(TEXT("monsters")).Num(),1);
    auto Default=Panel->MonsterDefaults();TestFalse(TEXT("Default ordinary attacks saved off"),Default->GetBoolField(TEXT("bolt")));
    TestFalse(TEXT("Default streak attacks saved off"),Default->GetBoolField(TEXT("streak")));
    Panel->MonsterName->SetText(FText::FromString(TEXT("Sir Bellas")));
    auto AddByName=FindButton(Panel,TEXT("Add"));if(TestTrue(TEXT("Add by name action exists"),AddByName.IsValid()))AddByName->SimulateClick();
    auto MatrixRule=Plugin->Profile->GetArrayField(TEXT("monsters")).Last()->AsObject();
    TestFalse(TEXT("New monster inherits disabled ordinary attacks"),Panel->MonsterFlag(MatrixRule,TEXT("bolt")));
    TestFalse(TEXT("New monster is a compact row, not expanded details"),Panel->ExpandedMonster!=INDEX_NONE);
    Click(TEXT("Sir Bellas"),TEXT("bolt"));Click(TEXT("Sir Bellas"),TEXT("debuff_imperil"));
    TMap<FString,bool> SavedDebuffs;
    for(const auto& Key:DebuffKeys){const bool Expected=!Panel->MonsterFlag(MatrixRule,Key);Click(TEXT("Sir Bellas"),Key);SavedDebuffs.Add(Key,Expected);}
    TestTrue(TEXT("Click overrides only the chosen monster"),Panel->MonsterFlag(MatrixRule,TEXT("bolt"))&&!Panel->MonsterFlag(Default,TEXT("bolt")));
    Click(TEXT("<DEFAULT>"),TEXT("debuff_vuln"));
    TestFalse(TEXT("Default edits leave existing mob switches unchanged"),Panel->MonsterFlag(MatrixRule,TEXT("debuff_vuln")));
    TestEqual(TEXT("New mob priority starts at zero"),Num(MatrixRule,TEXT("priority")),0.);
    TestTrue(TEXT("Each mob has a priority dropdown"),FindMobWidget(Panel,TEXT("UCMMonster.Sir Bellas.priority")).IsValid());
    TestFalse(TEXT("Pet configuration is kept in Details"),FindMobWidget(Panel,TEXT("UCMMonster.Sir Bellas.pet_damage_type")).IsValid());
    Panel->AddMonster(TEXT("sir bellas"));TestEqual(TEXT("Adding the same monster does not duplicate it"),Plugin->Profile->GetArrayField(TEXT("monsters")).Num(),2);
    Panel->ExpandedMonster=INDEX_NONE;Panel->Rebuild();
    TestFalse(TEXT("No duplicate pet-only shortcut"),FindButton(Panel,TEXT("Summon without attacking")).IsValid());
    TestFalse(TEXT("Editing chart leaves summoning paused"),Plugin->Profile->GetBoolField(TEXT("summon_pets")));
    TestEqual(TEXT("Editing chart leaves combat paused"),Plugin->Profile->GetStringField(TEXT("combat")),FString(TEXT("off")));
    TestTrue(TEXT("Switch to another Monsters section"),Host->SelectUCMSection(TEXT("Monsters"),TEXT("Default")));
    Panel->Rebuild();TestEqual(TEXT("Default profile is displayed simply as Default"),ProfileLabel(),FString(TEXT("Default")));
    TestTrue(TEXT("Reload saved Monsters section"),Host->SelectUCMSection(TEXT("Monsters"),TEXT("Matrix")));
    Panel->Rebuild();MatrixRule=Plugin->Profile->GetArrayField(TEXT("monsters"))[1]->AsObject();
    TestTrue(TEXT("MatrixRule switches survive saving and reloading"),Panel->MonsterFlag(MatrixRule,TEXT("bolt"))&&Panel->MonsterFlag(MatrixRule,TEXT("debuff_imperil")));
    for(const auto& Entry:SavedDebuffs)TestEqual(*FString(TEXT("Debuff switch survives reloading: ")+Entry.Key),Panel->MonsterFlag(MatrixRule,Entry.Key),Entry.Value);
    TestFalse(TEXT("Default switches survive reloading"),Panel->MonsterFlag(Panel->MonsterDefaults(),TEXT("bolt")));
    Panel->MonsterFilter=TEXT("No match");Panel->Rebuild();
    TestTrue(TEXT("Default row remains editable under a search filter"),FindMonsterButton(TEXT("<DEFAULT>"),TEXT("bolt")).IsValid());
    TestFalse(TEXT("Filter hides unrelated monsters"),FindMonsterButton(TEXT("Sir Bellas"),TEXT("bolt")).IsValid());
    Panel->MonsterFilter.Empty();Panel->Rebuild();
    Click(TEXT("<DEFAULT>"),TEXT("debuff_fester"));
    TestFalse(TEXT("Chart can override a legacy fallback"),Panel->MonsterFlag(Panel->MonsterDefaults(),TEXT("debuff_fester")));
    Click(TEXT("Sir Bellas"),TEXT("debuff_corruption"));
    TestEqual(TEXT("Corruption can be toggled after reloading"),Panel->MonsterFlag(MatrixRule,TEXT("debuff_corruption")),!SavedDebuffs[TEXT("debuff_corruption")]);
    Plugin->Running=true;Click(TEXT("Sir Bellas"),TEXT("ring"));
    TestTrue(TEXT("Quick edits keep a running UCM active"),Plugin->Running);Plugin->Running=false;
    if(FApp::CanEverRender())
    {
        TestTrue(TEXT("Matrix retail artwork loads"),GI->GetSubsystem<UACEDatSubsystem>()->LoadDatDirectory(TEXT("C:/Turbine/Asheron's Call")));
        FWidgetRenderer Renderer(false,true);
        const float OriginalPluginScale=ACERuntimeOptions::Get(TEXT("PluginTextScale"));
        ON_SCOPE_EXIT{ACERuntimeOptions::Set(TEXT("PluginTextScale"),OriginalPluginScale);};
        for(float FontScale:{1.f,2.f})for(int32 Width:{450,1100,int32(1800*FontScale)})
        {
            ACERuntimeOptions::Set(TEXT("PluginTextScale"),FontScale);
            Panel->Rebuild();Panel->BodyScroll->ScrollToEnd();
            FVector2D Size(Width,760);auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
            auto& App=FSlateApplication::Get();auto Window=SNew(SVirtualWindow).Size(Size);Window->SetContent(Panel);App.RegisterVirtualWindow(Window);
            ON_SCOPE_EXIT{App.UnregisterVirtualWindow(Window);Target->ReleaseResource();};
            auto Paint=[&](){++GFrameCounter;Renderer.DrawWindow(Target,Window->GetHittestGrid(),Window,1.f,Size,.016f);FlushRenderingCommands();};
            // Scroll both axes as a user would; clipped descendants have no
            // paint geometry until they actually enter the viewport.
            Panel->MonsterTableScroll->ScrollDescendantIntoView(FindMonsterButton(TEXT("Sir Bellas"),TEXT("bolt")),false,EDescendantScrollDestination::TopOrLeft);
            for(int32 Pass=0;Pass<3;++Pass)Paint();
            // Wrapped header/help text settles after its allotted width is
            // known; request the final scroll using that completed layout.
            Panel->BodyScroll->ScrollToEnd();
            for(int32 Pass=0;Pass<3;++Pass)Paint();
            TSharedPtr<SScrollBar> HorizontalScrollbar;
            TFunction<void(TSharedRef<SWidget>)> FindScrollbar=[&](TSharedRef<SWidget> W){
                if(W->GetType()==TEXT("SScrollBar")&&StaticCastSharedRef<SScrollBar>(W)->GetOrientation()==Orient_Horizontal)HorizontalScrollbar=StaticCastSharedRef<SScrollBar>(W);
                auto* C=W->GetChildren();for(int32 I=0;I<C->Num();++I)FindScrollbar(C->GetChildAt(I));};FindScrollbar(Panel->MonsterTableScroll.ToSharedRef());
            if(TestTrue(TEXT("Matrix has an automatic horizontal scrollbar"),HorizontalScrollbar.IsValid()))
            {
                TestEqual(TEXT("Scrollbar is visible only when columns overflow"),HorizontalScrollbar->GetVisibility().IsVisible(),Width<=1100);
                TestEqual(TEXT("Wide matrix fits all columns"),HorizontalScrollbar->IsNeeded(),Width<=1100);
            }
            auto A=FindMonsterButton(TEXT("Sir Bellas"),TEXT("bolt"));auto R=FindMonsterButton(TEXT("Sir Bellas"),TEXT("ring"));
            auto Name=FindMonsterName(TEXT("Sir Bellas"));TestTrue(TEXT("Monster row has a fixed name cell"),Name.IsValid());
            if(A&&R){const auto& AG=A->GetCachedGeometry();const auto& RG=R->GetCachedGeometry();
                TestTrue(TEXT("Quick switches have full clickable targets"),AG.GetLocalSize().X>=40&&AG.GetLocalSize().Y>=30);
                const auto BodyBounds=Panel->BodyScroll->GetCachedGeometry().GetLayoutBoundingRect();
                TestTrue(TEXT("Scrolled switch is inside the visible body"),AG.GetAbsolutePosition().Y>=BodyBounds.Top&&AG.GetAbsolutePosition().Y+AG.GetAbsoluteSize().Y<=BodyBounds.Bottom+1);
                TestTrue(TEXT("Adjacent switches do not overlap"),AG.GetAbsolutePosition().X+AG.GetAbsoluteSize().X<=RG.GetAbsolutePosition().X);
                if(Name){const auto NG=Name->GetCachedGeometry();
                    TestTrue(TEXT("Name stays aligned with its attack switches"),FMath::IsNearlyEqual(NG.GetAbsolutePosition().Y+NG.GetAbsoluteSize().Y*.5f,AG.GetAbsolutePosition().Y+AG.GetAbsoluteSize().Y*.5f,1.));
                    TestTrue(TEXT("Name remains fully inside the visible body"),NG.GetAbsolutePosition().X>=BodyBounds.Left&&NG.GetAbsolutePosition().X+NG.GetAbsoluteSize().X<=BodyBounds.Right);
                    TestTrue(TEXT("Name does not overlap the scrolling controls"),NG.GetAbsolutePosition().X+NG.GetAbsoluteSize().X<=Panel->MonsterTableScroll->GetCachedGeometry().GetAbsolutePosition().X+1);}}
            const float NameX=Name?Name->GetCachedGeometry().GetAbsolutePosition().X:0;
            TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Width,760,Pixels,PNG);
            FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("UCMMonsterMatrix-%d-%.0f.png"),Width,FontScale)));
            TMap<FName,TSharedRef<SWidget>> Headers;
            TFunction<void(TSharedRef<SWidget>)> FindHeaders=[&](TSharedRef<SWidget> W){if(W->GetTag().ToString().StartsWith(TEXT("UCMMonster.Header.")))Headers.Add(W->GetTag(),W);
                auto* C=W->GetChildren();for(int32 I=0;I<C->Num();++I)FindHeaders(C->GetChildAt(I));};FindHeaders(Panel);
            TestEqual(TEXT("All columns are always available with hover help"),Headers.Num(),17);
            for(const auto& Entry:Headers)
            {
                const auto W=Entry.Value;const FString Key=Entry.Key.ToString().RightChop(18);
                Panel->MonsterTableScroll->ScrollDescendantIntoView(W,false,EDescendantScrollDestination::IntoView);
                Panel->BodyScroll->ScrollDescendantIntoView(W,false,EDescendantScrollDestination::IntoView);
                for(int32 Pass=0;Pass<3;++Pass)Paint();
                if(Name)TestTrue(TEXT("Monster name remains fixed while scrolling every mode column"),FMath::IsNearlyEqual(NameX,Name->GetCachedGeometry().GetAbsolutePosition().X,1.));
                const FVector2D At=W->GetCachedGeometry().GetAbsolutePosition()+W->GetCachedGeometry().GetAbsoluteSize()*.5f;
                const auto Hit=App.LocateWindowUnderMouse(At,{Window},false,0);
                TestTrue(*FString(TEXT("Header reachable by real hover: ")+Key),Hit.ContainsWidget(&W.Get()));
                auto Tip=W->GetToolTip();TestTrue(TEXT("Header tooltip is nonempty"),Tip.IsValid()&&!Tip->IsEmpty());
                if(auto Switch=FindMonsterButton(TEXT("Sir Bellas"),Key))
                {
                    TestTrue(TEXT("Switch uses the shared retail tooltip"),Switch->GetToolTip().IsValid()&&Switch->GetToolTip()->AsWidget()->GetType()==TEXT("SACEPluginTooltip"));
                }
            }
            if(Width>1100&&HorizontalScrollbar)
            {
                // Resize the same live table after scrolling: no reopen or
                // page rebuild should be needed to show/hide the scrollbar.
                const auto Resize=[&](int32 NewWidth){Size.X=NewWidth;Window->Resize(Size);Target->ResizeTarget(NewWidth,760);for(int32 Pass=0;Pass<4;++Pass)Paint();
                    Panel->BodyScroll->ScrollToEnd();for(int32 Pass=0;Pass<4;++Pass)Paint();};
                Resize(450);Panel->MonsterTableScroll->ScrollToEnd();for(int32 Pass=0;Pass<3;++Pass)Paint();
                TestTrue(TEXT("Shrinking the live window reveals scrollbar"),HorizontalScrollbar->GetVisibility().IsVisible());
                TestTrue(TEXT("Narrow table scrolls to its last columns"),Panel->MonsterTableScroll->GetScrollOffset()>0);
                Resize(Width);
                TestTrue(TEXT("Widening collapses scrollbar without reserving space"),HorizontalScrollbar->GetVisibility()==EVisibility::Collapsed);
                TestTrue(TEXT("Widening shows the start of the table again"),FMath::IsNearlyZero(Panel->MonsterTableScroll->GetViewOffsetFraction()));
                const auto Bounds=Panel->MonsterTableScroll->GetCachedGeometry().GetLayoutBoundingRect();
                for(const auto& Entry:Headers){const auto Cell=Entry.Value->GetCachedGeometry().GetLayoutBoundingRect();
                    TestTrue(*FString(TEXT("Column fits after widening: ")+Entry.Key.ToString()),Cell.Left>=Bounds.Left&&Cell.Right<=Bounds.Right);}
            }
        }
    }
    Click(TEXT("<DEFAULT>"),TEXT("details"));
    TestTrue(TEXT("Default details uses its readable name"),FindMobWidget(Panel,TEXT("Monsters/Default")).IsValid());
    TestFalse(TEXT("Default rule cannot be renamed"),FindMobWidget(Panel,TEXT("UCMMobDetail.Name")).IsValid());
    Click(TEXT("Sir Bellas"),TEXT("details"));
    for(const auto& Mode:Panel->MonsterModes())
    {
        auto W=FindMobWidget(Panel,FName(*(TEXT("UCMMobDetail.Sir Bellas.")+FString(Mode.Key))));
        if(TestTrue(TEXT("Every attack and debuff has a Details switch"),W.IsValid()))
        {
            const bool Before=Panel->MonsterFlag(MatrixRule,Mode.Key);
            StaticCastSharedPtr<SButton>(W)->SimulateClick();
            TestEqual(TEXT("Details updates the same row settings as the table"),Panel->MonsterFlag(MatrixRule,Mode.Key),!Before);
            Click(TEXT("Sir Bellas"),Mode.Key);
            TestEqual(TEXT("Table updates the same row settings as Details"),Panel->MonsterFlag(MatrixRule,Mode.Key),Before);
            auto Reset=FindMobWidget(Panel,FName(*(TEXT("UCMMobDefault.Sir Bellas.")+FString(Mode.Key))));
            TestFalse(TEXT("Independent mob rows have no ambiguous inheritance checkbox"),Reset.IsValid());
            TestTrue(TEXT("Every mode remains explicitly saved"),MatrixRule->HasField(Mode.Key));
        }
    }
    for(const TCHAR* Key:{TEXT("combat"),TEXT("damage_type"),TEXT("pet_damage_type"),TEXT("secondary_vuln"),TEXT("weapon"),TEXT("secondary_equip")})
        TestTrue(TEXT("Details exposes every attack profile selector"),FindMobWidget(Panel,FName(*(FString(TEXT("UCMMobDetail.Sir Bellas."))+Key))).IsValid());
    for(const TCHAR* Key:{TEXT("priority"),TEXT("range")})TestTrue(TEXT("Details exposes targeting controls"),FindMobWidget(Panel,FName(Key)).IsValid());
    auto Exact=FindMobWidget(Panel,TEXT("UCMMobDetail.Exact"));
    if(TestTrue(TEXT("Native mob matching is editable"),Exact.IsValid()))StaticCastSharedPtr<SCheckBox>(Exact)->ToggleCheckedState();
    TestTrue(TEXT("Exact-name matching is saved"),MatrixRule->GetBoolField(TEXT("exact")));
    if(FApp::CanEverRender())
    {
        auto& Slate=FSlateApplication::Get();const auto Focus=Slate.GetUserFocusedWidget(0);FVector2D Size(1100,1000);
        auto Window=SNew(SWindow).ClientSize(Size)[Panel];Slate.AddWindow(Window,false);
        FWidgetRenderer Renderer(false,true);auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,true);
        const float OriginalScale=ACERuntimeOptions::Get(TEXT("PluginTextScale"));
        ON_SCOPE_EXIT{ACERuntimeOptions::Set(TEXT("PluginTextScale"),OriginalScale);Slate.DismissAllMenus();if(Focus)Slate.SetUserFocus(0,Focus);Window->RequestDestroyWindow();Target->ReleaseResource();};
        auto Paint=[&](){++GFrameCounter;Renderer.DrawWindow(Target,Window->GetHittestGrid(),Window,1.f,Size,.016f);FlushRenderingCommands();};
        auto Choose=[&](const TCHAR* Key,const TCHAR* Caption)
        {
            auto W=FindMobWidget(Panel,FName(*(FString(Key==FString(TEXT("priority"))?TEXT("UCMMonster.Sir Bellas."):TEXT("UCMMobDetail.Sir Bellas."))+Key)));if(!TestTrue(TEXT("Mob selector exists"),W.IsValid()))return;
            Panel->BodyScroll->ScrollDescendantIntoView(W,false,EDescendantScrollDestination::IntoView);for(int Pass=0;Pass<4;++Pass)Paint();
            auto Combo=StaticCastSharedPtr<SComboButton>(W);Combo->SetIsOpen(true);Paint();auto Menu=Combo->GetMenuWindow();
            if(!TestTrue(TEXT("Details selector opens its actual menu"),Menu.IsValid()))return;
            auto Entry=FindMobWidget(Menu.ToSharedRef(),FName(*(FString(TEXT("UCMMenu."))+Caption)));
            if(TestTrue(TEXT("Details menu has the requested option"),Entry.IsValid()))StaticCastSharedPtr<SButton>(Entry)->SimulateClick();
            Combo->SetIsOpen(false);Paint();
        };
        ACERuntimeOptions::Set(TEXT("PluginTextScale"),1);Panel->Rebuild();for(int Pass=0;Pass<4;++Pass)Paint();
        Choose(TEXT("combat"),TEXT("No attacks"));Choose(TEXT("damage_type"),TEXT("Fire"));Choose(TEXT("pet_damage_type"),TEXT("Cold"));
        Choose(TEXT("secondary_vuln"),TEXT("Electric"));Choose(TEXT("weapon"),TEXT("Casting device"));Choose(TEXT("secondary_equip"),TEXT("Shield"));
        Choose(TEXT("priority"),TEXT("4"));
        TestTrue(TEXT("Reload another mob profile"),Host->SelectUCMSection(TEXT("Monsters"),TEXT("Default")));
        TestTrue(TEXT("Reload Details edits"),Host->SelectUCMSection(TEXT("Monsters"),TEXT("Matrix")));
        MatrixRule=Plugin->Profile->GetArrayField(TEXT("monsters"))[1]->AsObject();Panel->ExpandedMonster=1;Panel->Rebuild();
        TestEqual(TEXT("Combat mode survives reload"),Str(MatrixRule,TEXT("combat")),FString(TEXT("off")));
        TestEqual(TEXT("Matrix priority selection survives reload"),Num(MatrixRule,TEXT("priority")),4.);
        TestEqual(TEXT("Damage type survives reload"),Num(MatrixRule,TEXT("damage_type")),16.);
        TestEqual(TEXT("Pet type survives reload"),Num(MatrixRule,TEXT("pet_damage_type")),5.);
        TestEqual(TEXT("Secondary vulnerability survives reload"),Num(MatrixRule,TEXT("secondary_vuln")),64.);
        TestEqual(TEXT("Weapon survives reload"),Num(MatrixRule,TEXT("weapon"),-1),0.);
        TestEqual(TEXT("Offhand survives reload"),Num(MatrixRule,TEXT("secondary_equip")),1.);
        TestTrue(TEXT("Exact-name matching survives reload"),Bool(MatrixRule,TEXT("exact")));
        auto Copy=FindButton(Panel,TEXT("Copy Default settings"));
        if(TestTrue(TEXT("Details can explicitly copy Default"),Copy.IsValid()))Copy->SimulateClick();
        TestEqual(TEXT("Explicit copy restores Default damage"),Num(Panel->EffectiveMonster(MatrixRule),TEXT("damage_type")),Num(Panel->EffectiveMonster(Panel->MonsterDefaults()),TEXT("damage_type")));
        // Use a virtual window for offscreen hit testing; a hidden native
        // window is intentionally excluded from LocateWindowUnderMouse.
        Window->SetContent(SNew(SBox));
        auto Preview=SNew(SVirtualWindow).Size(Size);Preview->SetContent(Panel);Slate.RegisterVirtualWindow(Preview);
        ON_SCOPE_EXIT{Slate.UnregisterVirtualWindow(Preview);};
        auto PreviewPaint=[&](){++GFrameCounter;Renderer.DrawWindow(Target,Preview->GetHittestGrid(),Preview,1.f,Size,.016f);FlushRenderingCommands();};
        for(float Scale:{1.f,2.f})for(int32 Width:{450,1100})
        {
            ACERuntimeOptions::Set(TEXT("PluginTextScale"),Scale);Panel->Rebuild();Size.X=Width;Preview->Resize(Size);Target->ResizeTarget(Width,1000);for(int Pass=0;Pass<4;++Pass)PreviewPaint();
            for(const auto& Mode:Panel->MonsterModes())
            {
                auto W=FindMobWidget(Panel,FName(*(FString(TEXT("UCMMobDetail.Sir Bellas."))+Mode.Key)));if(!W)continue;
                Panel->BodyScroll->ScrollDescendantIntoView(W,false,EDescendantScrollDestination::IntoView);for(int Pass=0;Pass<4;++Pass)PreviewPaint();
                // Wrapped labels above the row acquire their final height
                // after paint. Scroll against that settled layout as well.
                Panel->BodyScroll->ScrollDescendantIntoView(W,false,EDescendantScrollDestination::IntoView);for(int Pass=0;Pass<4;++Pass)PreviewPaint();
                const auto G=W->GetCachedGeometry();const auto Bounds=Panel->BodyScroll->GetCachedGeometry().GetLayoutBoundingRect();
                TestTrue(TEXT("Details switches fit at narrow widths and larger text scales"),G.GetLayoutBoundingRect().Left>=Bounds.Left&&G.GetLayoutBoundingRect().Right<=Bounds.Right);
                const auto Hit=Slate.LocateWindowUnderMouse(G.LocalToAbsolute(G.GetLocalSize()*.5),{Preview},false,0);
                TestTrue(*FString::Printf(TEXT("Details switch remains clickable: %s, width %d, scale %.0f"),Mode.Key,Width,Scale),Hit.ContainsWidget(W.Get()));
                if(FString(Mode.Key)==TEXT("debuff_corrosion"))
                {TArray<FColor> Pixels;Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Width,1000,Pixels,PNG);
                    FFileHelper::SaveArrayToFile(PNG,*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("UCMMobDetails-%d-%.0f.png"),Width,Scale)));}
            }
        }
    }
    Panel->ExpandedMonster=INDEX_NONE;Panel->Rebuild();
    Panel->AddMonster(TEXT("Temporary mob"));Click(TEXT("Temporary mob"),TEXT("remove"));
    TestFalse(TEXT("Remove acts directly from the matrix"),FindMonsterName(TEXT("Temporary mob")).IsValid());
    Panel->AddMonster(TEXT("species==Tusker"));
    auto ExpressionRule=Plugin->Profile->GetArrayField(TEXT("monsters")).Last()->AsObject();
    TestTrue(TEXT("Native Add compiles a mob species expression"),ExpressionRule->HasField(TEXT("expression")));
    TestTrue(TEXT("Expression name appears in the matrix"),FindMonsterName(TEXT("species==Tusker")).IsValid());
    const int32 ExpressionCount=Plugin->Profile->GetArrayField(TEXT("monsters")).Num();
    Panel->AddMonster(TEXT("species=="));
    TestEqual(TEXT("Invalid expression does not add a broken mob rule"),Plugin->Profile->GetArrayField(TEXT("monsters")).Num(),ExpressionCount);
    Panel->Save();Host->SelectUCMSection(TEXT("Monsters"),TEXT("Default"));Host->SelectUCMSection(TEXT("Monsters"),TEXT("Matrix"));
    ExpressionRule=Plugin->Profile->GetArrayField(TEXT("monsters")).Last()->AsObject();
    TestTrue(TEXT("Native expression survives profile save/reload"),ExpressionRule->HasField(TEXT("expression")));
    Panel->RemoveMonster(ExpressionRule);
    auto ManyRules=Plugin->Profile->GetArrayField(TEXT("monsters"));
    for(int32 I=0;I<24;++I){auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("name"),FString::Printf(TEXT("Monster %d"),I));ManyRules.Add(MakeShared<FJsonValueObject>(R));}
    Plugin->Profile->SetArrayField(TEXT("monsters"),ManyRules);Panel->Save();Panel->AddMonster(TEXT("Last monster"));
    TestEqual(TEXT("Adding the 26th monster selects its page"),Panel->MonsterPage,1);
    TestTrue(TEXT("Added monster is immediately visible"),FindMonsterButton(TEXT("Last monster"),TEXT("bolt")).IsValid());
    TestTrue(TEXT("Default stays visible on later pages"),FindMonsterButton(TEXT("<DEFAULT>"),TEXT("bolt")).IsValid());
    Panel->AddMonster(TEXT("Sir Bellas"));TestEqual(TEXT("Selecting an existing monster goes to its page"),Panel->MonsterPage,0);
    Panel->Page=TEXT("Import");Panel->Rebuild();
}
