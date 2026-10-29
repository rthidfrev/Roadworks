"""Run through MCP for Blender; derive UE-ready V2 exports without editing art.

The art export compensates Unreal's handedness conversion to preserve numeric
coordinates. That reverses front-view text/right-left when moving between the
two coordinate systems. These integration copies deliberately omit that extra
Y reflection. Unreal's normal conversion then produces UE Y = -Blender Y.
"""
import hashlib
import json
import os
from pathlib import Path

import bpy


ART = Path(bpy.data.filepath).resolve().parent
assert (ART / 'monitor-v2-contract.json').is_file(), 'Open the V2 source file first'
DEST = ART / 'integration'
DEST.joinpath('exports').mkdir(parents=True, exist_ok=True)
source = json.loads((ART / 'monitor-v2-contract.json').read_text())
scene = bpy.data.scenes['PR_Monitor_V2']
before_scene = bpy.context.window.scene
before_selection = list(bpy.context.selected_objects)
before_active = bpy.context.view_layer.objects.active
bpy.context.window.scene = scene
report = {}


def reflected(point):
    return [point[0], -point[1], point[2]]


def portable_fbx_paths(path):
    """Keep source/texture links relative, including FBX's native-file metadata."""
    from io_scene_fbx import encode_bin, parse_fbx
    tree, version = parse_fbx.parse(str(path))
    methods = dict(zip(b'BCZYILFDRSilfdbc', (
        'bool', 'char', 'int8', 'int16', 'int32', 'int64', 'float32', 'float64',
        'bytes', 'string', 'int32_array', 'int64_array', 'float32_array',
        'float64_array', 'bool_array', 'byte_array')))

    def clone(node):
        result = encode_bin.FBXElem(node.id)
        for kind, value in zip(node.props_type, node.props):
            if kind == ord('S') and value.startswith(str(ART).encode() + b'/'):
                value = os.path.relpath(value.decode(), path.parent).replace(os.sep, '/').encode()
            getattr(result, 'add_' + methods[kind])(value)
        result.elems = [clone(child) for child in node.elems]
        return result

    encode_bin.write(str(path), clone(tree), version)


try:
    for name, art_spec in source['assets'].items():
        baked = bpy.data.objects['SM_RoadMonitor' + name + '_V2_Baked']
        assert len(baked.data.materials) == 1 and baked.data.materials[0].name == 'PR2_Baked_PBR'
        mesh = baked.data.copy()
        export = bpy.data.objects.new('PR_UE_Export_' + name, mesh)
        scene.collection.objects.link(export)
        try:
            atlas = mesh.uv_layers.get('PR2_Atlas')
            assert atlas, (name, list(mesh.uv_layers.keys()))
            for layer in list(mesh.uv_layers):
                if layer != atlas:
                    mesh.uv_layers.remove(layer)
            atlas.name = 'UVMap'
            mesh.uv_layers.active_index = 0
            atlas.active_render = True
            bpy.ops.object.select_all(action='DESELECT')
            export.select_set(True)
            bpy.context.view_layer.objects.active = export
            path = DEST / 'exports' / ('SM_RoadMonitor' + name + '_V2_UE.fbx')
            properties = bpy.ops.export_scene.fbx.get_rna_type().properties
            for prop, value in [('axis_forward', 'X'), ('axis_up', 'Z'),
                                ('apply_scale_options', 'FBX_SCALE_NONE'), ('mesh_smooth_type', 'FACE')]:
                assert value in [entry.identifier for entry in properties[prop].enum_items]
            bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={'MESH'},
                axis_forward='X', axis_up='Z', global_scale=1, apply_unit_scale=True,
                apply_scale_options='FBX_SCALE_NONE', use_space_transform=True,
                bake_space_transform=False, bake_anim=False, use_mesh_modifiers=True,
                mesh_smooth_type='FACE', add_leaf_bones=False, path_mode='AUTO')
            portable_fbx_paths(path)
            old_min = art_spec['bounds_cm']['min']
            old_max = art_spec['bounds_cm']['max']
            minimum = [old_min[0], -old_max[1], old_min[2]]
            maximum = [old_max[0], -old_min[1], old_max[2]]
            report[name] = {
                'file': str(path.relative_to(DEST)),
                'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                'triangles': len(baked.data.polygons), 'material_slots': 1,
                'materials': ['PR2_Baked_PBR'], 'uv_channels': 1,
                'dimensions_cm': [maximum[i] - minimum[i] for i in range(3)],
                'coordinate_check': {'min_cm': minimum, 'max_cm': maximum,
                                     'landmarks_cm': [reflected(p) for p in art_spec['landmarks_cm']]},
            }
        finally:
            bpy.data.objects.remove(export, do_unlink=True)
            bpy.data.meshes.remove(mesh)
finally:
    bpy.context.window.scene = before_scene
    bpy.ops.object.select_all(action='DESELECT')
    for obj in before_selection:
        if obj.name in bpy.context.view_layer.objects:
            obj.select_set(True)
    if before_active and before_active.name in bpy.context.view_layer.objects:
        bpy.context.view_layer.objects.active = before_active

contract = {
    'schema_version': 2, 'art_version': 'V2 Unreal integration',
    'source_art_contract_sha256': hashlib.sha256((ART / 'monitor-v2-contract.json').read_bytes()).hexdigest(),
    'units': 'cm', 'blender_unit_scale': .01,
    'coordinate_system': 'Unreal X depth, Y width, Z up; front -X; UE Y = -Blender Y',
    'fbx': {'axis_forward': 'X', 'axis_up': 'Z', 'apply_unit_scale': True,
            'export_mirror_y': False, 'unreal_force_front_x_axis': False,
            'generate_lightmap_uvs': False},
    'screen': source['screen'],
    'assembly': {**{key: reflected(value) for key, value in source['assembly'].items() if key.endswith('_cm')},
                 'hinge_axis': 'X', 'unreal_fold_roll_degrees': {'arm': 95, 'housing': -80}},
    'assets': report, 'material': source['material'], 'textures': source['textures'],
    'notes': ['Derived export of the V2 source; procedural geometry and baked maps are unchanged.',
              'Text local +X maps to Blender -Y; UE Y reflection preserves front-view reading.',
              'Mirror all assembly Y offsets and hinge roll signs; keep the dynamic widget native and unmirrored.',
              'Import checks must compare these UE coordinates, not the original Blender coordinate bounds.'],
}
(DEST / 'monitor-unreal-contract.json').write_text(json.dumps(contract, indent=2) + '\n')
print(json.dumps({'result': 'exported', 'contract': str(DEST / 'monitor-unreal-contract.json'),
                  'assets': report, 'assembly': contract['assembly']}, indent=2))
