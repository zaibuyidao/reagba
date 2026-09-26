"""Publish the ReaGBA core, web assets and Lua launcher for ReaPack."""
from pathlib import Path
import release

CATEGORY='ReaGBA'
PLATFORM_IDS={'windows-x64':'win64','macos-x86_64':'darwin64','macos-arm64':'darwin-arm64','linux-x86_64':'linux64','linux-aarch64':'linux-aarch64'}
BASE_URL='https://raw.githubusercontent.com/zaibuyidao/ReaScripts/$commit/Modules/ReaGBA'
WEB_FILES=('zaibuyidao_ReaGBA.lua','index.html','style.css','i18n.js','app.js','bridge.js','video.js','popout.js','game.html','game.js')
CHANGELOG=(
    'Add a canvas-only game window with the game name in its title, 3px page spacing and restoration on Windows, macOS and Linux.',
    'Preserve game size on height changes and retain the main canvas across pop-out and restore, with collapsed-library information at the top and controls at the bottom.',
    'Fix detail-card cover alignment and play-button overflow, preserve status spacing when detached, and remove symbols from start, resume and pause buttons.',
)

def sources():
    entries=[]
    for platform,(binary,helper) in release.PLATFORMS.items():
        native=PLATFORM_IDS[platform]
        entries.append(dict(platform=native,type='extension',file=binary,path='extension/'+binary))
    for name in WEB_FILES:
        entries.append(dict(platform='all',type='script',file='web/'+name,path='web/'+name))
    return entries

def manifest():
    lines=['@description ReaGBA', '@version '+release.version(), '@author zaibuyidao',
           '@link https://forum.cockos.com/showthread.php?t=311202', '@provides']
    for entry in sources():
        options=entry['platform']+' '+entry['type']+(' nomain' if entry['type']=='script' else '')
        lines.append('  ['+options+'] '+entry['file']+' '+BASE_URL+'/'+entry['path'])
    lines+=['@changelog']+['  '+line for line in CHANGELOG]
    return '\n'.join(lines)+'\n'

def bundle_name(version=None):
    return 'ReaGBA-ReaPack-v'+(version or release.version())+'.zip'

def publisher_files(platform, installed):
    binary,helper=release.PLATFORMS[platform]
    result={CATEGORY+'/extension/'+binary:installed['UserPlugins/'+binary]}
    if helper:result[CATEGORY+'/extension/'+helper]=installed[(release.PRODUCT/'extension'/helper).as_posix()]
    for name in WEB_FILES:
        result[CATEGORY+'/web/'+name]=(release.ROOT/'web'/name).read_bytes().replace(b'\r\n',b'\n')
    return result
