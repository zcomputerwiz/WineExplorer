#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic document/provenance fixtures: no vendor text or payload downloads."""
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('components', ROOT / 'scripts/wex-components.py')
components = importlib.util.module_from_spec(spec)
spec.loader.exec_module(components)
W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
R = 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'


def document(extra='', fmt='lowerLetter'):
    text = 'SYNTHETIC TEST TERMS ONLY; NO VENDOR AGREEMENT. ' * 20
    body = f'<w:p><w:r><w:t>{text}</w:t></w:r></w:p>'
    body += '<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr></w:pPr><w:r><w:t>First test clause.</w:t></w:r></w:p>'
    body += '<w:p><w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr></w:pPr><w:r><w:t>Second test clause.</w:t></w:r></w:p>'
    body += '<w:p><w:hyperlink r:id="testLink"><w:r><w:t>Test link</w:t></w:r></w:hyperlink></w:p>'
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w') as archive:
        archive.writestr('word/document.xml', f'<w:document xmlns:w="{W}" xmlns:r="{R}"><w:body>{body}{extra}</w:body></w:document>')
        archive.writestr('word/numbering.xml', f'<w:numbering xmlns:w="{W}"><w:abstractNum w:abstractNumId="0"><w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="{fmt}"/><w:lvlText w:val="%1."/></w:lvl></w:abstractNum><w:num w:numId="1"><w:abstractNumId w:val="0"/></w:num></w:numbering>')
        archive.writestr('word/_rels/document.xml.rels', f'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="testLink" Type="{R}/hyperlink" Target="https://example.invalid/fixture" TargetMode="External"/></Relationships>')
    return out.getvalue()


class WebView2TermsTests(unittest.TestCase):
    """The WebView2 agreement arrives as JSON holding HTML; fixtures are synthetic."""

    @staticmethod
    def payload(title='MICROSOFT EDGE WEBVIEW2 RUNTIME', body='Synthetic clause. ' * 40):
        html = f'<div><p>SYNTHETIC TERMS</p><p>{title}</p><hr/><p>{body}</p><h3><b>2.</b> Second &amp; last</h3></div>'
        return json.dumps({'evergreenHtml': html}).encode()

    def test_extraction_keeps_paragraphs_and_entities(self):
        text = components.extract_webview2_terms(self.payload())
        self.assertIn('MICROSOFT EDGE WEBVIEW2 RUNTIME\n\nSynthetic clause.', text)
        self.assertIn('2. Second & last', text)
        self.assertNotIn('<', text)

    def test_other_agreement_or_missing_field_is_refused(self):
        with self.assertRaises(ValueError):
            components.extract_webview2_terms(self.payload(title='MICROSOFT EDGE WEBVIEW2 RUNTIME (FIXED VERSION)'))
        with self.assertRaises(ValueError):
            components.extract_webview2_terms(json.dumps({'strings': {}}).encode())
        with self.assertRaises(ValueError):
            components.extract_webview2_terms(self.payload(body='too short'))

    def test_component_pins(self):
        meta = components.COMPONENTS_METADATA['webview2']
        payload = meta['payloads'][0]
        self.assertTrue(meta['terms_url'].startswith('https://'))
        self.assertEqual(meta['terms_format'], 'webview2-json')
        self.assertEqual(len(meta['terms_sha256']), 64)
        self.assertEqual(len(payload['sha256']), 64)
        self.assertEqual(urllib_host(payload['url']), payload['host'])


