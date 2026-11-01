# SPDX-License-Identifier: GPL-2.0-or-later
"""R6 combined-build deploy packager: wine.inf [WinmdFiles] inclusion + missing-resource fail-fast.
Small local fixtures only; never builds the real archive and never touches the VM/guest."""
import hashlib
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock
from types import SimpleNamespace
import g4_combined_r6_deploy as deploy_module

REPO=Path(__file__).resolve().parents[1]
FONTS=deploy_module.REQUIRED_FONTS
WINMD=('windows.applicationmodel.winmd','windows.globalization.winmd','windows.ui.xaml.winmd')
# The full [WinmdFiles] closure of the recipe wine/loader/wine.inf.in (order as listed there).
REPO_WINMD=('windows.applicationmodel.winmd','windows.globalization.winmd','windows.graphics.winmd','windows.media.winmd',
    'windows.networking.winmd','windows.perception.winmd','windows.storage.winmd','windows.system.winmd',
    'windows.ui.winmd','windows.ui.xaml.winmd')

def trial():
    p=SimpleNamespace()
    p.required_winmd=lambda: deploy_module.required_winmd(p.MY_BUILD)
    p.preflight_winmd=lambda: deploy_module.preflight_winmd(p.MY_BUILD)
    p.deploy_relpaths=lambda names=(): deploy_module.deploy_relpaths(p.MY_BUILD,names)
    p.build_deploy=lambda: deploy_module.build_deploy(p.MY_BUILD,p.DEPLOY_ARC_DIR)
    return p

def make_fixture(root,winmd=WINMD,inf_winmd=None,drop=(),empty=(),inf_text=None,drop_fonts=()):
    """Tiny in-tree-layout host build: launcher + wine symlink stand-in, one builtin PE, recipe wine.inf,
    include/ winmd resources, wow64/fonts/ with the six required Wine TrueType fonts. Returns the build root."""
    if inf_winmd is None:inf_winmd=winmd
    b=root/'build'
    for d in ('wow64/loader','wow64/tools/wine','wow64/dlls/foo/x86_64-windows','wow64/fonts','include'):
        (b/d).mkdir(parents=True)
    if inf_text is None:
        inf=['[BaseInstall]','CopyFiles=WinmdFiles','','[WinmdFiles]']+list(inf_winmd)+['','[WineSourceDirs]','WinmdFiles = winmd,include']
        inf_text='\n'.join(inf)+'\n'
    (b/'wow64/loader/wine.inf').write_text(inf_text)
    (b/'wow64/tools/wine/wine').write_bytes(b'launcher')
    (b/'wow64/wine').write_bytes(b'launcher')  # stand-in for the wine->tools/wine/wine symlink (see symlink test)
    (b/'wow64/dlls/foo/x86_64-windows/foo.dll').write_bytes(b'PE')
    for n in winmd:
        if n in drop:continue
        (b/'include'/n).write_bytes(b'' if n in empty else b'winmd-'+n.encode())
    for n in FONTS:
        if n in drop_fonts:continue
        (b/'wow64/fonts'/n).write_bytes(b'font-'+n.encode())
    return b

