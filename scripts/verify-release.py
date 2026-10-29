#!/usr/bin/env python3
"""Freeze and verify a local release. Never starts a game or uploads anything."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent.parent
PLUGIN = ROOT / 'Mods/Roadworks'
LOGS = ROOT / '.local/logs'
FROZEN = LOGS / 'release-frozen-source.json'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes():
    return {str(p.relative_to(ROOT)): digest(p) for p in sorted(PLUGIN.rglob('*'))
            if p.is_file() and not {'Binaries', 'Intermediate', 'Saved'}.intersection(p.relative_to(PLUGIN).parts)}


def verify_tests(path, editor_exit=None):
    text = path.read_text()
    expected = set()
    for source in (PLUGIN / 'Source').rglob('*.cpp'):
        expected.update(re.findall(r'IMPLEMENT_SIMPLE_AUTOMATION_TEST\([^,]+,\s*"(Roadworks\.Editor\.[^"]+)"', source.read_text()))
    results = re.findall(r'Test Completed\. Result=\{([^}]+)\} Name=\{[^}]+\} Path=\{([^}]+)\}', text)
    require(bool(expected), 'No worldless tests found in source')
    require(len(results) == len(expected) and {name for _, name in results} == expected,
            'Incomplete or duplicate worldless test results')
    require(all(result == 'Success' for result, _ in results), 'A worldless test failed')
    require(f'Automation Test Queue Empty {len(expected)} tests performed.' in text, 'Missing completed-queue marker')
    require(editor_exit in (None, 0, 1), f'Unexpected editor exit: {editor_exit}')
    return {'result': 'passed', 'tests': len(expected), 'names': sorted(expected),
            'scope': 'Worldless editor tests only; no game, PIE or multiplayer qualification.'}


def inspect_package(archive, descriptor, prefix=''):
    """Check actual Alpakit shape, CRC, native binaries, notices and no loose config."""
    with zipfile.ZipFile(archive) as package:
        require(package.testzip() is None, 'ZIP CRC failure')
        names = [info.filename for info in package.infolist() if not info.is_dir()]
        require(len(names) == len(set(names)), 'Duplicate ZIP member')
        for info in package.infolist():
            path = PurePosixPath(info.filename)
            require(not path.is_absolute() and '..' not in path.parts and '\\' not in info.filename
                    and not stat.S_ISLNK(info.external_attr >> 16), 'Unsafe archive member')
            require(info.filename.startswith(prefix), 'Unexpected platform in release')
        relative = [name[len(prefix):] for name in names]
        for name in relative:
            require(name == 'Roadworks.uplugin' or name.startswith(('Binaries/Win64/', 'Content/Paks/Windows/', 'Resources/')),
                    f'Unexpected shipping file: {name}')
            require(not name.lower().endswith(('.ini', '.cpp', '.h', '.blend', '.fbx', '.json')), f'Development data shipped: {name}')
        packed = json.loads(package.read(prefix + 'Roadworks.uplugin'))
        require(packed['VersionName'] == descriptor['VersionName'] == packed['SemVersion'], 'Version mismatch')
        require(packed['RequiredOnRemote'] is False and packed['GameVersion'] == descriptor['GameVersion'], 'Compatibility metadata changed')
        for store in ('Steam', 'EGS'):
            stem = f'FactoryGame{store}'
            require(f'Binaries/Win64/{stem}-Roadworks-Win64-Shipping.dll' in relative, f'Missing {store} binary')
            modules = json.loads(package.read(prefix + f'Binaries/Win64/{stem}-Win64-Shipping.modules'))
            require(set(modules['Modules']) == {'Roadworks'}, 'Foreign module in payload')
        for name, source in [('License.txt', 'LICENSE'), ('AssetLicense.txt', 'ASSET_LICENSE.md'), ('ThirdPartyNotices.txt', 'THIRD_PARTY_NOTICES.md')]:
            require(package.read(prefix + 'Resources/' + name) == (ROOT / source).read_bytes(), f'Missing/stale notice: {name}')
        for suffix in ('pak', 'utoc', 'ucas'):
            require(any(name.endswith('.' + suffix) for name in relative), f'Missing cooked {suffix}')
        return {name[len(prefix):]: hashlib.sha256(package.read(name)).hexdigest() for name in names}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--freeze', action='store_true')
    parser.add_argument('--tests-only', action='store_true')
    parser.add_argument('--editor-exit', type=int)
    parser.add_argument('--test-log', type=Path, default=LOGS / 'release-editor-tests.log')
    parser.add_argument('--build-log', type=Path, default=LOGS / 'release-editor-build.console.log')
    parser.add_argument('--starter', type=Path, default=Path(os.environ.get('SATISFACTORY_STARTER', ROOT / '.local/SatisfactoryModLoader')))
    args = parser.parse_args()
    if args.freeze:
        LOGS.mkdir(parents=True, exist_ok=True)
        frozen_text = json.dumps(source_hashes(), indent=2) + '\n'
        if not FROZEN.exists() or FROZEN.read_text() != frozen_text:
            FROZEN.write_text(frozen_text)
        print('Plugin inputs frozen; do not change them before verification.')
        return
    tests = verify_tests(args.test_log, args.editor_exit)
    if args.tests_only:
        print(json.dumps(tests))
        return
    frozen = json.loads(FROZEN.read_text())
    require(source_hashes() == frozen, 'Plugin inputs changed after freeze')
    require(not subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip(), 'Commit reviewed source before delivery')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    require('Result: Succeeded' in args.build_log.read_text(), 'Linux editor build did not succeed')
    require('BUILD SUCCESSFUL' in (LOGS / 'release-package.console.log').read_text(), 'Windows packaging failed')
    descriptor = json.loads((PLUGIN / 'Roadworks.uplugin').read_text())
    version = descriptor['SemVersion']
    require(descriptor['VersionName'] == version and descriptor['Version'] == int(version.split('.')[0]), 'Alpakit version fields differ')
    archive_dir = args.starter / 'Saved/ArchivedPlugins/Roadworks'
    per_platform = archive_dir / 'Roadworks-Windows.zip'
    combined = archive_dir / 'Roadworks.zip'
    files = inspect_package(per_platform, descriptor)
    require(inspect_package(combined, descriptor, 'Windows/') == files, 'Combined release differs from Windows package')
    shader_file = LOGS / 'monitor-shader-verification.json'
    shaders = json.loads(shader_file.read_text())
    require(shaders['result'] == 'passed' and len(shaders['materials']) == 2, 'Monitor shader signatures failed')
    require(shader_file.stat().st_mtime >= combined.stat().st_mtime, 'Stale shader evidence')
    latest_input = max((ROOT / path).stat().st_mtime for path in frozen)
    require(args.test_log.stat().st_mtime >= latest_input and args.build_log.stat().st_mtime >= latest_input,
            'Build/test evidence predates plugin inputs')
    authored = PLUGIN / 'Content'
    assets = sorted(str(path.relative_to(authored)) for path in authored.rglob('*.uasset'))
    cooked = PLUGIN / 'Saved/Cooked/Windows/FactoryGame/Mods/Roadworks/Content'
    require(bool(assets) and all((cooked / path).is_file() for path in assets), 'An authored asset was not cooked')
    asset_audit = json.loads((LOGS / 'release-assets.json').read_text())
    require(asset_audit['result'] == 'passed' and asset_audit['assets'] == len(assets), 'Asset dependency audit failed')
    require(asset_audit['authored_sha256'] == {str(p.relative_to(authored)): digest(p) for p in authored.rglob('*') if p.is_file()},
            'Asset audit no longer describes the source')
    lock = json.loads((ROOT / 'environment.lock.json').read_text())
    dist = ROOT / 'dist'; dist.mkdir(exist_ok=True)
    outputs = {}
    for suffix, source in [('Windows', per_platform), ('SMR', combined)]:
        target = dist / f'Roadworks-{version}-{suffix}.zip'
        shutil.copy2(source, target)
        outputs[suffix] = {'artifact': target.name, 'bytes': target.stat().st_size, 'sha256': digest(target)}
    source_zip = dist / f'Roadworks-{version}-Source.zip'
    subprocess.run(['git', 'archive', '--format=zip', f'--output={source_zip}', 'HEAD'], cwd=ROOT, check=True)
    with zipfile.ZipFile(source_zip) as source_package:
        require(source_package.testzip() is None, 'Source ZIP CRC failure')
        for info in source_package.infolist():
            name = info.filename
            require(not stat.S_ISLNK(info.external_attr >> 16), f'Local symlink in source export: {name}')
            parts = PurePosixPath(name).parts
            require(not {'.local', 'dist', 'Binaries', 'Intermediate', 'Saved', 'DerivedDataCache', '__pycache__'}.intersection(parts),
                    f'Generated or private directory in source export: {name}')
            require(not name.startswith(('.env', 'art/ui/')), f'Local data in source export: {name}')
    outputs['source'] = {'artifact': source_zip.name, 'bytes': source_zip.stat().st_size, 'sha256': digest(source_zip)}
    manifest = {'version': version, 'commit': commit, 'validated_plugin_commit': commit, **outputs['Windows'],
        'target': 'Windows Shipping (Steam + EGS binaries); no server plugin',
        'game_changelist': int(lock['game']['observed_changelist']), 'sml_version': lock['starter']['sml_version'],
        'outputs': outputs, 'packaged_files': files, 'source_sha256': frozen, 'cooked_assets': assets,
        'checks': {'linux_editor_build': 'passed', 'windows_package': 'passed', 'worldless_editor': tests,
                   'cooked_monitor_shaders': shaders, 'asset_dependencies': {'result': 'passed', 'assets': len(assets)},
                   'zip_crc_and_release_structure': 'passed'},
        'manual_qualification': {'dev35_local_use': 'Accepted by the maintainer before source cleanup.',
            'this_candidate_in_game': 'Not yet tested.', 'save_reload_after_removal': 'Not yet tested.',
            'vanilla_host_multiplayer': 'Not yet tested.', 'epic_store_runtime': 'Not yet tested.'}}
    for name in ('manifest.json', f'Roadworks-{version}-manifest.json'):
        (dist / name).write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'version': version, 'commit': commit, 'assets': len(assets), 'tests': tests['tests'], 'outputs': outputs}, indent=2))


if __name__ == '__main__':
    main()