class WebPageTermsTests(unittest.TestCase):
    """Edge and Copilot: the agreement is the text of a web page's main element; fixtures are synthetic."""

    @staticmethod
    def page(start='SYNTHETIC AGREEMENT', body='Synthetic clause. ' * 40):
        return (f'<html><head><script>var nav = "menu";</script></head><body><nav><p>Menu link</p></nav>'
                f'<main id="m"><p>Last updated</p><h1>{start}</h1><style>p{{}}</style><p>{body}</p>'
                f'<noscript>Enable scripts</noscript><p>Fees &amp; costs</p></main>'
                f'<footer><p>Footer link</p></footer></body></html>').encode()

    def test_main_text_from_the_agreement_on(self):
        text = components.extract_html_terms(self.page(), 'SYNTHETIC AGREEMENT')
        self.assertTrue(text.startswith('SYNTHETIC AGREEMENT\n\nSynthetic clause.'))
        self.assertIn('Fees & costs', text)
        for outside in ('Menu link', 'Footer link', 'Last updated', 'var nav', 'Enable scripts', 'p{}'):
            self.assertNotIn(outside, text)

    def test_other_page_or_short_text_is_refused(self):
        with self.assertRaises(ValueError):
            components.extract_html_terms(self.page(start='OTHER TERMS'), 'SYNTHETIC AGREEMENT')
        with self.assertRaises(ValueError):
            components.extract_html_terms(self.page(body='too short'), 'SYNTHETIC AGREEMENT')
        with self.assertRaises(ValueError):
            components.extract_html_terms(self.page(), '')

    def test_extracted_text_is_what_is_pinned(self):
        page = self.page()
        text = components.extract_html_terms(page, 'SYNTHETIC AGREEMENT')
        pinned = {'url': 'https://example.invalid/terms', 'format': 'html', 'start': 'SYNTHETIC AGREEMENT',
                  'sha256': hashlib.sha256(text.encode()).hexdigest()}
        with patch.object(components, '_terms_download', return_value=(page.replace(b'menu', b'other'), None)) as get:
            self.assertEqual(components.terms_document_text('edge', pinned), text)
        self.assertNotIn('Wget', get.call_args[0][1]['User-Agent'])
        changed = page.replace(b'Synthetic clause. ', b'Changed clause. ', 1)
        with patch.object(components, '_terms_download', return_value=(changed, None)), self.assertRaises(ValueError):
            components.terms_document_text('edge', pinned)

    def test_component_pins(self):
        for verb in ('edge', 'copilot'):
            meta = components.COMPONENTS_METADATA[verb]
            payload = meta['payloads'][0]
            self.assertEqual(meta['terms_format'], 'html')
            self.assertEqual(meta['terms_sha256'], meta['terms_text_sha256'])
            self.assertEqual(len(meta['terms_sha256']), 64)
            self.assertEqual(len(payload['sha256']), 64)
            self.assertEqual(urllib_host(payload['url']), payload['host'])
            self.assertEqual(meta['requires'], ['webview2'])
            self.assertEqual(meta['execution_policy'], components.EDGEUPDATE_MSI_POLICY)

    def test_prerequisites_come_first_unless_installed(self):
        self.assertEqual(components.with_prerequisites(['copilot', 'edge'], installed=set()),
                         ['webview2', 'edge', 'copilot'])
        self.assertEqual(components.with_prerequisites(['edge'], installed={'webview2'}), ['edge'])
        self.assertEqual(components.with_prerequisites(['corefonts'], installed=set()), ['corefonts'])


def urllib_host(url):
    import urllib.parse
    return urllib.parse.urlsplit(url).hostname


class DocumentTests(unittest.TestCase):
    def test_real_renderer_preserves_clauses_and_target(self):
        text = components.extract_docx_terms(document())
        self.assertIn('a. First test clause.', text)
        self.assertIn('b. Second test clause.', text)
        self.assertIn('https://example.invalid/fixture', text)
        self.assertEqual(text.count('SYNTHETIC TEST TERMS ONLY'), 20)

    def test_bullets_and_decimal(self):
        self.assertIn('\u2022 First test clause.', components.extract_docx_terms(document(fmt='bullet')))
        self.assertIn('2. Second test clause.', components.extract_docx_terms(document(fmt='decimal')))

    def test_inherited_heading_numbering_and_subclause_restart(self):
        with zipfile.ZipFile(io.BytesIO(document(fmt='decimal'))) as source:
            parts = {name: source.read(name) for name in source.namelist()}
        style = '<w:pPr><w:pStyle w:val="LegalChild"/></w:pPr>'
        old = '<w:pPr><w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr></w:pPr>'
        parts['word/document.xml'] = parts['word/document.xml'].decode().replace(old, style).encode()
        parts['word/styles.xml'] = f'<w:styles xmlns:w="{W}"><w:style w:styleId="LegalBase"><w:pPr><w:numPr><w:numId w:val="1"/></w:numPr></w:pPr></w:style><w:style w:styleId="LegalChild"><w:basedOn w:val="LegalBase"/></w:style></w:styles>'.encode()
        result = io.BytesIO()
        with zipfile.ZipFile(result, 'w') as archive:
            for name, content in parts.items():
                archive.writestr(name, content)
        rendered = components.extract_docx_terms(result.getvalue())
        self.assertIn('1. First test clause.', rendered)
        self.assertIn('2. Second test clause.', rendered)

    def test_unknown_and_embedded_content_fail_closed(self):
        for extra, fmt in [('<w:object/>', 'decimal'), ('<w:del/>', 'decimal'),
                           ('<w:instrText>test field</w:instrText>', 'decimal'), ('', 'unsupported')]:
            with self.subTest(extra=extra, fmt=fmt), self.assertRaises(ValueError):
                components.extract_docx_terms(document(extra, fmt))

    def test_truncated_and_expanded_document_rejected(self):
        with self.assertRaises(zipfile.BadZipFile):
            components.extract_docx_terms(document()[:100])
        with patch.object(components, 'MAX_TERMS_BYTES', 100), self.assertRaises(ValueError):
            components.extract_docx_terms(document())


