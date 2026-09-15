"""Assemble the local UI; generated runtime files never replace the UI sources."""
from pathlib import Path
import sys

def assemble(source, output, version):
    text = (source / 'index.html').read_text(encoding='utf-8')
    for marker, name in [('/* REAGBA_STYLE */', 'style.css'), ('/* REAGBA_SCRIPT */', 'app.js')]:
        if text.count(marker) != 1:
            raise ValueError(f'Expected one {marker} in the UI template')
        text = text.replace(marker, (source / name).read_text(encoding='utf-8'))
    text = text.replace('@REAGBA_VERSION@', version)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'index.html').write_text(text, encoding='utf-8', newline='\n')

if __name__ == '__main__':
    assemble(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3])
