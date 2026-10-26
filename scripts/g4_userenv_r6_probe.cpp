// SPDX-License-Identifier: GPL-2.0-or-later
// Original bounded R6 registration metadata sanity probe. No profile/token/process changes.
#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

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

typedef HRESULT (WINAPI *derive_t)(PCWSTR,PSID *);
typedef HRESULT (WINAPI *register_t)(PSID,LPCWSTR,LPCWSTR);
typedef HRESULT (WINAPI *unregister_t)(PSID);
typedef HRESULT (WINAPI *lookup_t)(PSID,LPWSTR *);
typedef void (WINAPI *freemem_t)(void *);

int wmain(int argc,wchar_t **argv) {
    if(argc!=3||wcscmp(argv[1],L"--nonce")||wcslen(argv[2])!=32)return 2;
    for(unsigned i=0;i<32;++i)if(!((argv[2][i]>='0'&&argv[2][i]<='9')||(argv[2][i]>='a'&&argv[2][i]<='f')))return 2;
    const wchar_t *name=argv[2]; // nonce-derived AppContainer name, 32 WCHARs (<=64)
    if(wcslen(name)>64)return 2;
    wchar_t moniker[96],display[96];
    if(swprintf(moniker,96,L"wex-r6-%ls",name)<0)return 2;
    if(swprintf(display,96,L"WEX R6 %ls",name)<0)return 2;

    HMODULE ue=LoadLibraryW(L"userenv.dll");
    HMODULE kb=LoadLibraryW(L"kernelbase.dll");
    derive_t derive=NULL;register_t register_sid=NULL;unregister_t unregister_sid=NULL;lookup_t lookup_moniker=NULL;freemem_t free_mem=NULL;
    if(ue) {
        FARPROC p=GetProcAddress(ue,"DeriveAppContainerSidFromAppContainerName");
        static_assert(sizeof(p)==sizeof(derive),"Win32 function pointer size");
        memcpy(&derive,&p,sizeof(derive));
    }
    if(kb) {
        FARPROC p;
        p=GetProcAddress(kb,"AppContainerRegisterSid");memcpy(&register_sid,&p,sizeof(register_sid));
        p=GetProcAddress(kb,"AppContainerUnregisterSid");memcpy(&unregister_sid,&p,sizeof(unregister_sid));
        p=GetProcAddress(kb,"AppContainerLookupMoniker");memcpy(&lookup_moniker,&p,sizeof(lookup_moniker));
        p=GetProcAddress(kb,"AppContainerFreeMemory");memcpy(&free_mem,&p,sizeof(free_mem));
    }

    WCHAR ue_path[512]={0},kb_path[512]={0},ue_exp[512]={0},kb_exp[512]={0};
    if(GetSystemDirectoryW(ue_exp,480))wcscat(ue_exp,L"\\userenv.dll");
    if(GetSystemDirectoryW(kb_exp,480))wcscat(kb_exp,L"\\kernelbase.dll");
    bool ue_normal=ue&&GetModuleFileNameW(ue,ue_path,512)&&!lstrcmpiW(ue_path,ue_exp);
    bool kb_normal=kb&&GetModuleFileNameW(kb,kb_path,512)&&!lstrcmpiW(kb_path,kb_exp);

    // 1..7: normal builtin paths + the five export resolutions.
    check(ue_normal);check(kb_normal);
    check(derive!=NULL);check(register_sid!=NULL);check(unregister_sid!=NULL);check(lookup_moniker!=NULL);check(free_mem!=NULL);

    PSID sid=NULL;PSID t=(PSID)0xdeadbeef;
    // 8..13: nonce-derived name -> legitimate AppContainer SID (layout + authority).
    check(derive&&derive(NULL,&t)==E_INVALIDARG&&t==(PSID)0xdeadbeef);
    check(derive&&derive(L"",&t)==E_INVALIDARG&&t==(PSID)0xdeadbeef);
    check(derive&&derive(name,NULL)==E_INVALIDARG);
    bool derived=derive&&derive(name,&sid)==S_OK&&sid!=NULL&&IsValidSid(sid)!=FALSE;check(derived);
    check(derived&&*GetSidSubAuthorityCount(sid)==8);
    if(derived) {
        SID_IDENTIFIER_AUTHORITY authority={{0,0,0,0,0,15}};
        check(!memcmp(GetSidIdentifierAuthority(sid),&authority,sizeof(authority))&&*GetSidSubAuthority(sid,0)==2);
    } else check(false);

    // 14..21: registration metadata lifecycle for a fresh identity this run owns.
    LPWSTR m=NULL,m2=NULL,m3=NULL,m4=NULL;
    check(derived&&lookup_moniker&&lookup_moniker(sid,&m)==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));if(m&&free_mem)free_mem(m);
    check(derived&&register_sid&&register_sid(sid,moniker,display)==S_OK);
    check(derived&&lookup_moniker&&lookup_moniker(sid,&m2)==S_OK&&m2!=NULL);
    check(m2!=NULL&&!wcscmp(m2,moniker));
    check(derived&&register_sid&&register_sid(sid,moniker,display)==HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
    check(derived&&lookup_moniker&&lookup_moniker(sid,&m3)==S_OK&&m3!=NULL&&!wcscmp(m3,moniker));
    check(derived&&unregister_sid&&unregister_sid(sid)==S_OK);
    check(derived&&lookup_moniker&&lookup_moniker(sid,&m4)==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    if(m2&&free_mem)free_mem(m2);
    if(m3&&free_mem)free_mem(m3);
    if(m4&&free_mem)free_mem(m4);

    char ue_json[4096]={0},kb_json[4096]={0},nonce_json[64]={0};
    if(!escaped(ue_path,ue_json,sizeof(ue_json))||!escaped(kb_path,kb_json,sizeof(kb_json))||!escaped(name,nonce_json,sizeof(nonce_json)))return 3;
    bool exports=derive&&register_sid&&unregister_sid&&lookup_moniker&&free_mem;
    bool passed=checks==21&&failures==0;
    printf("{\"v\":1,\"nonce\":\"%s\",\"pid\":%lu,\"userenv_path\":\"%s\",\"kernelbase_path\":\"%s\",\"paths_normal\":%s,\"exports_found\":%s,\"checks\":%u,\"failures\":%u,\"passed\":%s}\n",
        nonce_json,GetCurrentProcessId(),ue_json,kb_json,(ue_normal&&kb_normal)?"true":"false",exports?"true":"false",checks,failures,passed?"true":"false");
    fflush(stdout);
    if(sid)FreeSid(sid);
    if(ue)FreeLibrary(ue);
    if(kb)FreeLibrary(kb);
    return passed?0:1;
}
