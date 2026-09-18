#include "ACEInputBindings.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"
namespace ACEInputBindings
{
static TMap<FKey,TArray<FInputChord>> Live, Draft;
static bool bLoaded=false, bEditing=false;
const TArray<FAction>& Actions()
{
 static const TArray<FAction> List={
  {EKeys::W,TEXT("Move forward"),TEXT("Movement")},{EKeys::S,TEXT("Move backward"),TEXT("Movement")},
  {EKeys::A,TEXT("Turn left"),TEXT("Movement")},{EKeys::D,TEXT("Turn right"),TEXT("Movement")},
  {EKeys::Q,TEXT("Sidestep left"),TEXT("Movement")},{EKeys::E,TEXT("Sidestep right"),TEXT("Movement")},
  {EKeys::LeftShift,TEXT("Walk"),TEXT("Movement")},{EKeys::SpaceBar,TEXT("Jump"),TEXT("Movement")},
  {EKeys::NumLock,TEXT("Autorun"),TEXT("Movement")},
  {EKeys::NumPadFour,TEXT("Rotate camera left"),TEXT("Camera")},{EKeys::NumPadSix,TEXT("Rotate camera right"),TEXT("Camera")},
  {EKeys::NumPadEight,TEXT("Raise camera"),TEXT("Camera")},{EKeys::NumPadTwo,TEXT("Lower camera"),TEXT("Camera")},
  {EKeys::Add,TEXT("Zoom in"),TEXT("Camera")},{EKeys::Subtract,TEXT("Zoom out"),TEXT("Camera")},
  {EKeys::Tilde,TEXT("Combat mode"),TEXT("Combat")},{EKeys::Home,TEXT("Last attacker"),TEXT("Combat")},
  {EKeys::LeftBracket,TEXT("Previous nearby object"),TEXT("UI")},{EKeys::RightBracket,TEXT("Next nearby object"),TEXT("UI")},
  {EKeys::Semicolon,TEXT("Previous nearby enemy"),TEXT("Combat")},{EKeys::Apostrophe,TEXT("Next nearby enemy"),TEXT("Combat")},
  {EKeys::F,TEXT("Use / sort inventory"),TEXT("UI")},{EKeys::I,TEXT("Inventory"),TEXT("UI")},
  {EKeys::P,TEXT("Attributes and skills"),TEXT("CharacterSettings")},{EKeys::M,TEXT("Magic"),TEXT("UI")},
  {EKeys::U,TEXT("Contract journal"),TEXT("UI")},{EKeys::O,TEXT("Options"),TEXT("UI")},
  {EKeys::G,TEXT("Sit"),TEXT("Emotes")},{EKeys::B,TEXT("Lie down"),TEXT("Emotes")},
  {EKeys::C,TEXT("Crouch"),TEXT("Emotes")},{EKeys::K,TEXT("Point"),TEXT("Emotes")},{EKeys::J,TEXT("Wave"),TEXT("Emotes")}};
 return List;
}
static void Load()
{
 if(bLoaded)return; bLoaded=true;
 for(const auto& A:Actions())
 {
  auto& Bindings=Live.Add(A.Key); Bindings.SetNum(3); Bindings[0]=FInputChord(A.Key);
  for(int32 S=0;S<3;++S)
  {
   FString Text; if(!GConfig->GetString(TEXT("ACE.InputBindings"),*FString::Printf(TEXT("%s.%d"),*A.Key.ToString(),S),Text,GGameUserSettingsIni))continue;
   TArray<FString> Fields; Text.ParseIntoArray(Fields,TEXT("|"),false);
   if(Fields.Num()==5) Bindings[S]=FInputChord(FKey(*Fields[0]),Fields[1]==TEXT("1"),Fields[2]==TEXT("1"),Fields[3]==TEXT("1"),Fields[4]==TEXT("1"));
  }
 }
}
static bool Check(const APlayerController* PC,FKey Key,bool bPressed)
{
 Load(); if(!PC || bEditing)return false;
 const auto* Bindings=Live.Find(Key);
 if(!Bindings)return bPressed ? PC->WasInputKeyJustPressed(Key) : PC->IsInputKeyDown(Key);
 for(const auto& C:*Bindings)
  if(C.Key.IsValid() && (!C.bShift||PC->IsInputKeyDown(EKeys::LeftShift)||PC->IsInputKeyDown(EKeys::RightShift))
    && (!C.bCtrl||PC->IsInputKeyDown(EKeys::LeftControl)||PC->IsInputKeyDown(EKeys::RightControl))
    && (!C.bAlt||PC->IsInputKeyDown(EKeys::LeftAlt)||PC->IsInputKeyDown(EKeys::RightAlt))
    && (!C.bCmd||PC->IsInputKeyDown(EKeys::LeftCommand)||PC->IsInputKeyDown(EKeys::RightCommand))
    && (bPressed ? PC->WasInputKeyJustPressed(C.Key) : PC->IsInputKeyDown(C.Key)))return true;
 return false;
}
bool Down(const APlayerController* PC,FKey Key){return Check(PC,Key,false);}
bool Pressed(const APlayerController* PC,FKey Key){return Check(PC,Key,true);}
void BeginEdit(){Load();Draft=Live;bEditing=true;}
void Reload(){bLoaded=false;bEditing=false;Live.Reset();Draft.Reset();Load();}
void Defaults(){for(const auto& A:Actions()){auto& B=Draft.FindOrAdd(A.Key);B.SetNum(3);B[0]=FInputChord(A.Key);B[1]=B[2]=FInputChord();}}
void Revert(){Draft=Live;}
void Cancel(){Draft=Live;bEditing=false;}
bool IsEditing(){return bEditing;}
FInputChord Get(FKey Key,int32 Slot){Load();const auto* B=(bEditing?Draft:Live).Find(Key);return B&&B->IsValidIndex(Slot)?(*B)[Slot]:FInputChord();}
void Set(FKey Key,int32 Slot,FInputChord Chord)
{
 if(!bEditing||Slot<0||Slot>2)return;
 if(Chord.Key.IsValid())for(auto& Pair:Draft)for(auto& C:Pair.Value)if(C==Chord)C=FInputChord();
 auto& B=Draft.FindOrAdd(Key);B.SetNum(3);B[Slot]=Chord;
}
void Commit()
{
 if(!bEditing)return; Live=Draft;bEditing=false;
 for(const auto& Pair:Live)for(int32 S=0;S<Pair.Value.Num();++S)
 {
  const auto& C=Pair.Value[S]; const FString Value=FString::Printf(TEXT("%s|%d|%d|%d|%d"),*C.Key.ToString(),C.bShift,C.bCtrl,C.bAlt,C.bCmd);
  GConfig->SetString(TEXT("ACE.InputBindings"),*FString::Printf(TEXT("%s.%d"),*Pair.Key.ToString(),S),*Value,GGameUserSettingsIni);
 }
 GConfig->Flush(false,GGameUserSettingsIni);
}
}