@unittest.skipUnless(os.getuid() == 0, 'Trusted-cache fixtures require a root-owned /run sandbox')
class TrustedCacheTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='wex-terms-fixture-', dir='/run')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.raw = document()
        self.text = components.extract_docx_terms(self.raw)
        self.meta = dict(components.COMPONENTS_METADATA['corefonts'], disabled=False,
                         terms_url='https://example.invalid/fixture.docx',
                         terms_sha256=hashlib.sha256(self.raw).hexdigest(),
                         terms_text_sha256=hashlib.sha256(self.text.encode()).hexdigest())
        self.patch = patch.dict(components.COMPONENTS_METADATA, corefonts=self.meta)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def response(self):
        result = io.BytesIO(self.raw)
        result.url = self.meta['terms_url']
        return result

    def prepare(self):
        with patch.object(components.urllib.request, 'urlopen', return_value=self.response()) as network:
            result = components.fetch_vendor_terms('corefonts', self.directory)
            self.assertEqual(network.call_count, 1)
            return result

    def test_cache_uses_exact_source_and_package_binding(self):
        text, digest = self.prepare()
        self.assertEqual(text, self.text)
        self.assertNotEqual(digest, hashlib.sha256(text.encode()).hexdigest())
        self.assertEqual(components.license_text('corefonts', self.directory), (text, digest))
        with patch.object(components.urllib.request, 'urlopen') as network:
            self.assertEqual(components.fetch_vendor_terms('corefonts', self.directory), (text, digest))
            network.assert_not_called()
        self.meta['recipe_files'] = [('CHANGED-PACKAGE.exe', 'a' * 64)]
        with self.assertRaisesRegex(ValueError, 'identity changed'):
            components.license_text('corefonts', self.directory)

    def test_arbitrary_long_text_or_changed_cache_cannot_authorize(self):
        (self.directory / 'corefonts.txt').write_text(self.text)
        with self.assertRaises(ValueError):
            components.license_text('corefonts', self.directory)
        self.prepare()
        path = self.directory / 'corefonts.json'
        record = json.loads(path.read_text())
        record['text'] += '\nINJECTED TEST TEXT'
        path.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, 'identity changed'):
            components.license_text('corefonts', self.directory)

    def test_explicit_review_refreshes_stale_package_binding(self):
        _, old_digest = self.prepare()
        self.meta['recipe_files'] = [('CHANGED-PACKAGE.exe', 'a' * 64)]
        with self.assertRaises(components.StaleTermsError):
            components.license_text('corefonts', self.directory)
        with patch.object(components.urllib.request, 'urlopen', return_value=self.response()) as network:
            text, new_digest = components.prepare_terms('corefonts', self.directory)
        self.assertEqual(network.call_count, 1)
        self.assertEqual(text, self.text)
        self.assertNotEqual(new_digest, old_digest)

    def test_review_does_not_refresh_insecure_cached_path(self):
        self.prepare()
        (self.directory / 'corefonts.json').chmod(0o666)
        with patch.object(components.urllib.request, 'urlopen') as network:
            with self.assertRaisesRegex(ValueError, 'Insecure vendor license path'):
                components.prepare_terms('corefonts', self.directory)
            network.assert_not_called()

    def test_wrong_document_hash_writes_no_cache(self):
        self.meta['terms_sha256'] = '0' * 64
        with patch.object(components.urllib.request, 'urlopen', return_value=self.response()):
            with self.assertRaisesRegex(ValueError, 'document changed'):
                components.fetch_vendor_terms('corefonts', self.directory)
        self.assertEqual(list(self.directory.iterdir()), [])

    def test_changed_extraction_and_insecure_cache_fail_closed(self):
        self.meta['terms_text_sha256'] = '0' * 64
        with patch.object(components.urllib.request, 'urlopen', return_value=self.response()):
            with self.assertRaisesRegex(ValueError, 'extraction changed'):
                components.fetch_vendor_terms('corefonts', self.directory)
        self.meta['terms_text_sha256'] = hashlib.sha256(self.text.encode()).hexdigest()
        self.prepare()
        (self.directory / 'corefonts.json').chmod(0o666)
        with self.assertRaisesRegex(ValueError, 'Insecure vendor license path'):
            components.license_text('corefonts', self.directory)


if __name__ == '__main__':
    unittest.main(verbosity=2)
