"""ReaGBA core publishing; UI and its Lua launcher remain in this repository, distributed separately."""
from pathlib import Path
import release

CATEGORY='ReaGBA'
PLATFORM_IDS={'windows-x64':'win64','macos-x86_64':'darwin64','macos-arm64':'darwin-arm64','linux-x86_64':'linux64','linux-aarch64':'linux-aarch64'}
BASE_URL='https://raw.githubusercontent.com/zaibuyidao/ReaScripts/$commit/ReaGBA'

def sources():
    entries=[]
    for platform,(binary,helper) in release.PLATFORMS.items():
        native=PLATFORM_IDS[platform]
        entries.append(dict(platform=native,type='extension',file=binary,path='extension/'+binary))
    return entries

def manifest():
    lines=['@description ReaGBA', '@version '+release.version(), '@author zaibuyidao',
           '@link https://github.com/zaibuyidao/ReaScripts/tree/master/ReaGBA', '@provides']
    for entry in sources():
        options=entry['platform']+' '+entry['type']+(' nomain' if entry['type']=='script' else '')
        lines.append('  ['+options+'] '+entry['file']+' '+BASE_URL+'/'+entry['path'])
    lines+=['@changelog','  Headless GBA core only; no native action or WebView host.',
            '  Add the reagba Native Service and binary Frame Stream for ReaWebAPI v0.3.6.4+.']
    return '\n'.join(lines)+'\n'

def bundle_name(version=None):
    return 'ReaGBA-ReaPack-v'+(version or release.version())+'.zip'

def publisher_files(platform, installed):
    binary,helper=release.PLATFORMS[platform]
    result={CATEGORY+'/extension/'+binary:installed['UserPlugins/'+binary]}
    if helper:result[CATEGORY+'/extension/'+helper]=installed[(release.PRODUCT/'extension'/helper).as_posix()]
    return result
