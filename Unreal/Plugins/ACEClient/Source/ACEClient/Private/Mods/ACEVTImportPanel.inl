FString LegacyPath,LegacyName=TEXT("Imported"),LegacyFolder;
TMap<FString,FString> LibraryErrors;
TMap<FString,TArray<FString>> LibraryFiles;
void RefreshLibrary(){for(const TCHAR* Ext:{TEXT("nav"),TEXT("met"),TEXT("utl"),TEXT("usd")})LibraryFiles.Add(Ext,Host->GetProfileFiles(Ext));}
void LibrarySelector(const FString& Ext,const FString& Label)
{
    Add(Text(Label,16,Muted));
    Add(SNew(SComboButton).ButtonStyle(&Style()).ContentPadding(8).ButtonContent()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).ColorAndOpacity(Ink)
        .Text_Lambda([this,Ext](){const FString Source=Str(P(),*(Ext+TEXT("_source")));if(!Source.IsEmpty())return FText::FromString(FPaths::GetCleanFilename(Source));
            if(Ext==TEXT("utl")&&!Str(P(),TEXT("loot_profile")).IsEmpty())return FText::FromString(Str(P(),TEXT("loot_profile")));
            const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;const TCHAR* Key=Ext==TEXT("nav")?TEXT("route"):Ext==TEXT("met")?TEXT("vt_meta"):Ext==TEXT("usd")?TEXT("usd_keys"):TEXT("loot_rules");
            return FText::FromString(P()->TryGetArrayField(Key,Entries)&&Entries->Num()?TEXT("Custom / saved in this setup"):TEXT("[None] — choose from folder"));})]
        .OnGetMenuContent_Lambda([this,Ext]()->TSharedRef<SWidget>{
            auto List=SNew(SVerticalBox);
            List->AddSlot().AutoHeight()[Button(TEXT("[None]"),[this,Ext](){if(Host->ClearProfileFile(Ext))LibraryErrors.Remove(Ext);else LibraryErrors.Add(Ext,Host->Notice);FSlateApplication::Get().DismissAllMenus();Rebuild();})];
            for(const auto& File:LibraryFiles.FindOrAdd(Ext))List->AddSlot().AutoHeight()[Button(File,[this,Ext,File](){if(Host->SelectProfileFile(File))LibraryErrors.Remove(Ext);else LibraryErrors.Add(Ext,Host->Notice);FSlateApplication::Get().DismissAllMenus();Rebuild();})];
            if(LibraryFiles.FindOrAdd(Ext).IsEmpty())List->AddSlot().AutoHeight()[Text(TEXT("No .")+Ext+TEXT(" files in this folder"),14,Muted)];
            return SNew(SBox).MaxDesiredHeight(280).MinDesiredWidth(280)[SNew(SScrollBox)+SScrollBox::Slot()[List]];
        }));
    const FString Source=Str(P(),*(Ext+TEXT("_source")));
    if(!Source.IsEmpty())Add(Text(TEXT("Loaded: ")+Source,12,Accent));
    if(const FString* Error=LibraryErrors.Find(Ext))Add(Text(*Error,14,FLinearColor(1,.55f,.3f)));
}
void LibraryPanel()
{
    Heading(TEXT("Profile library"),TEXT("Choose to load. Refresh after changing files, then choose again to reload."));
    Add(SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).Padding(8).Text(FText::FromString(LegacyFolder)).HintText(FText::FromString(TEXT("Folder containing .nav, .met, .utl and .usd files")))
        .OnTextChanged_Lambda([this](const FText& V){LegacyFolder=V.ToString();}));
    Add(Button(TEXT("Refresh folder"),[this](){if(Host->SetProfileFolder(LegacyFolder)){RefreshLibrary();Rebuild();}}));
    LibrarySelector(TEXT("nav"),TEXT("Navigation"));LibrarySelector(TEXT("met"),TEXT("Meta"));LibrarySelector(TEXT("utl"),TEXT("Loot"));
    LibrarySelector(TEXT("usd"),TEXT("Character settings"));
}
TSharedPtr<FJsonObject> LegacyDocument,LegacyConverted;
TArray<FString> LegacyIssues;
void PreviewLegacy(const FString& Path)
{
    LegacyPath=Path;LegacyDocument=Host->InspectLegacyProfile(Path);LegacyIssues.Reset();LegacyConverted.Reset();
    if(LegacyDocument)LegacyConverted=ACEVTProfile::ConvertFile(Path,LegacyIssues);
    Rebuild();
}
void ImportPanel()
{
    Heading(TEXT("Virindi profile import"),TEXT("Read .utl loot rules, .nav routes, .met metas and .usd settings. Originals are never changed. Only fully supported conversions can be activated; unsupported data is retained in an import report. Supported /vt commands become /ucm actions. Each import saves a new native JSON setup under Profiles/ucm; repeated names receive numbered suffixes. Linked profiles are bundled. Decal plugins are not loaded."));
    Add(Text(TEXT("File path (or copy files to Saved/ClientPlugins/ImportInbox)"),15,Muted));
    Add(SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(FText::FromString(LegacyPath)).OnTextChanged_Lambda([this](const FText& V){LegacyPath=V.ToString();LegacyDocument.Reset();}));
    Add(Button(TEXT("Preview file"),[this](){PreviewLegacy(LegacyPath);}));
    Add(Text(TEXT("Folder to browse"),15,Muted));
    Add(SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(FText::FromString(LegacyFolder)).HintText(FText::FromString(TEXT("Folder containing VT profiles"))).OnTextChanged_Lambda([this](const FText& V){LegacyFolder=V.ToString();}));
    Add(Button(TEXT("Refresh files"),[this](){Rebuild();}));
    Add(Button(TEXT("Create a new UTL loot profile"),[this](){LegacyDocument=MakeShared<FJsonObject>();LegacyDocument->SetStringField(TEXT("format"),TEXT("utl"));LegacyDocument->SetArrayField(TEXT("rules"),{});LegacyDocument->SetArrayField(TEXT("extras"),{});LegacyRule.Reset();Page=TEXT("Legacy loot");Rebuild();}));
    const FString Folder=LegacyFolder.IsEmpty()?Host->UserDirectory()/TEXT("ImportInbox"):LegacyFolder;
    TArray<FString> Files;for(const TCHAR* Ext:{TEXT("utl"),TEXT("nav"),TEXT("met"),TEXT("usd")}){TArray<FString> Matches;IFileManager::Get().FindFiles(Matches,*(Folder/(FString(TEXT("*."))+Ext)),true,false);Files.Append(Matches);}
    auto FileList=SNew(SVerticalBox);Files.Sort();for(int32 I=0;I<FMath::Min(256,Files.Num());++I){const FString File=Files[I];FileList->AddSlot().AutoHeight()[Button(File,[this,Folder,File](){PreviewLegacy(Folder/File);})];}
    Add(SNew(SBox).MaxDesiredHeight(220)[SNew(SScrollBox)+SScrollBox::Slot()[FileList]]);
    if(!LegacyDocument)return;
    if(Str(LegacyDocument,TEXT("format"))==TEXT("utl"))Add(Button(TEXT("Edit UTL rules / save compatible copy"),[this](){LegacyRule.Reset();Page=TEXT("Legacy loot");Rebuild();}));
    Heading(TEXT("Preview: ")+FPaths::GetCleanFilename(LegacyPath),LegacyIssues.IsEmpty()?TEXT("Supported conversion. Import creates a named UCM profile and stops automation."):TEXT("Not ready to run. Resolve these gaps before using the profile. Saving the report does not change your active profile."));
    for(const TCHAR* Key:{TEXT("rules"),TEXT("points"),TEXT("tables")}){const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(LegacyDocument->TryGetArrayField(Key,Values))Add(Text(FString::Printf(TEXT("%s: %d"),Key,Values->Num()),16));}
    Add(Text(FString::Printf(TEXT("Compatibility issues: %d"),LegacyIssues.Num()),17,LegacyIssues.IsEmpty()?Accent:FLinearColor(1,.6f,.25f)));
    for(int32 I=0;I<FMath::Min(100,LegacyIssues.Num());++I)Add(Text(LegacyIssues[I],14,Muted));
    if(LegacyIssues.Num()>100)Add(Text(TEXT("The complete issue list is included in the saved report."),14,Muted));
    Add(SNew(SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(FText::FromString(LegacyName)).HintText(FText::FromString(TEXT("Import name"))).OnTextChanged_Lambda([this](const FText& V){LegacyName=V.ToString();}));
    const FString PreviewPath=LegacyPath;
    Add(Button(LegacyIssues.IsEmpty()?TEXT("Import supported profile"):TEXT("Save compatibility report"),[this,PreviewPath](){Host->ImportLegacyProfile(PreviewPath,LegacyName);}));
}
