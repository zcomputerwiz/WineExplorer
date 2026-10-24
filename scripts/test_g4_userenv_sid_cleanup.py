# SPDX-License-Identifier: LGPL-2.1-or-later
"""Host fault callbacks compile the actual new Wine function, not a Python model.

Real SHA/NLS/SID/FreeSid behavior is tested separately by the Wine-native suite.
"""
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'build/wine-userenv-appcontainer/dlls/userenv/userenv_main.c'
LAB=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022')
PREFIX=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef uint16_t WCHAR;
typedef uint8_t BYTE;
typedef uint32_t DWORD;
typedef int32_t NTSTATUS, HRESULT;
typedef void *PSID, *BCRYPT_ALG_HANDLE, *BCRYPT_HASH_HANDLE;
typedef struct { BYTE Value[6]; } SID_IDENTIFIER_AUTHORITY;
#define WINAPI
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define SECURITY_APP_PACKAGE_AUTHORITY {0,0,0,0,0,15}
#define SECURITY_APP_PACKAGE_BASE_RID 2
#define SECURITY_APP_PACKAGE_RID_COUNT 8
#define BCRYPT_SHA256_ALGORITHM ((const WCHAR *)0)
#define E_INVALIDARG ((HRESULT)0x80070057)
#define S_OK 0
#define HRESULT_FROM_NT(s) ((HRESULT)((uint32_t)(s)|0x10000000))
#define HRESULT_FROM_WIN32(s) ((HRESULT)(0x80070000|(s)))
static int fail, calls, closed, destroyed, allocated;
static NTSTATUS step(void) { return ++calls==fail?(NTSTATUS)0xc0000001:0; }
static WCHAR RtlDowncaseUnicodeChar(WCHAR c) { return c>='A'&&c<='Z'?c+32:c; }
static NTSTATUS BCryptOpenAlgorithmProvider(void **a,const WCHAR *x,const WCHAR *y,DWORD f)
{ NTSTATUS s; (void)x;(void)y;(void)f; s=step();if (!s)*a=(void *)1;return s; }
static NTSTATUS BCryptCreateHash(void *a,void **h,BYTE *x,DWORD n,BYTE *k,DWORD m,DWORD f)
{ NTSTATUS s; assert(a==(void *)1);(void)x;(void)n;(void)k;(void)m;(void)f;s=step();if(!s)*h=(void *)2;return s; }
static NTSTATUS BCryptHashData(void *h,BYTE *p,DWORD n,DWORD f)
{ assert(h==(void *)2);assert(n==8);assert(p[0]=='t'&&p[2]=='e'&&p[4]=='s'&&p[6]=='t');(void)f;return step(); }
static NTSTATUS BCryptFinishHash(void *h,BYTE *p,DWORD n,DWORD f)
{ unsigned i;assert(h==(void *)2&&n==32);(void)f;for(i=0;i<n;++i)p[i]=i+1;return step(); }
static NTSTATUS BCryptDestroyHash(void *h)
{ assert(h==(void *)2&&!closed);++destroyed;return 0; }
static NTSTATUS BCryptCloseAlgorithmProvider(void *a,DWORD f)
{ assert(a==(void *)1);(void)f;++closed;return 0; }
static DWORD GetLastError(void) { return 8; }
static int AllocateAndInitializeSid(SID_IDENTIFIER_AUTHORITY *a,BYTE n,DWORD base,
 DWORD v0,DWORD v1,DWORD v2,DWORD v3,DWORD v4,DWORD v5,DWORD v6,PSID *out)
{
 assert(closed==1&&destroyed==1);assert(a->Value[5]==15&&n==8&&base==2);
 assert(v0==0x04030201&&v1==0x08070605&&v2==0x0c0b0a09&&v3==0x100f0e0d);
 assert(v4==0x14131211&&v5==0x18171615&&v6==0x1c1b1a19);
 ++allocated;if(fail==5)return 0;*out=(void *)0x1234;return 1;
}
'''
SUFFIX=r'''
int main(void)
{
 WCHAR name[]={'T','E','S','T',0},empty[]={0},long_name[66];PSID sid;HRESULT hr;unsigned i;
 for(fail=1;fail<=5;++fail)
 {
  calls=closed=destroyed=allocated=0;sid=(void *)0xdead;
  hr=DeriveAppContainerSidFromAppContainerName(name,&sid);
  assert(hr<0&&sid==(void *)0xdead);
  assert(closed==(fail>1)&&destroyed==(fail>2)&&allocated==(fail==5));
 }
 fail=calls=closed=destroyed=allocated=0;sid=(void *)0xdead;
 assert(DeriveAppContainerSidFromAppContainerName(name,&sid)==0&&sid==(void *)0x1234);
 assert(closed==1&&destroyed==1&&allocated==1);
 calls=closed=destroyed=allocated=0;sid=(void *)0xdead;
 assert(DeriveAppContainerSidFromAppContainerName(NULL,&sid)==E_INVALIDARG);
 assert(DeriveAppContainerSidFromAppContainerName(empty,&sid)==E_INVALIDARG);
 assert(DeriveAppContainerSidFromAppContainerName(name,NULL)==E_INVALIDARG);
 for(i=0;i<65;++i)long_name[i]='A';long_name[65]=0;
 assert(DeriveAppContainerSidFromAppContainerName(long_name,&sid)==E_INVALIDARG);
 assert(!calls&&!closed&&!destroyed&&!allocated&&sid==(void *)0xdead);
 puts("PASS actual function: crypto failures1..4, allocation failure, reverse cleanup, unchanged output");return 0;
}
'''

class CleanupTests(unittest.TestCase):
    def test_actual_function_operational_failures_preserve_output_and_release_handles(self):
        source=SOURCE.read_text();begin=source.index('HRESULT WINAPI DeriveAppContainerSidFromAppContainerName(')
        end=source.index('\n}\n',begin)+3
        function=source[begin:end]
        with tempfile.TemporaryDirectory(prefix='userenv-sid-cleanup-',dir=LAB) as folder:
            path=Path(folder);(path/'fault.c').write_text(PREFIX+function+SUFFIX)
            built=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(path/'fault.c'),'-o',str(path/'fault')],capture_output=True,text=True,timeout=20)
            self.assertEqual(built.returncode,0,built.stderr)
            run=subprocess.run([str(path/'fault')],capture_output=True,text=True,timeout=5)
            self.assertEqual(run.returncode,0,run.stderr)
            self.assertIn('PASS actual function',run.stdout)
            print('extracted_function_sha256='+hashlib.sha256(function.encode()).hexdigest())

if __name__=='__main__':unittest.main()
