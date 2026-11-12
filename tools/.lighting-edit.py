import subprocess

# A tesztelt, pontosan rögzített véglegesítő csomag ismételt alkalmazása.
source_sha = '3cc2adb3e27c40ecb670d000c0e2fd25f292c946'
subprocess.run(['git', 'fetch', '--depth=1', 'origin', source_sha], check=True)
source = subprocess.check_output(['git', 'show', source_sha + ':tools/.lighting-edit.py'], text=True)
exec(compile(source, '<lighting-final-regression>', 'exec'))
