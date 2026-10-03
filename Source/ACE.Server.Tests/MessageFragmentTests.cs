using System;
using System.Threading.Tasks;

using ACE.Server.Network;
using ACE.Server.Network.GameMessages;

using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace ACE.Server.Tests
{
    [TestClass]
    public class MessageFragmentTests
    {
        private sealed class BroadcastMessage : GameMessage
        {
            public BroadcastMessage(byte[] payload) : base(GameMessageOpcode.None, GameMessageGroup.UIQueue)
            {
                Writer.Write(payload);
            }
        }

        private static byte[] Payload(int length)
        {
            var bytes = new byte[length];
            for (int i = 0; i < length; i++)
                bytes[i] = (byte)(i % 251 + 1);
            return bytes;
        }

        private static void VerifyFragment(ServerPacketFragment fragment, byte[] payload, uint sequence, int count)
        {
            int offset = fragment.Header.Index * PacketFragment.MaxFragmentDataSize;
            int length = Math.Min(PacketFragment.MaxFragmentDataSize, payload.Length - offset);
            Assert.AreEqual(length, fragment.Data.Length);
            Assert.IsTrue(fragment.Data.AsSpan().SequenceEqual(payload.AsSpan(offset, length)), "Fragment payload was corrupted.");
            Assert.AreEqual(sequence, fragment.Header.Sequence);
            Assert.AreEqual((ushort)count, fragment.Header.Count);
            Assert.AreEqual((ushort)GameMessageGroup.UIQueue, fragment.Header.Queue);
            Assert.AreEqual(0x80000000u, fragment.Header.Id);
        }

        [TestMethod]
        public void FragmentationPreservesPayloadHeadersAndSharedStreamPosition()
        {
            int size = PacketFragment.MaxFragmentDataSize;
            foreach (int length in new[] { 0, 1, size - 1, size, size + 1, size * 2, size * 2 + 1, 2048 })
            {
                var payload = Payload(length);
                var message = new BroadcastMessage(payload);
                message.Data.Position = 0;
                var fragments = new MessageFragment(message, 42);
                int count = (length + size - 1) / size;
                Assert.AreEqual(PacketFragmentHeader.HeaderSize + (length == 0 ? 0 : length - (count - 1) * size), fragments.TailSize);
                var restored = new byte[length];
                for (int index = 0; index < count; index++)
                {
                    int expectedSize = fragments.NextSize;
                    var fragment = fragments.GetNextFragment();
                    Assert.AreEqual((ushort)index, fragment.Header.Index);
                    Assert.AreEqual(expectedSize, fragment.Length);
                    VerifyFragment(fragment, payload, 42, count);
                    fragment.Data.CopyTo(restored, index * size);
                }
                CollectionAssert.AreEqual(payload, restored);
                Assert.AreEqual(0, fragments.DataRemaining);
                Assert.AreEqual(0L, message.Data.Position, "Sending must not mutate a broadcast message's shared stream cursor.");
            }
        }

        [TestMethod]
        public void TailFirstFragmentsReportTheirActualSizeAndReassemble()
        {
            int size = PacketFragment.MaxFragmentDataSize;
            foreach (int length in new[] { size + 1, size * 2, size * 2 + 1, size * 3, 2048 })
            {
                var payload = Payload(length);
                var message = new BroadcastMessage(payload);
                var fragments = new MessageFragment(message, 7);
                int count = fragments.Count;
                var restored = new byte[length];
                Assert.IsFalse(fragments.TailSent);
                int expectedTailSize = fragments.TailSize;
                var tail = fragments.GetTailFragment();
                Assert.AreEqual(expectedTailSize, tail.Length, "Packet packing must reserve the full tail, including exact multiples of fragment size.");
                Assert.AreEqual((ushort)(count - 1), tail.Header.Index);
                VerifyFragment(tail, payload, 7, count);
                tail.Data.CopyTo(restored, (count - 1) * size);
                Assert.IsTrue(fragments.TailSent);
                for (int index = 0; index < count - 1; index++)
                {
                    var fragment = fragments.GetNextFragment();
                    Assert.AreEqual((ushort)index, fragment.Header.Index);
                    VerifyFragment(fragment, payload, 7, count);
                    fragment.Data.CopyTo(restored, index * size);
                }
                Assert.AreEqual(0, fragments.DataRemaining);
                CollectionAssert.AreEqual(payload, restored);
            }
        }

        [TestMethod]
        public void ParallelRecipientsReceiveIdenticalBroadcastBytes()
        {
            const int recipients = 8;
            var options = new ParallelOptions { MaxDegreeOfParallelism = recipients };
            foreach (int length in new[] { 64, 448, 449, 896, 2048 })
            {
                var payload = Payload(length);
                var message = new BroadcastMessage(payload);
                for (int trial = 0; trial < 2000; trial++)
                {
                    Parallel.For(0, recipients, options, recipient =>
                    {
                        uint sequence = (uint)(recipient + 1);
                        var fragments = new MessageFragment(message, sequence);
                        int count = fragments.Count;
                        bool tailFirst = count > 1 && recipient % 2 == 0;
                        if (tailFirst)
                            VerifyFragment(fragments.GetTailFragment(), payload, sequence, count);
                        for (int index = 0; index < count - (tailFirst ? 1 : 0); index++)
                        {
                            var fragment = fragments.GetNextFragment();
                            Assert.AreEqual((ushort)index, fragment.Header.Index);
                            VerifyFragment(fragment, payload, sequence, count);
                        }
                        Assert.AreEqual(0, fragments.DataRemaining);
                    });
                }
                Assert.AreEqual((long)length, message.Data.Position);
                CollectionAssert.AreEqual(payload, message.Data.ToArray());
            }
        }
    }
}
