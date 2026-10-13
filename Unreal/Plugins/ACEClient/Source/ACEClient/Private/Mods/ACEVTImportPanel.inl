FString LegacyPath,LegacyName=TEXT("Imported"),LegacyFolder;
FString ImportExtension=TEXT("utl");
TMap<FString,TArray<FString>> LibraryFiles;
void RefreshLibrary(){for(const TCHAR* Ext:{TEXT("nav"),TEXT("met"),TEXT("utl"),TEXT("usd")})LibraryFiles.Add(Ext,Host->GetProfileFiles(Ext));}
void ImportHome()
{
    Heading(TEXT("Import"),TEXT("Choose a vTank file to create reusable profiles. Meta imports include referenced files and embedded routes. Keep dependency files together in the import folder."));
    for(const auto& Entry:TArray<TPair<FString,FString>>{
        {TEXT("utl"),TEXT("Import vTank Loot Profile (*.utl)")},{TEXT("nav"),TEXT("Import vTank Route (*.nav)")},
        {TEXT("met"),TEXT("Import vTank Meta (*.met)")},{TEXT("usd"),TEXT("Import vTank Character Settings (*.usd)")}})
        Add(Button(Entry.Value,[this,Ext=Entry.Key](){ImportExtension=Ext;RefreshLibrary();Page=TEXT("Import vTank");Rebuild();}));
    Add(Button(TEXT("Compatibility preview / reports"),[this](){Page=TEXT("Import tools");Rebuild();}));
}
void SectionImportPanel()
{
    Add(Button(TEXT("Back to Import"),[this](){Page=TEXT("Import");Rebuild();}));
    Heading(TEXT("Import vTank file (*.")+ImportExtension+TEXT(")"),TEXT("Creates and selects profiles named after the file. Existing names receive a numbered suffix. Original files are kept."));
    Add(Text(TEXT("Import folder"),14,Muted));
    Add(SNew(SEditableTextBox).Style(&FACEPluginStyle::Current()->Edit).Text(FText::FromString(LegacyFolder)).OnTextChanged_Lambda([this](const FText& Value){LegacyFolder=Value.ToString();}));
    Add(Button(TEXT("Refresh folder"),[this](){if(Host->SetProfileFolder(LegacyFolder)){RefreshLibrary();Rebuild();}}));
    int32 Count=0;
    for(const auto& File:LibraryFiles.FindRef(ImportExtension))
    {
        ++Count;Add(Button(File,[this,File](){
            if(!CanLeaveLootRule())return;
            if((ImportExtension==TEXT("nav")||ImportExtension==TEXT("utl"))?Host->ImportUCMSection(File):Host->SelectProfileFile(File))ResetProfileEditors();Rebuild();}));
    }
    if(!Count)Add(Text(TEXT("No .")+ImportExtension+TEXT(" files found. Copy files into this folder, then refresh."),15,Muted));
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
    Heading(TEXT("Virindi profile import"),TEXT("Read .utl loot rules, .nav routes, .met metas and .usd settings. Originals are never changed. Only fully supported conversions can be activated; unsupported data is retained in an import report. Supported /vt commands become /ucm actions. Imports create separate reusable profiles; repeated names receive numbered suffixes. Decal plugins are not loaded."));
    Add(Text(TEXT("File path (or copy files to Saved/ClientPlugins/ImportInbox)"),15,Muted));
    Add(SNew(SEditableTextBox).Style(&FACEPluginStyle::Current()->Edit).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(FText::FromString(LegacyPath)).OnTextChanged_Lambda([this](const FText& V){LegacyPath=V.ToString();LegacyDocument.Reset();}));
    Add(Button(TEXT("Preview file"),[this](){PreviewLegacy(LegacyPath);}));
    Add(Text(TEXT("Folder to browse"),15,Muted));
    Add(SNew(SEditableTextBox).Style(&FACEPluginStyle::Current()->Edit).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(FText::FromString(LegacyFolder)).HintText(FText::FromString(TEXT("Folder containing VT profiles"))).OnTextChanged_Lambda([this](const FText& V){LegacyFolder=V.ToString();}));
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
    Add(SNew(SEditableTextBox).Style(&FACEPluginStyle::Current()->Edit).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(FText::FromString(LegacyName)).HintText(FText::FromString(TEXT("Import name"))).OnTextChanged_Lambda([this](const FText& V){LegacyName=V.ToString();}));
    const FString PreviewPath=LegacyPath;
    Add(Button(LegacyIssues.IsEmpty()?TEXT("Import supported profile"):TEXT("Save compatibility report"),[this,PreviewPath](){Host->ImportLegacyProfile(PreviewPath,LegacyName);}));
}
