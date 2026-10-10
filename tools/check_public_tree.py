"""Check staged or tracked Git blobs; report paths/categories, never secret values."""
from pathlib import Path
import argparse
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tracked', action='store_true', help='Check all index blobs instead of only staged changes')
    args = parser.parse_args()
    if args.tracked:
        paths = git('ls-files', '-z').decode().split('\0')
    else:
        paths = git('diff', '--cached', '--name-only', '--diff-filter=ACMR', '-z').decode().split('\0')
    paths = [p for p in paths if p]
    private_values = []
    for name in ('user_config.h', 'spotify_config.h'):
        path = ROOT / 'esp32_system' / name
        if path.exists():
            for value in re.findall(r'=\s*"([^"\n]+)"', path.read_text(encoding='utf-8')):
                if len(value) >= 6 and not value.startswith('YOUR_'):
                    private_values.append(value.encode())
    patterns = {
        'token-shaped value': rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|sk-[A-Za-z0-9_-]{32,})',
        'private key': rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',
        'personal absolute path': rb'(?:[A-Za-z]:[\\/](?:Users|vibecoding)[\\/]|/home/[A-Za-z0-9._-]+/)',
    }
    failures = []
    for path in paths:
        if (path in ('esp32_system/user_config.h', 'esp32_system/spotify_config.h', 'STABILITY_REPAIR.md')
            or any(part in ('build', 'tmp', 'third_party', '__pycache__') for part in Path(path).parts)
            or Path(path).suffix.lower() in ('.bin', '.elf', '.map', '.log', '.pem', '.key')):
            failures.append((path, 'private/generated path'))
        data = git('show', ':' + path)
        if any(value in data for value in private_values):
            failures.append((path, 'local configuration value'))
        for label, pattern in patterns.items():
            if re.search(pattern, data):
                failures.append((path, label))
        if Path(path).suffix.lower() in ('.h', '.cpp', '.py', '.md', '.yml'):
            for match in re.finditer(rb'(?:WIFI_PASSWORD|SPOTIFY_CLIENT_SECRET|SPOTIFY_REFRESH_TOKEN)\s*=\s*"([^"\n]*)"', data):
                value = match.group(1)
                if value and not value.startswith(b'YOUR_') and b'{' not in value:
                    failures.append((path, 'non-placeholder credential assignment'))
    for path, label in failures:
        print(f'BLOCKED {path}: {label}')
    if failures:
        raise SystemExit(1)
    print(f'Privacy guard passed: {len(paths)} Git blobs checked. No secret values printed.')

if __name__ == '__main__':
    main()
