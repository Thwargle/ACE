using System.Numerics;
using System.Text.Json;
using ACE.DatLoader;
using ACE.DatLoader.Entity;
using ACE.DatLoader.FileTypes;

// Independent oracle: the server DAT reader, not Unreal's reader/mesh builder.
internal static class RetailWorldFixtures
{
    // D3DPolyRender::ConstructMesh: one front, with a second winding for None
    // or a separately surfaced back for Clockwise. Stippling controls packed UVs.
    private static int DrawTriangles(IEnumerable<Polygon> polygons, int surfaceCount) => polygons.Sum(p => {
        int sides = p.PosSurface >= 0 && p.PosSurface < surfaceCount ? 1 : 0;
        if ((int)p.SidesType == 1) sides *= 2;
        if ((int)p.SidesType == 2 && p.NegSurface >= 0 && p.NegSurface < surfaceCount) sides++;
        return Math.Max(0, p.VertexIds.Count - 2) * sides;
    });

    public static void Export(string datDirectory, string output)
    {
        DatManager.Initialize(datDirectory, keepOpen: true, loadCell: true);
        var towns = new HashSet<uint> { 0xA9B40000, 0xA9B30000, 0xC6A90000,
            0xCE950000, 0xE74E0000, 0x7D640000, 0x01AC0000, 0xF8000000, 0x00070000 };
        var ids = DatManager.CellDat.AllFiles.Keys.Where(id => (id & 0xFFFF) >= 0x100
            && (id & 0xFFFF) < 0xFFFE).Order().ToArray();
        var selected = ids.Where((id, i) => towns.Contains(id & 0xFFFF0000) || i % 257 == 0).ToArray();
        var rows = new List<object>();
        int overlappingIds = 0, emptyPhysics = 0;
        foreach (var id in selected)
        {
            var cell = DatManager.CellDat.ReadFromDat<EnvCell>(id);
            var env = DatManager.PortalDat.ReadFromDat<ACE.DatLoader.FileTypes.Environment>(cell.EnvironmentId);
            var shape = env.Cells[cell.CellStructure];
            var physicsIds = new HashSet<ushort>();
            void Visit(BSPNode? node)
            {
                if (node == null) return;
                if (node.InPolys != null) physicsIds.UnionWith(node.InPolys);
                Visit(node.PosNode); Visit(node.NegNode);
            }
            Visit(shape.PhysicsBSP.RootNode);
            // Empty trees have no collision faces; do not invent collision from drawing.
            if (physicsIds.Count == 0) emptyPhysics++;
            var count = physicsIds.Sum(poly => Math.Max(0, shape.PhysicsPolygons[poly].VertexIds.Count - 2)) * 2;
            overlappingIds += cell.CellPortals.Count(p => physicsIds.Contains(p.PolygonId));
            var points = physicsIds.SelectMany(poly => shape.PhysicsPolygons[poly].VertexIds)
                .Select(v => shape.VertexArray.Vertices[(ushort)v].Origin).ToArray();
            Vector3 sum = Vector3.Zero;
            foreach (var p in points) sum += p;
            var portals = cell.CellPortals.Select(p => {
                var polygon = shape.Polygons[p.PolygonId];
                var vertices = polygon.VertexIds.Select(v => shape.VertexArray.Vertices[(ushort)v].Origin).ToArray();
                var n = Vector3.Normalize(Vector3.Cross(vertices[1] - vertices[0], vertices[2] - vertices[0]));
                if (((uint)p.Flags & 2) != 0) n = -n; // CCellPortal::UnPack portal_side = (~flags >> 1) & 1.
                return new { normal = new[] { -n.X, n.Y, n.Z }, d = -Vector3.Dot(n, vertices[0]) * 100.0f };
            }).ToArray();
            var drawn = new HashSet<ushort>();
            void VisitDrawing(BSPNode? node)
            {
                if (node == null) return;
                if (node.InPolys != null) drawn.UnionWith(node.InPolys);
                VisitDrawing(node.PosNode); VisitDrawing(node.NegNode);
            }
            VisitDrawing(shape.DrawingBSP?.RootNode);
            // ACRenderDevice::DrawEnvCell -> DrawingBSP::draw_check. Include only
            // the node's drawing faces, even when also used for connectivity.
            rows.Add(new { id = $"{id:X8}", triangles = count,
                drawTriangles = DrawTriangles(shape.Polygons.Where(p => drawn.Contains(p.Key)).Select(p => p.Value), cell.Surfaces.Count), vertexCount = points.Length,
                vertexSum = new[] { -sum.X * 100.0f, sum.Y * 100.0f, sum.Z * 100.0f }, portals });
        }
        var gfxRows = new List<object>();
        foreach (var id in DatManager.PortalDat.AllFiles.Keys.Where(id => (id >> 24) == 1).Order()
            .Where((id, i) => i % 97 == 0 || id == 0x010025E3))
        {
            var gfx = DatManager.PortalDat.ReadFromDat<GfxObj>(id);
            var polyIds = new HashSet<ushort>();
            void VisitGfx(BSPNode? node)
            {
                if (node == null) return;
                if (node.InPolys != null) polyIds.UnionWith(node.InPolys);
                VisitGfx(node.PosNode); VisitGfx(node.NegNode);
            }
            VisitGfx(gfx.PhysicsBSP.RootNode);
            var apertures = new HashSet<ushort>();
            void VisitDrawing(BSPNode? node)
            {
                if (node == null) return;
                if (node is BSPPortal portal) foreach (var p in portal.InPortals) apertures.Add((ushort)p.PolygonId);
                VisitDrawing(node.PosNode); VisitDrawing(node.NegNode);
            }
            VisitDrawing(gfx.DrawingBSP.RootNode);
            var count = polyIds.Sum(poly => Math.Max(0, gfx.PhysicsPolygons[poly].VertexIds.Count - 2)) * 2;
            gfxRows.Add(new { id = $"{id:X8}", triangles = count,
                // PView replaces these faces with the view through the aperture.
                // Unreal admits those cells directly, retaining PORT geometry separately.
                drawTriangles = DrawTriangles(gfx.Polygons.Where(p => !apertures.Contains(p.Key)).Select(p => p.Value), gfx.Surfaces.Count),
                portalPolygons = apertures.Count });
        }
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output))!);
        File.WriteAllText(output, JsonSerializer.Serialize(new { cells = rows, gfx = gfxRows }, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"World fixtures: {rows.Count} cells; {overlappingIds} drawing-portal/physics ID overlaps; {emptyPhysics} empty physics trees.");
        Console.WriteLine($"GfxObj fixtures: {gfxRows.Count} models.");
        foreach (uint id in new uint[] { 0x020002F3, 0x020002F4, 0x020002F5 })
        {
            var setup = DatManager.PortalDat.ReadFromDat<SetupModel>(id);
            Console.WriteLine($"Setup {id:X8}: BSP={setup.HasPhysicsBSP}, cylinders={setup.CylSpheres.Count}, spheres={setup.Spheres.Count}, parts={string.Join(',', setup.Parts.Select(p => p.ToString("X8")))}");
        }
    }
}
