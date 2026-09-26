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
            path=root/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'fixture')
        for relative in ('ui/index.html','Scripts/Open.lua','ROM/private.gba','reagba-webview-x86_64'):
            path=root/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'excluded')

    def test_core_only_packages(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            for platform,(binary,helper) in release.PLATFORMS.items():
                self.assertIsNone(helper)
                stage=root/platform;self.stage(stage,platform)
                target=release.collect(stage,root/'artifacts',platform)
                with zipfile.ZipFile(target) as archive:
                    self.assertEqual(set(archive.namelist()),{'UserPlugins/'+binary,'ReaGBA-core.json'})
                    self.assertEqual(json.loads(archive.read('ReaGBA-core.json'))['role'],'gba-core')
            release.aggregate(root/'artifacts',root/'dist','v'+release.version())
            self.assertEqual({p.name for p in (root/'dist').iterdir()},set(github_release.asset_names(release.version())))
            with zipfile.ZipFile(root/'dist'/reapack.bundle_name()) as archive:
                expected={'ReaGBA/ReaGBA.ext'}|{'ReaGBA/extension/'+pair[0] for pair in release.PLATFORMS.values()}
                self.assertEqual(set(archive.namelist()),expected)
            for line in (root/'dist/SHA256SUMS.txt').read_text().splitlines():
                digest,name=line.split('  ');self.assertEqual(digest,hashlib.sha256((root/'dist'/name).read_bytes()).hexdigest())
            with self.assertRaises(ValueError):release.aggregate(root/'artifacts',root/'bad','v99.0.0')
            with zipfile.ZipFile(root/'artifacts'/release.asset_name('windows-x64'),'a') as archive:archive.writestr('ui/private.html',b'bad')
            with self.assertRaises(ValueError):release.aggregate(root/'artifacts',root/'bad','v'+release.version())

    def test_missing_binary(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            with self.assertRaises(ValueError):release.collect(root,root/'out','windows-x64')

    def test_reapack_only_installs_core(self):
        manifest=reapack.manifest()
        self.assertNotIn('github.com/zaibuyidao/reagba',manifest)
        self.assertNotIn('_REAGBA_SHOW',manifest)
        self.assertEqual(len(reapack.sources()),5)
        for entry in reapack.sources():
            self.assertEqual(entry['type'],'extension')
            self.assertNotIn('/',entry['file'])
            self.assertIn(reapack.BASE_URL+'/'+entry['path'],manifest)

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
