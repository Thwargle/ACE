using System.Text;
using ACE.DatLoader;
using ACE.DatLoader.FileTypes;

Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
DatManager.Initialize(@"C:\Turbine\Asheron's Call", true, false);

uint lb = 0x7D640000;
var info = DatManager.CellDat.ReadFromDat<LandblockInfo>(lb);
Console.WriteLine($"buildings={info.Buildings.Count} cells={info.NumCells}");
for (int i = 0; i < info.Buildings.Count; i++)
{
    var b = info.Buildings[i];
    Console.WriteLine($"[{i}] model=0x{b.ModelId:X8} portals={b.Portals.Count} origin=({b.Frame.Origin.X:F1},{b.Frame.Origin.Y:F1},{b.Frame.Origin.Z:F1})");
    try {
      var setup = DatManager.PortalDat.ReadFromDat<SetupModel>(b.ModelId);
      int parts = setup.Parts.Count(p => p != 0 && p != 0x010001EC);
      Console.WriteLine($"  setup parts={parts}");
    } catch (Exception ex) { Console.WriteLine($"  setup err {ex.Message}"); }
}
