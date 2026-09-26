"""No network: validate package boundaries and immutable publication decisions."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
import release
import reapack
import github_release

class Packages(unittest.TestCase):
    def stage(self, root, platform):
        for relative in release.package_files(platform):
            path=root/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'fixture')
        for relative in ('web/index.html','Scripts/zaibuyidao_ReaGBA.lua','roms/private.gba','reagba-webview-x86_64'):
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
                expected|={'ReaGBA/web/'+name for name in reapack.WEB_FILES}
                self.assertEqual(set(archive.namelist()),expected)
                self.assertEqual(archive.read('ReaGBA/ReaGBA.ext').decode(),reapack.manifest())
                for name in reapack.WEB_FILES:
                    self.assertEqual(archive.read('ReaGBA/web/'+name),(release.ROOT/'web'/name).read_bytes().replace(b'\r\n',b'\n'))
            for line in (root/'dist/SHA256SUMS.txt').read_text().splitlines():
                digest,name=line.split('  ');self.assertEqual(digest,hashlib.sha256((root/'dist'/name).read_bytes()).hexdigest())
            with self.assertRaises(ValueError):release.aggregate(root/'artifacts',root/'bad','v99.0.0')
            with zipfile.ZipFile(root/'artifacts'/release.asset_name('windows-x64'),'a') as archive:archive.writestr('web/private.html',b'bad')
            with self.assertRaises(ValueError):release.aggregate(root/'artifacts',root/'bad','v'+release.version())

    def test_missing_binary(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            with self.assertRaises(ValueError):release.collect(root,root/'out','windows-x64')

    def test_reapack_installs_core_and_web(self):
        manifest=reapack.manifest()
        self.assertIn('@version '+release.version()+'\n',manifest)
        self.assertNotIn('github.com/zaibuyidao/reagba',manifest)
        self.assertNotIn('_REAGBA_SHOW',manifest)
        self.assertEqual(len(reapack.sources()),10)
        self.assertIn('@link https://forum.cockos.com/showthread.php?t=311202\n',manifest)
        self.assertEqual(manifest.split('@changelog\n')[1],''.join('  '+line+'\n' for line in reapack.CHANGELOG))
        self.assertIn('/Modules/ReaGBA',reapack.BASE_URL)
        for entry in reapack.sources():
            if entry['type']=='extension':
                self.assertNotIn('/',entry['file'])
            else:
                self.assertEqual(entry['platform'],'all')
                self.assertTrue(entry['file'].startswith('web/'))
            self.assertIn(reapack.BASE_URL+'/'+entry['path'],manifest)
        self.assertIn('[all script nomain] web/zaibuyidao_ReaGBA.lua',manifest)

    def test_manifest_tracks_cmake_version(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            with patch.object(release,'ROOT',root):
                for version in ('0.1.0','0.1.1'):
                    (root/'CMakeLists.txt').write_text(f'project(ReaGBA VERSION {version} LANGUAGES C CXX)\n')
                    self.assertIn('@version '+version+'\n',reapack.manifest())
                    self.assertEqual(reapack.bundle_name(),f'ReaGBA-ReaPack-v{version}.zip')

    def test_local_publisher_includes_web(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);self.stage(root/'stage','windows-x64')
            release.prepare_publisher(root/'stage',root/'publisher','windows-x64')
            self.assertEqual((root/'publisher/ReaGBA/ReaGBA.ext').read_text(),reapack.manifest())
            self.assertEqual({p.name for p in (root/'publisher/ReaGBA/web').iterdir()},set(reapack.WEB_FILES))

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

    def test_current_notes_for_new_and_resumed_release(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);expected=github_release.asset_names('0.2.0')
            for name in expected:(root/name).write_bytes(b'fixture')
            for existing in (None,{'draft':True,'target_commitish':self.sha}):
                notes=[]
                def capture(command,**kwargs):
                    self.assertNotIn('--generate-notes',command)
                    if command[2]=='edit':
                        notes.append(Path(command[command.index('--notes-file')+1]).read_text(encoding='utf-8'))
                github_release.publish(self.api(existing),root,'example/reagba','v0.2.0',self.sha,expected,capture)
                self.assertEqual(notes,[''.join(f'- {line}\n' for line in reapack.CHANGELOG)])
                self.assertNotIn('Full Changelog',notes[0])

if __name__=='__main__':unittest.main()
