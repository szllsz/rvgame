"""Read-only hash and binary golden-reference checks; Python standard library only."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def verify(root, manifest):
    root = Path(root).resolve()
    errors = []
    checked_files = 0
    checked_comparisons = 0

    def path(name):
        value = (root / name).resolve()
        if not value.is_relative_to(root):
            raise ValueError('Path escapes artifact root: ' + name)
        return value

    for name, expected in manifest.get('files', {}).items():
        try:
            if not isinstance(expected, str) or not re.fullmatch('[0-9a-f]{64}', expected):
                raise ValueError('Expected lowercase SHA256')
            actual = hashlib.sha256(path(name).read_bytes()).hexdigest()
            if actual != expected:
                raise ValueError('SHA256 mismatch')
            checked_files += 1
        except (OSError, ValueError) as error:
            errors.append({'file': name, 'error': str(error)})
    for index, pair in enumerate(manifest.get('comparisons', [])):
        try:
            actual = path(pair['actual']).read_bytes()
            reference = path(pair['reference']).read_bytes()
            ao = pair.get('actual_offset', 0)
            ro = pair.get('reference_offset', 0)
            length = pair.get('length')
            if not isinstance(ao, int) or not isinstance(ro, int) or ao < 0 or ro < 0:
                raise ValueError('Offsets must be nonnegative integers')
            if length is None:
                if ao or ro or len(actual) != len(reference):
                    raise ValueError('Full comparison requires equal lengths and zero offsets')
                length = len(actual)
            if not isinstance(length, int) or length <= 0 or ao + length > len(actual) or ro + length > len(reference):
                raise ValueError('Comparison range is empty or outside artifact')
            a = actual[ao:ao + length]
            r = reference[ro:ro + length]
            mismatch = next((n for n, (x, y) in enumerate(zip(a, r)) if x != y), None)
            if mismatch is not None:
                raise ValueError('Byte mismatch at actual offset ' + str(ao + mismatch))
            checked_comparisons += 1
        except (OSError, ValueError, KeyError, TypeError) as error:
            errors.append({'comparison': index, 'error': str(error)})
    if not manifest.get('files') and not manifest.get('comparisons'):
        errors.append({'error': 'Manifest contains no checks'})
    return {'pass': not errors, 'checked_files': checked_files,
        'checked_comparisons': checked_comparisons, 'errors': errors,
        'scope': 'artifact bytes and version identity; no hardware or semantic correctness claim'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    args = parser.parse_args()
    result = verify(args.root, json.loads(args.manifest.read_text(encoding='utf-8')))
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['pass'] else 1)


if __name__ == '__main__':
    main()
