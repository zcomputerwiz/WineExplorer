// SPDX-License-Identifier: GPL-2.0-or-later
// Original read-only public API probe. No profile/token/process changes.
#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
static void check(bool value) { ++checks;if (!value) ++failures; }
static bool escaped(const wchar_t *s,char *out,size_t cap) {
    char utf8[2048];int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s,-1,utf8,sizeof(utf8),NULL,NULL);
    size_t used=0;if (!n)return false;
    for(int i=0;i<n-1;++i) {
        unsigned char c=utf8[i];if(c<32||used+3>=cap)return false;
        if(c=='"'||c=='\\')out[used++]='\\';
        out[used++]=c;
    }
    out[used]=0;return true;
}
int wmain(int argc,wchar_t **argv) {
    if(argc!=3||wcscmp(argv[1],L"--nonce")||wcslen(argv[2])!=32)return 2;
    for(unsigned i=0;i<32;++i)if(!((argv[2][i]>='0'&&argv[2][i]<='9')||(argv[2][i]>='a'&&argv[2][i]<='f')))return 2;
    HMODULE module=LoadLibraryW(L"userenv.dll");
    typedef HRESULT (WINAPI *derive_t)(PCWSTR,PSID *);
    derive_t derive=NULL;
    if(module) {
        FARPROC proc=GetProcAddress(module,"DeriveAppContainerSidFromAppContainerName");
        static_assert(sizeof(proc)==sizeof(derive),"Win32 function pointer size");
        memcpy(&derive,&proc,sizeof(derive));
    }
    WCHAR path[512]={0},expected[512]={0};char path_json[4096]={0},nonce[64]={0};
    bool normal=false;
    if(module&&GetModuleFileNameW(module,path,512)&&GetSystemDirectoryW(expected,480)) {
        wcscat(expected,L"\\userenv.dll");normal=!lstrcmpiW(path,expected);
    }
    check(normal);check(derive!=NULL);
    static const struct { const WCHAR *name;const char *sid; } cases[]={
        {L"test","S-1-15-2-1980125950-1037672881-421768107-1949737198-2922275827-507320043-1582245000"},
        {L"TEST","S-1-15-2-1980125950-1037672881-421768107-1949737198-2922275827-507320043-1582245000"},
        {L"WineExplorer.MixedCase_01","S-1-15-2-3520518378-1338609999-3243366982-3993260403-3393427587-956535139-1620658767"},
        {L"wineexplorer.mixedcase_01","S-1-15-2-3520518378-1338609999-3243366982-3993260403-3393427587-956535139-1620658767"},
        {L"W\x00e4ne\x03a3","S-1-15-2-1925959860-2464893839-2425465101-3504107519-2374159773-1197180585-2285453026"},
        {L"a/b:*? ","S-1-15-2-4173701422-852867342-4044399721-1874327424-3122796747-664920408-875619919"}
    };
    if(derive) {
        PSID sid=(PSID)0xdeadbeef;
        check(derive(NULL,&sid)==E_INVALIDARG&&sid==(PSID)0xdeadbeef);
        check(derive(L"",&sid)==E_INVALIDARG&&sid==(PSID)0xdeadbeef);
        check(derive(L"test",NULL)==E_INVALIDARG);
        for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
            sid=NULL;HRESULT hr=derive(cases[i].name,&sid);check(hr==S_OK&&sid!=NULL);
            if(hr==S_OK&&sid) {
                bool valid=IsValidSid(sid)!=FALSE;check(valid);
                bool layout=valid&&*GetSidSubAuthorityCount(sid)==8;check(layout);
                SID_IDENTIFIER_AUTHORITY authority={{0,0,0,0,0,15}};
                check(layout&&!memcmp(GetSidIdentifierAuthority(sid),&authority,sizeof(authority))&&*GetSidSubAuthority(sid,0)==2);
                char *text=NULL;BOOL ret=ConvertSidToStringSidA(sid,&text);
                check(ret&&text&&!strcmp(text,cases[i].sid));LocalFree(text);check(FreeSid(sid)==NULL);
            }
        }
        for(unsigned length=1;length<=65;++length) {
            if(length!=1&&length!=63&&length!=64&&length!=65)continue;
            WCHAR name[66];for(unsigned i=0;i<length;++i)name[i]='A';name[length]=0;
            sid=(PSID)0xdeadbeef;HRESULT hr=derive(name,&sid);
            if(length==65)check(hr==E_INVALIDARG&&sid==(PSID)0xdeadbeef);
            else { bool valid=hr==S_OK&&sid&&sid!=(PSID)0xdeadbeef&&IsValidSid(sid);check(valid);if(valid)FreeSid(sid); }
        }
    }
    if(!escaped(path,path_json,sizeof(path_json))||!escaped(argv[2],nonce,sizeof(nonce)))return 3;
    bool passed=checks==45&&failures==0;
    printf("{\"v\":1,\"nonce\":\"%s\",\"pid\":%lu,\"module_path\":\"%s\",\"module_path_normal\":%s,\"export_found\":%s,\"checks\":%u,\"failures\":%u,\"passed\":%s}\n",
        nonce,GetCurrentProcessId(),path_json,normal?"true":"false",derive?"true":"false",checks,failures,passed?"true":"false");
    fflush(stdout);if(module)FreeLibrary(module);return passed?0:1;
}
