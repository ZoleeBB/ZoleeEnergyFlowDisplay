from pathlib import Path
import subprocess

# Az előző, pontosan azonosított ellenőrzési csomagból a már meglévő
# UTF-8-védelmet nem írjuk át második alkalommal.
source_sha = '667201c78424a92d583aa72248e73b1a15f986f6'
subprocess.run(['git', 'fetch', '--depth=1', 'origin', source_sha], check=True)
source = subprocess.check_output(['git', 'show', source_sha + ':tools/.lighting-edit.py'], text=True)
start = source.index("p = 'main/app_loxone_lighting_model.inc'")
end = source.index("p = 'main/app_loxone_lighting_ui.inc'", start)
source = source[:start] + source[end:]
exec(compile(source, '<lighting-final-regression>', 'exec'))
