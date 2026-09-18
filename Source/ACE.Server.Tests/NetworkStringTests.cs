using System;
using System.IO;
using System.Text;
using ACE.Common.Extensions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace ACE.Server.Tests
{
    [TestClass]
    public class NetworkStringTests
    {
        [TestMethod]
        public void RetailNameBytesPreserveAccentsAndFollowingFields()
        {
            Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
            // The C3/A9 pair is also valid UTF-8; ReadChars(2) would consume
            // the next field to get its second character. Retail counts bytes.
            byte[][] strings = { Array.Empty<byte>(), new byte[] { 65, 66 }, new byte[] { 0xc3, 0xa9 }, new byte[] { 82, 101, 110, 0xe9, 32, 0x8c, 0x92 } };
            string[] expected = { "", "AB", "\u00c3\u00a9", "Ren\u00e9 \u0152\u2019" };
            for (int i = 0; i < strings.Length; ++i)
            {
                using var stream = new MemoryStream();
                using var writer = new BinaryWriter(stream, Encoding.UTF8, true);
                writer.Write((ushort)strings[i].Length);
                writer.Write(strings[i]);
                while (stream.Position % 4 != 0) writer.Write((byte)0);
                writer.Write(0x12345678u);
                stream.Position = 0;
                using var reader = new BinaryReader(stream);
                Assert.AreEqual(expected[i], reader.ReadString16L());
                Assert.AreEqual(0x12345678u, reader.ReadUInt32());
                Assert.AreEqual(stream.Length, stream.Position);
            }
        }

        [TestMethod]
        public void TruncatedNameCannotBeAcceptedAsACompleteString()
        {
            using var reader = new BinaryReader(new MemoryStream(new byte[] { 3, 0, 65, 66 }));
            Assert.ThrowsExactly<EndOfStreamException>(() => reader.ReadString16L());
        }
    }
}
