from pathlib import Path
import subprocess

# Az Ubuntu runner rendszer-Pythonjához a teljes meglévő képtesztsor is kell.
subprocess.run(['sudo', 'apt-get', 'install', '-y', 'python3-numpy', 'python3-yaml'], check=True)
source_sha = '667201c78424a92d583aa72248e73b1a15f986f6'
subprocess.run(['git', 'fetch', '--depth=1', 'origin', source_sha], check=True)
source = subprocess.check_output(['git', 'show', source_sha + ':tools/.lighting-edit.py'], text=True)
start = source.index("p = 'main/app_loxone_lighting_model.inc'")
end = source.index("p = 'main/app_loxone_lighting_ui.inc'", start)
source = source[:start] + source[end:]
# A normál CI-ben is legyen explicit a rendszer-Python képellenőrző készlete.
anchor = "# A fejlesztői munkaterület nem kerül a kész PR-ba."
addition = """edit('.github/workflows/ci-shared.yml',
    'sudo apt-get update && sudo apt-get install -y libcjson-dev zlib1g-dev libmbedtls-dev',
    'sudo apt-get update && sudo apt-get install -y libcjson-dev zlib1g-dev libmbedtls-dev python3-numpy python3-pil', count=2)
"""
assert source.count(anchor) == 1
source = source.replace(anchor, addition + anchor)
exec(compile(source, '<lighting-final-regression>', 'exec'))
