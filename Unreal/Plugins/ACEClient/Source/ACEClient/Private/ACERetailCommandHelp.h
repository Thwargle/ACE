#pragma once
#include "CoreMinimal.h"

// Public end-of-retail registry: ClientCommunicationSystem::InitializeCommands
// and StartupTurbineChatSystem. Groups are help topics, not executable commands.
// Keep server-defined commands outside this registry so they still reach Talk.
namespace ACERetailCommandHelp
{
struct FEntry { const TCHAR* Names; const TCHAR* Help; };
inline constexpr FEntry Entries[]={
 {TEXT("? help"),TEXT("@help <command or topic> — command syntax. Topics: commands, allegiances, channels, chatting, death, status, text. Both @ and / prefixes work.")},
 {TEXT("commands"),TEXT("Retail commands: @allegiance @motd @speaker @join @leave @chat @notell @reply @mr @pr @retell @say @tell @afk @consent @corpse @die @lifestone @marketplace @permit @pkarena @pklarena @emote @emotes @fillcomps @loadfile @friends @house @hslist @squelch @unsquelch @messagetypes @age @birth @day @endurance @framerate @loc @pklite @render @version @saveui @loadui @saveautoui @loadautoui @lockui @clear @filter @unfilter @log @title @index @clist @on @off.\nChat shortcuts: @a @general @trade @lfg @roleplay @society @olthoi @f @ab @c @m @p @v. Use @help <command> for syntax and @acuehelp for AC:Unreal tools.")},
 {TEXT("allegiances"),TEXT("Allegiance commands: @allegiance @motd @ab @ah @m @p @v @c. Use @help allegiance for administration and @help channels for chat.")},
 {TEXT("channels"),TEXT("@a (allegiance), @general, @trade, @lfg, @roleplay, @society, @olthoi: followed by message text. @join/@leave <channel> controls listening. @f (fellowship), @ab (allegiance broadcast), @m (monarch), @p (patron), @v (vassals), @c (co-vassals) send one message without changing the chat destination. Legacy channel controls: @index, @clist, @on, @off.")},
 {TEXT("chatting"),TEXT("Chat commands: @say @tell @reply @mr @pr @retell @afk @emote @emotes @friends @squelch @unsquelch @chat @notell. Use @help <command> for syntax.")},
 {TEXT("death"),TEXT("Death and recall commands: @consent @permit @corpse @die @lifestone @marketplace @pkarena @pklarena. Use @help <command> for details.")},
 {TEXT("status"),TEXT("Status commands: @age @birth @day @endurance @framerate @loc @pklite @render @version. Use @help <command> for details.")},
 {TEXT("text"),TEXT("Text commands: @clear [all], @filter/@unfilter <message type>, @messagetypes, @title <title>, @log [filename], @loadfile <filename>.")},
 {TEXT("allegiance all"),TEXT("@allegiance boot [-account] <name> | info <name> | hometown\n@allegiance chat on|off|gag <name>|ungag <name>|kick <name>[,reason] | broadcast <message>\n@allegiance ban list|add <name>|remove <name>\n@allegiance officer [list|clear|set <1-3> <name>|remove <name>]\n@allegiance title [list|clear|set <1-3> <title>]\n@allegiance motd [set <message>|clear] | name [set <name>|clear]\n@allegiance lock [on|off|toggle|check|bypass [clear|name]]\n@allegiance house [guest open|close|storage open|close]. Aliases: all, ch, br, ho.")},
 {TEXT("motd"),TEXT("@motd — display the allegiance message of the day. @motd set <message> or @motd clear requires allegiance permission.")},
 {TEXT("speaker"),TEXT("This command is no longer in use. Use @allegiance officer; see @help allegiance.")},
 {TEXT("alh ah"),TEXT("@ah or @alh — recall to your allegiance hometown. Takes no arguments; the server determines eligibility.")},
 {TEXT("a guild gu"),TEXT("@a <message> — allegiance channel. Aliases: guild, gu. Enable with @join allegiance.")},
 {TEXT("general cg"),TEXT("@general <message> — General channel. Alias: cg. Enable with @join general.")},
 {TEXT("trade ct"),TEXT("@trade <message> — Trade channel. Alias: ct. Enable with @join trade.")},
 {TEXT("lfg clfg"),TEXT("@lfg <message> — Looking For Group channel. Alias: clfg. Enable with @join lfg.")},
 {TEXT("roleplay crp"),TEXT("@roleplay <message> — Roleplay channel. Alias: crp. Enable with @join roleplay. @rp is reply, as in retail.")},
 {TEXT("society soc"),TEXT("@society <message> — your society channel. Alias: soc. Enable with @join society.")},
 {TEXT("olthoi o"),TEXT("@olthoi <message> — Olthoi channel. Alias: o. Enable with @join olthoi.")},
 {TEXT("ab"),TEXT("@ab <message> — broadcast to the allegiance. Equivalent to @allegiance broadcast <message>.")},
 {TEXT("co-vassals covassals covassal c"),TEXT("@c <message> — broadcast to your co-vassals. Aliases: co-vassals, covassals, covassal.")},
 {TEXT("fellowship fellows fellow f group g party"),TEXT("@f <message> — fellowship chat. Aliases: fellowship, fellows, fellow, group, g, party.")},
 {TEXT("monarch m"),TEXT("@m <message> — send monarch chat. Alias: monarch. @mr replies to the last monarch-channel sender.")},
 {TEXT("patron p"),TEXT("@p <message> — send patron chat. Alias: patron. @pr replies to the last patron-channel sender.")},
 {TEXT("vassals vassal v"),TEXT("@v <message> — broadcast to your vassals. Aliases: vassals, vassal.")},
 {TEXT("join leave"),TEXT("@join <channel> / @leave <channel> — enable/disable general, trade, lfg, roleplay, society, allegiance or olthoi chat.")},
 {TEXT("chat notell"),TEXT("@chat on|off — enable/disable local speech. @notell on|off — block/allow private tells.")},
 {TEXT("reply r rp mr pr"),TEXT("@reply <message> — reply to the last tell; aliases r, rp. @mr/@pr reply to the last monarch/patron-channel sender. With no message, prepare the reply in the chat input.")},
 {TEXT("retell rt"),TEXT("@retell <message> — send to the last person you told. Alias: rt.")},
 {TEXT("say s"),TEXT("@say <message> — local speech, regardless of the selected chat destination. Alias: s.")},
 {TEXT("tell t send whisper w"),TEXT("@tell <name>, <message> — private tell. Keep the comma after the character name. Aliases: t, send, whisper, w.")},
 {TEXT("afk"),TEXT("@afk [on|off] — enable/disable away mode; no argument enables it. @afk msg <message> changes the auto-reply (191 characters plus newline); @afk msg resets it.")},
 {TEXT("consent"),TEXT("@consent on|off — accept/refuse corpse-looting permissions. @consent who lists them; @consent remove <name> removes one; @consent clear removes all. The server enforces ownership and PK restrictions.")},
 {TEXT("permit"),TEXT("@permit add <name> / @permit remove <name> — grant/revoke another player's permission to loot your corpse. Use @consent to manage permissions others grant you.")},
 {TEXT("corpse cor"),TEXT("@corpse — display the server's last outdoor corpse location. Alias: cor. Indoor deaths do not replace this location; it does not guarantee the corpse still exists.")},
 {TEXT("die"),TEXT("@die — ask to kill your character. Confirm Yes to proceed; items and vitae may be lost. Takes no arguments.")},
 {TEXT("lifestone lif ls"),TEXT("@lifestone — recall to your lifestone. Aliases: lif, ls. Takes no arguments; the server determines eligibility.")},
 {TEXT("marketplace mar mp"),TEXT("@marketplace — recall to the Marketplace. Aliases: mar, mp. Takes no arguments.")},
 {TEXT("pkarena pka"),TEXT("@pkarena — recall to the PK arena while Player Killer. Alias: pka. Takes no arguments; the server enforces recall restrictions.")},
 {TEXT("pklarena pla"),TEXT("@pklarena — recall to the PK Lite arena while PK Lite. Alias: pla. Takes no arguments; the server enforces recall restrictions.")},
 {TEXT("e em emote me"),TEXT("@emote <text> — describe an action. Aliases: e, em, me; : and ; also introduce emotes. Use @emotes for DAT animated poses.")},
 {TEXT("emotes"),TEXT("@emotes — list the animated poses available in your DAT. Enter *pose* to perform one.")},
 {TEXT("fillcomps"),TEXT("@fillcomps [maximum cost] — add missing spell components to the open vendor buy list using your refill quantities. Review the list and choose Buy All.")},
 {TEXT("loadfile"),TEXT("@loadfile <filename> — run commands and chat lines from a text file. %DATE% expands to the current date. Quote paths containing spaces.")},
 {TEXT("friends friends_add friends_remove"),TEXT("@friends [online] — list friends. @friends add <name> / @friends remove <name> / @friends remove -all. Aliases: friends_add <name>, friends_remove <name>. @friends old uses the legacy server friends request.")},
 {TEXT("house hou"),TEXT("@house recall|re — recall home; mansion_recall|alleg_recall|ma — allegiance housing.\n@house open|close | guest add|remove <name> | guest list|show|remove_all|add_allegiance|remove_allegiance\n@house storage add|remove <name or -all> | storage list|show|remove_all|add_allegiance|remove_allegiance\n@house boot|remove <name or -all> | boot_all|remove_all | hooks on|off\n@house available apartment|cottage|villa|mansion | abandon (two confirmations). Alias: hou. @house alone displays help.")},
 {TEXT("hslist"),TEXT("@hslist apartment|cottage|villa|mansion — ask the server for available housing of that type.")},
 {TEXT("hor hr"),TEXT("@hr — recall to your house. Alias: hor. Takes no arguments.")},
 {TEXT("hom hoa"),TEXT("@hom — recall to your allegiance housing. Alias: hoa. Takes no arguments.")},
 {TEXT("squelch unsquelch"),TEXT("@squelch / @unsquelch [-account] [-reply] [-messageType] <name>. With no arguments, display the squelch list. @messagetypes lists message types.")},
 {TEXT("messagetypes message_types msgtypes msg_types"),TEXT("@messagetypes — list message categories used by @filter, @unfilter and @squelch. Aliases: message_types, msgtypes, msg_types.")},
 {TEXT("age birth"),TEXT("@age — ask the server for time played. @birth — ask for the character's creation date.")},
 {TEXT("day"),TEXT("@day — toggle always-daylight outdoor lighting.")},
 {TEXT("endurance"),TEXT("@endurance — explain Endurance's effects on regeneration, stamina costs and natural resistance.")},
 {TEXT("framerate"),TEXT("@framerate — toggle the in-game frame-rate display.")},
 {TEXT("loc"),TEXT("@loc — display your cell, local coordinates and orientation.")},
 {TEXT("pklite pkl"),TEXT("@pklite — request Player Killer Lite status. Alias: pkl. The server determines eligibility.")},
 {TEXT("render"),TEXT("@render fov <10-160> — set field of view in degrees. @render radius <5-25> — set landscape landblock radius; native Android retains its memory limit. Settings persist.")},
 {TEXT("version"),TEXT("@version — display the current AC:Unreal client version and platform.")},
 {TEXT("saveui loadui saveautoui loadautoui lockui"),TEXT("@saveui [file] / @loadui [file] — save/load a UI layout. @saveautoui / @loadautoui use the automatic layout. @lockui toggles layout locking. Quote filenames containing spaces.")},
 {TEXT("clear"),TEXT("@clear — clear the chat window the command came from. @clear all clears all chat windows.")},
 {TEXT("filter unfilter"),TEXT("@filter / @unfilter -<message type> — globally hide/show categories, for example @filter -Combat -Magic. No arguments lists filters; @messagetypes lists categories. Per-window filters are in Chat Settings.")},
 {TEXT("log"),TEXT("@log <filename> — append chat to a text file. @log with no argument stops logging. Relative paths use Saved/Logs; quote paths containing spaces.")},
 {TEXT("title"),TEXT("@title <title> — name the source floating chat window (up to 100 characters). Main chat cannot be renamed.")},
 {TEXT("index clist on off"),TEXT("Legacy channel commands: @index lists channels; @clist <channel> lists members; @on <channel> / @off <channel> joins/leaves. Modern channels use @join/@leave.")}
};
inline const FEntry* Find(const FString& Topic)
{
    for (const auto& Entry:Entries)
    {
        TArray<FString> Names;FString(Entry.Names).ParseIntoArrayWS(Names);
        if (Names.Contains(Topic.ToLower())) return &Entry;
    }
    return nullptr;
}
}
