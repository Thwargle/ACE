#include "ACEInputBindings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
// Retail CMasterInputMap::LoadFromFile uses PFileNode, not an INI file.
struct FNode { FString Name; TArray<FNode> Children; };
struct FParser
{
 const FString& Text; int32 At=0, Count=0; FString Error;
 void Space(){while(At<Text.Len()){if(FChar::IsWhitespace(Text[At])||Text[At]==0xfeff){++At;continue;}if(Text[At]==TEXT('#')){while(At<Text.Len()&&Text[At]!=TEXT('\n'))++At;continue;}break;}}
 bool Nodes(TArray<FNode>& Out,int32 Depth=0)
 {
  if(Depth>12){Error=TEXT("The keymap is nested too deeply.");return false;}
  while(true)
  {
   Space();if(At==Text.Len()){if(Depth)Error=TEXT("Missing closing bracket in keymap.");return !Depth;}
   if(Text[At]==TEXT(']')){++At;if(!Depth)Error=TEXT("Unexpected closing bracket in keymap.");return Depth>0;}
   if(Text[At]==TEXT('[')||++Count>30000){Error=TEXT("Invalid or oversized keymap structure.");return false;}
   FNode Node;
   if(Text[At]==TEXT('"'))
   {
    ++At;bool Closed=false;
    while(At<Text.Len()){TCHAR C=Text[At++];if(C==TEXT('"')){Closed=true;break;}if(C==TEXT('\\')&&At<Text.Len()&&(Text[At]==TEXT('"')||Text[At]==TEXT('\\')))C=Text[At++];Node.Name+=C;}
    if(!Closed){Error=TEXT("Unterminated quoted string in keymap.");return false;}
   }
   else {const int32 Start=At;while(At<Text.Len()&&!FChar::IsWhitespace(Text[At])&&Text[At]!=TEXT('[')&&Text[At]!=TEXT(']')&&Text[At]!=TEXT('#'))++At;Node.Name=Text.Mid(Start,At-Start);}
   Space();if(At<Text.Len()&&Text[At]==TEXT('[')){++At;if(!Nodes(Node.Children,Depth+1))return false;}
   Out.Add(MoveTemp(Node));
  }
 }
};
const FNode* Find(const TArray<FNode>& Nodes,const TCHAR* Name){return Nodes.FindByPredicate([&](const FNode& N){return N.Name==Name;});}
bool Number(const FString& Text,uint32& Out)
{
 if(Text.IsEmpty()||Text.StartsWith(TEXT("-")))return false;
 TCHAR* End=nullptr;const uint64 Value=FCString::Strtoui64(*Text,&End,0);
 if(!End||*End||End==*Text||Value>MAX_uint32)return false;Out=uint32(Value);return true;
}
const TMap<FString,FKey>& PhysicalNames();
FKey Physical(const FString& Device,const FString& Control)
{
 if(Device==TEXT("Mouse"))
 {
  static const FKey Mouse[]={EKeys::LeftMouseButton,EKeys::RightMouseButton,EKeys::MiddleMouseButton,EKeys::ThumbMouseButton,EKeys::ThumbMouseButton2};
  for(int32 I=0;I<5;++I)if(Control==FString::Printf(TEXT("DIMOFS_BUTTON%d"),I))return Mouse[I];
  return FKey();
 }
 if(Device!=TEXT("Keyboard")||!Control.StartsWith(TEXT("DIK_")))return FKey();
 const FString Name=Control.Mid(4);
 if(Name.Len()==1&&Name[0]>=TEXT('A')&&Name[0]<=TEXT('Z'))return FKey(FName(*Name));
 static const FKey Digits[]={EKeys::Zero,EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine};
 if(Name.Len()==1&&FChar::IsDigit(Name[0]))return Digits[Name[0]-TEXT('0')];
 if(Name.StartsWith(TEXT("F"))){uint32 N=0;if(Number(Name.Mid(1),N)&&N>=1&&N<=12)return FKey(FName(*Name));}
 if(const FKey* Key=PhysicalNames().Find(Name))return *Key;return FKey();
}
const TMap<FString,FKey>& PhysicalNames()
{
 static const TMap<FString,FKey> Keys={
  {TEXT("ESCAPE"),EKeys::Escape},{TEXT("BACK"),EKeys::BackSpace},{TEXT("BACKSPACE"),EKeys::BackSpace},{TEXT("TAB"),EKeys::Tab},
  {TEXT("RETURN"),EKeys::Enter},{TEXT("SPACE"),EKeys::SpaceBar},{TEXT("MINUS"),EKeys::Hyphen},{TEXT("EQUALS"),EKeys::Equals},
  {TEXT("LBRACKET"),EKeys::LeftBracket},{TEXT("RBRACKET"),EKeys::RightBracket},{TEXT("SEMICOLON"),EKeys::Semicolon},
  {TEXT("APOSTROPHE"),EKeys::Apostrophe},{TEXT("GRAVE"),EKeys::Tilde},{TEXT("BACKSLASH"),EKeys::Backslash},
  {TEXT("COMMA"),EKeys::Comma},{TEXT("PERIOD"),EKeys::Period},{TEXT("SLASH"),EKeys::Slash},
  {TEXT("LSHIFT"),EKeys::LeftShift},{TEXT("RSHIFT"),EKeys::RightShift},{TEXT("LCONTROL"),EKeys::LeftControl},{TEXT("RCONTROL"),EKeys::RightControl},
  {TEXT("LMENU"),EKeys::LeftAlt},{TEXT("LALT"),EKeys::LeftAlt},{TEXT("RMENU"),EKeys::RightAlt},{TEXT("RALT"),EKeys::RightAlt},
  {TEXT("LWIN"),EKeys::LeftCommand},{TEXT("RWIN"),EKeys::RightCommand},{TEXT("CAPITAL"),EKeys::CapsLock},
  {TEXT("UP"),EKeys::Up},{TEXT("UPARROW"),EKeys::Up},{TEXT("DOWN"),EKeys::Down},{TEXT("DOWNARROW"),EKeys::Down},
  {TEXT("LEFT"),EKeys::Left},{TEXT("LEFTARROW"),EKeys::Left},{TEXT("RIGHT"),EKeys::Right},{TEXT("RIGHTARROW"),EKeys::Right},
  {TEXT("HOME"),EKeys::Home},{TEXT("END"),EKeys::End},{TEXT("PRIOR"),EKeys::PageUp},{TEXT("PGUP"),EKeys::PageUp},
  {TEXT("NEXT"),EKeys::PageDown},{TEXT("PGDN"),EKeys::PageDown},{TEXT("INSERT"),EKeys::Insert},{TEXT("DELETE"),EKeys::Delete},
  {TEXT("NUMLOCK"),EKeys::NumLock},{TEXT("SCROLL"),EKeys::ScrollLock},{TEXT("PAUSE"),EKeys::Pause},
  {TEXT("NUMPAD0"),EKeys::NumPadZero},{TEXT("NUMPAD1"),EKeys::NumPadOne},{TEXT("NUMPAD2"),EKeys::NumPadTwo},
  {TEXT("NUMPAD3"),EKeys::NumPadThree},{TEXT("NUMPAD4"),EKeys::NumPadFour},{TEXT("NUMPAD5"),EKeys::NumPadFive},
  {TEXT("NUMPAD6"),EKeys::NumPadSix},{TEXT("NUMPAD7"),EKeys::NumPadSeven},{TEXT("NUMPAD8"),EKeys::NumPadEight},{TEXT("NUMPAD9"),EKeys::NumPadNine},
  {TEXT("ADD"),EKeys::Add},{TEXT("NUMPADPLUS"),EKeys::Add},{TEXT("SUBTRACT"),EKeys::Subtract},{TEXT("NUMPADMINUS"),EKeys::Subtract},
  {TEXT("MULTIPLY"),EKeys::Multiply},{TEXT("NUMPADSTAR"),EKeys::Multiply},{TEXT("DIVIDE"),EKeys::Divide},{TEXT("NUMPADSLASH"),EKeys::Divide},
  {TEXT("DECIMAL"),EKeys::Decimal},{TEXT("NUMPADPERIOD"),EKeys::Decimal}
 };
 return Keys;
}
const TMap<FString,FKey>& ActionNames()
{
 using namespace ACEInputBindings;
 static const TMap<FString,FKey> Map={
  {TEXT("MovementForward"),EKeys::W},{TEXT("MovementBackup"),EKeys::S},{TEXT("MovementTurnLeft"),EKeys::A},{TEXT("MovementTurnRight"),EKeys::D},
  {TEXT("MovementStrafeLeft"),EKeys::Q},{TEXT("MovementStrafeRight"),EKeys::E},{TEXT("MovementWalkMode"),EKeys::LeftShift},
  {TEXT("MovementRunLock"),EKeys::NumLock},{TEXT("MovementStop"),Action(TEXT("Stop"))},{TEXT("MovementJump"),EKeys::SpaceBar},
  {TEXT("Ready"),Action(TEXT("Ready"))},{TEXT("Sitting"),EKeys::G},{TEXT("Crouch"),EKeys::C},{TEXT("Sleeping"),EKeys::B},
  {TEXT("SelectionPickUp"),Action(TEXT("Pickup"))},{TEXT("SelectionClosestItem"),Action(TEXT("ClosestItem"))},
  {TEXT("SelectionPreviousItem"),EKeys::LeftBracket},{TEXT("SelectionNextItem"),EKeys::RightBracket},
  {TEXT("SelectionClosestMonster"),Action(TEXT("ClosestMonster"))},{TEXT("SelectionPreviousMonster"),EKeys::Semicolon},{TEXT("SelectionNextMonster"),EKeys::Apostrophe},
  {TEXT("SelectionLastAttacker"),EKeys::Home},{TEXT("SelectionExamine"),Action(TEXT("Examine"))},
  {TEXT("ToggleInventoryPanel"),EKeys::I},{TEXT("ToggleAttributesPanel"),EKeys::P},{TEXT("ToggleSkillsPanel"),Action(TEXT("Skills"))},
  {TEXT("ToggleSpellbookPanel"),EKeys::M},{TEXT("ToggleSpellComponentsPanel"),Action(TEXT("Components"))},{TEXT("ToggleWorldPanel"),Action(TEXT("World"))},
  {TEXT("ToggleOptionsPanel"),EKeys::O},{TEXT("ToggleAllegiancePanel"),Action(TEXT("Allegiance"))},{TEXT("ToggleFellowshipPanel"),Action(TEXT("Fellowship"))},
  {TEXT("ToggleContractsPanel"),EKeys::U},
  {TEXT("USE"),EKeys::F},{TEXT("EscapeKey"),EKeys::Escape},{TEXT("EnterChatMode"),Action(TEXT("Chat"))},{TEXT("CombatToggleCombat"),EKeys::Tilde},
  {TEXT("PointState"),EKeys::K},{TEXT("Wave"),EKeys::J},{TEXT("Laugh"),Action(TEXT("Laugh"))},{TEXT("Cheer"),Action(TEXT("Cheer"))},{TEXT("Cry"),Action(TEXT("Cry"))},
  {TEXT("CreateShortcut"),EKeys::Zero},
  {TEXT("CameraRotateLeft"),EKeys::NumPadFour},{TEXT("CameraRotateRight"),EKeys::NumPadSix},{TEXT("CameraRotateUp"),EKeys::NumPadEight},{TEXT("CameraRotateDown"),EKeys::NumPadTwo},
  {TEXT("CameraMoveToward"),EKeys::Add},{TEXT("CameraMoveAway"),EKeys::Subtract},{TEXT("CameraViewDefault"),EKeys::NumPadZero},
  {TEXT("CameraViewFirstPerson"),EKeys::NumPadFive},{TEXT("CameraViewLookDown"),EKeys::NumPadThree},
  {TEXT("CombatDecreaseAttackPower"),Action(TEXT("MeleeDecrease"))},{TEXT("CombatIncreaseAttackPower"),Action(TEXT("MeleeIncrease"))},
  {TEXT("CombatLowAttack"),Action(TEXT("MeleeLow"))},{TEXT("CombatMediumAttack"),Action(TEXT("MeleeMedium"))},{TEXT("CombatHighAttack"),Action(TEXT("MeleeHigh"))},
  {TEXT("CombatDecreaseMissileAccuracy"),Action(TEXT("MissileDecrease"))},{TEXT("CombatIncreaseMissileAccuracy"),Action(TEXT("MissileIncrease"))},
  {TEXT("CombatAimLow"),Action(TEXT("MissileLow"))},{TEXT("CombatAimMedium"),Action(TEXT("MissileMedium"))},{TEXT("CombatAimHigh"),Action(TEXT("MissileHigh"))},
  {TEXT("CombatPrevSpellTab"),Action(TEXT("SpellPrevTab"))},{TEXT("CombatNextSpellTab"),Action(TEXT("SpellNextTab"))},
  {TEXT("CombatPrevSpell"),Action(TEXT("SpellPrevious"))},{TEXT("CombatNextSpell"),Action(TEXT("SpellNext"))},{TEXT("CombatCastCurrentSpell"),Action(TEXT("SpellCast"))},
  {TEXT("CombatFirstSpellTab"),Action(TEXT("SpellFirstTab"))},{TEXT("CombatLastSpellTab"),Action(TEXT("SpellLastTab"))},
  {TEXT("CombatFirstSpell"),Action(TEXT("SpellFirst"))},{TEXT("CombatLastSpell"),Action(TEXT("SpellLast"))}
 };
 return Map;
}
FKey ActionFor(const FString& Name)
{
 using namespace ACEInputBindings;
 if(const FKey* Key=ActionNames().Find(Name))return *Key;
 for(int32 I=0;I<18;++I)if(Name==FString::Printf(TEXT("UseQuickSlot_%d"),I+1))return Shortcut(I);
 for(int32 I=0;I<9;++I)if(Name==FString::Printf(TEXT("UseSpellSlot_%d"),I+1))return SpellSlot(I);
 return FKey();
}
FString NameFor(FKey Key)
{
 for(const auto& P:ActionNames())if(P.Value==Key)return P.Key;
 for(int32 I=0;I<18;++I)if(Key==ACEInputBindings::Shortcut(I))return FString::Printf(TEXT("UseQuickSlot_%d"),I+1);
 for(int32 I=0;I<9;++I)if(Key==ACEInputBindings::SpellSlot(I))return FString::Printf(TEXT("UseSpellSlot_%d"),I+1);
 return FString();
}
FString GroupFor(const FString& Name,int32 Context,const TCHAR* Page)
{
 // ActionMap 0x26000000 defines the permitted retail input map for each action.
 if(Context==2)return TEXT("MeleeCombat");if(Context==4)return TEXT("MissileCombat");if(Context==8)return TEXT("MagicCombat");
 if(Name.StartsWith(TEXT("Movement"))||Name==TEXT("Ready")||Name==TEXT("Sitting")||Name==TEXT("Crouch")||Name==TEXT("Sleeping"))return TEXT("MovementCommands");
 if(Name.StartsWith(TEXT("Camera")))return TEXT("CameraControls");
 if(FString(Page)==TEXT("Emotes"))return TEXT("Emotes");
 if(Name.StartsWith(TEXT("UseQuickSlot"))||Name==TEXT("CreateShortcut"))return TEXT("QuickslotCommands");
 if(Name.StartsWith(TEXT("Selection"))&&Name!=TEXT("SelectionExamine"))return TEXT("ItemSelectionCommands");
 if(Name==TEXT("EnterChatMode"))return TEXT("ChatCommands");
 if(Name==TEXT("CombatToggleCombat"))return TEXT("Combat");
 return TEXT("UICommands");
}
}

