"""Read-only plugin asset registry audit in the pinned editor Python commandlet.
No asset import, save, world mutation, PIE or gameplay operation.
"""
import hashlib
import json
import os
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parent.parent
PROJECT = Path(unreal.Paths.get_project_file_path()).resolve()
EXPECTED = Path(os.environ['SATISFACTORY_STARTER']) / 'FactoryGame.uproject'
assert PROJECT == EXPECTED.resolve(), (PROJECT, EXPECTED)
assert (PROJECT.parent / 'Mods/ParametricRoads').resolve() == ROOT / 'Mods/ParametricRoads'
CONTENT = ROOT / 'Mods/ParametricRoads/Content'
hashes = lambda: {str(p.relative_to(CONTENT)): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in CONTENT.rglob('*') if p.is_file()}
before = hashes()
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(['/ParametricRoads'], force_rescan=True)
assets = registry.get_assets_by_path('/ParametricRoads', recursive=True)
packages = {str(asset.package_name) for asset in assets}
expected = {'/ParametricRoads/' + str(p.relative_to(CONTENT).with_suffix('')) for p in CONTENT.rglob('*.uasset')}
assert packages == expected, {'missing': sorted(expected-packages), 'extra': sorted(packages-expected)}
options = unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,
    include_soft_package_references=True, include_searchable_names=False,
    include_soft_management_references=False, include_hard_management_references=False)
dependencies = {package: sorted(str(name) for name in registry.get_dependencies(package, options))
                for package in sorted(packages)}
missing = {name for values in dependencies.values() for name in values
           if name.startswith('/ParametricRoads/') and name not in packages}
assert not missing, missing
external = sorted({name for values in dependencies.values() for name in values
                   if not name.startswith('/ParametricRoads/')})
assert hashes() == before, 'Read-only asset inspection changed authored content'
report = {'result': 'passed', 'assets': len(packages), 'dependencies': dependencies,
          'external_references': external, 'authored_sha256': before,
          'scope': 'Asset registry only; unchanged content; no world, PIE or gameplay qualification.'}
output = ROOT / '.local/logs/release-assets.json'
output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'result': 'passed', 'assets': len(packages), 'external_references': external}))
