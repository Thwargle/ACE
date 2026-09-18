using System.Text.Json;
using ACE.DatLoader;
using ACE.DatLoader.FileTypes;

internal static class RetailFontFixtures
{
    public static void Export(string datDirectory, string output)
    {
        DatManager.Initialize(datDirectory, keepOpen: true, loadCell: false);
        var rows = new List<object>();
        foreach (uint id in DatManager.PortalDat.AllFiles.Keys.Where(id => (id >> 24) == 0x40).Order())
        {
            var font = DatManager.PortalDat.ReadFromDat<Font>(id);
            ulong hash = 14695981039346656037UL;
            void Add(uint value, int bytes) {
                for (int i = 0; i < bytes; ++i) hash = unchecked((hash ^ ((value >> (8 * i)) & 255)) * 1099511628211UL);
            }
            foreach (var c in font.CharDescs) {
                Add(c.Unicode, 2); Add(c.OffsetX, 2); Add(c.OffsetY, 2);
                Add(c.Width, 1); Add(c.Height, 1); Add(c.HorizontalOffsetBefore, 1);
                Add(c.HorizontalOffsetAfter, 1); Add(c.VerticalOffsetBefore, 1);
            }
            int Width(string text) => text.Sum(c => {
                var glyph = font.CharDescs.FirstOrDefault(g => g.Unicode == c)
                    ?? font.CharDescs.FirstOrDefault(g => g.Unicode == '?');
                return glyph == null ? 0 : unchecked((byte)(glyph.Width + glyph.HorizontalOffsetBefore + glyph.HorizontalOffsetAfter));
            });
            rows.Add(new { id = $"{id:X8}", height = font.MaxCharHeight, maxWidth = font.MaxCharWidth,
                count = font.NumCharacters, glyphHash = $"{hash:X16}",
                samples = new[] { "Spellbook", "Inventory", "Burden: 123/456", "Inspection", "Wi.i?", "ENTER", "CREDITS", "EXIT" }
                    .Select(text => new { text, width = Width(text) }) });
        }
        Directory.CreateDirectory(Path.GetDirectoryName(output)!);
        File.WriteAllText(output, JsonSerializer.Serialize(new { fonts = rows }, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"Exported {rows.Count} independent font fixtures to {output}");
    }
}
