#include "Mods/ACEActionDeckSubsystem.h"
#include "Mods/ACEPluginSubsystem.h"
#include "ACEClientSubsystem.h"
#include "ACEPlayerController.h"
#include "UI/ACEUIGameplayBinder.h"
#include "Engine/GameInstance.h"

bool UACEActionDeckSubsystem::ParseMacro(const FString& Text,TArray<FACEDeckMacroStep>& Steps,FString& Error)
{
    Steps.Reset();Error.Empty();
    auto Fail=[&](const FString& Why){Steps.Reset();Error=Why;return false;};
    if(Text.Len()>MaxMacroLength)return Fail(TEXT("Keep the macro within 8,192 characters."));
    TArray<FString> Lines;Text.ParseIntoArrayLines(Lines,false);
    int32 Commands=0;
    for(int32 I=0;I<Lines.Num();++I)
    {
        const FString Line=Lines[I].TrimStartAndEnd();if(Line.IsEmpty())continue;
        const auto BadLine=[&](const TCHAR* Why){return Fail(FString::Printf(TEXT("Line %d: %s"),I+1,Why));};
        if(Steps.Num()>=128)return Fail(TEXT("Use at most 128 commands and waits per macro."));
        if(Line[0]!='/'&&Line[0]!='@')return BadLine(TEXT("start each chat command with / or @."));
        int32 End=1;while(End<Line.Len()&&!FChar::IsWhitespace(Line[End]))++End;
        if(End==1)return BadLine(TEXT("enter a command after / or @."));
        FACEDeckMacroStep Step;
        if(Line.Mid(1,End-1).Equals(TEXT("wait"),ESearchCase::IgnoreCase))
        {
            const FString Number=Line.Mid(End).TrimStartAndEnd();
            int64 Milliseconds=0;
            if(Number.IsEmpty())return BadLine(TEXT("use /wait followed by milliseconds, such as /wait 30000."));
            for(const TCHAR C:Number)
            {
                if(C<'0'||C>'9')return BadLine(TEXT("wait must be a whole number of milliseconds."));
                Milliseconds=Milliseconds*10+(C-'0');
                if(Milliseconds>3600000)return BadLine(TEXT("wait must be between 0 and 3,600,000 milliseconds."));
            }
            Step.WaitMilliseconds=int32(Milliseconds);
        }
        else
        {
            if(Line.Len()>500)return BadLine(TEXT("keep each chat command within 500 characters."));
            Step.Command=Line;++Commands;
        }
        Steps.Add(MoveTemp(Step));
    }
    if(!Commands)return Fail(TEXT("Add at least one chat command."));
    return true;
}

bool UACEActionDeckSubsystem::BindMacro(int32 Index,const FString& Name,uint32 Icon,const FString& Commands,int32 DelayMs,const FInputChord& Key)
{
    EnsureCharacter();
    if(!Slots.IsValidIndex(Index))return false;
    const FString Title=Name.TrimStartAndEnd();
    if(Title.IsEmpty()||Title.Len()>80){Notice=TEXT("Enter a macro name of 1 to 80 characters.");return false;}
    if((Icon>>24)!=6){Notice=TEXT("Choose an icon from the icon picker.");return false;}
    TArray<FACEDeckMacroStep> Steps;
    if(!ParseMacro(Commands,Steps,Notice))return false;
    if(DelayMs<0||DelayMs>3600000){Notice=TEXT("Command spacing must be 0 to 3,600,000 milliseconds.");return false;}
    const int32 Other=MatchKey(Key);
    if(!ValidKey(Key.Key)){Notice=TEXT("Choose a keyboard hotkey.");return false;}
    if(Other!=INDEX_NONE&&Other!=Index){Notice=TEXT("That hotkey belongs to another Action Deck slot.");return false;}
    const FACEDeckSlot Previous=Slots[Index];
    FACEDeckSlot S;S.Action=EACEDeckAction::Macro;S.Item.Name=Title;S.Item.Icon=int32(Icon);
    S.MacroText=Commands;S.MacroDelayMs=DelayMs;S.Key=Key;
    S.CustomKey=Previous.CustomKey||Key!=EffectiveKey(Index);
    Slots[Index]=MoveTemp(S);
    if(!Save()){Slots[Index]=Previous;return false;}
    Notice.Empty();Refresh(true);return true;
}

void UACEActionDeckSubsystem::StopMacro()
{
    MacroSteps.Reset();MacroStep=0;MacroDue=0;MacroSession.Reset();RunningMacroName.Empty();
}

FString UACEActionDeckSubsystem::MacroStatus() const
{
    if(!IsMacroRunning())return Notice;
    const int32 Seconds=FMath::CeilToInt(FMath::Max(0.,MacroDue-FPlatformTime::Seconds()));
    return RunningMacroName+(Seconds>0?FString::Printf(TEXT(" · waiting %ds"),Seconds):TEXT(" · running"));
}

bool UACEActionDeckSubsystem::TickMacros(float)
{
    if(IsMacroEditorOpen()&&(!Client||Client->GetSessionState()!=EACESessionState::InWorld
        ||Client->GetPlayerGuid()!=Player||Client->GetServerName()!=Server||!IsEnabled()))CloseMacroEditor();
    AdvanceMacro(FPlatformTime::Seconds());return true;
}

void UACEActionDeckSubsystem::AdvanceMacro(double Now)
{
    if(!IsMacroRunning())return;
    const auto State=Client?Client->GetSessionState():EACESessionState::Disconnected;
    if(!Client||!IsEnabled()||MacroSession.Pin()!=Client->GetSession()
        ||Client->GetPlayerGuid()!=MacroPlayer||Client->GetServerName()!=MacroServer
        ||(State!=EACESessionState::InWorld&&State!=EACESessionState::EnteringWorld))
    {StopMacro();Notice=TEXT("Macro stopped: character or connection changed.");return;}
    if(Now<MacroDue)return;
    auto* PC=Cast<AACEPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    // A recall may enter portal space during /wait. Keep the wall-clock delay,
    // but do not send its next command until the same character is ready again.
    if(State!=EACESessionState::InWorld||Client->IsInPortalSpace()||(PC&&PC->IsWorldTransitionActive()))return;
    bool Sent=false;
    while(MacroStep<MacroSteps.Num())
    {
        const FACEDeckMacroStep Step=MacroSteps[MacroStep];
        if(!Step.Command.IsEmpty())
        {
            if(Sent)return; // At most one chat send per tick, including after a long frame.
            ++MacroStep;
            auto* B=Binder();
            if(!B||!B->DispatchActionDeckCommand(Step.Command))
            {StopMacro();Notice=TEXT("Macro stopped: command could not be dispatched.");return;}
            // A command may close or disable the plugin or log out synchronously.
            if(!IsMacroRunning())return;
            Sent=true;MacroDue=Now+double(MacroSpacingMs)/1000.;
        }
        else
        {
            ++MacroStep;MacroDue=FMath::Max(Now,MacroDue)+double(Step.WaitMilliseconds)/1000.;
        }
        if(MacroDue>Now)return;
    }
    StopMacro();Notice=TEXT("Macro complete.");
}
