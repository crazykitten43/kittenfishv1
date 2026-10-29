"""Fetch the exact 100M-trained NNUE release and verify it before installation."""
import hashlib
import pathlib
import tempfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEST = ROOT / 'networks/kf100m.nnue'
URL = 'https://github.com/crazykitten43/kittenfishv1/releases/download/nnue-100m-v1/kf100m.nnue'
SHA256 = '95f19b6465c82de79a0924f5b6a2f3343233ea3ded73712f533bd46bf335f819'


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
