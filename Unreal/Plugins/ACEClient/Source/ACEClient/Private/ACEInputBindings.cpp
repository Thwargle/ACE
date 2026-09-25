#include "ACEInputBindings.h"
#include "ACERetailInputActions.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"
namespace ACEInputBindings
{
static TMap<FKey,TArray<FInputChord>> Live, Draft;
static TArray<FBlockedBinding> LiveBlocked, DraftBlocked;
FKey ApplicationsKey()
{
 static const FKey Key(TEXT("ACEApplications"));
 if(!Key.IsValid())EKeys::AddKey(FKeyDetails(Key,NSLOCTEXT("ACEInput","Applications","Menu"),0));
 return Key;
}
struct FCandidate {FKey ActionKey;FInputChord Chord;};
FKey NumpadEnterKey()
{
 static const FKey Key(TEXT("ACENumpadEnter"));
 if(!Key.IsValid())EKeys::AddKey(FKeyDetails(Key,NSLOCTEXT("ACEInput","NumpadEnter","Num Enter"),0));
 return Key;
}
static TMap<FKey,TArray<FCandidate>> PhysicalBindings;
static void IndexBindings()
{
 PhysicalBindings.Reset();
 for(const auto& Pair:Live)for(const auto& Chord:Pair.Value)if(Chord.Key.IsValid())PhysicalBindings.FindOrAdd(Chord.Key).Add({Pair.Key,Chord});
}
static bool bLoaded=false, bEditing=false;
static int32 ActiveContext=1;
void SetCombatContext(int32 Mode){ActiveContext=Mode;}
FKey Action(const TCHAR* Name){return FKey(FName(*(FString(TEXT("ACE."))+Name)));}
FKey Shortcut(int32 Slot){return Action(*FString::Printf(TEXT("Shortcut%d"),Slot+1));}
FKey SpellSlot(int32 Slot){return Action(*FString::Printf(TEXT("Spell%d"),Slot+1));}
const TArray<FAction>& Actions()
{
 static const TArray<FAction> List=[]
 {
  // client_portal.dat 14000000 + 14000002: gmDefaultMap and DefaultMap.
  // Enter/slash chat is the intentional AC:Unreal convenience binding.
  TArray<FAction> Result={
   {EKeys::W,TEXT("Move forward"),TEXT("Movement"),{FInputChord(EKeys::W),FInputChord(EKeys::Up)}},
   {EKeys::S,TEXT("Move backward"),TEXT("Movement"),{FInputChord(EKeys::X),FInputChord(EKeys::Down)}},
   {EKeys::A,TEXT("Turn left"),TEXT("Movement"),{FInputChord(EKeys::A),FInputChord(EKeys::Left)}},
   {EKeys::D,TEXT("Turn right"),TEXT("Movement"),{FInputChord(EKeys::D),FInputChord(EKeys::Right)}},
   {EKeys::Q,TEXT("Strafe left"),TEXT("Movement"),{FInputChord(EKeys::Z),FInputChord(EKeys::A,false,false,true,false),FInputChord(EKeys::Left,false,false,true,false)}},
   {EKeys::E,TEXT("Strafe right"),TEXT("Movement"),{FInputChord(EKeys::C),FInputChord(EKeys::D,false,false,true,false),FInputChord(EKeys::Right,false,false,true,false)}},
   {EKeys::LeftShift,TEXT("Run / walk modifier"),TEXT("Movement"),{FInputChord(EKeys::LeftShift)}},
   {EKeys::SpaceBar,TEXT("Jump"),TEXT("Movement"),{FInputChord(EKeys::SpaceBar)}},
   {EKeys::NumLock,TEXT("Autorun"),TEXT("Movement"),{FInputChord(EKeys::Q)}},
   {Action(TEXT("Stop")),TEXT("Stop moving"),TEXT("Movement"),{FInputChord(EKeys::S)}},
   {EKeys::NumPadFour,TEXT("Rotate camera left"),TEXT("Camera"),{FInputChord(EKeys::NumPadFour)}},
   {EKeys::NumPadSix,TEXT("Rotate camera right"),TEXT("Camera"),{FInputChord(EKeys::NumPadSix)}},
   {EKeys::NumPadEight,TEXT("Rotate camera up"),TEXT("Camera"),{FInputChord(EKeys::NumPadEight)}},
   {EKeys::NumPadTwo,TEXT("Rotate camera down"),TEXT("Camera"),{FInputChord(EKeys::NumPadTwo)}},
   {EKeys::Add,TEXT("Zoom camera in"),TEXT("Camera"),{FInputChord(EKeys::Subtract),FInputChord(EKeys::MouseScrollUp)}},
   {EKeys::Subtract,TEXT("Zoom camera out"),TEXT("Camera"),{FInputChord(EKeys::Add),FInputChord(EKeys::MouseScrollDown)}},
   {EKeys::NumPadZero,TEXT("Reset camera"),TEXT("Camera"),{FInputChord(EKeys::NumPadZero)}},
   {EKeys::NumPadFive,TEXT("First person camera"),TEXT("Camera"),{FInputChord(EKeys::Decimal)}},
   {EKeys::NumPadThree,TEXT("Overhead camera"),TEXT("Camera"),{FInputChord(EKeys::NumPadFive)}},
   {EKeys::Tilde,TEXT("Combat mode"),TEXT("Combat"),{FInputChord(EKeys::Tilde)}},
   {EKeys::Home,TEXT("Last attacker"),TEXT("Combat"),{FInputChord(EKeys::Home)}},
   {EKeys::Semicolon,TEXT("Previous monster"),TEXT("Combat"),{FInputChord(EKeys::L)}},
   {EKeys::Apostrophe,TEXT("Next monster"),TEXT("Combat"),{FInputChord(EKeys::Semicolon)}},
   {Action(TEXT("ClosestMonster")),TEXT("Closest monster"),TEXT("Combat"),{FInputChord(EKeys::Apostrophe)}},
   {Action(TEXT("MeleeDecrease")),TEXT("Melee: decrease power"),TEXT("Combat"),{FInputChord(EKeys::Insert)},2},
   {Action(TEXT("MeleeIncrease")),TEXT("Melee: increase power"),TEXT("Combat"),{FInputChord(EKeys::PageUp)},2},
   {Action(TEXT("MeleeLow")),TEXT("Melee: low attack"),TEXT("Combat"),{FInputChord(EKeys::Delete)},2},
   {Action(TEXT("MeleeMedium")),TEXT("Melee: medium attack"),TEXT("Combat"),{FInputChord(EKeys::End)},2},
   {Action(TEXT("MeleeHigh")),TEXT("Melee: high attack"),TEXT("Combat"),{FInputChord(EKeys::PageDown)},2},
   {Action(TEXT("MissileDecrease")),TEXT("Missile: decrease accuracy"),TEXT("Combat"),{FInputChord(EKeys::Insert)},4},
   {Action(TEXT("MissileIncrease")),TEXT("Missile: increase accuracy"),TEXT("Combat"),{FInputChord(EKeys::PageUp)},4},
   {Action(TEXT("MissileLow")),TEXT("Missile: low attack"),TEXT("Combat"),{FInputChord(EKeys::Delete)},4},
   {Action(TEXT("MissileMedium")),TEXT("Missile: medium attack"),TEXT("Combat"),{FInputChord(EKeys::End)},4},
   {Action(TEXT("MissileHigh")),TEXT("Missile: high attack"),TEXT("Combat"),{FInputChord(EKeys::PageDown)},4},
   {Action(TEXT("SpellPrevTab")),TEXT("Magic: previous spell tab"),TEXT("Combat"),{FInputChord(EKeys::Insert)},8},
   {Action(TEXT("SpellNextTab")),TEXT("Magic: next spell tab"),TEXT("Combat"),{FInputChord(EKeys::PageUp)},8},
   {Action(TEXT("SpellPrevious")),TEXT("Magic: previous spell"),TEXT("Combat"),{FInputChord(EKeys::Delete)},8},
   {Action(TEXT("SpellCast")),TEXT("Magic: cast selected spell"),TEXT("Combat"),{FInputChord(EKeys::End)},8},
   {Action(TEXT("SpellNext")),TEXT("Magic: next spell"),TEXT("Combat"),{FInputChord(EKeys::PageDown)},8},
   {Action(TEXT("SpellFirstTab")),TEXT("Magic: first spell tab"),TEXT("Combat"),{FInputChord(EKeys::Insert,false,true,false,false)},8},
   {Action(TEXT("SpellLastTab")),TEXT("Magic: last spell tab"),TEXT("Combat"),{FInputChord(EKeys::PageUp,false,true,false,false)},8},
   {Action(TEXT("SpellFirst")),TEXT("Magic: first spell"),TEXT("Combat"),{FInputChord(EKeys::Delete,false,true,false,false)},8},
   {Action(TEXT("SpellLast")),TEXT("Magic: last spell"),TEXT("Combat"),{FInputChord(EKeys::PageDown,false,true,false,false)},8},
   {EKeys::LeftBracket,TEXT("Previous nearby item"),TEXT("UI"),{FInputChord(EKeys::LeftBracket)}},
   {EKeys::RightBracket,TEXT("Next nearby item"),TEXT("UI"),{FInputChord(EKeys::RightBracket)}},
   {Action(TEXT("ClosestItem")),TEXT("Closest nearby item"),TEXT("UI"),{FInputChord(EKeys::Backslash)}},
   {EKeys::F,TEXT("Use selected object"),TEXT("UI"),{FInputChord(EKeys::R)}},
   {Action(TEXT("Pickup")),TEXT("Pick up selected object"),TEXT("UI"),{FInputChord(EKeys::F)}},
   {Action(TEXT("Examine")),TEXT("Examine selected object"),TEXT("UI"),{FInputChord(EKeys::E)}},
   {Action(TEXT("Chat")),TEXT("Begin chat"),TEXT("UI"),{FInputChord(EKeys::Enter),FInputChord(EKeys::Slash)}},
   {EKeys::Escape,TEXT("Dismiss window / options"),TEXT("UI"),{FInputChord(EKeys::Escape)}},
   {EKeys::I,TEXT("Inventory"),TEXT("UI"),{FInputChord(EKeys::F12)}},
   {EKeys::P,TEXT("Attributes"),TEXT("CharacterSettings"),{FInputChord(EKeys::F8)}},
   {Action(TEXT("Skills")),TEXT("Skills"),TEXT("CharacterSettings"),{FInputChord(EKeys::F9)}},
   {EKeys::M,TEXT("Spellbook"),TEXT("UI"),{FInputChord(EKeys::F5)}},
   {Action(TEXT("Components")),TEXT("Spell components"),TEXT("UI"),{FInputChord(EKeys::F6)}},
   {EKeys::U,TEXT("Contract journal"),TEXT("UI"),{}},
   {EKeys::O,TEXT("Options"),TEXT("UI"),{FInputChord(EKeys::F11)}},
   {Action(TEXT("World")),TEXT("World panel"),TEXT("UI"),{FInputChord(EKeys::F10)}},
   {Action(TEXT("Allegiance")),TEXT("Allegiance"),TEXT("CharacterSettings"),{FInputChord(EKeys::F3)}},
   {Action(TEXT("Fellowship")),TEXT("Fellowship"),TEXT("CharacterSettings"),{FInputChord(EKeys::F4)}},
   {EKeys::G,TEXT("Sit"),TEXT("Emotes"),{FInputChord(EKeys::G)}},
   {EKeys::B,TEXT("Lie down"),TEXT("Emotes"),{FInputChord(EKeys::B)}},
   {EKeys::C,TEXT("Crouch"),TEXT("Emotes"),{FInputChord(EKeys::H)}},
   {EKeys::K,TEXT("Point"),TEXT("Emotes"),{FInputChord(EKeys::K)}},
   {EKeys::J,TEXT("Wave"),TEXT("Emotes"),{FInputChord(EKeys::J)}},
   {Action(TEXT("Ready")),TEXT("Ready"),TEXT("Emotes"),{FInputChord(EKeys::Y)}},
   {Action(TEXT("Laugh")),TEXT("Laugh"),TEXT("Emotes"),{FInputChord(EKeys::I)}},
   {Action(TEXT("Cheer")),TEXT("Cheer"),TEXT("Emotes"),{FInputChord(EKeys::O)}},
   {Action(TEXT("Cry")),TEXT("Cry"),TEXT("Emotes"),{FInputChord(EKeys::U)}},
   {EKeys::Zero,TEXT("Create shortcut"),TEXT("UI"),{FInputChord(EKeys::Zero),FInputChord(EKeys::Zero,false,true,false,false)}},
  };
  static const FKey Digits[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine};
  static const TCHAR* Labels[]={TEXT("Shortcut 1"),TEXT("Shortcut 2"),TEXT("Shortcut 3"),TEXT("Shortcut 4"),TEXT("Shortcut 5"),TEXT("Shortcut 6"),TEXT("Shortcut 7"),TEXT("Shortcut 8"),TEXT("Shortcut 9"),TEXT("Shortcut 10"),TEXT("Shortcut 11"),TEXT("Shortcut 12"),TEXT("Shortcut 13"),TEXT("Shortcut 14"),TEXT("Shortcut 15"),TEXT("Shortcut 16"),TEXT("Shortcut 17"),TEXT("Shortcut 18")};
  for(int32 I=0;I<18;++I)
  {
   TArray<FInputChord> Keys;
   if(I<9){Keys.Add(FInputChord(Digits[I]));Keys.Add(FInputChord(Digits[I],false,true,false,false));}
   else Keys.Add(FInputChord(Digits[I-9],false,false,true,false));
   Result.Emplace(Shortcut(I),Labels[I],TEXT("Combat"),MoveTemp(Keys));
  }
  static const TCHAR* SpellLabels[]={TEXT("Magic: spell 1"),TEXT("Magic: spell 2"),TEXT("Magic: spell 3"),TEXT("Magic: spell 4"),TEXT("Magic: spell 5"),TEXT("Magic: spell 6"),TEXT("Magic: spell 7"),TEXT("Magic: spell 8"),TEXT("Magic: spell 9")};
  for(int32 I=0;I<9;++I)Result.Emplace(SpellSlot(I),SpellLabels[I],TEXT("Combat"),TArray<FInputChord>{FInputChord(Digits[I])},8);
  Result.Append(ACERetailAdditionalActions());
  return Result;
 }();
 return List;
}
static int32 Context(FKey Key)
{
 static const TMap<FKey,int32> Contexts=[] {TMap<FKey,int32> Map;for(const auto& A:Actions())Map.Add(A.Key,A.Context);return Map;}();
 return Contexts.FindRef(Key);
}
static bool Active(FKey Key){const int32 Mode=Context(Key);return !Mode||Mode==ActiveContext;}
static void Load()
{
 if(bLoaded)return; bLoaded=true;
 ApplicationsKey(); // Register imported physical keys before indexing saved chords.
 TSet<FKey> SavedActions;
 for(const auto& A:Actions())
 {
  auto& Bindings=Live.Add(A.Key,A.DefaultBindings); Bindings.SetNum(3);
  for(int32 S=0;S<3;++S)
  {
   FString Text; if(!GConfig->GetString(TEXT("ACE.InputBindings"),*FString::Printf(TEXT("%s.%d"),*A.Key.ToString(),S),Text,GGameUserSettingsIni))continue;
   TArray<FString> Fields; Text.ParseIntoArray(Fields,TEXT("|"),false);
   if(Fields.Num()==5) {SavedActions.Add(A.Key); Bindings[S]=FInputChord(FKey(*Fields[0]),Fields[1]==TEXT("1"),Fields[2]==TEXT("1"),Fields[3]==TEXT("1"),Fields[4]==TEXT("1"));}
  }
 }
 // Newly exposed actions must not steal an existing player customization.
 for(auto& Pair:Live)if(!SavedActions.Contains(Pair.Key))for(auto& Chord:Pair.Value)
  for(FKey Saved:SavedActions)if(Context(Pair.Key)==Context(Saved) && Chord.Key.IsValid() && Live[Saved].Contains(Chord))Chord=FInputChord();
 IndexBindings();
 TArray<FString> Blocks;GConfig->GetArray(TEXT("ACE.InputBindings"),TEXT("Blocked"),Blocks,GGameUserSettingsIni);
 for(const auto& Text:Blocks)
 {
  TArray<FString> F;Text.ParseIntoArray(F,TEXT("|"),false);
  if(F.Num()==7)LiveBlocked.Add({F[0],FInputChord(FKey(*F[1]),F[2]==TEXT("1"),F[3]==TEXT("1"),F[4]==TEXT("1"),F[5]==TEXT("1")),FCString::Atoi(*F[6])});
 }
}
static int32 Specificity(const FInputChord& C){return C.bShift+C.bCtrl+C.bAlt+C.bCmd;}
static bool Allows(const FInputChord& C,const FInputChord& Input)
{
 return C.Key.IsValid() && C.Key==Input.Key && (!C.bShift||Input.bShift)
  && (!C.bCtrl||Input.bCtrl) && (!C.bAlt||Input.bAlt) && (!C.bCmd||Input.bCmd);
}
bool Matches(FKey Key,const FInputChord& Input)
{
 Load();if(bEditing||!Active(Key))return false;
 const auto* Bindings=Live.Find(Key);if(!Bindings)return Input.Key==Key;
 for(const auto& C:*Bindings)if(Allows(C,Input))
 {
  bool MoreSpecific=false;
  for(const auto& B:LiveBlocked)if((!B.Context||B.Context==ActiveContext)&&Allows(B.Chord,Input)
   && (Specificity(B.Chord)>Specificity(C) || (Specificity(B.Chord)==Specificity(C) && (B.Context||!Context(Key)))))MoreSpecific=true;
  // Alt+A must strafe rather than also turn. Shift+W still moves while the
  // separate run/walk modifier is held; only an actual chord takes precedence.
  if(const auto* Candidates=PhysicalBindings.Find(Input.Key))for(const auto& Other:*Candidates)if(Active(Other.ActionKey))
   if(Allows(Other.Chord,Input) && (Specificity(Other.Chord)>Specificity(C)
    || (Specificity(Other.Chord)==Specificity(C) && Context(Other.ActionKey)>0 && Context(Key)==0)))MoreSpecific=true;
  if(!MoreSpecific)return true;
 }
 return false;
}
static bool Check(const APlayerController* PC,FKey Key,bool bPressed)
{
 Load(); if(!PC || bEditing)return false;
 const auto* Bindings=Live.Find(Key);
 if(!Bindings)return bPressed ? PC->WasInputKeyJustPressed(Key) : PC->IsInputKeyDown(Key);
 const bool Shift=PC->IsInputKeyDown(EKeys::LeftShift)||PC->IsInputKeyDown(EKeys::RightShift);
 const bool Ctrl=PC->IsInputKeyDown(EKeys::LeftControl)||PC->IsInputKeyDown(EKeys::RightControl);
 const bool Alt=PC->IsInputKeyDown(EKeys::LeftAlt)||PC->IsInputKeyDown(EKeys::RightAlt);
 const bool Cmd=PC->IsInputKeyDown(EKeys::LeftCommand)||PC->IsInputKeyDown(EKeys::RightCommand);
 for(const auto& C:*Bindings)
  if(C.Key.IsValid() && (bPressed?PC->WasInputKeyJustPressed(C.Key):PC->IsInputKeyDown(C.Key))
   && (bPressed || (C.Key!=EKeys::MouseScrollUp && C.Key!=EKeys::MouseScrollDown))
   && Matches(Key,FInputChord(C.Key,Shift,Ctrl,Alt,Cmd)))return true;
 return false;
}
bool Down(const APlayerController* PC,FKey Key){return Check(PC,Key,false);}
bool Pressed(const APlayerController* PC,FKey Key){return Check(PC,Key,true);}
void BeginEdit(){Load();Draft=Live;DraftBlocked=LiveBlocked;bEditing=true;}
void Reload(){bLoaded=false;bEditing=false;ActiveContext=1;Live.Reset();Draft.Reset();LiveBlocked.Reset();DraftBlocked.Reset();Load();}
void Defaults(){DraftBlocked.Reset();for(const auto& A:Actions()){auto& B=Draft.FindOrAdd(A.Key);B=A.DefaultBindings;B.SetNum(3);}}
void Revert(){Draft=Live;DraftBlocked=LiveBlocked;}
void Cancel(){Revert();bEditing=false;}
void ReplaceBlockedBindings(const TSet<FString>& Groups,const TArray<FBlockedBinding>& Bindings)
{
 if(!bEditing)return;
 DraftBlocked.RemoveAll([&](const FBlockedBinding& B){return Groups.Contains(B.Group);});
 DraftBlocked.Append(Bindings);
}
const TArray<FBlockedBinding>& GetBlockedBindings(){Load();return bEditing?DraftBlocked:LiveBlocked;}
bool IsEditing(){return bEditing;}
FInputChord Get(FKey Key,int32 Slot){Load();const auto* B=(bEditing?Draft:Live).Find(Key);return B&&B->IsValidIndex(Slot)?(*B)[Slot]:FInputChord();}
void Set(FKey Key,int32 Slot,FInputChord Chord)
{
 if(!bEditing||Slot<0||Slot>2)return;
 if(Chord.Key.IsValid())DraftBlocked.RemoveAll([&](const FBlockedBinding& B){return B.Context==Context(Key)&&B.Chord==Chord;});
 if(Chord.Key.IsValid())for(auto& Pair:Draft)if(Context(Pair.Key)==Context(Key))for(auto& C:Pair.Value)if(C==Chord)C=FInputChord();
 auto& B=Draft.FindOrAdd(Key);B.SetNum(3);B[Slot]=Chord;
}
void Commit()
{
 if(!bEditing)return; Live=Draft;LiveBlocked=DraftBlocked;bEditing=false;
 IndexBindings();
 for(const auto& Pair:Live)for(int32 S=0;S<Pair.Value.Num();++S)
 {
  const auto& C=Pair.Value[S]; const FString Value=FString::Printf(TEXT("%s|%d|%d|%d|%d"),*C.Key.ToString(),C.bShift,C.bCtrl,C.bAlt,C.bCmd);
  GConfig->SetString(TEXT("ACE.InputBindings"),*FString::Printf(TEXT("%s.%d"),*Pair.Key.ToString(),S),*Value,GGameUserSettingsIni);
 }
 TArray<FString> Blocks;
 for(const auto& B:LiveBlocked){const auto& C=B.Chord;Blocks.Add(FString::Printf(TEXT("%s|%s|%d|%d|%d|%d|%d"),*B.Group,*C.Key.ToString(),C.bShift,C.bCtrl,C.bAlt,C.bCmd,B.Context));}
 GConfig->SetArray(TEXT("ACE.InputBindings"),TEXT("Blocked"),Blocks,GGameUserSettingsIni);
 GConfig->Flush(false,GGameUserSettingsIni);
}
}
