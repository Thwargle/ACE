using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.IO;
using System.Linq;
using System.Net;
using System.Numerics;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using ACE.Common;
using ACE.Database;
using ACE.DatLoader;
using ACE.Entity;
using ACE.Entity.Enum;
using ACE.Entity.Enum.Properties;
using ACE.Server.Entity;
using ACE.Server.Entity.Actions;
using ACE.Server.Factories;
using ACE.Server.Managers;
using ACE.Server.Network;
using ACE.Server.Network.GameAction;
using ACE.Server.Network.GameAction.Actions;
using ACE.Server.Network.Managers;
using ACE.Server.WorldObjects;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace ACE.Server.Tests
{
    // Uses stock world templates/DAT and the Unreal desktop drag fixture's
    // emitted action. All character, inventory and quest changes stay in memory.
    [TestClass, DoNotParallelize]
    public class ApostateQuestIntegrationTests
    {
        private const BindingFlags PrivateInstance = BindingFlags.Instance | BindingFlags.NonPublic;
        private const string TurnIn = "NexusCrawlResonatorTurnIn0509";
        private const string Completed = "NexusCrawlCompleted0509";
        private static byte[] giveAction;

        [ClassInitialize]
        public static void Initialize(TestContext context)
        {
            var config = Environment.GetEnvironmentVariable("ACE_TEST_CONFIG");
            var dat = Environment.GetEnvironmentVariable("ACE_TEST_DAT");
            var packet = Environment.GetEnvironmentVariable("ACE_TEST_APOSTATE_PACKET");
            if (string.IsNullOrEmpty(config) || string.IsNullOrEmpty(dat) || !File.Exists(packet))
                Assert.Inconclusive("Set ACE_TEST_CONFIG, ACE_TEST_DAT and ACE_TEST_APOSTATE_PACKET (DesktopHighlights' ApostateGiveObject.bin).");
            Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
            ConfigManager.Initialize(config);
            DatManager.Initialize(dat);
            _ = new ACE.Server.Physics.PhysicsEngine(new ACE.Server.Physics.Common.ObjectMaint(),
                new ACE.Server.Physics.Common.SmartBox()) { Server = true };
            typeof(SocketManager).GetField("listeners", BindingFlags.Static | BindingFlags.NonPublic)
                .SetValue(null, Array.Empty<ConnectionListener>());
            giveAction = File.ReadAllBytes(packet);
            Assert.AreEqual(16, giveAction.Length, "Give action must contain opcode, recipient, item and amount.");
        }

        [TestMethod]
        [DataRow(0f)]
        [DataRow(2f)]
        [DataRow(12f)]
        public void ClientResonatorDropCompletesDepositAndNedReward(float playerHeight)
        {
            using var message = new BinaryReader(new MemoryStream(giveAction));
            Assert.AreEqual((uint)GameActionType.GiveObjectRequest, message.ReadUInt32());
            uint targetGuid = message.ReadUInt32(), itemGuid = message.ReadUInt32();
            Assert.AreEqual(1, message.ReadInt32());
            using var f = new Fixture(targetGuid, itemGuid);
            f.PlacePlayer(playerHeight);
            Console.WriteLine($"Deposit height={f.Deposit.PhysicsObj.GetHeight():F3}, radius={f.Deposit.PhysicsObj.GetRadius():F3}; player elevation={playerHeight:F2}, cylinder distance={f.Player.GetCylinderDistance(f.Deposit):F3}, use radius={f.Deposit.UseRadius}");
            bool inRange = f.Player.IsWithinUseRadiusOf(f.Deposit);
            Assert.AreEqual(playerHeight < 12, inRange, "Vertical separation must participate in reach checks.");
            Assert.IsTrue(f.Resonator.IsAttunedOrContainsAttuned);
            Assert.IsFalse(f.Player.QuestManager.HasQuest(TurnIn));
            Assert.IsFalse(f.Player.IsBusy || f.Player.Teleporting || f.Player.suicideInProgress, "Fixture is ready for a normal interaction.");
            Assert.AreSame(f.Deposit, f.Player.FindObject(targetGuid, Player.SearchLocations.Landblock), "Deposit is known to the player.");
            Assert.AreSame(f.Resonator, f.Player.FindObject(itemGuid, Player.SearchLocations.MyInventory), "Resonator is in the player's inventory.");
            GameActionGiveObjectRequest.Handle(new ClientMessage(giveAction), f.Player.Session);
            if (!inRange)
            {
                f.Pump(1);
                Assert.IsTrue(f.Player.Inventory.ContainsKey(f.Resonator.Guid), "A visible target outside reach cannot consume the item.");
                Assert.IsFalse(f.Player.QuestManager.HasQuest(TurnIn), "Looking down from a ledge cannot bypass reach.");
                // Model the normal client approach arriving at the deposit. The
                // pending original action must finish without resending Give.
                f.PlacePlayer(0);
            }
            f.Pump(8);
            Assert.IsFalse(f.Player.Inventory.ContainsKey(f.Resonator.Guid), "The server must actually remove the handed-in resonator.");
            Assert.IsTrue(f.Resonator.IsDestroyed);
            Assert.IsTrue(f.Player.QuestManager.HasQuest(TurnIn), "The stock deposit emote must stamp the turn-in quest.");
            Assert.IsFalse(f.Player.IsBusy);

            long xp = f.Player.TotalExperience ?? 0, lum = f.Player.AvailableLuminance ?? 0;
            f.Player.HandleActionUseItem(f.Ned.Guid.Full);
            f.Pump(20);
            Assert.IsTrue(f.Player.QuestManager.HasQuest(Completed), "Using Ned must finish the dungeon quest.");
            Assert.IsFalse(f.Player.QuestManager.HasQuest(TurnIn), "Ned must consume the intermediate turn-in flag.");
            Assert.IsFalse(f.Player.QuestManager.HasQuest("NexusCrawlStarted0509"));
            Assert.IsFalse(f.Player.QuestManager.HasQuest("ApostateNexusFlag"));
            Assert.IsTrue(f.Player.TotalExperience > xp, "Completion must award experience, not just print dialogue.");
            Assert.AreEqual(lum + 18000, f.Player.AvailableLuminance, "The stock completion must award luminance.");
            Assert.IsTrue(f.Player.Character.CharacterPropertiesTitleBook.Any(t => t.TitleId == (uint)CharacterTitle.NexusCrawler));
            var completedXp = f.Player.TotalExperience; var completedLum = f.Player.AvailableLuminance;
            f.Player.HandleActionUseItem(f.Ned.Guid.Full); f.Pump(20);
            Assert.AreEqual(completedXp, f.Player.TotalExperience, "Repeated use cannot award the completion twice.");
            Assert.AreEqual(completedLum, f.Player.AvailableLuminance);
            Console.WriteLine($"Completed: consumed resonator, stamped {Completed}, awarded {completedXp - xp} XP and {completedLum - lum} luminance; repeat use awarded nothing.");
        }

        private sealed class Fixture : IDisposable
        {
            public readonly Player Player;
            public readonly Creature Deposit, Ned;
            public readonly WorldObject Resonator;
            private readonly List<WorldObject> objects = new();
            private readonly ActionQueue queue, playerQueue;
            private readonly double originalTicks = Timers.PortalYearTicks;
            private readonly SerializedShardDatabase previousShard = DatabaseManager.Shard;
            private readonly SerializedShardDatabase inertShard;

            public Fixture(uint targetGuid, uint itemGuid)
            {
                // Inventory hand-ins queue persistence as part of their normal
                // path. Capture those requests without a database or worker.
                inertShard = (SerializedShardDatabase)Activator.CreateInstance(typeof(SerializedShardDatabase),
                    BindingFlags.Instance | BindingFlags.NonPublic, null, new object[] { null }, null);
                typeof(DatabaseManager).GetProperty("Shard").SetValue(null, inertShard);
                Player = new Player(DatabaseManager.World.GetCachedWeenie(1), new ObjectGuid(0x5ffffffb), 0);
                playerQueue = (ActionQueue)typeof(Player).GetField("actionQueue", PrivateInstance).GetValue(Player);
                Player.Name = "Apostate quest regression"; Player.Level = 200;
                Player.MaximumLuminance = 1000000; Player.AvailableLuminance = 0;
                var session = new Session(null, new IPEndPoint(IPAddress.Loopback, 0), 0, 1);
                typeof(Session).GetProperty("Account").SetValue(session, "apostate-regression");
                typeof(Session).GetProperty("Player").SetValue(session, Player);
                typeof(Player).GetField("<Session>k__BackingField", PrivateInstance).SetValue(Player, session);
                Deposit = (Creature)WorldObjectFactory.CreateWorldObject(DatabaseManager.World.GetCachedWeenie(40809), new ObjectGuid(targetGuid));
                // Omit only cosmetic equipment generation, which allocates shard
                // GUIDs; the stock NPC's use and reward emotes remain intact.
                var nedTemplate = System.Text.Json.JsonSerializer.Deserialize<ACE.Entity.Models.Weenie>(
                    System.Text.Json.JsonSerializer.Serialize(DatabaseManager.World.GetCachedWeenie(40807)));
                nedTemplate.PropertiesCreateList?.Clear(); nedTemplate.PropertiesDID.Remove(PropertyDataId.WieldedTreasureType);
                Ned = (Creature)WorldObjectFactory.CreateWorldObject(nedTemplate, new ObjectGuid(0x70001007));
                var block = new Landblock(new LandblockId(0x7F020107));
                queue = (ActionQueue)typeof(Landblock).GetField("actionQueue", PrivateInstance).GetValue(block);
                var worldObjects = (Dictionary<ObjectGuid, WorldObject>)typeof(Landblock).GetField("worldObjects", PrivateInstance).GetValue(block);
                foreach (var obj in new WorldObject[] { Player, Deposit, Ned })
                {
                    objects.Add(obj);
                    obj.Location = new Position(0x7F020107, 40, -20, .055f, 0, 0, 0, 1);
                    obj.InitPhysicsObj();
                    obj.PhysicsObj.Position = new ACE.Server.Physics.Common.Position(obj.Location);
                    worldObjects.Add(obj.Guid, obj);
                    typeof(WorldObject).GetProperty("CurrentLandblock").SetValue(obj, block);
                }
                // Begin at the dungeon hand-in with the ordinary quest starter
                // state and item. Do not run NPC item generation/GUID allocation.
                Resonator = WorldObjectFactory.CreateWorldObject(DatabaseManager.World.GetCachedWeenie(40808), new ObjectGuid(itemGuid));
                objects.Add(Resonator);
                Assert.IsTrue(Player.TryAddToInventory(Resonator, burdenCheck: false));
                Player.QuestManager.Stamp("NexusCrawlStarted0509");
                Player.QuestManager.Stamp("ApostateNexusFlag");
            }

            public void PlacePlayer(float height)
            {
                Player.Location.Pos = Deposit.Location.Pos + new Vector3(0, -2.5f, height);
                Player.PhysicsObj.Position.Frame.Origin = Player.Location.Pos;
            }

            public void Pump(int seconds)
            {
                for (int tick = 0; tick < seconds * 20; ++tick)
                {
                    typeof(Timers).GetProperty("PortalYearTicks").SetValue(null, Timers.PortalYearTicks + .05);
                    WorldManager.DelayManager.RunActions(); playerQueue.RunActions(); queue.RunActions(); WorldManager.ActionQueue.RunActions();
                }
            }

            public void Dispose()
            {
                playerQueue.Clear(); queue.Clear(); WorldManager.ActionQueue.Clear();
                foreach (var obj in objects.AsEnumerable().Reverse())
                {
                    obj.PhysicsObj?.DestroyObject();
                }
                typeof(DatabaseManager).GetProperty("Shard").SetValue(null, previousShard);
                Assert.IsNull(typeof(SerializedShardDatabase).GetField("_workerThread", PrivateInstance).GetValue(inertShard));
                var writes = (BlockingCollection<Task>)typeof(SerializedShardDatabase).GetField("_queue", PrivateInstance).GetValue(inertShard);
                while (writes.TryTake(out var write))
                    Assert.AreEqual(TaskStatus.Created, write.Status, "No queued character/item persistence may execute.");
                typeof(Timers).GetProperty("PortalYearTicks").SetValue(null, originalTicks);
            }
        }
    }
}
