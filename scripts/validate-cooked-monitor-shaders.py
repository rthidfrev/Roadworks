#!/usr/bin/env python3
"""Check the two current monitor materials' cooked SM6 velocity stage signatures.

Reads only a full ShaderPipelineCacheTools Dump and its matching cooked shader
archive. It does not open Unreal, render, run gameplay, or edit an asset/config.

Pinned formats: CSS UE 5.6.1 archive version 2, little endian non-mmapped
FSerializedShaderArchive, 17-byte FShaderCodeEntry, Oodle compression, DXBC
ISG1/OSG1 or legacy ISGN/OSGN signature chunks. Unsupported formats fail closed.

Example:
  python3 scripts/validate-cooked-monitor-shaders.py \
    --stable-key-dump /absolute/path/stable-keys-full.log \
    --archive /absolute/path/ShaderArchive-ParametricRoads-PCD3D_SM6-PCD3D_SM6.ushaderbytecode \
    --report /absolute/path/monitor-shader-verification.json

Generate the full dump separately, without an output-hash filter:
  UnrealEditor-Cmd FactoryGame.uproject -run=ShaderPipelineCacheTools Dump FILE.shk -nullrhi
"""
import argparse
import csv
import ctypes
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import sys


ROOT = Path(__file__).resolve().parent.parent
FOLDER = '/ParametricRoads/Monitor/Materials/'
MATERIALS = tuple(FOLDER + name for name in (
    'M_RoadMonitorWidget', 'MI_RoadMonitor_V2'))
DEFAULT_OODLE = ROOT / ('.local/UnrealEngineCSS/Engine/Source/Programs/UnrealBuildTool/'
                        'bin/Development/liboo2corelinux64.so.9')
RASTERIZER_GENERATED = {
    'SV_ISFRONTFACE', 'SV_SAMPLEINDEX', 'SV_COVERAGE', 'SV_INNERCOVERAGE',
    'SV_PRIMITIVEID', 'SV_BARYCENTRICS', 'SV_SHADINGRATE',
}
MAX_BYTES = 512 * 1024 * 1024


class VerificationError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_file(path):
    require(path.is_file(), f'File does not exist: {path}')
    require(path.stat().st_size <= MAX_BYTES, f'File exceeds the 512 MiB verifier limit: {path}')
    return path.read_bytes()


def stable_pairs(text):
    """Accept real commandlet log prefixes and repeated console log lines."""
    groups = {name: {} for name in MATERIALS}
    pattern = re.compile(r'(?:MaterialInstanceConstant|Material) /ParametricRoads/Monitor/Materials/[^\r\n]+')
    for line in text.splitlines():
        found = pattern.search(line)
        if not found:
            continue
        fields = next(csv.reader([found.group(0)]))
        if len(fields) != 12:
            continue
        obj, shader, _, _, feature, quality, frequency, platform, vf, permutation, output, pipeline = fields
        path = obj.split(' ', 1)[1].split('.', 1)[0]
        if path not in groups or shader not in ('FVelocityVS', 'FVelocityPS'):
            continue
        if (feature, platform, vf, permutation) != ('SM6', 'PCD3D_SM6', 'FLocalVertexFactory', 'Perm_0'):
            continue
        require(frequency == ('SF_Vertex' if shader.endswith('VS') else 'SF_Pixel'),
                f'Unexpected shader frequency in stable key: {found.group(0)}')
        require(re.fullmatch(r'[0-9a-fA-F]{40}', output) is not None
                and re.fullmatch(r'[0-9a-fA-F]{40}', pipeline) is not None,
                f'Unsupported shader hash format: {found.group(0)}')
        key = (quality, permutation, pipeline.upper())
        entry = groups[path].setdefault(key, {'FVelocityVS': set(), 'FVelocityPS': set()})
        entry[shader].add(output.upper())
    return groups


