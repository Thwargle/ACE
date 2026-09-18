using System.Text;
using System.Text.Json;
using ACE.DatLoader;
using ACE.DatLoader.Entity;
using ACE.DatLoader.FileTypes;
using ACE.Entity.Enum;

Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
RetailLayoutResolver.RunTests();
if (args.Contains("--self-test")) return;
if (args.Length > 0 && args[0] == "--visual-probe")
{
    DatManager.Initialize(args[1], keepOpen: true, loadCell: true);
    foreach (uint id in new uint[] { 0x020010AC, 0x02000D55, 0x02000B8E })
    {
        var setup = DatManager.PortalDat.ReadFromDat<SetupModel>(id);
        Console.WriteLine($"SETUP {id:X8} anim={setup.DefaultAnimation:X8} script={setup.DefaultScript:X8}");
        foreach (var part in setup.Parts)
        {
            var gfx = DatManager.PortalDat.ReadFromDat<GfxObj>(part);
            var drawn = new HashSet<ushort>();
            void Visit(BSPNode? node) { if (node == null) return; if (node.InPolys != null) drawn.UnionWith(node.InPolys); Visit(node.PosNode); Visit(node.NegNode); }
            Visit(gfx.DrawingBSP?.RootNode);
            Console.WriteLine($" GFX {part:X8} verts={gfx.VertexArray.Vertices.Count} poly={gfx.Polygons.Count} draw={drawn.Count} unused={string.Join(',',gfx.Polygons.Keys.Except(drawn))}");
            foreach (var sid in gfx.Surfaces)
            {
                var s = DatManager.PortalDat.ReadFromDat<Surface>(sid);
                Console.WriteLine($"  SURF {sid:X8} type={s.Type} color={s.ColorValue:X8} trans={s.Translucency} lum={s.Luminosity} diff={s.Diffuse}");
            }
        }
    }
    var pe = DatManager.PortalDat.ReadFromDat<ParticleEmitterInfo>(0x320005A8);
    Console.WriteLine(JsonSerializer.Serialize(pe,pe.GetType()));
    var particle = DatManager.PortalDat.ReadFromDat<GfxObj>(pe.HwGfxObjId);
    foreach(var v in particle.VertexArray.Vertices.Take(8)) Console.WriteLine($"WAND {v.Key}: {v.Value.Origin}");
    foreach(uint id in new uint[]{0x00070143,0x7D64010C,0x7D640100})
    {
        var cell=DatManager.CellDat.ReadFromDat<EnvCell>(id);
        Console.WriteLine($"LIGHT CELL {id:X8} pos={cell.Position.Origin} visible={cell.VisibleCells.Count}");
        foreach(var stab in cell.StaticObjects)
        {
            Console.WriteLine($" STAB {stab.Id:X8} pos={stab.Frame.Origin}");
            if ((stab.Id >> 24) != 2) continue;
            var setup=DatManager.PortalDat.ReadFromDat<SetupModel>(stab.Id);
            foreach(var light in setup.Lights.Values)
                Console.WriteLine($" LIGHT setup={stab.Id:X8} pos={stab.Frame.Origin} {JsonSerializer.Serialize(light,light.GetType())}");
        }
    }
    return;
}
if (args.Length > 0 && args[0] == "--pedestal-probe")
{
    DatManager.Initialize(args[1], keepOpen: true, loadCell: false);
    var setup=DatManager.PortalDat.ReadFromDat<SetupModel>(0x02000D55);
    Console.WriteLine($"PEDESTAL MT={setup.DefaultMotionTable:X8} anim={setup.DefaultAnimation:X8} script={setup.DefaultScript:X8}");
    const uint mtId = 0x090000F9; // Pedestal Weak Spot, weenie 16919.
    var mt=DatManager.PortalDat.ReadFromDat<MotionTable>(mtId);
    foreach(var from in new[]{MotionCommand.On, MotionCommand.Off})
    {
        var to=from==MotionCommand.On ? MotionCommand.Off : MotionCommand.On;
        foreach(var data in mt.Links.Values.SelectMany(l=>l).Where(c=>(c.Key & 0xFFFF)==((uint)to & 0xFFFF)).SelectMany(c=>c.Value.Anims).Concat(mt.Cycles.Where(c=>(c.Key & 0xFFFF)==((uint)to & 0xFFFF)).SelectMany(c=>c.Value.Anims)))
        {
            var anim=DatManager.PortalDat.ReadFromDat<Animation>(data.AnimId);
            if(!anim.PartFrames.Any(f=>f.Hooks.Count>0)) continue;
            Console.WriteLine($"MT {mtId:X8} {from}->{to}: {data.AnimId:X8} {data.LowFrame}..{data.HighFrame} @ {data.Framerate}");
            for(int f=0; f<anim.PartFrames.Count; ++f)
                foreach(var hook in anim.PartFrames[f].Hooks) Console.WriteLine($" frame {f}: {JsonSerializer.Serialize(hook,hook.GetType())}");
        }
    }
    return;
}
if (args.Length > 0 && args[0] == "--head-probe")
{
    DatManager.Initialize(args[1],keepOpen:true,loadCell:false);
    foreach(uint id in DatManager.PortalDat.AllFiles.Keys.Where(id=>(id>>24)==4).Order().Take(12))
    {
        var p=DatManager.PortalDat.ReadFromDat<Palette>(id);
        Console.WriteLine($"PAL {id:X8} count={p.Colors.Count} samples={string.Join(",",p.Colors.Skip(50).Take(3).Select(c=>c.ToString("X8")))}");
    }
    var setup=DatManager.PortalDat.ReadFromDat<SetupModel>(0x0200004E);
    var head=DatManager.PortalDat.ReadFromDat<GfxObj>(setup.Parts[16]);
    foreach(var surfaceId in head.Surfaces)
    {
        var s=DatManager.PortalDat.ReadFromDat<Surface>(surfaceId);
        Console.WriteLine($"SURF {surfaceId:X8} palette={s.OrigPaletteId:X8} tex={s.OrigTextureId:X8}");
        var st=DatManager.PortalDat.ReadFromDat<SurfaceTexture>(s.OrigTextureId);
        foreach(var id in st.Textures)
        {
            var tex=DatManager.PortalDat.ReadFromDat<Texture>(id);
            Console.WriteLine($" TEX {id:X8} {tex.Width}x{tex.Height} {tex.Format} pal={tex.DefaultPaletteId:X8}");
            if(tex.Format==SurfacePixelFormat.PFID_INDEX16 && tex.SourceData.Length>0)
                Console.WriteLine($" indices={string.Join(",",Enumerable.Range(0,tex.SourceData.Length/2).Select(i=>BitConverter.ToUInt16(tex.SourceData,i*2)).Distinct().Order().Take(12))}");
        }
    }
    return;
}
if (args.Length > 0 && args[0] == "--portal-effect-probe")
{
    DatManager.Initialize(args[1], keepOpen: true, loadCell: false);
    var setup=DatManager.PortalDat.ReadFromDat<SetupModel>(0x02000001);
    Console.WriteLine($"Setup PE={setup.DefaultScriptTable:X8}");
    var table=DatManager.PortalDat.ReadFromDat<PhysicsScriptTable>(0x34000004);
    foreach(var cmd in new uint[]{0x76,0x75})
        if(table.ScriptTable.TryGetValue(cmd, out var entry)) foreach(var row in entry.Scripts)
        {
            Console.WriteLine($"PS {cmd:X2} mod={row.Mod} id={row.ScriptId:X8}");
            var script=DatManager.PortalDat.ReadFromDat<PhysicsScript>(row.ScriptId);
            foreach(var hook in script.ScriptData) Console.WriteLine($" {hook.StartTime}: {hook.Hook.GetType().Name} {JsonSerializer.Serialize(hook.Hook,hook.Hook.GetType())}");
        }
    return;
}
if (args.Length > 0 && args[0] == "--emote-probe")
{
    DatManager.Initialize(args[1], keepOpen: true, loadCell: false);
    var mt = DatManager.PortalDat.ReadFromDat<MotionTable>(0x09000001);
    foreach (var pair in mt.Cycles)
        if ((pair.Key & 0xFFFF) == ((uint)MotionCommand.Point & 0xFFFF) || (pair.Key & 0xFFFF) == ((uint)MotionCommand.ATOYOT & 0xFFFF))
            foreach(var a in pair.Value.Anims) Console.WriteLine($"CYCLE {pair.Key:X8}: {a.AnimId:X8} {a.LowFrame}..{a.HighFrame} @ {a.Framerate}");
    foreach (var cmd in new[]{MotionCommand.Point, MotionCommand.PointState, MotionCommand.ATOYOT})
        foreach(var a in mt.GetAnimData(MotionStance.NonCombat, cmd, MotionCommand.Ready)) Console.WriteLine($"LINK {cmd}: {a.AnimId:X8} {a.LowFrame}..{a.HighFrame} @ {a.Framerate}");
    return;
}
if (args.Length > 0 && args[0] == "--dungeon-probe")
{
    DatManager.Initialize(args[1], keepOpen: true);
    foreach (uint id in new uint[] { 0x000701A6, 0x000701A7, 0x00070198, 0x7E650100, 0x7E650105, 0x7E650106 })
    {
        var cell = DatManager.CellDat.ReadFromDat<EnvCell>(id);
        var structure = DatManager.PortalDat.ReadFromDat<ACE.DatLoader.FileTypes.Environment>(cell.EnvironmentId).Cells[cell.CellStructure];
        var drawn = new HashSet<ushort>();
        void Visit(BSPNode? node) { if (node == null) return; if (node.InPolys != null) drawn.UnionWith(node.InPolys); Visit(node.PosNode); Visit(node.NegNode); }
        Visit(structure.DrawingBSP?.RootNode);
        Console.WriteLine($"CELL {id:X8} env={cell.EnvironmentId:X8} struct={cell.CellStructure} pos={cell.Position.Origin} statics={cell.StaticObjects.Count}");
        Console.WriteLine($" DRAW: {string.Join(',', drawn.Order())}; UNUSED: {string.Join(',', structure.Polygons.Keys.Except(drawn).Order())}");
        foreach(var portal in cell.CellPortals) Console.WriteLine($" PORT {portal.PolygonId} -> {portal.OtherCellId:X4} flags={portal.Flags} member={structure.Portals.Contains(portal.PolygonId)}");
        foreach (var p in structure.Polygons)
        {
            var verts=p.Value.VertexIds.Select(v=>structure.VertexArray.Vertices[(ushort)v].Origin).ToArray();
            var n=System.Numerics.Vector3.Cross(verts[1]-verts[0], verts[2]-verts[0]);
            if (Math.Abs(n.Z)>0.1f) Console.WriteLine($" POLY {p.Key} n={n} z={verts[0].Z} sides={p.Value.SidesType} pos={p.Value.PosSurface} neg={p.Value.NegSurface} portal={cell.CellPortals.Any(x=>x.PolygonId==p.Key)}");
        }
    }
    return;
}
if (args.Length > 0 && args[0] == "--regression-probe")
{
    DatManager.Initialize(args[1], keepOpen: true);
    foreach (var id in DatManager.PortalDat.AllFiles.Keys.Where(id => (id >> 24) == 0x25))
    {
        var map = DatManager.PortalDat.ReadFromDat<DidMapper>(id);
        if (map.ClientEnumToID.Values.Contains(0x06004D68u))
            foreach (var entry in map.ClientEnumToID) Console.WriteLine($"CURSOR {id:X8} {entry.Key} {entry.Value:X8}");
    }
    for (uint id=0x7E650100; id<0x7E650400; ++id)
    {
        if (!DatManager.CellDat.AllFiles.ContainsKey(id)) continue;
        var cell=DatManager.CellDat.ReadFromDat<EnvCell>(id);
        if (cell.Surfaces.Contains(0x080006FDu)) Console.WriteLine($"BLUE CELL {id:X8} env={cell.EnvironmentId:X8} struct={cell.CellStructure}");
    }
    return;
}
if (args.Length > 0 && args[0] == "--appraisal-resources")
{
    DatManager.Initialize(args[1], keepOpen: true, loadCell: false);
    foreach (var id in DatManager.PortalDat.AllFiles.Keys.Where(id => (id >> 24) == 0x25))
    {
        var map = DatManager.PortalDat.ReadFromDat<DidMapper>(id);
        if (!map.ClientEnumToID.TryGetValue(0x10000005, out var creatureMapId)) continue;
        Console.WriteLine($"Creature enum: mapper={id:X8} resource={creatureMapId:X8}");
        if ((creatureMapId >> 24) != 0x22) continue;
        var names = DatManager.PortalDat.ReadFromDat<EnumMapper>(creatureMapId);
        foreach (var entry in names.IdToStringMap) Console.WriteLine($"{entry.Key}: {entry.Value}");
    }
    var strings = DatManager.LanguageDat.ReadFromDat<StringTable>(0x23000001);
    foreach (var id in new uint[] {0x0990A1E2, 0x07100DAC})
        Console.WriteLine($"{id:X8}: {string.Join(" | ", strings.StringTableData.Single(row => row.Id == id).Strings)}");
    return;
}
if (args.Length > 0 && args[0] == "--world-fixtures") { RetailWorldFixtures.Export(args[1], args[2]); return; }
if (args.Length > 0 && args[0] == "--font-fixtures") { RetailFontFixtures.Export(args[1], args[2]); return; }
if (args.Length > 0 && args[0] == "--scene-fixtures") { RetailSceneFixtures.Export(args[1], args[2]); return; }

