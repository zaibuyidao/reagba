"""Build native installation ZIPs and the ReaScripts/ReaPack publishing bundle."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import zipfile

ROOT=Path(__file__).resolve().parents[1]
PRODUCT=Path('Scripts/zaibuyidao Scripts/ReaGBA')
PLATFORMS={
    'windows-x64':('reaper_reagba-x64.dll',None),
    'macos-x86_64':('reaper_reagba-x86_64.dylib',None),
    'macos-arm64':('reaper_reagba-arm64.dylib',None),
    'linux-x86_64':('reaper_reagba-x86_64.so','reagba-webview-x86_64'),
    'linux-aarch64':('reaper_reagba-aarch64.so','reagba-webview-aarch64'),
}

def version():
    match=re.search(r'project\(ReaGBA VERSION (\d+\.\d+\.\d+)\b',(ROOT/'CMakeLists.txt').read_text())
    if not match:raise ValueError('Missing ReaGBA CMake version')
    return match[1]

def asset_name(platform,release_version=None):
    if platform not in PLATFORMS:raise ValueError('Unsupported platform')
    return f'ReaGBA-v{release_version or version()}-{platform}.zip'

def package_files(platform):
    import reapack
    binary,helper=PLATFORMS[platform]
    files=[Path('UserPlugins')/binary]+[PRODUCT/'web'/name for name in reapack.web_files()]
    if helper:files.append(PRODUCT/'extension'/helper)
    return files

def executable(name):
    return Path(name).name.startswith('reagba-webview-')

def write_zip(target,files):
    target.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED) as archive:
        for name,data in sorted(files.items()):
            info=zipfile.ZipInfo(name);info.create_system=3
            info.external_attr=(0o100755 if executable(name) else 0o100644)<<16
            info.compress_type=zipfile.ZIP_DEFLATED
            archive.writestr(info,data)

def collect(stage,output,platform):
    files={}
    for relative in package_files(platform):
        path=stage/relative
        if path.is_symlink() or not path.is_file() or not path.stat().st_size:raise ValueError(f'Missing/unsafe install file: {relative}')
        data=path.read_bytes()
        # Common assets must be identical across Windows and Unix packages.
        if path.suffix in ('.html','.md','.txt'):data=data.replace(b'\r\n',b'\n')
        files[relative.as_posix()]=data
    html=files[(PRODUCT/'web/index.html').as_posix()].decode('utf-8')
    if any(marker in html for marker in ('/* REAGBA_STYLE */','/* REAGBA_SCRIPT */','@REAGBA_VERSION@')):raise ValueError('UI must be assembled before packaging')
    metadata={'version':version(),'platform':platform,'action':'_REAGBA_SHOW','action_name':'zaibuyidao: ReaGBA',
              'files':{name:hashlib.sha256(data).hexdigest() for name,data in files.items()}}
    files[(PRODUCT/'web/manifest.json').as_posix()]=json.dumps(metadata,indent=2).encode('utf-8')
    target=output/asset_name(platform);write_zip(target,files)
    return target

def read_package(source,platform):
    with zipfile.ZipFile(source) as archive:
        metadata_name=(PRODUCT/'web/manifest.json').as_posix()
        metadata=json.loads(archive.read(metadata_name));allowed={p.as_posix() for p in package_files(platform)}
        if metadata['version']!=version() or metadata['platform']!=platform or metadata.get('action')!='_REAGBA_SHOW' or metadata.get('action_name')!='zaibuyidao: ReaGBA':raise ValueError('Mixed platform or version packages')
        names=archive.namelist()
        if set(metadata['files'])!=allowed or set(names)!=allowed|{metadata_name} or len(names)!=len(set(names)):raise ValueError('Unexpected or missing files in package')
        files={name:archive.read(name) for name in allowed}
        if any(hashlib.sha256(files[name]).hexdigest()!=digest for name,digest in metadata['files'].items()):raise ValueError('Package checksum mismatch')
        return files

def aggregate(artifacts,output,tag):
    import reapack
    if tag!='v'+version():raise ValueError('Release tag must match CMake version')
    publishing={};raw={};candidates=[]
    for platform,(binary,helper) in PLATFORMS.items():
        source=artifacts/asset_name(platform)
        if not source.is_file():raise ValueError('Missing platform package: '+source.name)
        files=read_package(source,platform);candidates.append(source)
        for name,data in reapack.publisher_files(platform,files).items():
            if name in publishing and publishing[name]!=data:raise ValueError('Platform packages contain different web assets')
            publishing[name]=data
        raw[binary]=files['UserPlugins/'+binary]
        if helper:raw[helper]=files[(PRODUCT/'extension'/helper).as_posix()]
    publishing['ReaGBA/ReaGBA.ext']=reapack.manifest().encode('utf-8')
    output.mkdir(parents=True,exist_ok=True)
    for source in candidates:shutil.copy2(source,output/source.name)
    for name,data in raw.items():
        target=output/name;target.write_bytes(data)
        if executable(name):target.chmod(0o755)
    bundle=output/reapack.bundle_name();write_zip(bundle,publishing)
    paths=[output/p.name for p in candidates]+[output/name for name in raw]+[bundle]
    (output/'SHA256SUMS.txt').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in sorted(paths)),encoding='utf-8')

def prepare_publisher(stage,output,platform):
    """Stage available local binaries/web assets; CI aggregate requires all platforms."""
    import reapack
    files={}
    for relative in package_files(platform):
        path=stage/relative
        if not path.is_file():raise ValueError('Missing install file: '+str(path))
        data=path.read_bytes()
        if path.suffix in ('.html','.md','.txt'):data=data.replace(b'\r\n',b'\n')
        files[relative.as_posix()]=data
    publishing=reapack.publisher_files(platform,files);publishing['ReaGBA/ReaGBA.ext']=reapack.manifest().encode('utf-8')
    for name,data in publishing.items():
        target=output/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        if executable(name):target.chmod(0o755)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',type=Path);parser.add_argument('--platform',choices=PLATFORMS)
    parser.add_argument('--artifacts',type=Path);parser.add_argument('--tag')
    parser.add_argument('--publisher',action='store_true');parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.stage and args.platform and not args.artifacts:
        if args.publisher:prepare_publisher(args.stage,args.output,args.platform)
        else:print(collect(args.stage,args.output,args.platform))
    elif args.artifacts and args.tag and not args.stage:aggregate(args.artifacts,args.output,args.tag)
    else:parser.error('Use --stage/--platform or --artifacts/--tag')

if __name__=='__main__':main()
