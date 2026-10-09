/* Run inside the WineExplorer desktop, compiled with -mwindows -luxtheme.
 * Exercises isolated style previews, handle ownership, and applying metrics
 * after a preview has already parsed the file. Restores the original style. */
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <stdio.h>
#include <string.h>

typedef struct { WCHAR name[MAX_PATH+1],display[MAX_PATH+1],tip[MAX_PATH+1]; } NAMES;
typedef HRESULT (WINAPI *OPEN)(LPCWSTR,LPCWSTR,LPCWSTR,HANDLE *,DWORD);
typedef HRESULT (WINAPI *CLOSE)(HANDLE);
typedef HRESULT (WINAPI *ENUM)(LPWSTR,LPWSTR,DWORD,NAMES *);
typedef HRESULT (WINAPI *APPLY)(HANDLE,char *,HWND);
typedef HTHEME (WINAPI *DATA)(HANDLE,HWND,LPCWSTR,DWORD);

int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,char *command,int show)
{
    HMODULE module=LoadLibraryW(L"uxtheme.dll");
    OPEN open=(OPEN)GetProcAddress(module,MAKEINTRESOURCEA(2));
    CLOSE close=(CLOSE)GetProcAddress(module,MAKEINTRESOURCEA(3));
    APPLY apply=(APPLY)GetProcAddress(module,MAKEINTRESOURCEA(4));
    ENUM colors=(ENUM)GetProcAddress(module,MAKEINTRESOURCEA(9));
    ENUM sizes=(ENUM)GetProcAddress(module,MAKEINTRESOURCEA(10));
    DATA data=(DATA)GetProcAddress(module,MAKEINTRESOURCEA(16));
    WCHAR path[MAX_PATH],color[64],size[64],after[MAX_PATH],after_color[64],after_size[64];
    NAMES alternate,large;
    HANDLE original=NULL,pending=NULL; HTHEME preview=NULL;
    NONCLIENTMETRICSW before={sizeof(before)},now={sizeof(now)};
    LOGFONTW font; COLORREF live=GetSysColor(COLOR_BTNFACE),sample;
    FILE *report=fopen("Z:\\tmp\\theme-preview-probe.txt","w");
    int i,failed=0; BOOL applied=FALSE;
#define CHECK(condition,label) do { BOOL passed=(condition); fprintf(report,"%s %s\n",passed?"PASS":"FAIL",label); if(!passed) failed++; } while(0)
    if(!report) return 1;
    if(!open || !close || !apply || !colors || !sizes || !data ||
       FAILED(GetCurrentThemeName(path,MAX_PATH,color,64,size,64)))
    { fprintf(report,"SKIP no active style or preview API\n"); fclose(report); return 2; }
    for(i=0;SUCCEEDED(colors(path,NULL,i,&alternate));i++) if(wcsicmp(alternate.name,color)) break;
    if(FAILED(colors(path,NULL,i,&alternate)) || FAILED(sizes(path,alternate.name,1,&large)))
    { fprintf(report,"SKIP no alternate color/size\n"); fclose(report); return 2; }
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(before),&before,0);
    CHECK(SUCCEEDED(open(path,color,size,&original,0)),"open original");
    CHECK(SUCCEEDED(open(path,alternate.name,large.name,&pending,0)),"open alternate color and size");
    if(!original || !pending) goto done;
    preview=data(pending,NULL,L"Window",0);
    CHECK(preview!=NULL,"open isolated preview data");
    if(!preview) goto done;
    CHECK(SUCCEEDED(GetThemeSysFont(preview,TMT_CAPTIONFONT,&font)),"read preview caption font");
    sample=GetThemeSysColor(preview,COLOR_BTNFACE);
    CHECK(sample!=live,"preview color comes from selected variant");
    CHECK(font.lfHeight!=before.lfCaptionFont.lfHeight,"preview font comes from selected size");
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(now),&now,0);
    GetCurrentThemeName(after,MAX_PATH,after_color,64,after_size,64);
    CHECK(GetSysColor(COLOR_BTNFACE)==live && !memcmp(&now.lfCaptionFont,&before.lfCaptionFont,sizeof(font)),"preview leaves live colors and fonts unchanged");
    CHECK(!wcsicmp(path,after) && !wcsicmp(color,after_color) && !wcsicmp(size,after_size),"preview leaves active variant unchanged");
    applied=SUCCEEDED(apply(pending,NULL,NULL));
    CHECK(applied,"apply already parsed preview");
    if(applied)
    {
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(now),&now,0);
        CHECK(GetSysColor(COLOR_BTNFACE)==sample,"apply updates system colors");
        CHECK(now.lfCaptionFont.lfHeight==font.lfHeight,"apply updates system font metrics");
        CHECK(SUCCEEDED(apply(original,NULL,NULL)),"restore original style");
    }
    close(pending); pending=NULL;
    CHECK(GetThemeSysColor(preview,COLOR_BTNFACE)==sample,"preview data owns a file reference after file close");
done:
    if(preview) CloseThemeData(preview);
    if(pending) close(pending);
    if(original) close(original);
    fprintf(report,"Failures: %d\n",failed); fclose(report); FreeLibrary(module);
    return failed?1:0;
}
