"""Export launcher resources from AC-Icon.png. Requires Pillow; no game build needed."""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[2]
with Image.open(Path(__file__).with_name('AC-Icon.png')) as source:
    icon = source.convert('RGBA')
    windows = root / 'Build/Windows/Application.ico'
    windows.parent.mkdir(parents=True, exist_ok=True)
    icon.save(windows, sizes=[(n, n) for n in (16, 20, 24, 32, 40, 48, 64, 128, 256)])
    # UE's Android manifest uses @drawable/icon. Override every engine density
    # so Android never falls back to the engine logo on another screen density.
    for density, size in [('drawable', 192), ('drawable-ldpi', 36),
                          ('drawable-mdpi', 48), ('drawable-hdpi', 72),
                          ('drawable-xhdpi', 96), ('drawable-xxhdpi', 144),
                          ('drawable-xxxhdpi', 192)]:
        output = root / 'Build/Android/res' / density / 'icon.png'
        output.parent.mkdir(parents=True, exist_ok=True)
        icon.resize((size, size), Image.Resampling.LANCZOS).save(output)
    print(f'Exported Windows and Android launcher icons from {icon.width} x {icon.height} RGBA artwork.')