class ShaderArchive:
    def __init__(self, data, oodle):
        self.data = data
        self.oodle_path = oodle
        self.decoder = None
        self.position = 0
        require(self.number() == 2, 'Unsupported shader archive version; CSS UE 5.6.1 version 2 is required.')
        self.map_hashes = self.array(20)
        self.hashes = self.array(20)
        self.map_entries = self.array(16, '<4I')
        self.entries = self.array(17, '<QIIB')
        self.array(16, '<QQ')  # Preload entries: offset and size.
        self.indices = self.array(4, '<I')
        self.code_start = self.position
        require(len(self.hashes) == len(self.entries), 'Shader hash/entry count mismatch.')
        require(len(self.map_hashes) == len(self.map_entries), 'Shader map hash/entry count mismatch.')
        self.lookup = {h.hex().upper(): i for i, h in enumerate(self.hashes)}
        require(len(self.lookup) == len(self.hashes), 'Duplicate shader hashes in archive.')
        for offset, size, uncompressed, _ in self.entries:
            require(0 < size <= uncompressed <= MAX_BYTES, 'Invalid shader code size in archive.')
            require(self.code_start + offset + size <= len(data), 'Shader code extends past archive end.')
        for offset, count, _, _ in self.map_entries:
            require(offset + count <= len(self.indices), 'Shader map indices extend past index array.')
        require(all(i[0] < len(self.entries) for i in self.indices), 'Out-of-range shader index.')

    def number(self):
        require(self.position + 4 <= len(self.data), 'Truncated shader archive header.')
        value = struct.unpack_from('<I', self.data, self.position)[0]
        self.position += 4
        return value

    def array(self, width, fmt=None):
        count = self.number()
        require(count <= 1_000_000 and self.position + count * width <= len(self.data),
                f'Invalid archive array count {count} at byte {self.position - 4}.')
        values = [(struct.unpack_from(fmt, self.data, self.position + i * width) if fmt
                   else self.data[self.position + i * width:self.position + (i + 1) * width])
                  for i in range(count)]
        self.position += count * width
        return values

    def decompress(self, compressed, size):
        if self.decoder is None:
            require(self.oodle_path.is_file(), f'Pinned Oodle library not found: {self.oodle_path}')
            try:
                self.library = ctypes.CDLL(str(self.oodle_path))
                self.decoder = self.library.OodleLZ_Decompress
            except (OSError, AttributeError) as error:
                raise VerificationError(f'Cannot load Oodle decoder: {error}') from error
            self.decoder.argtypes = [ctypes.c_void_p, ctypes.c_int64, ctypes.c_void_p, ctypes.c_int64,
                                     ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_void_p,
                                     ctypes.c_int64, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                                     ctypes.c_int64, ctypes.c_int]
            self.decoder.restype = ctypes.c_int64
        source = ctypes.create_string_buffer(compressed)
        target = ctypes.create_string_buffer(size)
        result = self.decoder(source, len(compressed), target, size, 1, 0, 0,
                              None, 0, None, None, None, 0, 3)
        require(result == size, f'Oodle decompression failed: returned {result}, expected {size}.')
        return target.raw

    def shader(self, shader_hash, frequency):
        require(shader_hash in self.lookup, f'Stable-key shader hash is absent from archive: {shader_hash}')
        index = self.lookup[shader_hash]
        offset, size, uncompressed, actual_frequency = self.entries[index]
        require(actual_frequency == frequency,
                f'Shader {shader_hash} has frequency {actual_frequency}, expected {frequency}.')
        code = self.data[self.code_start + offset:self.code_start + offset + size]
        if size != uncompressed:
            code = self.decompress(code, uncompressed)
        # UE serializes its resource table before the DXBC container and appends
        # optional data afterwards. Validate the container instead of hard-coding
        # the resource table's variable length.
        start = code.find(b'DXBC')
        require(start >= 0 and start + 32 <= len(code), f'No DXBC container in shader {shader_hash}.')
        container_size = struct.unpack_from('<I', code, start + 24)[0]
        require(container_size >= 32 and start + container_size <= len(code),
                f'Truncated DXBC container in shader {shader_hash}.')
        return code[start:start + container_size]