var datDir = args.Length > 0 ? args[0] : @"C:\Turbine\Asheron's Call";
var outDir = args.Length > 1 ? args[1] : @"C:\dev\ACE\Unreal\Plugins\ACEClient\Docs\UI";
Directory.CreateDirectory(outDir);

Console.WriteLine($"DAT dir: {datDir}");
DatManager.Initialize(datDir, keepOpen: true, loadCell: false);

var layoutNameById = new Dictionary<uint, string>();
try
{
	var didMap = DatManager.PortalDat.ReadFromDat<DidMapper>(0x2500000Eu);
	Console.WriteLine(
		$"DidMapper 0x2500000E: ClientEnumToID={didMap.ClientEnumToID.Count} ClientEnumToName={didMap.ClientEnumToName.Count}");
	foreach (var kv in didMap.ClientEnumToID)
	{
		var name = didMap.ClientEnumToName.TryGetValue(kv.Key, out var n) ? n : $"Enum_{kv.Key}";
		layoutNameById[kv.Value] = name;
	}
}
catch (Exception ex)
{
	Console.WriteLine($"DidMapper 0x2500000E: {ex.Message}");
}

var elementIdNames = new Dictionary<uint, string>();
var stateIdNames = new Dictionary<uint, string>();
try
{
	var em = DatManager.PortalDat.ReadFromDat<EnumMapper>(0x2200001Bu);
	foreach (var kv in em.IdToStringMap)
		elementIdNames[kv.Key] = kv.Value;
	Console.WriteLine($"EnumMapper UIElementID count={elementIdNames.Count}");
}
catch (Exception ex)
{
	Console.WriteLine($"EnumMapper 0x2200001B: {ex.Message}");
}

