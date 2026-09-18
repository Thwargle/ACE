using ACE.DatLoader;
using ACE.DatLoader.FileTypes;
using System.Text.Json;
System.Text.Encoding.RegisterProvider(System.Text.CodePagesEncodingProvider.Instance);
DatManager.Initialize(@"C:\Turbine\Asheron's Call", true, true);
var output = Path.GetFullPath("Quest3Test/Performance/20260914/SkySurfaces");
Directory.CreateDirectory(output);
var region = DatManager.PortalDat.ReadFromDat<RegionDesc>(0x13000000);
var seen = new HashSet<uint>();
foreach (var scriptId in region.SkyInfo.DayGroups.SelectMany(g=>g.SkyObjects).Select(o=>o.DefaultPESObjectId).Where(id=>id!=0).Distinct())
foreach (var entry in DatManager.PortalDat.ReadFromDat<PhysicsScript>(scriptId).ScriptData)
if(entry.Hook is ACE.DatLoader.Entity.AnimationHooks.CreateParticleHook hook)
{
    var pe=DatManager.PortalDat.ReadFromDat<ParticleEmitterInfo>(hook.EmitterInfoId);
    var gid=pe.HwGfxObjId!=0?pe.HwGfxObjId:pe.GfxObjId;
    foreach(var sid in DatManager.PortalDat.ReadFromDat<GfxObj>(gid).Surfaces)
    {
        var s=DatManager.PortalDat.ReadFromDat<Surface>(sid);
        Console.WriteLine(JsonSerializer.Serialize(new{script=$"{scriptId:X8}",emitter=$"{hook.EmitterInfoId:X8}",gfx=$"{gid:X8}",surface=$"{sid:X8}",type=$"{(uint)s.Type:X}",s.Translucency,s.Luminosity,pe.StartTrans,pe.FinalTrans}));
        if(s.OrigTextureId==0) continue;
        var t=DatManager.PortalDat.ReadFromDat<SurfaceTexture>(s.OrigTextureId).Textures.Select(tid=>DatManager.PortalDat.ReadFromDat<Texture>(tid)).OrderByDescending(t=>t.Width*t.Height).First();
        using var bm=t.GetBitmap();bm.Save(Path.Combine(output,$"PES_{sid:X8}.png"),System.Drawing.Imaging.ImageFormat.Png);
    }
}
foreach(uint gid in new uint[]{0x010015F1,0x010015EF,0x01004C36})
{
    var gfx=DatManager.PortalDat.ReadFromDat<GfxObj>(gid);
    Console.WriteLine(JsonSerializer.Serialize(new {geometry=$"{gid:X8}",vertices=gfx.VertexArray.Vertices.Count,polygons=gfx.Polygons.Count,zeroNormals=gfx.VertexArray.Vertices.Values.Count(v=>v.Normal.LengthSquared()<.001f),faces=gfx.Polygons.Take(4).Select(p=>new{id=p.Key,side=p.Value.SidesType.ToString(),surface=p.Value.PosSurface,verts=p.Value.VertexIds.Select((vid,i)=>new{xyz=gfx.VertexArray.Vertices[(ushort)vid].Origin.ToString(),normal=gfx.VertexArray.Vertices[(ushort)vid].Normal.ToString(),uv=gfx.VertexArray.Vertices[(ushort)vid].UVs[p.Value.PosUVIndices.Count>i?p.Value.PosUVIndices[i]:0]})})}));
}
foreach (var group in region.SkyInfo.DayGroups)
foreach (var obj in group.SkyObjects)
{
    uint id = obj.DefaultGFXObjectId;
    if (id == 0 || !seen.Add(id)) continue;
    var parts = id >> 24 == 2 ? DatManager.PortalDat.ReadFromDat<SetupModel>(id).Parts : new List<uint> { id };
    foreach (var part in parts)
    foreach (var sid in DatManager.PortalDat.ReadFromDat<GfxObj>(part).Surfaces)
    {
        var s = DatManager.PortalDat.ReadFromDat<Surface>(sid);
        if (s.OrigTextureId == 0) continue;
        var st = DatManager.PortalDat.ReadFromDat<SurfaceTexture>(s.OrigTextureId);
        var t = st.Textures.Select(tid => DatManager.PortalDat.ReadFromDat<Texture>(tid)).OrderByDescending(t => t.Width * t.Height).First();
        if (t.Length == 0) continue;
        using var bitmap = t.GetBitmap();
        bitmap.Save(Path.Combine(output, $"{sid:X8}.png"), System.Drawing.Imaging.ImageFormat.Png);
        var corner = bitmap.GetPixel(0, 0);
        var index16 = t.Format == ACE.Entity.Enum.SurfacePixelFormat.PFID_INDEX16;
        Console.WriteLine(JsonSerializer.Serialize(new {gfx=$"{id:X8}",part=$"{part:X8}",surface=$"{sid:X8}",type=$"{(uint)s.Type:X}",s.Translucency,s.Luminosity,s.Diffuse,texture=$"{t.Id:X8}",format=t.Format.ToString(),t.Width,t.Height,corner=new int[]{corner.R,corner.G,corner.B,corner.A}, cornerIndex=index16?BitConverter.ToUInt16(t.SourceData,0):-1}));
    }
}
