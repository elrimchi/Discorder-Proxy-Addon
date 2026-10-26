import configparser
import re
import shutil
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parent
release = root.parent / 'Discorder-Proxy-Addon'

files = {
    'version.dll': root / 'build/version.dll',
    'MINHOOK-LICENSE.txt': root / 'vendor/minhook/LICENSE.txt',
    'Discord-Proxy.bat': root / 'Discord-Proxy.bat',
    'Manage-Install.ps1': root / 'Manage-Install.ps1',
    'discord-proxy.ini': root / 'discord-proxy.ini',
}

missing = [str(path) for path in files.values() if not path.is_file()]
if missing:
    raise SystemExit('Missing release files:\n' + '\n'.join(missing))

config = configparser.ConfigParser(interpolation=None)
config.read(root / 'discord-proxy.ini', encoding='utf-8-sig')
version = config.get('addon', 'version', fallback='').strip()
if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version):
    raise SystemExit('Invalid [addon] version in discord-proxy.ini')
archive = root.parent / f'Discorder-Proxy-Addon-v{version}.zip'

if release.exists():
    if release.resolve() != root.parent / 'Discorder-Proxy-Addon':
        raise SystemExit('Refusing to replace a redirected release directory')
    shutil.rmtree(release)
release.mkdir(parents=True)
for name, source in sorted(files.items()):
    shutil.copy2(source, release / name)

with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as output:
    for name in sorted(files):
        output.write(release / name, f'Discorder-Proxy-Addon/{name}')

with zipfile.ZipFile(archive) as output:
    bad_member = output.testzip()
    if bad_member:
        raise SystemExit(f'Invalid ZIP member: {bad_member}')
print(f'{archive}: {archive.stat().st_size:,} bytes; {len(files)} files')