try
{
	var em = DatManager.PortalDat.ReadFromDat<EnumMapper>(0x2200001Cu);
	foreach (var kv in em.IdToStringMap)
		stateIdNames[kv.Key] = kv.Value;
	Console.WriteLine($"EnumMapper UIState count={stateIdNames.Count}");
}
catch (Exception ex)
{
	Console.WriteLine($"EnumMapper 0x2200001C: {ex.Message}");
}

var layouts = new List<object>();
var layoutById = new Dictionary<uint, LayoutDesc>();
var layoutNameByLayoutId = new Dictionary<uint, string>();
var assetRows = new List<Dictionary<string, string>>();
var allMediaFiles = new HashSet<uint>();
var allCursors = new List<object>();
var allFonts = new HashSet<uint>();
var allRenderTextures = new HashSet<uint>();
var typeUsage = new Dictionary<uint, int>();

var langFiles = DatManager.LanguageDat.AllFiles
	.Where(kv => (kv.Key & 0xFF000000u) == 0x21000000u)
	.OrderBy(kv => kv.Key)
	.ToList();
Console.WriteLine($"LayoutDesc candidates in language DAT: {langFiles.Count}");

static string HexFile(uint file) => file == 0 ? "" : $"0x{file:X8}";

static (uint imageFile, uint alphaFile, uint drawMode) MediaSummary(IEnumerable<MediaDesc> media)
{
	uint image = 0, alpha = 0, drawMode = 0;
	foreach (var m in media)
	{
		if (m.Type == MediaType.Alpha && alpha == 0 && m.File != 0)
			alpha = m.File;
		if (m.Type == MediaType.Image && m.File != 0)
		{
			if (image == 0) { image = m.File; drawMode = m.DrawMode; }
		}
	}
	return (image, alpha, drawMode);
}

