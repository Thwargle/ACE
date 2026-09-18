"""Inventory owned runtime source and expensive-operation sites without reading configs.

Run after regenerating Unreal/Saved/PerformanceReview-source-files.txt with rg.
This is a navigation aid, not a substitute for frame profiling or code review.
"""
from pathlib import Path
import collections
import json
import re

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "Unreal/Saved/PerformanceReview-source-files.txt"
PATTERNS = {
    "render_flush": r"\bFlushRenderingCommands\s*\(",
    "render_proxy_rebuild": r"\bMarkRenderStateDirty\s*\(",
    "dynamic_mesh_rebuild": r"\bCreateMeshSection(?:_LinearColor)?\s*\(",
    "dynamic_mesh_update": r"\bUpdateMeshSection(?:_LinearColor)?\s*\(",
    "world_actor_scan": r"\bTActorIterator\s*<",
    "scene_capture": r"\bCaptureScene\s*\(",
    "physics_cook": r"\bCreatePhysicsMeshes\s*\(",
    "explicit_sleep": r"\b(?:Sleep|SleepNoStats)\s*\(",
}
modules = collections.defaultdict(lambda: {"files": 0, "lines": 0, "bytes": 0})
sites = collections.defaultdict(list)
largest = []
for relative in FILES.read_text(encoding="utf-8-sig").splitlines():
    path = ROOT / relative
    code = path.read_text(encoding="utf-8-sig", errors="replace")
    relative = relative.replace("\\", "/")
    parts = relative.split("/")
    module = "/".join(parts[:2] if parts[0] == "Source" else parts[:4])
    lines = code.splitlines()
    group = modules[module]
    group["files"] += 1
    group["lines"] += len(lines)
    group["bytes"] += path.stat().st_size
    largest.append({"path": relative, "lines": len(lines)})
    if "/Tests/" in relative or ".Tests/" in relative:
        continue
    for number, line in enumerate(lines, 1):
        if line.lstrip().startswith(("//", "*")):
            continue
        for name, pattern in PATTERNS.items():
            if re.search(pattern, line):
                sites[name].append({"path": relative, "line": number, "code": line.strip()})
result = {
    "scope": "Owned C++/C# source; excludes SDKs, binaries, object files, generated build folders, and third-party reference trees.",
    "modules": dict(sorted(modules.items())),
    "largest_files": sorted(largest, key=lambda entry: entry["lines"], reverse=True)[:30],
    "operation_sites": dict(sites),
}
output = ROOT / "Unreal/Saved/PerformanceReview-inventory.json"
output.write_text(json.dumps(result, indent=2), encoding="utf-8")
print(json.dumps({"source_files": sum(x["files"] for x in modules.values()),
                  "source_lines": sum(x["lines"] for x in modules.values()),
                  "operation_counts": {name: len(entries) for name, entries in sites.items()}}, indent=2))