class CombinedR6DeployTests(unittest.TestCase):
    def setUp(self):
        self._tmp=tempfile.TemporaryDirectory(prefix='r6-winmd-')
        self.root=Path(self._tmp.name)
        self.p=trial()

    def tearDown(self):
        self._tmp.cleanup()

    def deploy(self,winmd=WINMD,inf_winmd=None,drop=(),empty=(),inf_text=None,drop_fonts=()):
        self._n=getattr(self,'_n',0)+1;root=self.root/str(self._n)  # fresh tree per call
        b=make_fixture(root,winmd=winmd,inf_winmd=inf_winmd,drop=drop,empty=empty,inf_text=inf_text,drop_fonts=drop_fonts)
        self.p.MY_BUILD=b;self.p.DEPLOY_ARC_DIR=root/'arc'
        return b

    def test_repo_recipe_parses_and_all_winmd_deployed(self):
        recipe='[WinmdFiles]\n'+'\n'.join(REPO_WINMD)+'\n[WineSourceDirs]\nWinmdFiles=winmd,include\n'
        self.deploy(winmd=REPO_WINMD,inf_text=recipe)
        self.assertEqual(self.p.required_winmd(),list(REPO_WINMD))
        arc,manifest,total,count=self.p.build_deploy()
        for n in REPO_WINMD:
            self.assertEqual(manifest['wow64/include/'+n],hashlib.sha256(b'winmd-'+n.encode()).hexdigest())

    def test_present_winmd_in_archive_and_manifest(self):
        b=self.deploy()
        arc,manifest,total,count=self.p.build_deploy()
        for n in WINMD:
            self.assertEqual(manifest['wow64/include/'+n],hashlib.sha256(b'winmd-'+n.encode()).hexdigest())
        self.assertEqual(manifest['wow64/tools/wine/wine'],hashlib.sha256(b'launcher').hexdigest())
        self.assertEqual(manifest['wow64/dlls/foo/x86_64-windows/foo.dll'],hashlib.sha256(b'PE').hexdigest())
        expected={p.relative_to(b).as_posix():p.stat().st_size for p in (b/'wow64').rglob('*') if p.is_file()}
        expected.update({'wow64/include/'+n:(b/'include'/n).stat().st_size for n in WINMD})
        self.assertEqual(count,len(expected))  # wine.inf, wine, tools/wine/wine, foo.dll, 3 winmd
        self.assertEqual(total,sum(expected.values()))
        with tarfile.open(arc) as t:
            self.assertEqual({m.name for m in t.getmembers()},set(manifest))
            for n in WINMD:
                m=t.getmember('wow64/include/'+n)
                self.assertTrue(m.isfile())
                self.assertEqual(t.extractfile(m).read(),b'winmd-'+n.encode())
        self.assertEqual(self.p.deploy_relpaths(self.p.preflight_winmd()),sorted(manifest))

    def test_missing_winmd_fails_fast_before_archive(self):
        self.deploy(drop=('windows.ui.xaml.winmd',))
        with self.assertRaisesRegex(ValueError,'windows.ui.xaml.winmd'):self.p.preflight_winmd()
        with self.assertRaisesRegex(ValueError,'windows.ui.xaml.winmd'):self.p.build_deploy()
        self.assertFalse((self.p.DEPLOY_ARC_DIR/'wine-build.tar.gz').exists())  # nothing built, nothing to deploy

    def test_empty_winmd_fails_fast(self):
        self.deploy(empty=('windows.globalization.winmd',))
        with self.assertRaisesRegex(ValueError,'windows.globalization.winmd'):self.p.build_deploy()

    def test_recipe_rejects_path_escape(self):
        self.deploy(inf_winmd=('../outside.winmd',))
        with self.assertRaisesRegex(ValueError,'Invalid WinmdFiles'):self.p.build_deploy()

    def test_staged_runtime_uses_explicit_build_resources(self):
        build=self.deploy(inf_winmd=('external.winmd',))
        resource=self.root/'generated-include/external.winmd'
        resource.parent.mkdir()
        resource.write_bytes(b'original-build-resource')
        source=resource.parent
        with self.assertRaisesRegex(ValueError,'external.winmd'):
            deploy_module.preflight_winmd(build)
        arc,manifest,_,_=deploy_module.build_deploy(build,self.p.DEPLOY_ARC_DIR,source)
        self.assertEqual(manifest['wow64/include/external.winmd'],hashlib.sha256(resource.read_bytes()).hexdigest())
        with tarfile.open(arc) as tar:
            self.assertEqual(tar.extractfile('wow64/include/external.winmd').read(),resource.read_bytes())

    def test_real_launcher_symlink(self):
        build=self.deploy()
        launcher=build/'wow64/wine'
        launcher.unlink()
        try:
            launcher.symlink_to('tools/wine/wine')
        except OSError as error:
            if getattr(error,'winerror',None)==1314:
                self.skipTest('Windows symlink creation requires privilege; run on WSL')
            raise
        arc,manifest,_,_=self.p.build_deploy()
        self.assertEqual(manifest['wow64/wine'],hashlib.sha256(b'launcher').hexdigest())
        with tarfile.open(arc) as tar:
            entry=tar.getmember('wow64/wine')
            self.assertTrue(entry.issym())
            self.assertEqual(entry.linkname,'tools/wine/wine')

    def test_recipe_is_authoritative(self):
        extra='windows.extra.winmd'
        self.deploy(winmd=WINMD+(extra,),inf_winmd=WINMD+(extra,))
        arc,manifest,total,count=self.p.build_deploy()
        self.assertIn('wow64/include/'+extra,manifest)
        self.assertEqual(count,14)  # wine.inf, wine, tools/wine/wine, foo.dll, 4 winmd, 6 fonts
        self.deploy(winmd=WINMD,inf_winmd=WINMD+(extra,))  # recipe requires a resource the host build lacks
        with self.assertRaisesRegex(ValueError,extra):self.p.build_deploy()

    def test_missing_or_empty_recipe_fails(self):
        b=self.deploy(inf_winmd=())
        with self.assertRaisesRegex(ValueError,'WinmdFiles'):self.p.required_winmd()
        (b/'wow64/loader/wine.inf').unlink()
        with self.assertRaisesRegex(ValueError,'wine.inf'):self.p.required_winmd()

    def test_missing_font_fails_fast_before_archive(self):
        self.deploy(drop_fonts=('tahoma.ttf',))
        with self.assertRaisesRegex(ValueError,'tahoma.ttf'):deploy_module.preflight_fonts(self.p.MY_BUILD)
        with self.assertRaisesRegex(ValueError,'tahoma.ttf'):self.p.build_deploy()
        self.assertFalse((self.p.DEPLOY_ARC_DIR/'wine-build.tar.gz').exists())

    def test_empty_font_fails_fast(self):
        b=self.deploy(drop_fonts=('symbol.ttf',))
        (b/'wow64/fonts/symbol.ttf').write_bytes(b'')
        with self.assertRaisesRegex(ValueError,'symbol.ttf'):self.p.build_deploy()

    def test_font_symlink_dereferenced_into_archive(self):
        b=self.deploy()
        source=self.root/'wine-src/fonts/tahoma.ttf'  # stands in for an out-of-tree Wine source checkout
        source.parent.mkdir(parents=True)
        source.write_bytes(b'real-tahoma-bytes')
        link=b/'wow64/fonts/tahoma.ttf'
        link.unlink()
        try:
            link.symlink_to(source)
        except OSError as error:
            if getattr(error,'winerror',None)==1314:
                self.skipTest('Windows symlink creation requires privilege; run on WSL')
            raise
        arc,manifest,_,_=self.p.build_deploy()
        self.assertEqual(manifest['wow64/fonts/tahoma.ttf'],hashlib.sha256(b'real-tahoma-bytes').hexdigest())
        with tarfile.open(arc) as tar:
            member=tar.getmember('wow64/fonts/tahoma.ttf')
            self.assertTrue(member.isfile())  # not a symlink: the source tree will not exist once deployed
            self.assertEqual(tar.extractfile(member).read(),b'real-tahoma-bytes')

    def test_dangling_font_symlink_fails_fast(self):
        b=self.deploy()
        link=b/'wow64/fonts/tahoma.ttf'
        link.unlink()
        try:
            link.symlink_to(self.root/'does-not-exist.ttf')
        except OSError as error:
            if getattr(error,'winerror',None)==1314:
                self.skipTest('Windows symlink creation requires privilege; run on WSL')
            raise
        with self.assertRaisesRegex(ValueError,'tahoma.ttf'):deploy_module.preflight_fonts(b)
        with self.assertRaisesRegex(ValueError,'tahoma.ttf'):self.p.build_deploy()

    def test_launcher_symlink_stored_as_symlink(self):
        b=self.deploy()
        wine=b/'wow64/wine'
        with mock.patch.object(Path,'is_symlink',new=lambda self: self==wine),\
             mock.patch('os.readlink',new=lambda p:'tools/wine/wine'):
            arc,manifest,total,count=self.p.build_deploy()
        with tarfile.open(arc) as t:
            m=t.getmember('wow64/wine')
            self.assertEqual(m.type,tarfile.SYMTYPE)
            self.assertEqual(m.linkname,'tools/wine/wine')
            self.assertEqual(m.size,0)
        self.assertEqual(manifest['wow64/wine'],hashlib.sha256(b'launcher').hexdigest())

if __name__=='__main__':unittest.main()
