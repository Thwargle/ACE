using ACE.Database.Models.Shard;
using ACE.Server.Command;
using ACE.Server.Command.Handlers;
using ACE.Server.Managers;
using ACE.Server.WorldObjects;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace ACE.Server.Tests;

[TestClass]
public class MyQuestCommandTests
{
    [TestMethod]
    public void LookupIsOptIn()
    {
        Assert.IsFalse(DefaultPropertyManager.DefaultBooleanProperties["quest_lookup_enabled"].Item);
        Assert.AreEqual("The command \"myquest\" is not currently enabled on this server.",
            PlayerCommands.GetQuestLookupReply(null, false, new[] { "FirstFlag" }));
    }

    [TestMethod]
    [DataRow("/myquest firstflag", "FirstFlag completed 1 time.")]
    [DataRow("/myquest FIRSTFLAGREPEAT", "FirstFlagRepeat completed 3 times.")]
    [DataRow("/myquest First", "No completions recorded for First.")]
    [DataRow("/myquest StartedFlag", "No completions recorded for StartedFlag.")]
    [DataRow("/myquest", "Usage: /myquest <questflag>")]
    [DataRow("/myquest FirstFlag extra", "Usage: /myquest <questflag>")]
    public void LookupReadsOneExactFlag(string input, string expected)
    {
        var quests = new QuestManager((Creature)null);
        var registry = quests.GetQuests();
        registry.Add(new CharacterPropertiesQuestRegistry { QuestName = "FirstFlag", NumTimesCompleted = 1 });
        registry.Add(new CharacterPropertiesQuestRegistry { QuestName = "FirstFlagRepeat", NumTimesCompleted = 3 });
        registry.Add(new CharacterPropertiesQuestRegistry { QuestName = "StartedFlag", NumTimesCompleted = 0 });

        CommandManager.ParseCommand(input, out var command, out var arguments);
        Assert.AreEqual("myquest", command);
        Assert.AreEqual(expected, PlayerCommands.GetQuestLookupReply(quests, true, arguments));
        Assert.HasCount(3, registry);
    }
}
