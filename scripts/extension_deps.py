"""Fetch the pinned REAPER SDK and SWELL sources into build-only third_party/."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PINS = {
    'reaper-sdk': ('https://github.com/justinfrankel/reaper-sdk.git', '490ded57668727fba21482fabc50ba9853a457bb'),
    'WDL': ('https://github.com/justinfrankel/WDL.git', 'cc8eaf178ae871b96cd316a27d84e6be34540636'),
}

def bootstrap():
    env = {k.upper(): v for k, v in os.environ.items()} if os.name == 'nt' else dict(os.environ)
    for name, (url, commit) in PINS.items():
        path = ROOT / 'third_party' / name
        if not (path / '.git').exists():
            path.mkdir(parents=True, exist_ok=True)
            subprocess.run(['git', 'init', '-q', str(path)], env=env, check=True)
            subprocess.run(['git', '-C', str(path), 'remote', 'add', 'origin', url], env=env, check=True)
        head = subprocess.run(['git', '-C', str(path), 'rev-parse', 'HEAD'], env=env, capture_output=True, text=True)
        if head.returncode == 0 and head.stdout.strip() == commit:
            continue
        changed = subprocess.check_output(['git', '-C', str(path), 'status', '--porcelain'], env=env, text=True)
        if changed.strip():
            raise RuntimeError(f'{path} has local changes; preserve them before updating dependencies')
        subprocess.run(['git', '-C', str(path), 'fetch', '-q', '--depth', '1', 'origin', commit], env=env, check=True)
        subprocess.run(['git', '-C', str(path), 'checkout', '-q', '--detach', commit], env=env, check=True)
    print('Pinned REAPER SDK and SWELL ready')

if __name__ == '__main__':
    bootstrap()
