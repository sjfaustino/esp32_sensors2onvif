"""Writes the software bill of materials of a release as SPDX 2.3 JSON: the hub's firmware, and the software by others
built into it, from the "In the firmware" table of THIRD_PARTY_NOTICES.md, which stays the one list to keep up to date.

  python tools/sbom.py <version> <out.spdx.json>
"""
import datetime
import json
import re
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPO = 'https://github.com/sjfaustino/esp32_sensors2onvif'
SPDX_ID = re.compile(r'^[A-Za-z0-9.+-]+$')


def licence(text):
    """The SPDX expression for the table's licence column, or NOASSERTION where it names none."""
    text = re.sub(r'\s*\(\[text\]\([^)]*\)\)', '', text).strip()
    if text.lower() == 'public domain':
        return 'LicenseRef-Public-Domain'
    if ' with the GCC Runtime Library Exception' in text:
        return text.replace(' with the GCC Runtime Library Exception', ' WITH GCC-exception-3.1')
    parts = [p.strip() for p in text.split(' or ')]
    if all(SPDX_ID.match(p) for p in parts):
        return ' OR '.join(parts)
    return 'NOASSERTION'


def components():
    text = (ROOT / 'THIRD_PARTY_NOTICES.md').read_text(encoding='utf-8')
    table = text.split('## In the firmware', 1)[1].split('\n## ', 1)[0]
    for line in table.splitlines():
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        if len(cells) != 4 or cells[0] in ('Software', '---'):
            continue
        name = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', cells[0])
        name = re.sub(r'\s*\([^)]*\)', '', name.replace('`', '')).split(',')[0].strip()
        link = re.search(r'\]\((https?://[^)]*)\)', cells[0])
        yield name, cells[1], link.group(1) if link else None, licence(cells[2]), cells[3]


def main(version, out):
    packages = [{
        'SPDXID': 'SPDXRef-hub',
        'name': 'esp32_sensors2onvif',
        'versionInfo': version,
        'downloadLocation': f'{REPO}/releases/tag/{version}',
        'licenseConcluded': 'LicenseRef-All-Rights-Reserved',
        'licenseDeclared': 'LicenseRef-All-Rights-Reserved',
        'copyrightText': 'Sergio Faustino',
        'supplier': 'Person: Sergio Faustino',
        'primaryPackagePurpose': 'FIRMWARE',
    }]
    relationships = [{'spdxElementId': 'SPDXRef-DOCUMENT', 'relationshipType': 'DESCRIBES', 'relatedSpdxElement': 'SPDXRef-hub'}]
    for i, (name, ver, link, lic, copyright_) in enumerate(components(), 1):
        ref = f'SPDXRef-component-{i}'
        p = {
            'SPDXID': ref,
            'name': name,
            'downloadLocation': link or 'NOASSERTION',
            'licenseConcluded': lic,
            'licenseDeclared': lic,
            'copyrightText': copyright_ or 'NOASSERTION',
            'filesAnalyzed': False,
        }
        if ver:
            p['versionInfo'] = ver
        packages.append(p)
        relationships.append({'spdxElementId': 'SPDXRef-hub', 'relationshipType': 'CONTAINS', 'relatedSpdxElement': ref})
    doc = {
        'spdxVersion': 'SPDX-2.3',
        'dataLicense': 'CC0-1.0',
        'SPDXID': 'SPDXRef-DOCUMENT',
        'name': f'esp32_sensors2onvif {version}',
        'documentNamespace': f'{REPO}/spdx/{version}-{uuid.uuid4()}',
        'creationInfo': {
            'created': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
            'creators': ['Tool: tools/sbom.py'],
        },
        'packages': packages,
        'relationships': relationships,
        'hasExtractedLicensingInfos': [
            {'licenseId': 'LicenseRef-All-Rights-Reserved', 'name': 'All rights reserved', 'extractedText': 'See LICENSE.'},
            {'licenseId': 'LicenseRef-Public-Domain', 'name': 'Public domain', 'extractedText': 'Public domain.'},
        ],
    }
    Path(out).write_text(json.dumps(doc, indent=2) + '\n', encoding='utf-8')
    print(f'{out}: {len(packages) - 1} components')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    main(sys.argv[1], sys.argv[2])