(uint imageFile, uint alphaFile, uint drawMode) ElementMediaSummary(ElementDesc el)
{
    // UIElement::SetState uses the declared default state. Hash-table order is not
    // state priority: choosing the first state paints pressed/disabled artwork.
    if (el.States.TryGetValue(el.DefaultState, out var state) && state.Media.Count != 0)
        return MediaSummary(state.Media);
    return MediaSummary(el.Media);
}

object? PropertyValue(BaseProperty p) => p.PropertyType switch
{
    BasePropertyType.Bool => p.ValueBool,
    BasePropertyType.Enum => p.ValueEnum,
    BasePropertyType.DataFile => HexFile(p.ValueDataFile),
    BasePropertyType.Float => p.ValueFloat,
    BasePropertyType.Color => HexFile(p.ValueColor),
    BasePropertyType.Integer => p.ValueInt,
    BasePropertyType.StringInfo => new { table = HexFile(p.ValueString.TableId), id = HexFile(p.ValueString.StringId) },
    BasePropertyType.Array => p.ValueArray.Select(PropertyValue).ToArray(),
    BasePropertyType.Struct => p.ValueStruct.ToDictionary(k => HexFile(k.Key), k => PropertyValue(k.Value)),
    BasePropertyType.Bitfield32 => p.ValueBitfield32,
    BasePropertyType.Bitfield64 => p.ValueBitfield64,
    BasePropertyType.InstanceID => p.ValueInstanceId,
    BasePropertyType.Vector => new { x = p.ValueVector.X, y = p.ValueVector.Y, z = p.ValueVector.Z },
    _ => null
};

