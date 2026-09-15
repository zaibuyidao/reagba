"""No network: validate package boundaries and immutable publication decisions."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
import release
import reapack
import github_release

class Packages(unittest.TestCase):
    def stage(self, root, platform):
        for relative in release.package_files(platform):
            path=root/relative;path.parent.mkdir(parents=True, exist_ok=True);path.write_bytes(b'fixture')
        for relative in ('ROM/game.gba','data/states/user.state','runtime/ReaGBA.exe','ReaGBA.lua'):
            path=root/release.PRODUCT/relative;path.parent.mkdir(parents=True, exist_ok=True);path.write_bytes(b'private')

    def test_platforms_and_private_data_exclusion(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);artifacts=root/'artifacts'
            for platform, (_, helper) in release.PLATFORMS.items():
                stage=root/platform;self.stage(stage,platform)
                target=release.collect(stage,artifacts,platform)
                with zipfile.ZipFile(target) as archive:
                    self.assertFalse(any('/ROM/' in p or '/data/' in p or p.endswith(('.exe','.lua')) for p in archive.namelist()))
                    metadata=json.loads(archive.read((release.PRODUCT/'web/manifest.json').as_posix()))
                    self.assertEqual(metadata['action'],'_REAGBA_SHOW')
                    self.assertEqual(metadata['action_name'],'zaibuyidao: ReaGBA')
                    if helper:self.assertTrue((archive.getinfo((release.PRODUCT/'extension'/helper).as_posix()).external_attr>>16)&0o100)
            release.aggregate(artifacts,root/'dist','v'+release.version())
            lines=(root/'dist/SHA256SUMS.txt').read_text().splitlines();self.assertEqual(len(lines),13)
            for line in lines:
                checksum,name=line.split('  ');self.assertEqual(checksum,hashlib.sha256((root/'dist'/name).read_bytes()).hexdigest())
            self.assertEqual({p.name for p in (root/'dist').iterdir()},set(github_release.asset_names(release.version())))
            with zipfile.ZipFile(root/'dist'/reapack.bundle_name()) as archive:
                expected={'ReaGBA/ReaGBA.ext'}|{'ReaGBA/web/'+name for name in reapack.web_files()}
                expected|={'ReaGBA/extension/'+name for pair in release.PLATFORMS.values() for name in pair if name}
                self.assertEqual(set(archive.namelist()),expected)
                self.assertEqual(archive.read('ReaGBA/ReaGBA.ext').decode(),reapack.manifest())
                for name in expected:
                    if release.executable(name):self.assertTrue((archive.getinfo(name).external_attr>>16)&0o100)
            with self.assertRaises(ValueError):release.aggregate(artifacts,root/'dist','v99.0.0')
            with zipfile.ZipFile(artifacts/release.asset_name('windows-x64'),'a') as archive:archive.writestr('ROM/private.gba',b'bad')
            with self.assertRaises(ValueError):release.aggregate(artifacts,root/'dist','v'+release.version())

    def test_missing_files_and_unassembled_ui(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);self.stage(root/'stage','windows-x64')
            html=root/'stage'/release.PRODUCT/'web/index.html';html.write_text('/* REAGBA_SCRIPT */')
            with self.assertRaises(ValueError):release.collect(root/'stage',root/'out','windows-x64')
            html.unlink()
            with self.assertRaises(ValueError):release.collect(root/'stage',root/'out','windows-x64')
            with self.assertRaises(ValueError):release.aggregate(root/'empty',root/'out','v'+release.version())

    def test_common_web_assets_must_match(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            for platform in release.PLATFORMS:
                stage=root/platform;self.stage(stage,platform)
                # Git checkouts can have different line endings on Windows.
                (stage/release.PRODUCT/'web/index.html').write_bytes(b'html\r\n' if platform=='windows-x64' else b'html\n')
                release.collect(stage,root/'artifacts',platform)
            release.aggregate(root/'artifacts',root/'out','v'+release.version())
            stage=root/'windows-x64'
            (stage/release.PRODUCT/'web/index.html').write_text('incompatible UI')
            release.collect(stage,root/'artifacts','windows-x64')
            with self.assertRaisesRegex(ValueError,'different web assets'):
                release.aggregate(root/'artifacts',root/'bad','v'+release.version())
            self.assertFalse((root/'bad').exists())

    def test_reapack_uses_only_public_repository(self):
        manifest=reapack.manifest()
        self.assertNotIn('github.com/zaibuyidao/reagba',manifest)
        self.assertNotIn('SendFlow',manifest)
        self.assertNotIn('.lua',manifest)
        self.assertIn('@version '+release.version(),manifest)
        entries=reapack.sources()
        self.assertEqual({e['platform'] for e in entries},set(reapack.PLATFORM_IDS.values()))
        for entry in entries:
            self.assertIn(reapack.BASE_URL+'/'+entry['path'],manifest)
            if entry['type']=='extension':self.assertNotIn('/',entry['file'])
            else:self.assertTrue(entry['file'].startswith(('web/','extension/')))
        self.assertIn('/ReaScripts/$commit/ReaGBA',manifest)

class Publication(unittest.TestCase):
    sha='a'*40
    def api(self, existing=None, commit=None):
        values={'releases/tags/v0.2.0':existing,'releases?per_page=100&page=1':[],
                'git/ref/tags/v0.2.0':{'object':{'type':'commit','sha':commit}} if commit else None}
        return Mock(get=Mock(side_effect=values.__getitem__))
    def test_tags_and_existing_release(self):
        for event in ('push','workflow_dispatch'):self.assertEqual(github_release.release_tag(event,'refs/heads/main','0.2.0'),'v0.2.0')
        self.assertIn('ReaGBA-v9.0.0-windows-x64.zip',github_release.asset_names('9.0.0'))
        with self.assertRaises(ValueError):github_release.release_tag('push','refs/tags/v0.1.0','0.2.0')
        expected=github_release.asset_names('0.2.0');published={'draft':False,'assets':[{'name':n,'size':1} for n in expected]}
        self.assertEqual(github_release.plan(self.api(published),'v0.2.0',self.sha,expected),'skip')
        published['assets'].pop()
        with self.assertRaises(ValueError):github_release.plan(self.api(published),'v0.2.0',self.sha,expected)
        with self.assertRaises(ValueError):github_release.plan(self.api(commit='b'*40),'v0.2.0',self.sha,expected)
    def test_failed_upload_stays_draft(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);expected=github_release.asset_names('0.2.0')
            for name in expected:(root/name).write_bytes(b'fixture')
            def fail(command,**kwargs):
                if command[2]=='upload':raise RuntimeError('simulated upload failure')
            run=Mock(side_effect=fail)
            with self.assertRaises(RuntimeError):github_release.publish(self.api(),root,'example/reagba','v0.2.0',self.sha,expected,run)
            self.assertEqual([c.args[0][2] for c in run.call_args_list],['create','upload'])
            run=Mock();draft={'draft':True,'target_commitish':self.sha}
            github_release.publish(self.api(draft),root,'example/reagba','v0.2.0',self.sha,expected,run)
            self.assertEqual([c.args[0][2] for c in run.call_args_list],['upload','edit'])

if __name__=='__main__':unittest.main()
