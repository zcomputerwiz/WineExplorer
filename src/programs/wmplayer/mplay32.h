#undef __WINESRC__
#undef WINE_NO_UNICODE_MACROS
#ifndef UNICODE
#define UNICODE 1
#endif
#ifndef _UNICODE
#define _UNICODE 1
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <wingdi.h>
#include <winreg.h>
#include <commdlg.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <digitalv.h>
#include <commctrl.h>
#include <tchar.h>
#include <strsafe.h>
#include <winnls.h>

#include "resource.h"