object Properties(StateDesc state) => state.Properties.ToDictionary(
    p => DatManager.PortalDat.MasterProperty.m_emapper.GetValueOrDefault(p.Key, HexFile(p.Key)),
    p => PropertyValue(p.Value));

Dictionary<string, object?> ResolvedNode(ElementDesc el, Dictionary<uint, string> elementIdNames)
{
	// Keep base media distinct: SetState falls back to this media when the selected
	// state has none, including when the default state differs from state zero.
	var (imageFile, alphaFile, drawMode) = MediaSummary(el.Media);
	var props = (Dictionary<string, object?>)Properties(el);
	uint fontId = 0;
	if (props.TryGetValue("UICore_Text_fonts", out var fonts) && fonts is object?[] fs
		&& fs.FirstOrDefault() is string fontText && fontText.Length > 2)
		fontId = Convert.ToUInt32(fontText[2..], 16);
	var node = new Dictionary<string, object?>
	{
		["elementId"] = $"0x{el.ElementId:X8}",
		["elementName"] = elementIdNames.GetValueOrDefault(el.ElementId, ""),
		["type"] = $"0x{el.Type:X8}",
        ["defaultState"] = HexFile(el.DefaultState),
        ["passToChildren"] = el.PassToChildren,
        ["properties"] = props,
        ["fontHeight"] = fontId == 0 ? 0 : DatManager.PortalDat.ReadFromDat<Font>(fontId).MaxCharHeight,
        ["states"] = el.States.Values.OrderBy(st => st.StateId).Select(st => {
            var (image, alpha, mode) = MediaSummary(st.Media);
            return new { stateId = HexFile(st.StateId), stateName = stateIdNames.GetValueOrDefault(st.StateId, ""),
                passToChildren = st.PassToChildren, imageFile = HexFile(image), alphaFile = HexFile(alpha),
                drawMode = mode, hasMedia = st.Media.Count != 0, properties = Properties(st) };
        }).ToArray(),
		["x"] = el.X,
		["y"] = el.Y,
		["width"] = el.Width,
		["height"] = el.Height,
		["zLevel"] = el.ZLevel,
		["baseLayout"] = $"0x{el.BaseLayout:X8}",
		["baseElement"] = $"0x{el.BaseElement:X8}",
		["drawMode"] = drawMode,
		["leftEdge"] = el.LeftEdge,
		["topEdge"] = el.TopEdge,
		["rightEdge"] = el.RightEdge,
		["bottomEdge"] = el.BottomEdge,
	};
	if (imageFile != 0)
		node["imageFile"] = HexFile(imageFile);
	if (alphaFile != 0)
		node["alphaFile"] = HexFile(alphaFile);
	var children = new List<object?>();
	foreach (var ch in el.Children.Values.OrderBy(c => c.ZLevel).ThenBy(c => c.UiReadOrder))
		children.Add(ResolvedNode(ch, elementIdNames));
	node["children"] = children;
	return node;
}

