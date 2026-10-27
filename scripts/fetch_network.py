"""Fetch the exact upstream NNUE release and verify it before installation."""
import hashlib
import pathlib
import tempfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEST = ROOT / 'networks/lichess500k.nnue'
URL = 'https://github.com/crazykitten43/kittenfishnnue/releases/download/lichess-500k-v1/lichess500k.nnue'
SHA256 = '4e44ea059ecad6ed495a19c167d7b426036b9652dcfe73e2372791ddbea39be9'


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def main():
    if DEST.exists() and digest(DEST) == SHA256:
        print('NNUE checksum verified')
        return
    DEST.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=DEST.parent, delete=False) as stream:
        temporary = pathlib.Path(stream.name)
    try:
        with urllib.request.urlopen(URL, timeout=120) as response, temporary.open('wb') as target:
            while chunk := response.read(1024 * 1024):
                target.write(chunk)
        if digest(temporary) != SHA256:
            raise SystemExit('NNUE checksum mismatch; model not installed')
        temporary.replace(DEST)
        print('Downloaded and verified trained NNUE')
    finally:
        temporary.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
