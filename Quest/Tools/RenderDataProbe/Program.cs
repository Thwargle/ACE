using ACE.Common;
using ACE.Database;
using ACE.DatLoader;
using ACE.DatLoader.FileTypes;
using ACE.Entity;
using ACE.Server.Factories;
using System.Text.Json;
System.Text.Encoding.RegisterProvider(System.Text.CodePagesEncodingProvider.Instance);
ConfigManager.Initialize(Path.GetFullPath("Unreal/PackagedVR/Server/Config.js"));
DatManager.Initialize(@"C:\Turbine\Asheron's Call",true,true);
foreach(uint id in new uint[]{8904})
{
    var item=WorldObjectFactory.CreateWorldObject(DatabaseManager.World.GetCachedWeenie(id),new ObjectGuid(0x8fffffff));
    var desc=item.CalculateObjDesc();
    Console.WriteLine("ITEM "+JsonSerializer.Serialize(new {id, item.SetupTableId,desc},new JsonSerializerOptions{IncludeFields=true}));
    foreach(var part in desc.AnimPartChanges)
    foreach(var sid in DatManager.PortalDat.ReadFromDat<GfxObj>(part.AnimationId).Surfaces)
    {
        var s=DatManager.PortalDat.ReadFromDat<Surface>(sid);
        Console.WriteLine($"SURFACE {sid:X8} pal={s.OrigPaletteId:X8} tex={s.OrigTextureId:X8}");
    }
    foreach(var change in desc.TextureChanges)
    foreach(var tid in DatManager.PortalDat.ReadFromDat<SurfaceTexture>(change.NewTexture).Textures)
    {
        var t=DatManager.PortalDat.ReadFromDat<Texture>(tid);
        Console.WriteLine($"REPLACEMENT {tid:X8} fmt={t.Format} size={t.Width}x{t.Height} pal={t.DefaultPaletteId:X8}");
    }
}
foreach(uint scriptId in new uint[]{0x330007DB})
{
    var script=DatManager.PortalDat.ReadFromDat<PhysicsScript>(scriptId);
    Console.WriteLine("SCRIPT "+JsonSerializer.Serialize(script,new JsonSerializerOptions{IncludeFields=true}));
    foreach(var entry in script.ScriptData)
    {
        Console.WriteLine(JsonSerializer.Serialize(entry.Hook,entry.Hook.GetType(),new JsonSerializerOptions{IncludeFields=true}));
        if(entry.Hook is ACE.DatLoader.Entity.AnimationHooks.CreateParticleHook hook)
        {
            var pe=DatManager.PortalDat.ReadFromDat<ParticleEmitterInfo>(hook.EmitterInfoId);
            Console.WriteLine("EMITTER "+JsonSerializer.Serialize(pe,new JsonSerializerOptions{IncludeFields=true}));
            var gfx=DatManager.PortalDat.ReadFromDat<GfxObj>(pe.HwGfxObjId != 0 ? pe.HwGfxObjId : pe.GfxObjId);
            foreach(var sid in gfx.Surfaces) Console.WriteLine("PE_SURFACE "+JsonSerializer.Serialize(DatManager.PortalDat.ReadFromDat<Surface>(sid)));
        }
    }
}