void WalkElement(uint layoutId, string layoutName, ElementDesc el, string path, List<object> elementsOut)
{
	typeUsage[el.Type] = typeUsage.GetValueOrDefault(el.Type) + 1;
	var elementName = elementIdNames.GetValueOrDefault(el.ElementId, "");
	var (imageFile, alphaFile, drawMode) = ElementMediaSummary(el);
	var row = new Dictionary<string, object?>
	{
		["path"] = path,
		["elementId"] = $"0x{el.ElementId:X8}",
		["elementName"] = elementName,
		["type"] = $"0x{el.Type:X8}",
		["baseElement"] = $"0x{el.BaseElement:X8}",
		["baseLayout"] = $"0x{el.BaseLayout:X8}",
		["defaultState"] = $"0x{el.DefaultState:X8}",
		["x"] = el.X,
		["y"] = el.Y,
		["width"] = el.Width,
		["height"] = el.Height,
		["zLevel"] = el.ZLevel,
		["edges"] = new { el.LeftEdge, el.TopEdge, el.RightEdge, el.BottomEdge },
		["stateCount"] = el.States.Count,
		["childCount"] = el.Children.Count,
		["drawMode"] = drawMode,
	};
	if (imageFile != 0)
		row["imageFile"] = HexFile(imageFile);
	if (alphaFile != 0)
		row["alphaFile"] = HexFile(alphaFile);
	elementsOut.Add(row);

	void CollectMedia(IEnumerable<MediaDesc> media)
	{
		foreach (var m in media)
		{
			if (m.Type is MediaType.Image or MediaType.Alpha or MediaType.Sound)
			{
				if (m.File == 0)
					continue;
				allMediaFiles.Add(m.File);
				if ((m.File & 0xFF000000u) == 0x15000000u)
					allRenderTextures.Add(m.File);
				if ((m.File & 0xFF000000u) == 0x40000000u)
					allFonts.Add(m.File);
				assetRows.Add(Row(m.File, m.Type.ToString(), SuggestedUnrealPath(m.File, m.Type),
					layoutId, layoutName, path, ""));
			}
			else if (m.Type == MediaType.Cursor)
			{
				allMediaFiles.Add(m.File);
				allCursors.Add(new
				{
					file = $"0x{m.File:X8}",
					xHotspot = m.XHotspot,
					yHotspot = m.YHotspot,
					layout = $"0x{layoutId:X8}",
					layoutName,
					element = path
				});
				assetRows.Add(Row(m.File, "Cursor", $"AC_UI_Cursor_{m.File:X8}",
					layoutId, layoutName, path, $"hotspot=({m.XHotspot},{m.YHotspot})"));
			}
			else if (m.Type == MediaType.Animation && m.Frames != null)
			{
				foreach (var f in m.Frames)
				{
					if (f == 0)
						continue;
					allMediaFiles.Add(f);
					assetRows.Add(Row(f, "AnimationFrame", SuggestedUnrealPath(f, MediaType.Image),
						layoutId, layoutName, path, "animation frame"));
				}
			}
		}
	}

	CollectMedia(el.Media);
	foreach (var st in el.States.Values)
		CollectMedia(st.Media);

	foreach (var kv in el.Children)
	{
		var childName = elementIdNames.GetValueOrDefault(kv.Value.ElementId, kv.Key.ToString("X"));
		WalkElement(layoutId, layoutName, kv.Value, $"{path}/{childName}", elementsOut);
	}
}

