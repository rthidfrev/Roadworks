"""Reject invalid release artifacts before copying them to dist/."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import warnings
import zipfile

spec = importlib.util.spec_from_file_location('verify_release', Path(__file__).resolve().parents[1] / 'scripts/verify-release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseArchiveTests(unittest.TestCase):
    descriptor = {'VersionName': '1.0.0-rc.1', 'SemVersion': '1.0.0-rc.1',
                  'RequiredOnRemote': False, 'GameVersion': '>=513885'}

    def files(self):
        files = {'ParametricRoads.uplugin': json.dumps(self.descriptor).encode()}
        for store in ('Steam', 'EGS'):
            stem = f'FactoryGame{store}'
            files[f'Binaries/Win64/{stem}-ParametricRoads-Win64-Shipping.dll'] = b'fixture'
            files[f'Binaries/Win64/{stem}-Win64-Shipping.modules'] = json.dumps({'Modules': {'ParametricRoads': 'fixture'}}).encode()
        for ext in ('pak', 'utoc', 'ucas'):
            files[f'Content/Paks/Windows/ParametricRoads.{ext}'] = b'fixture'
        for name, source in [('License.txt', 'LICENSE'), ('AssetLicense.txt', 'ASSET_LICENSE.md'), ('ThirdPartyNotices.txt', 'THIRD_PARTY_NOTICES.md')]:
            files['Resources/' + name] = (release.ROOT / source).read_bytes()
        return files

    def inspect(self, files, prefix='', duplicate=False):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'candidate.zip'
            with zipfile.ZipFile(path, 'w') as package:
                for name, body in files.items():
                    package.writestr(prefix + name, body)
                if duplicate:
                    with warnings.catch_warnings():
                        warnings.simplefilter('ignore', UserWarning)
                        package.writestr(prefix + 'ParametricRoads.uplugin', b'{}')
            return release.inspect_package(path, self.descriptor, prefix)

    def test_native_merged_payload_matches_platform_payload(self):
        self.assertEqual(self.inspect(self.files()), self.inspect(self.files(), 'Windows/'))

    def test_rejects_wrong_target_and_development_payload(self):
        for bad in ('Config/Engine.ini', '../escaped.dll', 'Source/private.cpp'):
            with self.subTest(bad=bad):
                files = self.files(); files[bad] = b'fixture'
                with self.assertRaises(ValueError):
                    self.inspect(files)
        files = self.files()
        del files['Binaries/Win64/FactoryGameEGS-ParametricRoads-Win64-Shipping.dll']
        with self.assertRaisesRegex(ValueError, 'Missing EGS'):
            self.inspect(files)

    def test_rejects_ambiguous_or_unlicensed_payload(self):
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            self.inspect(self.files(), duplicate=True)
        files = self.files(); files['Resources/License.txt'] = b'wrong notice'
        with self.assertRaisesRegex(ValueError, 'notice'):
            self.inspect(files)
        files = self.files(); packed = dict(self.descriptor, VersionName='old')
        files['ParametricRoads.uplugin'] = json.dumps(packed).encode()
        with self.assertRaisesRegex(ValueError, 'Version mismatch'):
            self.inspect(files)


if __name__ == '__main__':
    unittest.main()
