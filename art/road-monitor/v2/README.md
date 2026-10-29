# Monitor V2

`RoadMonitor-V2.blend` contains one V2 scene, editable procedural parts and hidden
single-material baked export copies. The three integration FBXs are the current
Unreal import sources. Original audio lives in the adjacent `audio/` directory.

| Part | Triangles |
| --- | ---: |
| Housing | 50,269 |
| Arm | 26,134 |
| Mount | 3,764 |

All parts share a 2048² atlas: BaseColor is sRGB; ORM is linear (R=AO,
G=roughness, B=metallic); NormalDX is the Unreal normal map and NormalGL is for
Blender. Do not flip the DirectX green channel again.

Author through MCP for Blender. With this file open, run
`integration/export_for_unreal.py` to regenerate FBXs and their contract. It
exports one UV/material slot, no animation, cameras, lights or UI images. The
integration uses Unreal Y = −Blender Y; its contract defines mechanical offsets,
hinge signs and the 29 × 19.4 cm screen opening. Preserve local mesh origins.

`bake_textures.py` contains the staged atlas bake functions for deliberate mesh
changes; do not rerun setup on an already prepared atlas. Reimport changed FBXs
and maps into the existing plugin packages with automatic material creation off.
Use the Panini-aware V2 material and keep the dynamic WidgetComponent screen
separate. No old monitor, UI concept images or studio renders are required.
