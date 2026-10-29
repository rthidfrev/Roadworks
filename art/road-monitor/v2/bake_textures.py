"""MCP Blender stages: setup(), bake('BaseColor'/'ORM'/'NormalGL'), finish()."""
import bpy
from pathlib import Path
import json

ROOT = Path(bpy.data.filepath).resolve().parent
assert (ROOT / 'monitor-v2-contract.json').is_file(), 'Open the V2 source file first'
PARTS=['SM_RoadMonitor'+g+'_V2' for g in ['Housing','Arm','Mount']]

def context():
    s=bpy.data.scenes['PR_Monitor_V2'];bpy.context.window.scene=s
    objects=[bpy.data.objects[n] for n in PARTS]
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:o.select_set(True)
    bpy.context.view_layer.objects.active=objects[0]
    return s,objects,list({m for o in objects for m in o.data.materials})

def setup():
    s,objects,materials=context()
    for o in objects:
        uv=o.data.uv_layers.new(name='PR2_Atlas')
        o.data.uv_layers.active=uv;uv.active_render=True
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=1.15,island_margin=.003,area_weight=.5,scale_to_bounds=True)
    bpy.ops.object.mode_set(mode='OBJECT')
    for role in ['BaseColor','ORM','NormalGL']:
        img=bpy.data.images.new('T_PR2_'+role,width=2048,height=2048,alpha=False)
        if role!='BaseColor':img.colorspace_settings.name='Non-Color'
        img.filepath_raw=str(ROOT/'textures'/('T_RoadMonitorV2_'+role+'.png'))
        img.file_format='PNG'
    for m in materials:
        nt=m.node_tree;ns=nt.nodes;lk=nt.links
        bs=next(n for n in ns if n.type=='BSDF_PRINCIPLED')
        bs['pr2_surface']=True
        emission=ns.new('ShaderNodeEmission');emission.name='PR2_Bake_Emission'
        target=ns.new('ShaderNodeTexImage');target.name='PR2_Bake_Target'
        combine=ns.new('ShaderNodeCombineXYZ');combine.name='PR2_Bake_ORM'
        ao=ns.new('ShaderNodeAmbientOcclusion');ao.inputs['Distance'].default_value=.35
        lk.new(ao.outputs['AO'],combine.inputs[0])
        if bs.inputs['Roughness'].is_linked:lk.new(bs.inputs['Roughness'].links[0].from_socket,combine.inputs[1])
        else:combine.inputs[1].default_value=bs.inputs['Roughness'].default_value
        combine.inputs[2].default_value=bs.inputs['Metallic'].default_value
    s.render.engine='CYCLES';s.cycles.samples=16
    s.render.bake.margin=12;s.render.bake.use_clear=False
    s.render.bake.use_selected_to_active=False
    p=bpy.context.preferences.addons['cycles'].preferences
    try:
        p.compute_device_type='OPTIX';p.get_devices()
        for d in p.devices:d.use=d.type=='OPTIX'
        s.cycles.device='GPU'
    except Exception:s.cycles.device='CPU'
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'RoadMonitor-V2.blend'))
    print({'UV':'Unique shared 2048 atlas for three rigid meshes','materials':len(materials),'device':s.cycles.device})

def bake(role):
    s,objects,materials=context()
    img=bpy.data.images['T_PR2_'+role]
    for m in materials:
        nt=m.node_tree;ns=nt.nodes;lk=nt.links
        bs=next(n for n in ns if n.type=='BSDF_PRINCIPLED')
        output=next(n for n in ns if n.type=='OUTPUT_MATERIAL')
        target=ns['PR2_Bake_Target'];target.image=img
        for n in ns:n.select=False
        target.select=True;ns.active=target
        if role=='BaseColor':
            emission=ns['PR2_Bake_Emission']
            lk.new(bs.inputs['Base Color'].links[0].from_socket,emission.inputs[0])
            lk.new(emission.outputs[0],output.inputs['Surface'])
        elif role=='ORM':
            emission=ns['PR2_Bake_Emission']
            lk.new(ns['PR2_Bake_ORM'].outputs[0],emission.inputs[0])
            lk.new(emission.outputs[0],output.inputs['Surface'])
        else:lk.new(bs.outputs[0],output.inputs['Surface'])
    try:
        bpy.ops.object.bake(type='NORMAL' if role=='NormalGL' else 'EMIT')
        img.save()
    finally:
        for m in materials:
            nt=m.node_tree;ns=nt.nodes
            bs=next(n for n in ns if n.type=='BSDF_PRINCIPLED')
            output=next(n for n in ns if n.type=='OUTPUT_MATERIAL')
            nt.links.new(bs.outputs[0],output.inputs['Surface'])
    print({'baked':role,'path':img.filepath_raw,'size':list(img.size)})

def finish():
    import numpy as np
    s,objects,materials=context()
    gl=bpy.data.images['T_PR2_NormalGL']
    buf=np.empty(2048*2048*4,dtype=np.float32);gl.pixels.foreach_get(buf)
    buf[1::4]=1-buf[1::4]
    dx=bpy.data.images.new('T_PR2_NormalDX',width=2048,height=2048,alpha=False)
    dx.colorspace_settings.name='Non-Color';dx.pixels.foreach_set(buf)
    dx.filepath_raw=str(ROOT/'textures'/'T_RoadMonitorV2_NormalDX.png');dx.file_format='PNG';dx.save()
    material=bpy.data.materials.new('PR2_Baked_PBR');material.use_nodes=True
    ns=material.node_tree.nodes;lk=material.node_tree.links
    bs=next(n for n in ns if n.type=='BSDF_PRINCIPLED')
    for role in ['BaseColor','ORM','NormalGL']:
        n=ns.new('ShaderNodeTexImage');n.image=bpy.data.images['T_PR2_'+role];n.label=role
        if role=='BaseColor':lk.new(n.outputs['Color'],bs.inputs['Base Color'])
        elif role=='ORM':
            sep=ns.new('ShaderNodeSeparateXYZ');lk.new(n.outputs['Color'],sep.inputs[0])
            lk.new(sep.outputs[1],bs.inputs['Roughness']);lk.new(sep.outputs[2],bs.inputs['Metallic'])
        else:
            nm=ns.new('ShaderNodeNormalMap');lk.new(n.outputs['Color'],nm.inputs['Color']);lk.new(nm.outputs[0],bs.inputs['Normal'])
    material['texture_contract']='BaseColor sRGB; ORM linear R=AO G=roughness B=metallic; NormalDX for Unreal, NormalGL for Blender'
    # Keep procedural authoring meshes and create one-material export copies.
    coll=bpy.data.collections.new('PR2_Baked_Export');s.collection.children.link(coll)
    for o in objects:
        c=o.copy();c.data=o.data.copy();c.name=o.name+'_Baked'
        coll.objects.link(c)
        c.data.materials.clear();c.data.materials.append(material)
        for p in c.data.polygons:p.material_index=0
        c.hide_render=True;c.hide_set(True)
        c['purpose']='Baked single-material export source; same local pivot as authored part'
    for role in ['BaseColor','ORM','NormalGL','NormalDX']:
        i=bpy.data.images['T_PR2_'+role]
        i.pack();i.filepath='//textures/'+Path(i.filepath_raw).name
    s.render.engine='BLENDER_EEVEE'
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'RoadMonitor-V2.blend'))
    print({'textures':'four packed 2K maps, including Unreal DirectX normal','export_material_slots':1})