namespace ACEInputBindings
{
FImportResult ImportRetailKeymap(const FString& Text)
{
 FImportResult Result;
 if(!IsEditing()){Result.Error=TEXT("Open the keyboard settings before importing.");return Result;}
 if(Text.Len()>1024*1024){Result.Error=TEXT("Keymaps must be smaller than 1 MB.");return Result;}
 TArray<FNode> Roots;FParser Parser{Text};
 if(!Parser.Nodes(Roots)){Result.Error=Parser.Error;return Result;}
 const FNode* Devices=Find(Roots,TEXT("Devices")),*Bindings=Find(Roots,TEXT("Bindings")),*Meta=Find(Roots,TEXT("MetaKeys"));
 if(!Devices||!Bindings){Result.Error=TEXT("This is not a retail keymap: Devices and Bindings are required.");return Result;}
 auto Control=[&](const FNode& N)->FKey
 {
  uint32 Index=0;if(N.Children.Num()!=2||!Number(N.Children[0].Name,Index)||Index>=uint32(Devices->Children.Num()))return FKey();
  return Physical(Devices->Children[Index].Name,N.Children[1].Name);
 };
 TMap<uint32,int32> Modifiers;
 if(Meta)for(const auto& N:Meta->Children)
 {
  uint32 Ordinal=0;if(!Number(N.Name,Ordinal)||Ordinal<1||Ordinal>32)continue;
  const FKey Key=Control(N);int32 Flag=0;
  if(Key==EKeys::LeftShift||Key==EKeys::RightShift)Flag=1;
  if(Key==EKeys::LeftControl||Key==EKeys::RightControl)Flag=2;
  if(Key==EKeys::LeftAlt||Key==EKeys::RightAlt)Flag=4;
  if(Key==EKeys::LeftCommand||Key==EKeys::RightCommand)Flag=8;
  if(const int32* Existing=Modifiers.Find(Ordinal);Existing&&*Existing!=Flag)Flag=0;
  Modifiers.Add(Ordinal,Flag);
 }
 TMap<FKey,TArray<FInputChord>> Imported;
 TSet<FString> GroupsSeen;TSet<FKey> UnsupportedKeys;
 struct FUnbind{FInputChord Chord;int32 Context;};TArray<FUnbind> Unbinds;
 for(const FNode& Group:Bindings->Children)
 {
  // A camera action in CameraAlternateControls must never replace a movement
  // arrow in the ordinary map. Only contexts implemented by our dispatcher apply.
  static const TSet<FString> SupportedGroups={TEXT("MovementCommands"),TEXT("ItemSelectionCommands"),TEXT("UICommands"),TEXT("QuickslotCommands"),TEXT("ChatCommands"),TEXT("Combat"),TEXT("MeleeCombat"),TEXT("MissileCombat"),TEXT("MagicCombat"),TEXT("Emotes"),TEXT("CameraControls")};
  if(!SupportedGroups.Contains(Group.Name))
  {if(!Group.Children.IsEmpty())Result.Skipped.AddUnique(Group.Name+TEXT(": this input context is unchanged"));continue;}
  GroupsSeen.Add(Group.Name);
  const int32 Mode=Group.Name==TEXT("MeleeCombat")?2:Group.Name==TEXT("MissileCombat")?4:Group.Name==TEXT("MagicCombat")?8:0;
  for(const FNode& N:Group.Children)
  {
   const FKey Key=ActionFor(N.Name);const bool Nothing=N.Name==TEXT("DoNothing");
   auto Skip=[&](const TCHAR* Why){Result.Skipped.AddUnique(N.Name+TEXT(": ")+Why);if(!Key.GetFName().IsNone())UnsupportedKeys.Add(Key);};
   // Logical action IDs are not registered physical FKeys; NAME_None is the sentinel.
   if(Key.GetFName().IsNone()&&!Nothing){Skip(TEXT("action not supported"));continue;}
   if(N.Children.IsEmpty()){if(!Nothing)Imported.FindOrAdd(Key);continue;}
   if(N.Children.Num()>2||!N.Children[0].Name.IsEmpty()){Skip(TEXT("analog/double-click activation is not supported"));continue;}
   const FKey PhysicalKey=Control(N.Children[0]);
   if(!PhysicalKey.IsValid()){Skip(TEXT("device or physical key not supported"));continue;}
   uint32 Mask=0;if(N.Children.Num()==2&&!Number(N.Children[1].Name,Mask)){Skip(TEXT("invalid modifier mask"));continue;}
   int32 Flags=0;bool Valid=true;
   for(uint32 I=0;I<32;++I)if(Mask&(1u<<I))
   // CInputMap reverses the file mask before matching CMasterInputMap's
   // reversed ordinal bits. In the file, ordinal 1 is therefore mask bit 0.
   {const int32* Flag=Modifiers.Find(I+1);if(!Flag||!*Flag){Valid=false;break;}Flags|=*Flag;}
   if(!Valid){Skip(TEXT("custom modifier cannot be represented"));continue;}
   const FInputChord Chord(PhysicalKey,(Flags&1)!=0,(Flags&2)!=0,(Flags&4)!=0,(Flags&8)!=0);
   if(Nothing){Unbinds.Add({Chord,Mode});continue;}
   auto& Keys=Imported.FindOrAdd(Key);
   if(Keys.Contains(Chord))continue;
   if(Keys.Num()==3){Skip(TEXT("more than three bindings; first three retained"));continue;}
   Keys.Add(Chord);++Result.BindingCount;
  }
 }
 // Retail replaces each supplied input map. An omitted action is unbound;
 // preserve only actions whose supplied device/activation we cannot represent.
 for(const auto& A:Actions())
 {
  const FString Name=NameFor(A.Key);
  if(!Name.IsEmpty()&&GroupsSeen.Contains(GroupFor(Name,A.Context,A.Page))&&!UnsupportedKeys.Contains(A.Key))Imported.FindOrAdd(A.Key);
 }
 if(Imported.IsEmpty()){Result.Error=TEXT("No supported gameplay actions were found. Existing bindings are unchanged.");return Result;}
 // Validate everything before touching the draft. Imports never rewrite the source file.
 for(const auto& U:Unbinds)for(const auto& A:Actions())if(A.Context==U.Context)
  for(int32 I=0;I<3;++I)if(Get(A.Key,I)==U.Chord)Set(A.Key,I,FInputChord());
 for(const auto& Pair:Imported)for(int32 I=0;I<3;++I)Set(Pair.Key,I,FInputChord());
 // Catalog order makes conflicting bindings deterministic.
 for(const auto& A:Actions())if(const auto* Keys=Imported.Find(A.Key))for(int32 I=0;I<Keys->Num();++I)Set(A.Key,I,(*Keys)[I]);
 Result.bSuccess=true;return Result;
}
FImportResult ImportRetailKeymapFile(const FString& Path)
{
 FImportResult Result;const int64 Size=IFileManager::Get().FileSize(*Path);
 if(!FPaths::GetExtension(Path).Equals(TEXT("keymap"),ESearchCase::IgnoreCase)||Size<0||Size>1024*1024)
 {Result.Error=TEXT("Choose an existing .keymap file smaller than 1 MB.");return Result;}
 FString Text;if(!FFileHelper::LoadFileToString(Text,*Path)){Result.Error=TEXT("Unable to read that keymap file.");return Result;}
 return ImportRetailKeymap(Text);
}
bool ExportRetailKeymapFile(const FString& Path,FString& Error)
{
 if(!FPaths::GetExtension(Path).Equals(TEXT("keymap"),ESearchCase::IgnoreCase))
 {Error=TEXT("Choose a filename ending in .keymap.");return false;}
 TMap<FKey,FString> Names;for(const auto& P:ActionNames())Names.Add(P.Value,P.Key);
 for(int32 I=0;I<18;++I)Names.Add(Shortcut(I),FString::Printf(TEXT("UseQuickSlot_%d"),I+1));
 for(int32 I=0;I<9;++I)Names.Add(SpellSlot(I),FString::Printf(TEXT("UseSpellSlot_%d"),I+1));
 // First entry is ControlNameMapper's canonical DirectInput spelling; later
 // aliases are accepted on load but are not emitted into retail files.
 TMap<FKey,FString> Controls;for(const auto& P:PhysicalNames())if(!Controls.Contains(P.Value))Controls.Add(P.Value,TEXT("DIK_")+P.Key);
 for(TCHAR C=TEXT('A');C<=TEXT('Z');++C){const FString Name=FString::Chr(C);Controls.Add(FKey(FName(*Name)),TEXT("DIK_")+Name);}
 const FKey Digits[]={EKeys::Zero,EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine};
 for(int32 I=0;I<10;++I)Controls.Add(Digits[I],FString::Printf(TEXT("DIK_%d"),I));
 for(int32 I=1;I<=12;++I){const FString Name=FString::Printf(TEXT("F%d"),I);Controls.Add(FKey(FName(*Name)),TEXT("DIK_")+Name);}
 const FKey Mouse[]={EKeys::LeftMouseButton,EKeys::RightMouseButton,EKeys::MiddleMouseButton,EKeys::ThumbMouseButton,EKeys::ThumbMouseButton2};
 for(int32 I=0;I<5;++I)Controls.Add(Mouse[I],FString::Printf(TEXT("DIMOFS_BUTTON%d"),I));
 TMap<FString,FString> Groups;TArray<FString> Order;
 for(const auto& A:Actions())
 {
  const FString* Name=Names.Find(A.Key);
  if(!Name)
  {for(int32 I=0;I<3;++I)if(Get(A.Key,I).Key.IsValid()){Error=FString::Printf(TEXT("%s has no retail keymap action. Clear it before exporting."),A.Label);return false;}continue;}
  const FString Group=GroupFor(*Name,A.Context,A.Page);
  Order.AddUnique(Group);auto& Body=Groups.FindOrAdd(Group);
  for(int32 I=0;I<3;++I)
  {
   const auto C=Get(A.Key,I);if(!C.Key.IsValid())continue;
   const FString* Control=Controls.Find(C.Key);
   if(!Control){Error=FString::Printf(TEXT("%s cannot be saved in a retail keymap."),*C.Key.GetDisplayName().ToString());return false;}
   const uint32 Mask=(C.bShift?1u:0u)|(C.bCtrl?2u:0u)|(C.bAlt?4u:0u)|(C.bCmd?8u:0u);
   Body+=FString::Printf(TEXT("  %s [ \"\" [ %d %s ] 0x%X ]\n"),**Name,C.Key.IsMouseButton()?1:0,**Control,Mask);
  }
  // Retail rejects Action [ ] as an invalid control specification. Unbound
  // actions are absent from the group, including completely empty groups.
 }
 FString Text=TEXT("\"User Defined Keymap\" [ 00000000-0000-0000-0000-000000000000 ]\nDevices [ Keyboard [ GUID_SysKeyboard ] Mouse [ GUID_SysMouse ] ]\nMetaKeys [ 1 [ 0 DIK_LSHIFT ] 2 [ 0 DIK_LCONTROL ] 2 [ 0 DIK_RCONTROL ] 3 [ 0 DIK_LMENU ] 3 [ 0 DIK_RMENU ] 4 [ 0 DIK_LWIN ] 4 [ 0 DIK_RWIN ] ]\nBindings [\n");
 for(const auto& Group:Order)Text+=Group+TEXT(" [\n")+Groups[Group]+TEXT("]\n");Text+=TEXT("]\n");
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
 if(!FFileHelper::SaveStringToFile(Text,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
 {Error=TEXT("Unable to save the keymap at that location.");return false;}
 Error.Reset();return true;
}
}