def signature(payload, modern):
    require(len(payload) >= 8, 'Truncated DXBC signature header.')
    count, offset = struct.unpack_from('<II', payload)
    width = 32 if modern else 24
    require(count < 4096 and offset >= 8 and offset + count * width <= len(payload),
            'Unsupported or truncated DXBC signature parameters.')
    result = []
    for index in range(count):
        position = offset + index * width
        if modern:
            stream, name_offset, semantic_index, system, value_type, register = struct.unpack_from('<6I', payload, position)
            mask, used = struct.unpack_from('<BB', payload, position + 24)
        else:
            name_offset, semantic_index, system, value_type, register = struct.unpack_from('<5I', payload, position)
            mask, used = struct.unpack_from('<BB', payload, position + 20)
            stream = 0
        require(name_offset < len(payload), 'Invalid signature semantic name offset.')
        terminator = payload.find(b'\0', name_offset)
        require(terminator >= 0, 'Unterminated signature semantic name.')
        semantic = payload[name_offset:terminator].decode('ascii')
        require(value_type in (1, 2, 3), f'Unsupported signature component type {value_type}.')
        result.append({'semantic': semantic, 'index': semantic_index, 'system_value': system,
                       'component_type': value_type, 'register': register, 'mask': mask,
                       'used_mask': used, 'stream': stream})
    return result


def dxbc_signatures(code):
    require(code[:4] == b'DXBC' and len(code) >= 32, 'Invalid DXBC container.')
    count = struct.unpack_from('<I', code, 28)[0]
    require(count < 4096 and 32 + count * 4 <= len(code), 'Invalid DXBC chunk table.')
    output = {}
    chunks = set()
    for index in range(count):
        offset = struct.unpack_from('<I', code, 32 + index * 4)[0]
        require(offset + 8 <= len(code), 'DXBC chunk header extends past container.')
        name = code[offset:offset + 4].decode('ascii')
        size = struct.unpack_from('<I', code, offset + 4)[0]
        require(offset + 8 + size <= len(code), f'Truncated DXBC {name} chunk.')
        chunks.add(name)
        if name in ('ISG1', 'ISGN', 'OSG1', 'OSGN'):
            direction = 'input' if name.startswith('I') else 'output'
            require(direction not in output, f'Duplicate DXBC {direction} signature chunk.')
            output[direction] = signature(code[offset + 8:offset + 8 + size], name.endswith('1'))
    require('DXIL' in chunks, 'Expected a cooked SM6 DXIL shader, not an SM5 container.')
    require('input' in output and 'output' in output, 'DXBC input/output signature is missing.')
    return output


def linkage_errors(vertex, pixel):
    outputs = {(s['semantic'].upper(), s['index']): s for s in vertex['output']}
    errors = []
    for consumed in pixel['input']:
        # DXBC input ReadWriteMask records actually consumed components. For
        # outputs the same byte has inverse meaning, so use output Mask there.
        if not consumed['used_mask'] or consumed['semantic'].upper() in RASTERIZER_GENERATED:
            continue
        key = (consumed['semantic'].upper(), consumed['index'])
        produced = outputs.get(key)
        reasons = []
        if produced is None:
            reasons.append('semantic is not produced by vertex stage')
        else:
            if produced['register'] != consumed['register']:
                reasons.append(f"register mismatch: VS {produced['register']} -> PS {consumed['register']}")
            if produced['component_type'] != consumed['component_type']:
                reasons.append('component type mismatch')
            written_mask = produced['mask'] & ~produced['used_mask']
            if consumed['used_mask'] & ~written_mask:
                reasons.append('pixel shader consumes components not written by vertex output')
            if produced['stream'] != consumed['stream']:
                reasons.append('stream mismatch')
        if reasons:
            errors.append({'semantic': consumed['semantic'], 'index': consumed['index'],
                           'reasons': reasons, 'vertex_output': produced, 'pixel_input': consumed})
    return errors


