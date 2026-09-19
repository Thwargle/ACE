using ACE.Common;
using ACE.Database;
using ACE.DatLoader;
using ACE.DatLoader.FileTypes;
using ACE.Entity;
using ACE.Server.Factories;
using System.Text.Json;
System.Text.Encoding.RegisterProvider(System.Text.CodePagesEncodingProvider.Instance);
ConfigManager.Initialize(Path.GetFullPath("Unreal/PackagedVR/Server/Config.js"));
DatManager.Initialize(@"C:\Turbine\Asheron's Call", true, true);
var options = new JsonSerializerOptions { IncludeFields = true };
var item = WorldObjectFactory.CreateWorldObject(DatabaseManager.World.GetCachedWeenie(644), new ObjectGuid(0x7C88C032));
Console.WriteLine("ITEM " + JsonSerializer.Serialize(new { item.Name, item.SetupTableId, item.ObjectDescriptionFlags, desc=item.CalculateObjDesc() }, options));
var setup = DatManager.PortalDat.ReadFromDat<SetupModel>(0x02000489);
Console.WriteLine("SETUP " + JsonSerializer.Serialize(setup, options));
if (setup.DefaultAnimation != 0)
{
    var anim = DatManager.PortalDat.ReadFromDat<Animation>(setup.DefaultAnimation);
    Console.WriteLine("ANIMATION " + JsonSerializer.Serialize(new { anim.Id, anim.Flags, anim.NumParts, anim.NumFrames, first=anim.PartFrames.First(), last=anim.PartFrames.Last() }, options));
}


