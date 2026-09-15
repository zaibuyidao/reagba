"""Build the REAPER extension, run checks and stage its installable files."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

def main():
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=root/'build/native')
    parser.add_argument('--build-only',action='store_true')
    parser.add_argument('--parallel',type=int,default=2)
    args=parser.parse_args()
    env={k.upper():v for k,v in os.environ.items()} if os.name=='nt' else dict(os.environ)
    cmake=shutil.which('cmake') or ('C:/Program Files/CMake/bin/cmake.exe' if os.name=='nt' else 'cmake')
    def run(*command):subprocess.run(command,env=env,check=True,cwd=root)
    if not args.build_only:
        command=[cmake,'-S',str(root),'-B',str(args.build_dir),'-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
                 '-DCMAKE_BUILD_TYPE=Release','-DREAGBA_EXTENSION=ON','-DREAGBA_TESTS=ON']
        if os.name=='nt':command+=['-G','Visual Studio 17 2022','-A','x64']
        run(*command)
    run(cmake,'--build',str(args.build_dir),'--config','Release','--parallel',str(args.parallel))
    ctest=str(Path(cmake).with_name('ctest.exe' if os.name=='nt' else 'ctest')) if Path(cmake).is_absolute() else 'ctest'
    run(ctest,'--test-dir',str(args.build_dir),'-C','Release','--output-on-failure')
    run(cmake,'--install',str(args.build_dir),'--config','Release','--component','ReaGBA','--prefix',str(args.build_dir/'package'))

if __name__=='__main__':main()
