"""Copy the four directly runnable UI source files into the install tree."""
from pathlib import Path
import sys

def assemble(source, output, version):
    output.mkdir(parents=True, exist_ok=True)
    for name in ('index.html', 'style.css', 'i18n.js', 'app.js'):
        data = (source / name).read_bytes()
        text = data.decode('utf-8')
        if any(marker in text for marker in ('/* REAGBA_STYLE */', '/* REAGBA_SCRIPT */', '@REAGBA_VERSION@')):
            raise ValueError(f'{name} is not directly runnable')
        (output / name).write_bytes(data)

if __name__ == '__main__':
    assemble(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3])
