using ACE.Common;
using ACE.Database;
using ACE.Database.Models.World;
using ACE.DatLoader;
using ACE.DatLoader.FileTypes;
using ACE.Entity.Enum.Properties;
using Microsoft.EntityFrameworkCore;
using System.Text.Json;
System.Text.Encoding.RegisterProvider(System.Text.CodePagesEncodingProvider.Instance);
ConfigManager.Initialize(Path.GetFullPath("Unreal/PackagedVR/Server/Config.js"));
DatManager.Initialize(@"C:\Turbine\Asheron's Call",true,true);
using var db=new WorldDbContext();
var rats=db.WeeniePropertiesString.AsNoTracking().Where(p=>p.Type==(int)PropertyString.Name && p.Value.EndsWith("Rat")).Select(p=>new { p.ObjectId,p.Value }).ToList();
foreach(var rat in rats)
{
    var w=DatabaseManager.World.GetCachedWeenie(rat.ObjectId);
    if(!w.PropertiesDID.TryGetValue(PropertyDataId.Setup,out var setupId))continue;
    var s=DatManager.PortalDat.ReadFromDat<SetupModel>(setupId);
    var scale=w.PropertiesFloat!=null && w.PropertiesFloat.TryGetValue(PropertyFloat.DefaultScale,out var value)?value:1.0;
    Console.WriteLine("RAT "+JsonSerializer.Serialize(new {rat.ObjectId,rat.Value,w.ClassName,w.WeenieType,setup=setupId.ToString("X8"),scale,s.Height,s.Radius,s.SelectionSphere,s.CylSpheres,s.Spheres},new JsonSerializerOptions{IncludeFields=true}));
}