Dictionary<string, string> Row(uint file, string type, string unreal, uint layoutId, string layoutName, string path, string note) =>
	new()
	{
		["file"] = $"0x{file:X8}",
		["dat"] = "client_portal.dat",
		["type"] = type,
		["unreal"] = unreal,
		["layoutId"] = $"0x{layoutId:X8}",
		["layoutName"] = layoutName,
		["element"] = path,
		["note"] = note,
	};

string SuggestedUnrealPath(uint file, MediaType type) => type switch
{
	MediaType.Sound => $"AC_UI_Sound_{file:X8}",
	_ => $"AC_UI_Icon_{file:X8}",
};

foreach (var kv in langFiles)
{
	try
	{
		var layout = DatManager.LanguageDat.ReadFromDat<LayoutDesc>(kv.Key);
		layoutById[kv.Key] = layout;
		var name = layoutNameById.GetValueOrDefault(kv.Key, $"layout_{kv.Key:X8}");
		layoutNameByLayoutId[kv.Key] = name;
		var elements = new List<object>();
		if (layout.Elements.Count > 0)
		{
			foreach (var root in layout.Elements)
			{
				var rootName = elementIdNames.GetValueOrDefault(root.Value.ElementId, root.Key.ToString("X"));
				WalkElement(kv.Key, name, root.Value, rootName, elements);
			}
		}
		layouts.Add(new
		{
			id = $"0x{kv.Key:X8}",
			name,
			width = layout.DisplayWidth,
			height = layout.DisplayHeight,
			elements,
		});
		assetRows.Add(new Dictionary<string, string>
		{
			["file"] = $"0x{kv.Key:X8}",
			["dat"] = "client_local_English.dat",
			["type"] = "LayoutDesc",
			["unreal"] = $"AC_UI_Layout_{kv.Key:X8}",
			["layoutId"] = $"0x{kv.Key:X8}",
			["layoutName"] = name,
			["element"] = "",
			["note"] = $"{layout.DisplayWidth}x{layout.DisplayHeight}",
		});
	}
	catch (Exception ex)
	{
		Console.WriteLine($"Layout 0x{kv.Key:X8}: {ex.Message}");
	}
}

Console.WriteLine($"Layouts={layouts.Count} media={allMediaFiles.Count}");
File.WriteAllText(Path.Combine(outDir, "UIAssetManifest.json"),
	JsonSerializer.Serialize(new { layouts, cursors = allCursors, fonts = allFonts.OrderBy(x => x).Select(x => $"0x{x:X8}") },
		new JsonSerializerOptions { WriteIndented = true }));

var resolvedDir = Path.Combine(outDir, "Resolved");
Directory.CreateDirectory(resolvedDir);
foreach (var kv in layoutById.OrderBy(k => k.Key))
{
	var layoutId = kv.Key;
	var layout = kv.Value;
	var name = layoutNameByLayoutId.GetValueOrDefault(layoutId, $"layout_{layoutId:X8}");
	var roots = new List<object?>();
	foreach (var root in layout.Elements.Values.OrderBy(e => e.ZLevel).ThenBy(e => e.UiReadOrder))
	{
		try
		{
			var resolvedRoot = RetailLayoutResolver.Resolve(root, layoutById);
			roots.Add(ResolvedNode(resolvedRoot, elementIdNames));
		}
		catch (InvalidDataException ex)
		{
			Console.WriteLine($"Unresolved root {layoutId:X8}/{root.ElementId:X8}: {ex.Message}");
			if (layoutId == 0x21000005) throw; // gameplay must never ship a partial tree
		}
	}
	var doc = new Dictionary<string, object?>
	{
		["id"] = $"0x{layoutId:X8}",
		["name"] = name,
		["displayWidth"] = layout.DisplayWidth,
		["displayHeight"] = layout.DisplayHeight,
		["roots"] = roots,
	};
	var outPath = Path.Combine(resolvedDir, $"0x{layoutId:X8}.json");
	File.WriteAllText(outPath, JsonSerializer.Serialize(doc, new JsonSerializerOptions { WriteIndented = true }));
}
Console.WriteLine($"Resolved layouts written to {resolvedDir} ({layoutById.Count} files)");



