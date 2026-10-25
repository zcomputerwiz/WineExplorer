#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Embedded-terms review tests (work order cases N1-N3, S1-S2, A1-A2, P1-P2, C1, L1).

Synthetic PE packages and a fake HTTP opener only: no vendor payloads, no public
network, no real prefixes or accounts. Cache/TERMS/machine paths are private temp
trees isolated from /var/lib/wineexplorer. The security-boundary cases require
root (the section-5 lab runs the suite as root); pure parser tests run anywhere.
"""
from contextlib import contextmanager, redirect_stdout, redirect_stderr
import copy
import fcntl
import hashlib
import importlib.util
import io
import json
import os
import shutil
import ssl
import struct
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


components = module('components', ROOT / 'scripts/wex-components.py')

REVIEW_HOST = 'pkg.example.test'
URL_PREFIX = f'https://{REVIEW_HOST}/'
HOST_SUFFIXES = ('example.test',)

# Synthetic agreement texts (>= 200 chars after strip, CP1252-encodable).
AGREEMENT_A = (
    'SYNTHETIC FONT LICENSE AGREEMENT A\n'
    '-----------------------------------\n'
    'This is a synthetic agreement used only by isolated WineExplorer tests.\n'
    'It is not a real license and never ships. The renderer must preserve the\n'
    'complete ordered text of every distinct agreement, keep duplicate\n'
    'agreements grouped with the packages they apply to, and reject anything\n'
    'shorter than the completeness bound defined by the review backend.\n'
)
AGREEMENT_B = (
    'SYNTHETIC FONT LICENSE AGREEMENT B — international edition\n'
    '---------------------------------------------------------------\n'
    'Café, naïve and résumé text must survive CP1252 decoding,\n'
    'including curly quotes “and em dashes — in the rendered terms.\n'
    'This synthetic agreement is distinct from agreement A so that the\n'
    'renderer must show it as a separate clause with its own package list,\n'
    'while still preserving every line of this complete ordered text.\n'
)
AGREEMENT_C = (
    'SYNTHETIC FONT LICENSE AGREEMENT C\n'
    '----------------------------------\n'
    'This synthetic agreement belongs to the install-eligible fixture verb.\n'
    'Acceptance for that component is a separate, explicitly unchecked action\n'
    'bound to the exact rendered fingerprint; changing any fingerprint input\n'
    'must invalidate a previously captured acceptance digest.\n'
)


def build_pe(license_bytes, rtype=10, name='LICENSE', language=1033, *,
             duplicate_type=False, duplicate_language=False, breakage=''):
    """A minimal synthetic PE (PE32, .rsrc at RVA 0x1000) carrying one resource
    chain: type -> named entry -> language -> data. Structural knobs produce the
    malformed inputs the static parser must reject. Nothing is ever executed."""
    name_u16 = name.encode('utf-16-le')
    n1 = 2 if duplicate_type else 1
    n3 = 2 if duplicate_language else 1
    l1_off, l2_off = 0, 16 + 8 * n1
    l3_off, data_off = l2_off + 24, l2_off + 24 + 16 + 8 * n3
    name_off = data_off + 16
    rsrc_size = name_off + 2 + len(name_u16)

    def directory(off, named, ids):
        return struct.pack('<IIHHHH', 0, 0, 0, 0, named, ids)

    rsrc = bytearray(rsrc_size)
    rsrc[l1_off:l1_off + 16] = directory(l1_off, 0, n1)
    for index in range(n1):
        rsrc[l1_off + 16 + 8 * index:l1_off + 24 + 8 * index] = struct.pack('<II', rtype, l2_off | 0x80000000)
    rsrc[l2_off:l2_off + 16] = directory(l2_off, 1, 0)
    rsrc[l2_off + 16:l2_off + 24] = struct.pack('<II', 0x80000000 | name_off, l3_off | 0x80000000)
    rsrc[l3_off:l3_off + 16] = directory(l3_off, 0, n3)
    for index in range(n3):
        rsrc[l3_off + 16 + 8 * index:l3_off + 24 + 8 * index] = struct.pack('<II', language, data_off)
    resource_rva, payload = 0x1000, 0x200
    license_off = payload + rsrc_size
    struct.pack_into('<IIII', rsrc, data_off, resource_rva + rsrc_size, len(license_bytes), 0, 0)
    struct.pack_into('<H', rsrc, name_off, len(name_u16) // 2)
    rsrc[name_off + 2:name_off + 2 + len(name_u16)] = name_u16

    file_size = license_off + len(license_bytes)
    out = bytearray(file_size)
    out[0:2] = b'MZ'
    struct.pack_into('<I', out, 0x3c, 128)
    out[128:132] = b'PE\0\0'
    optional_size = 96 + 16 * 8
    sections = 2 if breakage == 'overlap-sections' else 1
    struct.pack_into('<HHIIIHH', out, 132, 0x14c, sections, 0, 0, 0, optional_size, 0)
    optional = 152
    struct.pack_into('<H', out, optional, 0x10b)
    struct.pack_into('<I', out, optional + 92, 16)
    struct.pack_into('<II', out, optional + 96 + 16, resource_rva, rsrc_size)
    table = optional + optional_size

    def section(offset, name, virtual_size, virtual, raw_size, raw):
        struct.pack_into('<8sIIIIIIHHI', out, offset, name, virtual_size, virtual,
                         raw_size, raw, 0, 0, 0, 0, 0)

    if breakage == 'overlap-sections':
        section(table, b'.rsrc\0\0', rsrc_size, resource_rva, file_size - payload, payload)
        section(table + 40, b'.data\0\0\0', 0x60, resource_rva + 0x40, 0x60, payload + 0x60)
    elif breakage == 'raw-zero':
        section(table, b'.rsrc\0\0', rsrc_size, resource_rva, rsrc_size, 0)
    else:
        section(table, b'.rsrc\0\0', rsrc_size, resource_rva, file_size - payload, payload)
    out[payload:payload + rsrc_size] = rsrc
    out[license_off:license_off + len(license_bytes)] = license_bytes
    return bytes(out)


def _fixture_packages(verb):
    if verb == 'synfont':
        return [('a1.exe', AGREEMENT_A), ('a2.exe', AGREEMENT_A), ('b1.exe', AGREEMENT_B)]
    return [('c1.exe', AGREEMENT_C)]


FIXTURES = {}
for _verb in ('synfont', 'synfont2'):
    files, table, recipe = {}, {}, []
    for name, text in _fixture_packages(_verb):
        license_bytes = text.encode('cp1252')
        pe = bytearray(build_pe(license_bytes))
        marker = (name + 'XXXXXX')[:8].encode('ascii')
        pe[0x40:0x40 + len(marker)] = marker  # per-package identity in the DOS stub
        files[name] = bytes(pe)
        table[name] = (len(bytes(pe)), len(license_bytes), hashlib.sha256(license_bytes).hexdigest())
        recipe.append((name, hashlib.sha256(bytes(pe)).hexdigest()))
    FIXTURES[_verb] = {'files': files, 'table': table, 'recipe': recipe}

COMPONENTS_MAP = dict(components.COMPONENTS)
COMPONENTS_MAP.update({'synfont': 'Synthetic review-only fonts',
                       'synfont2': 'Synthetic eligible fonts'})
METADATA_MAP = dict(components.COMPONENTS_METADATA)
for _verb, _disabled in (('synfont', True), ('synfont2', False)):
    METADATA_MAP[_verb] = {
        'title': COMPONENTS_MAP[_verb],
        'publisher': 'Synthetic Test Publisher',
        'description': f'Synthetic fixture component for review tests ({_verb}).',
        'size': '~1 MiB (synthetic)',
        'reboot': False,
        'disabled': _disabled,
        'disabled_reason': 'review only (synthetic fixture)' if _disabled else '',
        'terms_url': None,
        'recipe_files': FIXTURES[_verb]['recipe'],
        'review': {
            'mode': 'embedded-package-terms',
            'policy': f'synthetic-review-{_verb}-v1',
            'url_prefix': URL_PREFIX,
            'host_suffixes': HOST_SUFFIXES,
            'resource': {'type': 10, 'name': 'LICENSE', 'language': 1033, 'encoding': 'cp1252'},
            'packages': FIXTURES[_verb]['table'],
            'rendered_sha256': None,  # filled in below; must not be hand-pinned
        },
    }


def _finalize_rendered_pins():
    with patch.object(components, 'COMPONENTS', COMPONENTS_MAP), \
         patch.object(components, 'COMPONENTS_METADATA', METADATA_MAP):
        for verb in ('synfont', 'synfont2'):
            agreements = []
            for name, _sha in FIXTURES[verb]['recipe']:
                raw = components.pe_resource(FIXTURES[verb]['files'][name], 10, 'LICENSE', 1033)
                agreements.append((name, components.decode_embedded_agreement(raw, 'cp1252')))
            text = components.render_embedded_terms(verb, agreements, enforce_pin=False)
            METADATA_MAP[verb]['review']['rendered_sha256'] = hashlib.sha256(text.encode('utf-8')).hexdigest()


_finalize_rendered_pins()


@contextmanager
def with_fixtures():
    """Patch the catalog so the synthetic verbs are the only review route under test."""
    with patch.object(components, 'COMPONENTS', COMPONENTS_MAP), \
         patch.object(components, 'COMPONENTS_METADATA', METADATA_MAP):
        yield FIXTURES


class FakeResponse:
    def __init__(self, data, url, on_read=None, declared=None):
        self._stream = io.BytesIO(data)
        self.url = url
        self.headers = {'Content-Length': str(declared if declared is not None else len(data))}
        self.on_read = on_read

    def read(self, size=-1):
        chunk = self._stream.read(size)
        if self.on_read:
            self.on_read(chunk)
        return chunk

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self._stream.close()
        return False


class FakeOpener:
    """Serves pinned bytes from memory; records every requested URL in order."""

    def __init__(self, files, log=None, on_read=None, error_for=None, declared=None, urls=None):
        self.files = files
        self.log = log if log is not None else []
        self.on_read = on_read
        self.error_for = error_for or {}
        self.declared = declared or {}
        self.urls = urls or {}

    def open(self, request, timeout=None):
        url = getattr(request, 'full_url', str(request))
        name = url.rsplit('/', 1)[-1]
        self.log.append(url)
        if name in self.error_for:
            raise self.error_for[name]
        if name not in self.files:
            raise urllib.error.HTTPError(url, 404, 'not found', None, None)
        return FakeResponse(self.files[name], self.urls.get(name, url), on_read=self.on_read,
                            declared=self.declared.get(name))


def private_base():
    """Temp root whose ancestor chain satisfies the production security checks."""
    if os.geteuid() == 0:
        return Path(tempfile.mkdtemp(prefix='wex-review-test-', dir='/root'))
    return Path(tempfile.mkdtemp(prefix='wex-review-test-'))


class ReviewTestCase(unittest.TestCase):
    """Root-required base: cache/TERMS security checks are privileged by design."""

    def setUp(self):
        if os.geteuid() != 0:
            self.fail('review security-boundary tests require root (section-5 lab runs the suite as root)')
        self.base = private_base()
        self.addCleanup(shutil.rmtree, str(self.base), ignore_errors=True)
        self.cache_root = self.base / 'review-cache'
        self.terms_dir = self.base / 'terms'
        self.machine = self.base / 'machine'
        self.machine.mkdir(mode=0o700)

    def cache_dir(self, verb):
        return self.cache_root / verb

    def download(self, verb, opener=None, **kwargs):
        with with_fixtures():
            return components.review_download(verb, self.cache_dir(verb), self.terms_dir,
                                              opener=opener or FakeOpener(FIXTURES[verb]['files']),
                                              **kwargs)

    def seed_package(self, verb, name, data=None):
        path = self.cache_dir(verb) / name
        self.cache_dir(verb).mkdir(parents=True, mode=0o700, exist_ok=True)
        with open(path, 'wb') as out:
            out.write(FIXTURES[verb]['files'][name] if data is None else data)
        os.chmod(path, 0o600)
        return path

    def terms_record(self, verb):
        return json.loads((self.terms_dir / (verb + '.json')).read_text(encoding='utf-8'))


class PEParserTests(unittest.TestCase):
    """P2 static-parser cases; pure, no root or network involved."""

    def _license(self):
        return AGREEMENT_A.encode('cp1252')

    def test_P2_valid_pe_extracts_exact_license_bytes(self):
        self.assertEqual(components.pe_resource(build_pe(self._license()), 10, 'LICENSE', 1033),
                         self._license())

    def test_P2_non_mz_and_truncated_rejected(self):
        pe = bytearray(build_pe(self._license()))
        pe[0:2] = b'XZ'
        with self.assertRaises(ValueError):
            components.pe_resource(bytes(pe), 10, 'LICENSE', 1033)
        with self.assertRaises(ValueError):
            components.pe_resource(build_pe(self._license())[:-16], 10, 'LICENSE', 1033)

    def test_P2_bad_pe_offset_rejected(self):
        pe = bytearray(build_pe(self._license()))
        struct.pack_into('<I', pe, 0x3c, 0)
        with self.assertRaises(ValueError):
            components.pe_resource(bytes(pe), 10, 'LICENSE', 1033)

    def test_P2_ambiguous_duplicate_entries_rejected(self):
        with self.assertRaises(ValueError):
            components.pe_resource(build_pe(self._license(), duplicate_type=True), 10, 'LICENSE', 1033)
        with self.assertRaises(ValueError):
            components.pe_resource(build_pe(self._license(), duplicate_language=True), 10, 'LICENSE', 1033)

    def test_P2_wrong_name_or_language_rejected(self):
        pe = build_pe(self._license())
        with self.assertRaises(ValueError):
            components.pe_resource(pe, 10, 'NOTES', 1033)
        with self.assertRaises(ValueError):
            components.pe_resource(pe, 10, 'LICENSE', 2049)

    def test_P2_empty_or_oversized_resource_rejected(self):
        pe = bytearray(build_pe(self._license()))
        struct.pack_into('<I', pe, 0x200 + 72 + 4, 0)  # data entry size -> 0
        with self.assertRaises(ValueError):
            components.pe_resource(bytes(pe), 10, 'LICENSE', 1033)
        with self.assertRaises(ValueError):
            components.pe_resource(build_pe(self._license()), 10, 'LICENSE', 1033, limit=4)

    def test_P2_malformed_section_layouts_rejected(self):
        with self.assertRaises(ValueError):
            components.pe_resource(build_pe(self._license(), breakage='overlap-sections'), 10, 'LICENSE', 1033)
        with self.assertRaises(ValueError):
            components.pe_resource(build_pe(self._license(), breakage='raw-zero'), 10, 'LICENSE', 1033)

    def test_P2_zero_optional_header_control_rejected(self):
        """Reviewer negative control: zero SizeOfOptionalHeader with the first
        section's fields planted inside the (now empty) optional-header slot. The
        old parser reinterpreted those bytes as the section table and returned the
        license; the repaired parser must reject it structurally."""
        license_bytes = self._license()
        valid = build_pe(license_bytes)
        self.assertEqual(components.pe_resource(valid, 10, 'LICENSE', 1033), license_bytes)
        pe = bytearray(valid)
        pe_off = struct.unpack_from('<I', pe, 0x3c)[0]
        optional = pe_off + 24
        original_size = struct.unpack_from('<H', pe, pe_off + 20)[0]
        saved = bytes(pe[optional + original_size + 8: optional + original_size + 24])
        struct.pack_into('<H', pe, pe_off + 20, 0)      # SizeOfOptionalHeader -> 0
        pe[optional + 8: optional + 24] = saved          # plant section fields in the header slot
        with self.assertRaises(ValueError):
            components.pe_resource(bytes(pe), 10, 'LICENSE', 1033)

    def test_P2_short_or_truncated_optional_header_rejected(self):
        pe = bytearray(build_pe(self._license()))
        pe_off = struct.unpack_from('<I', pe, 0x3c)[0]
        # Shorter than the PE32 data-directory table (needs >= 96 + 3*8).
        struct.pack_into('<H', pe, pe_off + 20, 100)
        with self.assertRaises(ValueError):
            components.pe_resource(bytes(pe), 10, 'LICENSE', 1033)
        # Declared header runs past the end of the file.
        struct.pack_into('<H', pe, pe_off + 20, 0xFFFF)
        with self.assertRaises(ValueError):
            components.pe_resource(bytes(pe), 10, 'LICENSE', 1033)

    def test_P2_insufficient_directory_count_rejected(self):
        pe = bytearray(build_pe(self._license()))
        optional = struct.unpack_from('<I', pe, 0x3c)[0] + 24
        struct.pack_into('<I', pe, optional + 92, 2)     # NumberOfRvaAndSizes -> 2 (< 3)
        with self.assertRaises(ValueError):
            components.pe_resource(bytes(pe), 10, 'LICENSE', 1033)

    def test_P2_truncated_section_table_rejected(self):
        pe = bytearray(build_pe(b'L' * 300))             # small payload keeps the file bounded
        pe_off = struct.unpack_from('<I', pe, 0x3c)[0]
        struct.pack_into('<H', pe, pe_off + 6, 90)       # table runs past EOF (<= PE_MAX_SECTIONS)
        with self.assertRaises(ValueError):
            components.pe_resource(bytes(pe), 10, 'LICENSE', 1033)

    def test_P2_resource_crossing_section_span_rejected(self):
        license_bytes = self._license()
        pe = bytearray(build_pe(license_bytes))
        # Grow the declared resource size past its section's raw range (still under limit).
        struct.pack_into('<I', pe, 0x200 + 72 + 4, len(license_bytes) + 100)
        with self.assertRaises(ValueError):
            components.pe_resource(bytes(pe), 10, 'LICENSE', 1033)

    def test_P2_decode_rejects_bad_encoding_nul_controls_and_short_text(self):
        raw = self._license()
        with self.assertRaises(ValueError):
            components.decode_embedded_agreement(raw, 'utf-8')
        bad = bytearray(raw)
        bad[10] = 0
        with self.assertRaises(ValueError):
            components.decode_embedded_agreement(bytes(bad), 'cp1252')
        with self.assertRaises(ValueError):
            components.decode_embedded_agreement(b'\x01' + raw, 'cp1252')
        with self.assertRaises(ValueError):
            components.decode_embedded_agreement(b'Short license text.\n' * 5, 'cp1252')


class NetworkBoundaryTests(ReviewTestCase):
    """N1-N3: metadata-only open/select/decline never touch the network."""

    def test_N1_open_select_get_terms_missing_cache_zero_network(self):
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'is_admin', return_value=False), \
             patch('builtins.input', side_effect=AssertionError('no prompt allowed')), \
             patch.object(components.subprocess, 'run', side_effect=AssertionError('no subprocess')):
            state, total = components.review_state('synfont', directory=self.terms_dir)
            self.assertEqual(state, 'none')
            self.assertEqual(total, sum(size for size, _l, _s in FIXTURES['synfont']['table'].values()))
            with self.assertRaises(components.ReviewRequired):
                components.embedded_license_text('synfont', self.terms_dir)
            with self.assertRaises(ValueError) as ctx:
                components.acquire_review_terms('synfont', gui=False)
            self.assertIn('elevation', str(ctx.exception).lower())
        self.assertFalse((self.terms_dir / 'synfont.json').exists())
        self.assertEqual(list(self.machine.iterdir()), [])

    def test_N2_decline_download_no_network_no_elevation_call(self):
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
             redirect_stdout(io.StringIO()), \
             patch('builtins.input', return_value='no'), \
             patch.object(components.subprocess, 'run', side_effect=AssertionError('no subprocess')) as run_spy:
            result = components.acquire_review_terms('synfont', gui=False)
        self.assertIsNone(result)
        run_spy.assert_not_called()
        self.assertFalse((self.terms_dir / 'synfont.json').exists())
        self.assertEqual(list(self.machine.iterdir()), [])

    def test_N3_explicit_start_requests_exactly_pinned_packages_cached_repeat_zero(self):
        opener = FakeOpener(FIXTURES['synfont']['files'])
        text, fingerprint = self.download('synfont', opener=opener)
        self.assertEqual(opener.log, [URL_PREFIX + name for name, _sha in FIXTURES['synfont']['recipe']])
        # The shipping catalog row is the eleven-package case: pinned and policy-checked.
        with with_fixtures():
            core = components.review_packages('corefonts')
            self.assertEqual(len(core), 11)
            suffixes = components.review_meta('corefonts')['host_suffixes']
            for package in core:
                components.check_review_url(package['url'], suffixes)
            self.assertEqual(components.review_total_bytes('corefonts'), sum(p['size'] for p in core))
        # Cached repeat review makes zero requests and returns the identical result.
        again = FakeOpener(FIXTURES['synfont']['files'])
        text2, fp2 = self.download('synfont', opener=again)
        self.assertEqual(again.log, [])
        self.assertEqual((text2, fp2), (text, fingerprint))
        for name, data in FIXTURES['synfont']['files'].items():
            self.assertEqual((self.cache_dir('synfont') / name).read_bytes(), data)
        self.assertFalse(any(p.name.endswith('.part') for p in self.cache_dir('synfont').iterdir()))


class InstallIsolationTests(ReviewTestCase):
    """S1-S2: the review route never installs and disabled stays disabled."""

    def test_S1_review_download_never_installs_or_mutates_machine(self):
        with patch.object(components.subprocess, 'run', side_effect=AssertionError('no subprocess')) as run_spy, \
             patch.object(components, 'install', side_effect=AssertionError('no install')), \
             patch.object(components, 'MACHINE', self.machine), \
             patch.object(components, 'OPERATION_STATE', self.base / 'ops' / 'component-operations'):
            self.download('synfont')
        run_spy.assert_not_called()
        self.assertEqual(list(self.machine.iterdir()), [])
        record = self.terms_record('synfont')
        self.assertEqual(record['schema'], 2)
        self.assertEqual(record['kind'], 'embedded-review')
        self.assertEqual(set(record), {'schema', 'kind', 'component_identity',
                                       'rendered_sha256', 'fingerprint', 'text'})

    def test_S2_disabled_component_cached_review_works_install_still_rejected(self):
        text, fingerprint = self.download('synfont')
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'REVIEW_CACHE', self.cache_root):
            state, _total = components.review_state('synfont', directory=self.terms_dir)
            self.assertEqual(state, 'cached')
            reopened, fp = components.embedded_license_text('synfont', self.terms_dir)
            self.assertEqual((reopened, fp), (text, fingerprint))
            with self.assertRaises(ValueError) as ctx:
                components.install('synfont', fingerprint)
            self.assertIn('disabled', str(ctx.exception))
            with self.assertRaises(ValueError):
                components.consent('synfont', gui=False)


class ReviewStateTruthfulnessTests(ReviewTestCase):
    """Q2: availability (review_state) must never advertise 'cached' for a variant
    that strict retrieval rejects. A consistent terms document alone is an honest
    not-verified state, not verification. Each control mutates exactly one input of
    a complete valid cache and re-checks BOTH the availability seam (review_state)
    and the terms seam (embedded_license_text). Packages are cached, so every case
    makes zero network requests."""

    def _establish(self, verb='synfont'):
        """Download a complete valid review cache; assert it reads as 'cached'."""
        text, fingerprint = self.download(verb)
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'REVIEW_CACHE', self.cache_root):
            state, _total = components.review_state(verb, directory=self.terms_dir)
            self.assertEqual(state, 'cached')
            reopened, fp = components.embedded_license_text(verb, self.terms_dir)
            self.assertEqual((reopened, fp), (text, fingerprint))
        return text, fingerprint

    def _state(self, verb='synfont'):
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'REVIEW_CACHE', self.cache_root):
            return components.review_state(verb, directory=self.terms_dir)

    def _strict_terms_reject(self, verb='synfont'):
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'REVIEW_CACHE', self.cache_root):
            with self.assertRaises(ValueError):
                components.embedded_license_text(verb, self.terms_dir, strict=True)

    def test_Q2_complete_valid_cache_reads_cached(self):
        self._establish()

    def test_Q2_delete_one_package_is_unverified(self):
        # (a) delete one package: the document is still present and consistent, so
        # the honest state is 'unverified' -- never 'cached', never 'none'.
        self._establish()
        os.remove(self.cache_dir('synfont') / 'b1.exe')
        state, _ = self._state()
        self.assertEqual(state, 'unverified')
        self._strict_terms_reject()

    def test_Q2_changed_package_bytes_are_unverified(self):
        # (b) change a package's bytes: sha256 no longer matches the reviewed
        # identity -> integrity failure -> 'unverified'.
        self._establish()
        path = self.cache_dir('synfont') / 'b1.exe'
        data = bytearray(path.read_bytes())
        data[-1] ^= 0xFF
        path.write_bytes(bytes(data))
        os.chmod(path, 0o600)
        state, _ = self._state()
        self.assertEqual(state, 'unverified')
        self._strict_terms_reject()

    def test_Q2_changed_policy_is_stale(self):
        # (c) change a resource/hash/policy: pinned metadata no longer matches the
        # cached document's fingerprint -> an explicit 'stale', never 'cached'.
        self.download('synfont')
        variant = copy.deepcopy(METADATA_MAP)
        variant['synfont']['review'].update(policy='synthetic-review-synfont-v2')
        with patch.object(components, 'COMPONENTS', COMPONENTS_MAP), \
             patch.object(components, 'COMPONENTS_METADATA', variant), \
             patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'REVIEW_CACHE', self.cache_root):
            state, _ = components.review_state('synfont', directory=self.terms_dir)
            self.assertEqual(state, 'stale')
            with self.assertRaises(components.StaleTermsError):
                components.embedded_license_text('synfont', self.terms_dir)

    def test_Q2_symlinked_package_is_unverified(self):
        # (d) replace a cached file with a symlink -> insecure path -> 'unverified'.
        self._establish()
        real = self.cache_dir('synfont') / 'b1.exe'
        outside = self.base / 'b1-outside-target'
        shutil.copyfile(real, outside)
        os.remove(real)
        os.symlink(outside, real)
        state, _ = self._state()
        self.assertEqual(state, 'unverified')
        self._strict_terms_reject()

    def test_Q2_world_writable_package_is_unverified(self):
        # (e) alter modes: a non-private (world-writable) package is rejected; the
        # cache is never made world-readable to "fix" availability.
        self._establish()
        path = self.cache_dir('synfont') / 'b1.exe'
        os.chmod(path, 0o666)
        state, _ = self._state()
        self.assertEqual(state, 'unverified')
        self._strict_terms_reject()

    def test_Q2_unprivileged_view_is_honest_not_verified(self):
        # An unprivileged reader of the private cache cannot establish package
        # validity. It may still read the world-readable terms document, so the
        # honest state is 'unverified' -- a cached document, NOT a fully verified
        # package cache. A world-traversable base at / lets the non-root user reach
        # the doc but not the root-owned 0700 cache (mirrors the production layout,
        # where the private trees are under root-only paths).
        import pwd
        self.download('synfont')
        priv_base = Path(tempfile.mkdtemp(prefix='wex-q2-priv-', dir='/'))
        self.addCleanup(shutil.rmtree, str(priv_base), ignore_errors=True)
        os.chmod(priv_base, 0o755)
        terms = priv_base / 'terms'
        cache = priv_base / 'review-cache' / 'synfont'
        terms.mkdir(mode=0o755)
        cache.mkdir(parents=True, mode=0o700)
        os.chmod(priv_base / 'review-cache', 0o755)
        os.chmod(cache, 0o700)
        shutil.copyfile(self.terms_dir / 'synfont.json', terms / 'synfont.json')
        os.chown(terms / 'synfont.json', 0, 0)
        os.chmod(terms / 'synfont.json', 0o644)
        for name in FIXTURES['synfont']['files']:
            shutil.copyfile(self.cache_dir('synfont') / name, cache / name)
            os.chown(cache / name, 0, 0)
            os.chmod(cache / name, 0o600)
        nobody = pwd.getpwnam('nobody')
        uid, gid = os.getuid(), os.getgid()
        try:
            # Keep the saved IDs as root so the drop is reversible; only the
            # real/effective IDs become 'nobody' for the duration. (Setting the
            # saved ID to nobody would make regaining root impossible -> EPERM.)
            os.setresgid(nobody.pw_gid, nobody.pw_gid, gid)
            os.setresuid(nobody.pw_uid, nobody.pw_uid, uid)
            with patch.object(components, 'COMPONENTS', COMPONENTS_MAP), \
                 patch.object(components, 'COMPONENTS_METADATA', METADATA_MAP), \
                 patch.object(components, 'TERMS', terms), \
                 patch.object(components, 'REVIEW_CACHE', priv_base / 'review-cache'):
                state, _ = components.review_state('synfont', directory=terms)
        finally:
            os.setresuid(uid, uid, uid)
            os.setresgid(gid, gid, gid)
        self.assertEqual(state, 'unverified')


class AcceptanceSeparationTests(ReviewTestCase):
    """A1-A2: completion is not acceptance; acceptance is separate, unchecked,
    fingerprint-bound, and rechecked by the backend before any execution."""

    def _rewrite_terms(self, verb, **changes):
        path = self.terms_dir / (verb + '.json')
        record = json.loads(path.read_text(encoding='utf-8'))
        record.update(changes)
        with open(path, 'w', encoding='utf-8') as out:
            json.dump(record, out, sort_keys=True, indent=2)
            out.write('\n')
        os.chmod(path, 0o644)

    def test_A1_download_completion_is_not_acceptance_replay_cannot_install(self):
        with patch.object(components.subprocess, 'run', side_effect=AssertionError('no subprocess')), \
             patch.object(components, 'install', side_effect=AssertionError('no install')):
            text, fingerprint = self.download('synfont')
            self.assertNotIn('accepted_sha256', self.terms_record('synfont'))
            # Reopen and replay (a second explicit download over the full cache).
            again = FakeOpener(FIXTURES['synfont']['files'])
            text2, fp2 = self.download('synfont', opener=again)
        self.assertEqual(again.log, [])
        self.assertEqual((text2, fp2), (text, fingerprint))
        self.assertFalse((self.machine / 'component-receipts').exists())
        self.assertEqual(list(self.machine.iterdir()), [])
        with with_fixtures():
            with self.assertRaises(ValueError):
                components.install('synfont', fingerprint)

    def test_A2_eligible_fixture_requires_separate_unchecked_acceptance(self):
        text, fingerprint = self.download('synfont2')
        # Declining the separate acceptance action records nothing.
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'REVIEW_CACHE', self.cache_root), \
             redirect_stdout(io.StringIO()), patch('builtins.input', return_value='no'):
            self.assertIsNone(components.consent('synfont2', gui=False))
        # Accepting binds exactly the reviewed fingerprint.
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'REVIEW_CACHE', self.cache_root), \
             redirect_stdout(io.StringIO()), patch('builtins.input', return_value='yes'):
            accepted = components.consent('synfont2', gui=False)
        self.assertEqual(accepted, fingerprint)
        # The backend rechecks the exact digest before any execution.
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'REVIEW_CACHE', self.cache_root), \
             patch.object(components, 'validate_winetricks_integrity'), \
             patch.object(components, 'validate_recipe_files'), \
             patch.object(components.subprocess, 'run', side_effect=AssertionError('no subprocess')) as run_spy:
            with self.assertRaises(ValueError) as ctx:
                components.install('synfont2', '0' * 64)
            self.assertIn('Vendor terms changed', str(ctx.exception))
        run_spy.assert_not_called()
        # The correct digest passes the recheck (already installed: no execution).
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'REVIEW_CACHE', self.cache_root), \
             patch.object(components, 'MACHINE', self.machine), \
             patch.object(components, 'OPERATION_STATE', self.base / 'ops' / 'component-operations'), \
             patch.object(components, 'validate_winetricks_integrity'), \
             patch.object(components, 'validate_recipe_files'), \
             patch.object(components, 'is_installed', return_value=True), \
             patch.object(components.subprocess, 'run', side_effect=AssertionError('no subprocess')) as run_spy:
            self.assertIsNone(components.install('synfont2', fingerprint))
        run_spy.assert_not_called()

    def test_A2_each_fingerprint_input_change_invalidates_the_cached_binding(self):
        """A2: every fingerprint/identity input is load-bearing.

        Each mutation starts from an independently re-verified VALID record (the
        baseline success is asserted immediately before the single change) and a
        clean record is restored between subtests, so a case can only pass because
        that specific input is bound -- never because of leftover corruption left
        behind by an earlier case. A weakened-identity negative control proves the
        detection genuinely depends on the real binding rather than incidental state.
        """

        def fresh_baseline(verb):
            # Re-download restores a valid record bound to the ORIGINAL metadata and
            # re-caches the packages (FakeOpener serves in-memory bytes; no network).
            text, fp = self.download(verb)
            with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
                 patch.object(components, 'REVIEW_CACHE', self.cache_root):
                self.assertEqual(components.embedded_license_text(verb, self.terms_dir),
                                 (text, fp), f'baseline not valid for {verb}')
            return text, fp

        # Record-level mutations: the on-disk document itself changes.
        record_cases = [
            ('rendered text edit', lambda: self._rewrite_terms('synfont2', text='TAMPERED\n')),
            ('cached schema change', lambda: self._rewrite_terms('synfont2', schema=99)),
        ]
        for label, apply in record_cases:
            with self.subTest(label):
                fresh_baseline('synfont2')
                apply()
                with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
                     patch.object(components, 'REVIEW_CACHE', self.cache_root):
                    with self.assertRaises(components.StaleTermsError):
                        components.embedded_license_text('synfont2', self.terms_dir)
                    # The digest source install() rechecks must fail closed too.
                    with self.assertRaises(components.StaleTermsError):
                        components.license_text('synfont2', self.terms_dir)

        # Metadata-variant mutations: one catalog input changes while the stored
        # record is still bound to the original, so it must be rejected as stale.
        meta_cases = [
            ('component identity input', 'synfont2',
             lambda m: m.update(description=m['description'] + ' edited')),
            ('review policy', 'synfont2',
             lambda m: m['review'].update(policy='synthetic-review-synfont2-v2')),
            ('package license identity', 'synfont2',
             lambda m: m['review']['packages'].update(
                 {'c1.exe': (m['review']['packages']['c1.exe'][0],
                             m['review']['packages']['c1.exe'][1], '0' * 64)})),
            ('resource locator/language/encoding', 'synfont2',
             lambda m: m['review']['resource'].update(language=2049)),
            ('rendered text pin', 'synfont2',
             lambda m: m['review'].update(rendered_sha256='0' * 64)),
            # Package order is bound by the fingerprint's ordered raw term (and by the
            # recipe inside component_identity); it needs a multi-package fixture to vary.
            ('package order (raw term)', 'synfont',
             lambda m: m.update(recipe_files=list(reversed(m['recipe_files'])))),
        ]
        for label, verb, mutate in meta_cases:
            with self.subTest(label):
                fresh_baseline(verb)
                variant = copy.deepcopy(METADATA_MAP)
                mutate(variant[verb])
                with patch.object(components, 'COMPONENTS', COMPONENTS_MAP), \
                     patch.object(components, 'COMPONENTS_METADATA', variant), \
                     patch.object(components, 'TERMS', self.terms_dir), \
                     patch.object(components, 'REVIEW_CACHE', self.cache_root):
                    with self.assertRaises(components.StaleTermsError):
                        components.embedded_license_text(verb, self.terms_dir)
                    with self.assertRaises(components.StaleTermsError):
                        components.license_text(verb, self.terms_dir)

        # A previously captured acceptance digest cannot authorize an install after any
        # input change: install() re-reads the terms via license_text(), which now fails
        # closed before reaching execution.
        _text, old_fp = fresh_baseline('synfont2')
        variant = copy.deepcopy(METADATA_MAP)
        variant['synfont2']['review'].update(policy='synthetic-review-synfont2-v2')
        with patch.object(components, 'COMPONENTS', COMPONENTS_MAP), \
             patch.object(components, 'COMPONENTS_METADATA', variant), \
             patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'REVIEW_CACHE', self.cache_root), \
             patch.object(components, 'validate_winetricks_integrity'), \
             patch.object(components, 'validate_recipe_files'), \
             patch.object(components.subprocess, 'run', side_effect=AssertionError('no subprocess')) as run_spy:
            with self.assertRaises(ValueError) as ctx:
                components.install('synfont2', old_fp)
        self.assertIn('changed', str(ctx.exception).lower())
        run_spy.assert_not_called()

        # Negative control: prove the "component identity input" detection is caused by
        # component_identity binding description, not by leftover record corruption. An
        # identity that normalizes description to a constant makes a description-only edit
        # invisible; under it the same mutation must NOT raise. The un-weakened identity
        # above does raise, so this test fails if that binding were ever dropped.
        real_ci = components.component_identity

        def weak_ci(verb):
            v = components.validate_verb(verb)
            saved = components.COMPONENTS_METADATA[v]['description']
            try:
                components.COMPONENTS_METADATA[v]['description'] = 'PINNED-DESCRIPTION'
                return real_ci(v)
            finally:
                components.COMPONENTS_METADATA[v]['description'] = saved

        with patch.object(components, 'component_identity', weak_ci):
            fresh_baseline('synfont2')  # record bound to a description-insensitive id
            variant = copy.deepcopy(METADATA_MAP)
            variant['synfont2']['description'] += ' edited'
            with patch.object(components, 'COMPONENTS', COMPONENTS_MAP), \
                 patch.object(components, 'COMPONENTS_METADATA', variant), \
                 patch.object(components, 'TERMS', self.terms_dir), \
                 patch.object(components, 'REVIEW_CACHE', self.cache_root):
                got_text, got_fp = components.embedded_license_text('synfont2', self.terms_dir)
            # Must NOT raise: description is the only change and weak_ci ignores it.
            self.assertIsInstance(got_text, str)
            self.assertEqual(len(got_fp), 64)

        # Root strict reads recompute from the verified package cache...
        _text2, fingerprint = fresh_baseline('synfont2')
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'REVIEW_CACHE', self.cache_root):
            strict_text, strict_fp = components.embedded_license_text('synfont2', self.terms_dir, strict=True)
            self.assertEqual(strict_fp, fingerprint)
        # ...and a tampered cached package fails closed instead of re-accepting.
        data = FIXTURES['synfont2']['files']['c1.exe']
        self.seed_package('synfont2', 'c1.exe', data=data[:-4] + b'XXXX')
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'REVIEW_CACHE', self.cache_root):
            with self.assertRaises(ValueError):
                components.embedded_license_text('synfont2', self.terms_dir, strict=True)


class RenderingVerificationTests(ReviewTestCase):
    """P1: verified packages, raw resources and rendered clauses; duplicate
    agreements keep their package mapping."""

    def test_P1_duplicate_agreements_preserve_package_mapping(self):
        text, fingerprint = self.download('synfont')
        self.assertIn('2 distinct agreement(s) apply to 3 package(s)', text)
        self.assertIn('Agreement 1 of 2 - applies to: a1.exe, a2.exe', text)
        self.assertIn('Agreement 2 of 2 - applies to: b1.exe', text)
        self.assertLess(text.index('AGREEMENT A'), text.index('AGREEMENT B'))
        self.assertIn(AGREEMENT_A.strip(), text)
        self.assertIn(AGREEMENT_B.strip(), text)
        with with_fixtures():
            cached = components.embedded_terms_from_cache('synfont', self.cache_dir('synfont'))
        self.assertEqual(cached, (text, fingerprint))
        data = FIXTURES['synfont']['files']['a2.exe']
        self.seed_package('synfont', 'a2.exe', data=data[:-4] + b'TAMPER')
        with with_fixtures():
            with self.assertRaises(components.ReviewRequired) as ctx:
                components.embedded_terms_from_cache('synfont', self.cache_dir('synfont'))
        self.assertIn('a2.exe', str(ctx.exception))

    def test_P1_cp1252_unicode_and_complete_ordered_text(self):
        text, _fp = self.download('synfont')
        for fragment in ('Café', 'naïve', '—', '“'):
            self.assertIn(fragment, text)
        for agreement in (AGREEMENT_A, AGREEMENT_B):
            positions = []
            for line in agreement.splitlines():
                if not line.strip():
                    continue
                pos = text.find(line)
                self.assertNotEqual(pos, -1, f'missing line: {line!r}')
                positions.append(pos)
            self.assertEqual(positions, sorted(positions))


class CacheSecurityTests(ReviewTestCase):
    """C1: hash/size/redirect/TLS errors, stale records, tampered cache state,
    oversize input and disk-full all fail closed without receipts."""

    def _assert_no_publication(self, verb='synfont'):
        self.assertFalse((self.terms_dir / (verb + '.json')).exists())
        cache = self.cache_dir(verb)
        if cache.exists():
            self.assertFalse(any(p.name.endswith('.part') for p in cache.iterdir()))

    def test_C1_hash_size_truncation_oversize_redirect_and_tls_fail_closed(self):
        files = dict(FIXTURES['synfont']['files'])
        bad = bytearray(files['a1.exe'])
        bad[0x40] ^= 0xFF  # same size, wrong hash
        opener = FakeOpener({**files, 'a1.exe': bytes(bad)})
        with self.assertRaises(ValueError) as ctx:
            self.download('synfont', opener=opener)
        self.assertIn('does not match the reviewed package hash', str(ctx.exception))
        self.assertEqual(opener.log, [URL_PREFIX + 'a1.exe'])
        self._assert_no_publication()

        full = len(files['a1.exe'])
        opener = FakeOpener(files, declared={'a1.exe': full + 10})  # header lies about size
        with self.assertRaises(ValueError) as ctx:
            self.download('synfont', opener=opener)
        self.assertIn('size differs', str(ctx.exception))
        self._assert_no_publication()

        opener = FakeOpener({**files, 'a1.exe': files['a1.exe'][:-50]}, declared={'a1.exe': full})
        with self.assertRaises(ValueError) as ctx:
            self.download('synfont', opener=opener)
        self.assertIn('truncated', str(ctx.exception))
        self._assert_no_publication()

        opener = FakeOpener({**files, 'a1.exe': files['a1.exe'] + b'EXTRA'}, declared={'a1.exe': full})
        with self.assertRaises(ValueError) as ctx:
            self.download('synfont', opener=opener)
        self.assertIn('larger than the reviewed package', str(ctx.exception))
        self._assert_no_publication()

        opener = FakeOpener(files, urls={'a1.exe': 'https://evil.example.net/a1.exe'})
        with self.assertRaises(ValueError) as ctx:
            self.download('synfont', opener=opener)
        self.assertIn('source policy', str(ctx.exception))
        self._assert_no_publication()

        opener = FakeOpener(files, error_for={'a1.exe': ssl.SSLError('handshake failed')})
        with self.assertRaises(ssl.SSLError):
            self.download('synfont', opener=opener)
        self._assert_no_publication()

    def test_C1_stale_schema_and_oversized_terms_rejected(self):
        self.download('synfont')
        path = self.terms_dir / 'synfont.json'
        record = json.loads(path.read_text(encoding='utf-8'))
        record['schema'] = 1
        with open(path, 'w', encoding='utf-8') as out:
            json.dump(record, out, sort_keys=True, indent=2)
            out.write('\n')
        os.chmod(path, 0o644)
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir):
            with self.assertRaises(components.StaleTermsError):
                components.embedded_license_text('synfont', self.terms_dir)
        with open(path, 'w', encoding='utf-8') as out:
            out.write('x' * (2 * 1024 * 1024 + 1))
        os.chmod(path, 0o644)
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir):
            with self.assertRaises(ValueError) as ctx:
                components.embedded_license_text('synfont', self.terms_dir)
        self.assertIn('Oversized', str(ctx.exception))

    def test_C1_tampered_package_owner_mode_and_symlink_rejected(self):
        self.download('synfont')
        pkg = self.cache_dir('synfont') / 'a1.exe'
        with with_fixtures():
            os.chown(pkg, 1000, 1000)
            with self.assertRaises(components.ReviewRequired):
                components.embedded_terms_from_cache('synfont', self.cache_dir('synfont'))
            os.chown(pkg, 0, 0)
            os.chmod(pkg, 0o666)
            with self.assertRaises(components.ReviewRequired):
                components.embedded_terms_from_cache('synfont', self.cache_dir('synfont'))
            os.chmod(pkg, 0o600)
            target = self.base / 'valid-a1.exe'
            shutil.copyfile(pkg, target)
            pkg.unlink()
            os.symlink(target, pkg)
            with self.assertRaises(components.ReviewRequired):
                components.embedded_terms_from_cache('synfont', self.cache_dir('synfont'))
            # A root terms read recomputes from the cache and fails closed.
            with patch.object(components, 'TERMS', self.terms_dir), \
                 patch.object(components, 'REVIEW_CACHE', self.cache_root):
                with self.assertRaises(ValueError):
                    components.embedded_license_text('synfont', self.terms_dir, strict=True)

    def test_C1_symlinked_cache_dir_and_disk_full_fail_closed(self):
        (self.base / 'real-cache').mkdir(mode=0o700)
        os.symlink(self.base / 'real-cache', self.base / 'link-cache')
        with with_fixtures():
            with self.assertRaises(ValueError) as ctx:
                components.review_download('synfont', self.base / 'link-cache' / 'synfont',
                                           self.terms_dir,
                                           opener=FakeOpener(FIXTURES['synfont']['files']))
        self.assertIn('Insecure review cache path', str(ctx.exception))
        usage = type('Usage', (), {'free': 1, 'total': 2, 'used': 1})()
        with patch.object(components.shutil, 'disk_usage', return_value=usage):
            with self.assertRaises(OSError) as ctx:
                self.download('synfont')
        self.assertIn('Not enough free disk space', str(ctx.exception))
        self._assert_no_publication()

    def test_C1_normal_user_cannot_trigger_privileged_download(self):
        with with_fixtures(), patch.object(components.os, 'getuid', return_value=1000):
            with self.assertRaises(PermissionError):
                components.review_download('synfont')


class LifecycleCancellationTests(ReviewTestCase):
    """L1: cancellation at every boundary, deadline, interrupted publication and
    simultaneous starts; bounded time, owned partials removed, valid cache kept."""

    def test_L1_cancel_before_transfer(self):
        cancel = threading.Event()
        cancel.set()
        opener = FakeOpener(FIXTURES['synfont']['files'])
        with self.assertRaises(components.ReviewCancelled):
            self.download('synfont', opener=opener, cancel=cancel)
        self.assertEqual(opener.log, [])
        self.assertFalse((self.terms_dir / 'synfont.json').exists())
        cache = self.cache_dir('synfont')
        if cache.exists():
            self.assertEqual(list(cache.iterdir()), [])

    def test_L1_cancel_mid_transfer_removes_partial(self):
        cancel = threading.Event()

        class ChunkedResponse(FakeResponse):
            def read(self, size=-1):
                limit = 64 if size < 0 else min(size, 64)
                chunk = self._stream.read(limit)
                if chunk and not cancel.is_set():
                    cancel.set()
                return chunk

        class ChunkedOpener(FakeOpener):
            def open(self, request, timeout=None):
                url = getattr(request, 'full_url', str(request))
                name = url.rsplit('/', 1)[-1]
                self.log.append(url)
                return ChunkedResponse(self.files[name], url)

        opener = ChunkedOpener(FIXTURES['synfont']['files'])
        with self.assertRaises(components.ReviewCancelled):
            self.download('synfont', opener=opener, cancel=cancel)
        cache = self.cache_dir('synfont')
        self.assertFalse((cache / 'a1.exe').exists())
        self.assertFalse(any(p.name.endswith('.part') for p in cache.iterdir()))
        self.assertFalse((self.terms_dir / 'synfont.json').exists())

    def test_L1_cancel_after_last_chunk_before_publish(self):
        cancel = threading.Event()

        class EndOfStreamResponse(FakeResponse):
            def read(self, size=-1):
                chunk = self._stream.read(size)
                if not chunk:
                    cancel.set()  # cancelled exactly when the stream ends
                return chunk

        class Opener(FakeOpener):
            def open(self, request, timeout=None):
                url = getattr(request, 'full_url', str(request))
                name = url.rsplit('/', 1)[-1]
                self.log.append(url)
                return EndOfStreamResponse(self.files[name], url)

        opener = Opener(FIXTURES['synfont']['files'])
        with self.assertRaises(components.ReviewCancelled):
            self.download('synfont', opener=opener, cancel=cancel)
        cache = self.cache_dir('synfont')
        self.assertFalse((cache / 'a1.exe').exists())  # finished but never published
        self.assertFalse(any(p.name.endswith('.part') for p in cache.iterdir()))
        self.assertEqual(opener.log, [URL_PREFIX + 'a1.exe'])

    def test_L1_full_cache_needs_no_network(self):
        for name in FIXTURES['synfont']['files']:
            self.seed_package('synfont', name)
        opener = FakeOpener(FIXTURES['synfont']['files'])
        text, fingerprint = self.download('synfont', opener=opener)
        self.assertEqual(opener.log, [])
        record = self.terms_record('synfont')
        self.assertEqual(record['fingerprint'], fingerprint)

    def test_L1_deadline_exceeded_fails_closed(self):
        with patch.object(components, 'REVIEW_DEADLINE_SECONDS', -1):
            with self.assertRaises(TimeoutError) as ctx:
                self.download('synfont')
        self.assertIn('time limit', str(ctx.exception))
        cache = self.cache_dir('synfont')
        self.assertFalse(any(p.name.endswith('.part') for p in cache.iterdir()))
        self.assertFalse((self.terms_dir / 'synfont.json').exists())

    def test_L1_interrupted_publication_keeps_valid_cache_without_terms(self):
        cancel = threading.Event()
        original = components.embedded_terms_from_cache

        def interrupting(verb, cache_dir=None):
            cancel.set()
            return original(verb, cache_dir)

        with patch.object(components, 'embedded_terms_from_cache', side_effect=interrupting):
            with self.assertRaises(components.ReviewCancelled):
                self.download('synfont', cancel=cancel)
        self.assertFalse((self.terms_dir / 'synfont.json').exists())
        for name, data in FIXTURES['synfont']['files'].items():
            self.assertEqual((self.cache_dir('synfont') / name).read_bytes(), data)

    def test_L1_simultaneous_starts_serialize_on_the_lock(self):
        self.cache_dir('synfont').mkdir(parents=True, mode=0o700)
        descriptor = os.open(self.cache_root / 'synfont.lock', os.O_RDWR | os.O_CREAT, 0o600)
        fcntl.flock(descriptor, fcntl.LOCK_EX)
        with patch.object(components, 'REVIEW_LOCK_WAIT_SECONDS', 0.4):
            started = time.monotonic()
            with self.assertRaises(OSError) as ctx:
                self.download('synfont')
            elapsed = time.monotonic() - started
        os.close(descriptor)
        self.assertIn('Another review download is active', str(ctx.exception))
        self.assertLess(elapsed, 10)
        # After release two concurrent downloads both complete and agree. One
        # fixture patch is held for the whole section: per-call re-entry would
        # unpatch while the other thread is still mid-download.
        results = []

        def worker():
            opener = FakeOpener(FIXTURES['synfont']['files'],
                                on_read=lambda _chunk: time.sleep(0.05))
            results.append(components.review_download('synfont', self.cache_dir('synfont'),
                                                      self.terms_dir, opener=opener))

        with with_fixtures():
            threads = [threading.Thread(target=worker) for _ in range(2)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join(timeout=60)
        self.assertFalse(any(thread.is_alive() for thread in threads))
        self.assertEqual(len(results), 2)
        self.assertEqual(results[0], results[1])

    def test_L1_lock_wait_is_cancellation_aware(self):
        # A cancelled review download must stop waiting for the per-cache lock
        # instead of blocking out REVIEW_LOCK_WAIT_SECONDS and then writing into
        # a cache its caller has abandoned (no orphaned daemon worker).
        verb = 'synfont'
        self.cache_dir(verb).mkdir(parents=True, mode=0o700)
        descriptor = os.open(self.cache_root / (verb + '.lock'), os.O_RDWR | os.O_CREAT, 0o600)
        fcntl.flock(descriptor, fcntl.LOCK_EX)   # hold the lock so review_download must wait
        cancel = threading.Event()
        outcome = {}

        def worker():
            try:
                self.download(verb, cancel=cancel)
                outcome['ok'] = True
            except components.ReviewCancelled:
                outcome['cancelled'] = True
            except Exception as error:
                outcome['error'] = repr(error)

        with patch.object(components, 'REVIEW_LOCK_WAIT_SECONDS', 5.0):
            started = time.monotonic()
            thread = threading.Thread(target=worker)
            thread.start()
            time.sleep(0.5)          # let the worker enter the lock wait loop
            cancel.set()             # cancel while it is blocked on the held lock
            thread.join(timeout=10)
            elapsed = time.monotonic() - started
        os.close(descriptor)
        self.assertFalse(thread.is_alive(), 'worker still blocked after cancellation')
        self.assertTrue(outcome.get('cancelled'), f'expected ReviewCancelled, got {outcome}')
        self.assertLess(elapsed, 2.5, 'waited out the lock instead of cancelling promptly')
        # Nothing was downloaded into the abandoned cache.
        for name in FIXTURES[verb]['files']:
            self.assertFalse((self.cache_dir(verb) / name).exists(),
                             f'{name} written after cancellation')


class ComponentCLITests(ReviewTestCase):
    """CLI contract tests for wex-components main() entry point."""

    def test_cli_list_outputs_all_components_and_disabled_reasons(self):
        f = io.StringIO()
        with patch.object(sys, 'argv', ['wex-components', 'list']), redirect_stdout(f):
            components.main()
        output = f.getvalue()
        for verb, meta in components.COMPONENTS_METADATA.items():
            self.assertIn(verb, output)
            if meta.get('disabled'):
                self.assertIn(f"(disabled: {meta['disabled_reason']})", output)
            else:
                self.assertIn(f"{verb}: {meta['title']}\n", output)

    def test_cli_review_cached_terms_displays_terms_and_installed_nothing_note(self):
        with with_fixtures():
            text, _ = self.download('synfont')
            f = io.StringIO()
            with patch.object(sys, 'argv', ['wex-components', 'review', 'synfont']), \
                 patch.object(components, 'TERMS', self.terms_dir), \
                 patch.object(components, 'REVIEW_CACHE', self.cache_root), \
                 redirect_stdout(f):
                components.main()
            output = f.getvalue()
            self.assertIn(text.strip()[:60], output)
            self.assertIn('Installation of Synthetic review-only fonts is not available yet. Reviewing these terms installed nothing.', output)
            self.assertEqual(list(self.machine.iterdir()), [])

    def test_cli_review_decline_download_prints_prompt_and_exits_cleanly(self):
        with with_fixtures(), patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'REVIEW_CACHE', self.cache_root), \
             patch.object(components, 'is_admin', return_value=True), \
             patch('builtins.input', return_value='no'), \
             patch.object(components.subprocess, 'run', side_effect=AssertionError('no subprocess allowed')) as run_spy:
            f = io.StringIO()
            with patch.object(sys, 'argv', ['wex-components', 'review', 'synfont']), redirect_stdout(f):
                components.main()
            output = f.getvalue()
        run_spy.assert_not_called()
        self.assertIn('Download ', output)
        self.assertIn('No installer or package code will run and nothing will be installed.', output)
        self.assertFalse((self.terms_dir / 'synfont.json').exists())
        self.assertEqual(list(self.machine.iterdir()), [])

    def test_cli_review_rejects_component_without_embedded_terms(self):
        with patch.object(sys, 'argv', ['wex-components', 'review', 'vcrun2022']):
            with self.assertRaises(ValueError) as cm:
                components.main()
        self.assertIn('has no embedded license terms to review', str(cm.exception))

    def test_cli_mutual_exclusion_and_argument_validation(self):
        invalid_combinations = [
            ['wex-components', '--oobe-coordinator', 'list'],
            ['wex-components', 'review'],
            ['wex-components', 'review', 'synfont', 'synfont2'],
            ['wex-components', 'install'],
            ['wex-components', '--reconcile-operation', 'op_1'],
            ['wex-components', '--reconcile-action', 'FAILED'],
        ]
        for argv in invalid_combinations:
            with self.subTest(argv=argv):
                f_err = io.StringIO()
                with patch.object(sys, 'argv', argv), patch('sys.stderr', f_err):
                    with self.assertRaises(SystemExit) as cm:
                        components.main()
                    self.assertEqual(cm.exception.code, 2)

    def test_cli_prepare_terms_rejects_embedded_components(self):
        with with_fixtures(), \
             patch.object(sys, 'argv', ['wex-components', '--prepare-terms', 'synfont']), \
             patch.object(components, 'TERMS', self.terms_dir):
            with self.assertRaisesRegex(ValueError, 'terms are embedded in its package; use the explicit review download'):
                components.main()

    def test_cli_prepare_terms_dispatches_fetch_when_terms_url_present(self):
        with with_fixtures(), \
             patch.object(sys, 'argv', ['wex-components', '--prepare-terms', 'synfont']), \
             patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'fetch_vendor_terms', return_value=('license text', 'sha256')) as mock_fetch, \
             patch.dict(components.COMPONENTS_METADATA['synfont'], {'terms_url': 'https://example.invalid/terms.docx', 'disabled': False}):
            components.main()
            mock_fetch.assert_called_once_with('synfont', self.terms_dir)

    def test_cli_review_download_reports_progress_and_trace(self):
        opener = FakeOpener(FIXTURES['synfont']['files'])
        err = io.StringIO()
        with with_fixtures(), \
             patch.object(components, 'TERMS', self.terms_dir), \
             patch.object(components, 'REVIEW_CACHE', self.cache_root), \
             patch.object(components.urllib.request, 'build_opener', return_value=opener), \
             patch.object(sys, 'argv', ['wex-components', '--review-download', 'synfont']), \
             redirect_stderr(err):
            components.main()
        stderr_output = err.getvalue()
        self.assertIn('REVIEW GET', stderr_output)
        self.assertIn('Downloading for review: 100%', stderr_output)
        self.assertTrue((self.terms_dir / 'synfont.json').is_file())
        for name in FIXTURES['synfont']['files']:
            self.assertTrue((self.cache_dir('synfont') / name).is_file(), f'{name} not written')


if __name__ == '__main__':
    unittest.main(verbosity=2)