def verify(args, receipt):
    archive_bytes = read_file(args.archive)
    dump_bytes = read_file(args.stable_key_dump)
    receipt['inputs'] = {
        'archive': {'path': str(args.archive.resolve()), 'sha256': digest(archive_bytes)},
        'stable_key_dump': {'path': str(args.stable_key_dump.resolve()), 'sha256': digest(dump_bytes)},
        'oodle_library': str(args.oodle_library.resolve()),
    }
    archive = ShaderArchive(archive_bytes, args.oodle_library)
    receipt['archive'] = {'version': 2, 'shader_maps': len(archive.map_hashes),
                          'shaders': len(archive.hashes), 'code_offset': archive.code_start}
    groups = stable_pairs(dump_bytes.decode('utf-8', errors='replace'))
    cached = {}
    for material, pipelines in groups.items():
        entry = {'material': material, 'pipelines': [], 'errors': []}
        receipt['materials'].append(entry)
        if not pipelines:
            entry['errors'].append('No FLocalVertexFactory SM6 Perm_0 velocity keys. Supply the FULL stable-key dump.')
        for (quality, permutation, pipeline_hash), stages in sorted(pipelines.items()):
            pair = {'pipeline_hash': pipeline_hash, 'quality': quality, 'permutation': permutation, 'errors': []}
            entry['pipelines'].append(pair)
            try:
                for stage, frequency in [('FVelocityVS', 0), ('FVelocityPS', 3)]:
                    hashes = stages[stage]
                    require(len(hashes) == 1, f'{stage} requires one exact hash; found {len(hashes)}. Dump may be filtered or ambiguous.')
                    shader_hash = next(iter(hashes))
                    pair[stage] = shader_hash
                    if shader_hash not in cached:
                        cached[shader_hash] = dxbc_signatures(archive.shader(shader_hash, frequency))
                pair['errors'] = linkage_errors(cached[pair['FVelocityVS']], cached[pair['FVelocityPS']])
                pair['result'] = 'failed' if pair['errors'] else 'passed'
            except (VerificationError, UnicodeError, struct.error) as error:
                pair['result'] = 'error'
                pair['errors'].append(str(error))
        entry['result'] = ('failed' if entry['errors'] or any(p['result'] != 'passed' for p in entry['pipelines']) else 'passed')
    receipt['signatures'] = cached
    receipt['result'] = 'passed' if all(m['result'] == 'passed' for m in receipt['materials']) else 'failed'
    return 0 if receipt['result'] == 'passed' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--stable-key-dump', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--oodle-library', type=Path, default=DEFAULT_OODLE)
    args = parser.parse_args()
    receipt = {'schema_version': 1, 'observed_at': datetime.now(timezone.utc).isoformat(),
               'result': 'error', 'materials': [],
               'scope': 'Targeted offline SM6 velocity stage-link check; no graphics device, PSO creation, rendering or gameplay.',
               'limitations': ['Does not validate root signatures, other passes/platforms, driver acceptance, or runtime appearance.'],
               'format': 'CSS UE 5.6.1 archive v2 non-mmapped + DXBC signatures, pinned source layout.'}
    try:
        code = verify(args, receipt)
    except (VerificationError, OSError, UnicodeError, struct.error) as error:
        receipt['error'] = str(error)
        code = 2
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(receipt, indent=2) + '\n')
    print(f"Cooked monitor velocity signatures: {receipt['result']}; {args.report}")
    for material in receipt['materials']:
        print(f"  {material['material'].rsplit('/', 1)[-1]}: {material['result']}")
        for pair in material['pipelines']:
            for error in pair['errors']:
                print('    ' + (str(error) if isinstance(error, str) else '; '.join(error['reasons'])))
    if 'error' in receipt:
        print(receipt['error'], file=sys.stderr)
    return code


if __name__ == '__main__':
    sys.exit(main())
