using System.Text.Json;
using ACE.DatLoader;
using ACE.DatLoader.FileTypes;

// Source records for the reported Yaraq scenes, independent of Unreal's parser.
internal static class RetailSceneFixtures
{
    public static void Export(string datDir, string path)
    {
        DatManager.Initialize(datDir, keepOpen: true, loadCell: true);
        var blocks = new[] { 0x7D630000u, 0x7D640000u }.Select(id =>
            DatManager.CellDat.ReadFromDat<LandblockInfo>(id | 0xFFFE)).ToArray();
        var cells = blocks.SelectMany(b => Enumerable.Range(0x100, (int)b.NumCells)
            .Select(i => DatManager.CellDat.ReadFromDat<EnvCell>((b.Id & 0xFFFF0000) | (uint)i))).ToArray();
        var environments = cells.Select(c => c.EnvironmentId).Distinct()
            .Select(id => DatManager.PortalDat.ReadFromDat<ACE.DatLoader.FileTypes.Environment>(id)).ToArray();
        var setups = blocks.SelectMany(b => b.Buildings.Select(x => x.ModelId))
            .Concat(blocks.SelectMany(b => b.Objects.Select(x => x.Id)))
            .Concat(new[] { 0x02000D55u, 0x020007DDu }).Where(id => (id >> 24) == 2).Distinct()
            .Select(id => DatManager.PortalDat.ReadFromDat<SetupModel>(id)).ToArray();
        var gfx = setups.SelectMany(s => s.Parts).Concat(blocks.SelectMany(b => b.Buildings.Select(x => x.ModelId)))
            .Concat(blocks.SelectMany(b => b.Objects.Select(x => x.Id))).Where(id => (id >> 24) == 1)
            .Distinct().Select(id => DatManager.PortalDat.ReadFromDat<GfxObj>(id)).ToArray();
        var surfaces = gfx.SelectMany(g => g.Surfaces).Concat(cells.SelectMany(c => c.Surfaces)).Distinct()
            .ToDictionary(id => id.ToString("X8"), id => DatManager.PortalDat.ReadFromDat<Surface>(id));
        var mappers = DatManager.PortalDat.AllFiles.Keys.Where(id => (id >> 24) == 0x25)
            .Select(id => DatManager.PortalDat.ReadFromDat<DidMapper>(id)).ToArray();
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var envRecords = environments.Select(e => new { e.Id, Cells = e.Cells.ToDictionary(p => p.Key, p => new {
            p.Value.VertexArray, p.Value.Polygons, p.Value.Portals, p.Value.PhysicsPolygons }) });
        var gfxRecords = gfx.Select(g => new { g.Id, g.Surfaces, g.VertexArray, g.Polygons, g.PhysicsPolygons });
        File.WriteAllText(path, JsonSerializer.Serialize(new { blocks, cells, environments = envRecords, setups, gfx = gfxRecords, surfaces, mappers },
            new JsonSerializerOptions { IncludeFields = true, WriteIndented = false }));
        Console.WriteLine($"Exported Yaraq: {cells.Length} cells, {gfx.Length} models, {surfaces.Count} surfaces.");
    }
}
