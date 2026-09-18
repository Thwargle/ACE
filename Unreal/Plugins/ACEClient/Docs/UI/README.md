# Asheron's Call UI documentation (Stages 1–4)

| File | Purpose |
|------|---------|
| [DATPath.md](DATPath.md) | Retail DAT directory |
| [UIAssetManifest.md](UIAssetManifest.md) | Stage 2 summary |
| [UIAssetManifest.json](UIAssetManifest.json) | Full LayoutDesc trees + media |
| [UIAssetManifest.csv](UIAssetManifest.csv) | Flat asset rows |
| [ElementTypeRegistry.md](ElementTypeRegistry.md) | Type → C++ class |
| [ElementTypeRegistry.csv](ElementTypeRegistry.csv) | Same as CSV |
| [Architecture.md](Architecture.md) | Unreal class mapping |

Regenerate Stage 2:

```text
dotnet run --project Unreal/diag/UIInventory/UIInventory.csproj -c Release -- "C:\Turbine\Asheron's Call" "Unreal/Plugins/ACEClient/Docs/UI"
```
