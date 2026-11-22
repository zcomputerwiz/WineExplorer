/*
 * mi.dll: the client side of the Management Infrastructure API over WMI (docs/POWERSHELL.md)
 *
 * The table layout is that of OMI's MI.h (Microsoft, MIT licence, see LICENSE-omi). Only what PowerShell's CIM cmdlets
 * need is implemented; every other entry of the tables is a tracing stub (mi_tables.c, made by gen_tables.py)
 *
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <initguid.h>
#include <wbemcli.h>
#include "mi_priv.h"


/* ---------------------------------------------------------------- strings */

static WCHAR *wdup(const WCHAR *s)
{
    size_t n;
    WCHAR *r;
    if (!s) return NULL;
    n = lstrlenW(s) + 1;
    r = malloc(n * sizeof(WCHAR));
    if (r) memcpy(r, s, n * sizeof(WCHAR));
    return r;
}

/* ------------------------------------------------------------- instances */

struct elem
{
    WCHAR *name;
    MI_Type type;
    MI_Value val;
    MI_Uint32 flags;
};

struct inst
{
    MI_Instance base;
    WCHAR *cls, *ns, *server, *relpath;
    MI_Uint32 count;
    struct elem *el;
};

static void value_free(MI_Type type, MI_Value *v)
{
    MI_Uint32 i;
    if (type == MI_STRING) free(v->string);
    else if (type == MI_STRINGA)
    {
        for (i = 0; i < v->stringa.size; i++) free(v->stringa.data[i]);
        free(v->stringa.data);
    }
    else if (type & 16) free(v->array.data);
    memset(v, 0, sizeof(*v));
}

static size_t array_elem_size(MI_Type t)
{
    switch (t)
    {
    case MI_BOOLEANA: return sizeof(MI_Boolean);
    case MI_UINT8A: case MI_SINT8A: return 1;
    case MI_UINT16A: case MI_SINT16A: case MI_CHAR16A: return 2;
    case MI_UINT32A: case MI_SINT32A: case MI_REAL32A: return 4;
    case MI_UINT64A: case MI_SINT64A: case MI_REAL64A: return 8;
    case MI_DATETIMEA: return sizeof(MI_Datetime);
    default: return 0;
    }
}

static MI_Result value_copy(MI_Type type, const MI_Value *src, MI_Value *dst)
{
    MI_Uint32 i;
    memset(dst, 0, sizeof(*dst));
    if (!src) return MI_RESULT_OK;
    if (type == MI_STRING) { dst->string = wdup(src->string); return MI_RESULT_OK; }
    if (type == MI_STRINGA)
    {
        dst->stringa.size = src->stringa.size;
        dst->stringa.data = calloc(src->stringa.size ? src->stringa.size : 1, sizeof(MI_Char *));
        for (i = 0; i < src->stringa.size; i++) dst->stringa.data[i] = wdup(src->stringa.data[i]);
        return MI_RESULT_OK;
    }
    if (type == MI_INSTANCE || type == MI_REFERENCE || type == MI_INSTANCEA || type == MI_REFERENCEA)
        return MI_RESULT_NOT_SUPPORTED;
    if (type & 16)
    {
        size_t es = array_elem_size(type);
        dst->array.size = src->array.size;
        dst->array.data = malloc(es * (src->array.size ? src->array.size : 1));
        memcpy(dst->array.data, src->array.data, es * src->array.size);
        return MI_RESULT_OK;
    }
    *dst = *src;
    return MI_RESULT_OK;
}


static struct inst *inst_new(const WCHAR *cls)
{
    struct inst *i = calloc(1, sizeof(*i));
    i->base.ft = &mi_tbl_MI_InstanceFT;
    i->cls = wdup(cls);
    return i;
}

static void inst_free(struct inst *i)
{
    MI_Uint32 n;
    for (n = 0; n < i->count; n++) { free(i->el[n].name); value_free(i->el[n].type, &i->el[n].val); }
    free(i->el); free(i->cls); free(i->ns); free(i->server); free(i->relpath);
    free(i);
}

static struct elem *inst_find(struct inst *i, const WCHAR *name, MI_Uint32 *index)
{
    MI_Uint32 n;
    for (n = 0; n < i->count; n++)
        if (!lstrcmpiW(i->el[n].name, name)) { if (index) *index = n; return &i->el[n]; }
    return NULL;
}

/* takes ownership of the copied value */
static struct elem *inst_add(struct inst *i, const WCHAR *name, MI_Type type, const MI_Value *v, MI_Uint32 flags)
{
    struct elem *e;
    i->el = realloc(i->el, (i->count + 1) * sizeof(*i->el));
    e = &i->el[i->count++];
    e->name = wdup(name);
    e->type = type;
    e->flags = flags & ~(MI_FLAG_BORROW | MI_FLAG_ADOPT);
    value_copy(type, v, &e->val);
    if (!v) e->flags |= MI_FLAG_NULL;
    return e;
}

MI_Result MI_CALL impl_MI_InstanceFT_Clone(const MI_Instance *self, MI_Instance **out)
{
    struct inst *s = (struct inst *)self, *c;
    MI_Uint32 n;
    c = inst_new(s->cls);
    c->ns = wdup(s->ns); c->server = wdup(s->server); c->relpath = wdup(s->relpath);
    for (n = 0; n < s->count; n++)
        inst_add(c, s->el[n].name, s->el[n].type, (s->el[n].flags & MI_FLAG_NULL) ? NULL : &s->el[n].val, s->el[n].flags);
    *out = &c->base;
    return MI_RESULT_OK;
}
MI_Result MI_CALL impl_MI_InstanceFT_Destruct(MI_Instance *self) { return MI_RESULT_OK; }
MI_Result MI_CALL impl_MI_InstanceFT_Delete(MI_Instance *self) { inst_free((struct inst *)self); return MI_RESULT_OK; }
MI_Result MI_CALL impl_MI_InstanceFT_GetClassName(const MI_Instance *self, const MI_Char **name)
{ *name = ((struct inst *)self)->cls; return MI_RESULT_OK; }
MI_Result MI_CALL impl_MI_InstanceFT_SetNameSpace(MI_Instance *self, const MI_Char *ns)
{ struct inst *i = (struct inst *)self; free(i->ns); i->ns = wdup(ns); i->base.nameSpace = i->ns; return MI_RESULT_OK; }
MI_Result MI_CALL impl_MI_InstanceFT_GetNameSpace(const MI_Instance *self, const MI_Char **ns)
{ *ns = ((struct inst *)self)->ns; return MI_RESULT_OK; }
MI_Result MI_CALL impl_MI_InstanceFT_GetServerName(const MI_Instance *self, const MI_Char **n)
{ *n = ((struct inst *)self)->server; return MI_RESULT_OK; }
MI_Result MI_CALL impl_MI_InstanceFT_SetServerName(MI_Instance *self, const MI_Char *n)
{ struct inst *i = (struct inst *)self; free(i->server); i->server = wdup(n); i->base.serverName = i->server; return MI_RESULT_OK; }
MI_Result MI_CALL impl_MI_InstanceFT_GetElementCount(const MI_Instance *self, MI_Uint32 *count)
{ *count = ((struct inst *)self)->count; return MI_RESULT_OK; }

MI_Result MI_CALL impl_MI_InstanceFT_AddElement(MI_Instance *self, const MI_Char *name, const MI_Value *v, MI_Type type, MI_Uint32 flags)
{
    struct inst *i = (struct inst *)self;
    if (inst_find(i, name, NULL)) return MI_RESULT_ALREADY_EXISTS;
    if (flags & MI_FLAG_NULL) v = NULL;
    inst_add(i, name, type, v, flags);
    return MI_RESULT_OK;
}

MI_Result MI_CALL impl_MI_InstanceFT_SetElement(MI_Instance *self, const MI_Char *name, const MI_Value *v, MI_Type type, MI_Uint32 flags)
{
    struct inst *i = (struct inst *)self;
    struct elem *e = inst_find(i, name, NULL);
    if (!e) return impl_MI_InstanceFT_AddElement(self, name, v, type, flags);
    value_free(e->type, &e->val);
    e->type = type;
    e->flags = flags & ~(MI_FLAG_BORROW | MI_FLAG_ADOPT);
    if (flags & MI_FLAG_NULL) v = NULL;
    value_copy(type, v, &e->val);
    if (!v) e->flags |= MI_FLAG_NULL;
    return MI_RESULT_OK;
}

MI_Result MI_CALL impl_MI_InstanceFT_SetElementAt(MI_Instance *self, MI_Uint32 index, const MI_Value *v, MI_Type type, MI_Uint32 flags)
{
    struct inst *i = (struct inst *)self;
    if (index >= i->count) return MI_RESULT_INVALID_PARAMETER;
    return impl_MI_InstanceFT_SetElement(self, i->el[index].name, v, type, flags);
}

static void elem_out(struct elem *e, MI_Value *value, MI_Type *type, MI_Uint32 *flags)
{
    if (value) { if (e->flags & MI_FLAG_NULL) memset(value, 0, sizeof(*value)); else *value = e->val; }
    if (type) *type = e->type;
    if (flags) *flags = e->flags;
}

MI_Result MI_CALL impl_MI_InstanceFT_GetElement(const MI_Instance *self, const MI_Char *name, MI_Value *value, MI_Type *type, MI_Uint32 *flags, MI_Uint32 *index)
{
    MI_Uint32 n = 0;
    struct elem *e = inst_find((struct inst *)self, name, &n);
    if (!e) return MI_RESULT_NO_SUCH_PROPERTY;
    elem_out(e, value, type, flags);
    if (index) *index = n;
    return MI_RESULT_OK;
}

MI_Result MI_CALL impl_MI_InstanceFT_GetElementAt(const MI_Instance *self, MI_Uint32 index, const MI_Char **name, MI_Value *value, MI_Type *type, MI_Uint32 *flags)
{
    struct inst *i = (struct inst *)self;
    if (index >= i->count) return MI_RESULT_NOT_FOUND;
    if (name) *name = i->el[index].name;
    elem_out(&i->el[index], value, type, flags);
    return MI_RESULT_OK;
}

MI_Result MI_CALL impl_MI_InstanceFT_ClearElement(MI_Instance *self, const MI_Char *name)
{
    struct elem *e = inst_find((struct inst *)self, name, NULL);
    if (!e) return MI_RESULT_NO_SUCH_PROPERTY;
    value_free(e->type, &e->val);
    e->flags |= MI_FLAG_NULL;
    return MI_RESULT_OK;
}

MI_Result MI_CALL impl_MI_InstanceFT_ClearElementAt(MI_Instance *self, MI_Uint32 index)
{
    struct inst *i = (struct inst *)self;
    if (index >= i->count) return MI_RESULT_NOT_FOUND;
    return impl_MI_InstanceFT_ClearElement(self, i->el[index].name);
}

/* ------------------------------------------------------ WMI to instances */

static BOOL parse_dmtf(const WCHAR *s, MI_Datetime *d)
{
    unsigned y, mo, da, h, mi, se, us; int utc = 0; WCHAR sign;
    memset(d, 0, sizeof(*d));
    if (!s || lstrlenW(s) != 25) return FALSE;
    if (swscanf(s, L"%4u%2u%2u%2u%2u%2u.%6u%c%3d", &y, &mo, &da, &h, &mi, &se, &us, &sign, &utc) < 8) return FALSE;
    if (s[21] == ':')   /* interval: ddddddddhhmmss.mmmmmm:000 */
    {
        unsigned dd, hh, mm, ss, uu;
        if (swscanf(s, L"%8u%2u%2u%2u.%6u", &dd, &hh, &mm, &ss, &uu) < 5) return FALSE;
        d->isTimestamp = 0;
        d->u.interval.days = dd; d->u.interval.hours = hh; d->u.interval.minutes = mm;
        d->u.interval.seconds = ss; d->u.interval.microseconds = uu;
        return TRUE;
    }
    d->isTimestamp = 1;
    d->u.timestamp.year = y; d->u.timestamp.month = mo; d->u.timestamp.day = da;
    d->u.timestamp.hour = h; d->u.timestamp.minute = mi; d->u.timestamp.second = se;
    d->u.timestamp.microseconds = us;
    d->u.timestamp.utc = (sign == '-') ? -utc : utc;
    return TRUE;
}

static void *variant_array(SAFEARRAY *sa, VARTYPE vt, size_t es, MI_Uint32 *size)
{
    LONG lo, hi, n;
    char *data, *out = NULL;
    if (FAILED(SafeArrayGetLBound(sa, 1, &lo)) || FAILED(SafeArrayGetUBound(sa, 1, &hi))) return NULL;
    *size = hi - lo + 1;
    out = calloc(*size ? *size : 1, es);
    if (SUCCEEDED(SafeArrayAccessData(sa, (void **)&data)))
    {
        for (n = 0; n < (LONG)*size; n++) memcpy(out + n * es, data + n * es, es);
        SafeArrayUnaccessData(sa);
    }
    return out;
}

static BOOL convert_variant(VARIANT *v, CIMTYPE ct, MI_Type *type, MI_Value *val)
{
    BOOL arr = (ct & CIM_FLAG_ARRAY) != 0;
    CIMTYPE base = ct & ~CIM_FLAG_ARRAY;
    memset(val, 0, sizeof(*val));
    if (V_VT(v) == VT_NULL || V_VT(v) == VT_EMPTY) return FALSE;
    if (!arr)
    {
        VARIANT s;
        switch (base)
        {
        case CIM_BOOLEAN: *type = MI_BOOLEAN; val->boolean = V_BOOL(v) != VARIANT_FALSE; return TRUE;
        case CIM_UINT8: *type = MI_UINT8; val->uint8 = V_UI1(v); return TRUE;
        case CIM_SINT8: *type = MI_SINT8; val->sint8 = V_I1(v); return TRUE;
        case CIM_SINT16: *type = MI_SINT16; val->sint16 = V_I2(v); return TRUE;
        case CIM_UINT16: *type = MI_UINT16; val->uint16 = (V_VT(v) == VT_I4) ? (MI_Uint16)V_I4(v) : V_UI2(v); return TRUE;
        case CIM_SINT32: *type = MI_SINT32; val->sint32 = V_I4(v); return TRUE;
        case CIM_UINT32: *type = MI_UINT32; val->uint32 = (V_VT(v) == VT_I4) ? (MI_Uint32)V_I4(v) : V_UI4(v); return TRUE;
        case CIM_REAL32: *type = MI_REAL32; val->real32 = V_R4(v); return TRUE;
        case CIM_REAL64: *type = MI_REAL64; val->real64 = V_R8(v); return TRUE;
        case CIM_CHAR16: *type = MI_CHAR16; val->char16 = V_I2(v); return TRUE;
        case CIM_SINT64: case CIM_UINT64:
            VariantInit(&s);
            if (FAILED(VariantChangeType(&s, v, 0, VT_BSTR))) return FALSE;
            *type = (base == CIM_SINT64) ? MI_SINT64 : MI_UINT64;
            if (base == CIM_SINT64) val->sint64 = _wtoi64(V_BSTR(&s)); else val->uint64 = _wcstoui64(V_BSTR(&s), NULL, 10);
            VariantClear(&s);
            return TRUE;
        case CIM_DATETIME:
            if (V_VT(v) != VT_BSTR) return FALSE;
            *type = MI_DATETIME;
            return parse_dmtf(V_BSTR(v), &val->datetime);
        case CIM_STRING: case CIM_REFERENCE:
            if (V_VT(v) != VT_BSTR) return FALSE;
            *type = (base == CIM_REFERENCE) ? MI_STRING : MI_STRING;
            val->string = wdup(V_BSTR(v));
            return TRUE;
        default: return FALSE;
        }
    }
    if (!(V_VT(v) & VT_ARRAY) || !V_ARRAY(v)) return FALSE;
    switch (base)
    {
    case CIM_STRING: case CIM_REFERENCE: case CIM_DATETIME:
    {
        SAFEARRAY *sa = V_ARRAY(v); LONG lo, hi, n; BSTR *data;
        if (FAILED(SafeArrayGetLBound(sa, 1, &lo)) || FAILED(SafeArrayGetUBound(sa, 1, &hi))) return FALSE;
        val->stringa.size = hi - lo + 1;
        val->stringa.data = calloc(val->stringa.size ? val->stringa.size : 1, sizeof(MI_Char *));
        if (SUCCEEDED(SafeArrayAccessData(sa, (void **)&data)))
        {
            for (n = 0; n < (LONG)val->stringa.size; n++) val->stringa.data[n] = wdup(data[n]);
            SafeArrayUnaccessData(sa);
        }
        *type = MI_STRINGA;
        return TRUE;
    }
    case CIM_UINT16: *type = MI_UINT16A; val->array.data = variant_array(V_ARRAY(v), VT_I4, 2, &val->array.size); return val->array.data != NULL;
    case CIM_SINT16: *type = MI_SINT16A; val->array.data = variant_array(V_ARRAY(v), VT_I2, 2, &val->array.size); return val->array.data != NULL;
    case CIM_UINT32: *type = MI_UINT32A; val->array.data = variant_array(V_ARRAY(v), VT_I4, 4, &val->array.size); return val->array.data != NULL;
    case CIM_SINT32: *type = MI_SINT32A; val->array.data = variant_array(V_ARRAY(v), VT_I4, 4, &val->array.size); return val->array.data != NULL;
    case CIM_UINT8: *type = MI_UINT8A; val->array.data = variant_array(V_ARRAY(v), VT_UI1, 1, &val->array.size); return val->array.data != NULL;
    default: return FALSE;
    }
}

static struct inst *inst_from_wbem(IWbemClassObject *obj, const WCHAR *ns, const WCHAR *server)
{
    struct inst *i;
    VARIANT v;
    CIMTYPE ct;
    BSTR name;
    MI_Type type;
    MI_Value val;
    LONG flavor;
    WCHAR cls[256] = L"";

    VariantInit(&v);
    if (SUCCEEDED(IWbemClassObject_Get(obj, L"__CLASS", 0, &v, NULL, NULL)) && V_VT(&v) == VT_BSTR)
        lstrcpynW(cls, V_BSTR(&v), 256);
    VariantClear(&v);
    i = inst_new(cls);
    i->ns = wdup(ns); i->base.nameSpace = i->ns;
    i->server = wdup(server); i->base.serverName = i->server;
    i->base.classDecl = NULL;
    if (SUCCEEDED(IWbemClassObject_Get(obj, L"__RELPATH", 0, &v, NULL, NULL)) && V_VT(&v) == VT_BSTR)
        i->relpath = wdup(V_BSTR(&v));
    VariantClear(&v);
    IWbemClassObject_BeginEnumeration(obj, WBEM_FLAG_NONSYSTEM_ONLY);
    while (IWbemClassObject_Next(obj, 0, &name, &v, &ct, &flavor) == S_OK)
    {
        BOOL ok = convert_variant(&v, ct, &type, &val);
        if (ok) { inst_add(i, name, type, &val, 0); value_free(type, &val); }
        else
        {
            MI_Type t = MI_STRING;
            CIMTYPE b = ct & ~CIM_FLAG_ARRAY;
            if (ct & CIM_FLAG_ARRAY) t = (b == CIM_STRING || b == CIM_DATETIME) ? MI_STRINGA : MI_UINT32A;
            else switch (b) { case CIM_BOOLEAN: t = MI_BOOLEAN; break; case CIM_UINT8: t = MI_UINT8; break;
                case CIM_SINT8: t = MI_SINT8; break; case CIM_UINT16: t = MI_UINT16; break; case CIM_SINT16: t = MI_SINT16; break;
                case CIM_UINT32: t = MI_UINT32; break; case CIM_SINT32: t = MI_SINT32; break; case CIM_UINT64: t = MI_UINT64; break;
                case CIM_SINT64: t = MI_SINT64; break; case CIM_REAL32: t = MI_REAL32; break; case CIM_REAL64: t = MI_REAL64; break;
                case CIM_DATETIME: t = MI_DATETIME; break; case CIM_CHAR16: t = MI_CHAR16; break; default: break; }
            inst_add(i, name, t, NULL, MI_FLAG_NULL);
        }
        SysFreeString(name);
        VariantClear(&v);
    }
    IWbemClassObject_EndEnumeration(obj);
    return i;
}

/* -------------------------------------------------------------- sessions */

struct session { WCHAR *dest; };
struct application { int dummy; };

MI_Result MI_CALL impl_MI_ApplicationFT_Close(MI_Application *a)
{ free((void *)a->reserved2); a->reserved2 = 0; return MI_RESULT_OK; }

MI_Result MI_MAIN_CALL MI_Application_InitializeV1(MI_Uint32 flags, const MI_Char *id, MI_Instance **extErr, MI_Application *app)
{
    mi_trace("MI_Application_InitializeV1 id=%ls", id ? id : L"(null)");
    if (extErr) *extErr = NULL;
    memset(app, 0, sizeof(*app));
    app->reserved2 = (ptrdiff_t)calloc(1, sizeof(struct application));
    app->ft = &mi_tbl_MI_ApplicationFT;
    return MI_RESULT_OK;
}

MI_Result MI_CALL impl_MI_ApplicationFT_NewSession(MI_Application *app, const MI_Char *protocol, const MI_Char *dest,
        MI_DestinationOptions *opts, MI_SessionCallbacks *cb, MI_Instance **extErr, MI_Session *session)
{
    struct session *s = calloc(1, sizeof(*s));
    mi_trace("NewSession protocol=%ls dest=%ls", protocol ? protocol : L"-", dest ? dest : L"-");
    if (extErr) *extErr = NULL;
    s->dest = wdup(dest);
    memset(session, 0, sizeof(*session));
    session->reserved2 = (ptrdiff_t)s;
    session->ft = &mi_tbl_MI_SessionFT;
    return MI_RESULT_OK;
}

MI_Result MI_CALL impl_MI_ApplicationFT_NewInstance(MI_Application *app, const MI_Char *cls, const MI_ClassDecl *decl, MI_Instance **out)
{
    *out = &inst_new(cls)->base;
    return MI_RESULT_OK;
}

MI_Result MI_CALL impl_MI_ApplicationFT_NewDestinationOptions(MI_Application *app, MI_DestinationOptions *o)
{ memset(o, 0, sizeof(*o)); o->ft = &mi_tbl_MI_DestinationOptionsFT; return MI_RESULT_OK; }
MI_Result MI_CALL impl_MI_ApplicationFT_NewOperationOptions(MI_Application *app, MI_Boolean must, MI_OperationOptions *o)
{ memset(o, 0, sizeof(*o)); o->ft = &mi_tbl_MI_OperationOptionsFT; return MI_RESULT_OK; }

MI_Result MI_CALL impl_MI_SessionFT_Close(MI_Session *s, void *ctx, void (MI_CALL *completion)(void *))
{
    struct session *p = (struct session *)s->reserved2;
    if (p) { free(p->dest); free(p); }
    s->reserved2 = 0;
    if (completion) completion(ctx);
    return MI_RESULT_OK;
}

MI_Result MI_CALL impl_MI_SessionFT_GetApplication(const MI_Session *s, MI_Application *a)
{ memset(a, 0, sizeof(*a)); a->ft = &mi_tbl_MI_ApplicationFT; return MI_RESULT_OK; }

/* ------------------------------------------------------------ operations */

enum kind { K_ENUM, K_QUERY, K_INVOKE, K_GET, K_NOOP };

struct operation
{
    MI_Operation *handle;           /* the caller's structure */
    MI_Session session;
    enum kind kind;
    WCHAR *ns, *cls, *query, *method;
    struct inst *in_inst, *in_params;
    MI_OperationCallbacks cb;
    BOOL have_cb;
    CRITICAL_SECTION cs;
    HANDLE avail, ack, thread;
    struct inst **queue;
    int qn, qhead;
    BOOL done;
    MI_Result result;
    WCHAR *errmsg;
    LONG refs;
    BOOL cancel;
};

static void op_push(struct operation *o, struct inst *i)
{
    EnterCriticalSection(&o->cs);
    o->queue = realloc(o->queue, (o->qn + 1) * sizeof(*o->queue));
    o->queue[o->qn++] = i;
    LeaveCriticalSection(&o->cs);
    SetEvent(o->avail);
}

static MI_Result MI_CALL ack_fn(MI_Operation *op)
{
    struct operation *o = (struct operation *)op->reserved2;
    if (o) SetEvent(o->ack);
    return MI_RESULT_OK;
}

static void op_deliver(struct operation *o, struct inst *i, BOOL more, MI_Result rc, const WCHAR *err)
{
    mi_trace("deliver inst=%p more=%d rc=%d have_cb=%d", (void *)i, more, rc, o->have_cb);
    if (o->have_cb && o->cb.instanceResult)
    {
        ResetEvent(o->ack);
        o->cb.instanceResult(o->handle, o->cb.callbackContext, i ? &i->base : NULL, more, rc, err, NULL, ack_fn);
        WaitForSingleObject(o->ack, 60000);
    }
}

const WCHAR *mi_instance_class_name(const MI_Instance *self) { return ((const struct inst *)self)->cls; }

static WCHAR *slash_to_backslash(const WCHAR *ns)
{
    WCHAR *r = wdup(ns && *ns ? ns : L"root\\cimv2"), *p;
    for (p = r; *p; p++) if (*p == '/') *p = '\\';
    return r;
}

IWbemServices *mi_connect_ns(const WCHAR *ns, HRESULT *hr)
{
    IWbemLocator *loc = NULL;
    IWbemServices *svc = NULL;
    BSTR b;
    WCHAR *n = slash_to_backslash(ns);

    *hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (void **)&loc);
    if (SUCCEEDED(*hr))
    {
        b = SysAllocString(n);
        *hr = IWbemLocator_ConnectServer(loc, b, NULL, NULL, NULL, 0, NULL, NULL, &svc);
        SysFreeString(b);
        IWbemLocator_Release(loc);
    }
    free(n);
    return svc;
}

static MI_Result hr_to_result(HRESULT hr)
{
    switch (hr)
    {
    case WBEM_E_INVALID_NAMESPACE: return MI_RESULT_INVALID_NAMESPACE;
    case WBEM_E_INVALID_CLASS: return MI_RESULT_INVALID_CLASS;
    case WBEM_E_NOT_FOUND: return MI_RESULT_NOT_FOUND;
    case WBEM_E_ACCESS_DENIED: return MI_RESULT_ACCESS_DENIED;
    case WBEM_E_INVALID_QUERY: case WBEM_E_INVALID_QUERY_TYPE: return MI_RESULT_INVALID_QUERY;
    case WBEM_E_NOT_SUPPORTED: return MI_RESULT_NOT_SUPPORTED;
    default: return MI_RESULT_FAILED;
    }
}

static void run_enum(struct operation *o, IWbemServices *svc, IEnumWbemClassObject *en)
{
    IWbemClassObject *obj;
    ULONG got;
    while (!o->cancel && IEnumWbemClassObject_Next(en, WBEM_INFINITE, 1, &obj, &got) == S_OK && got)
    {
        struct inst *i = inst_from_wbem(obj, o->ns, NULL);
        IWbemClassObject_Release(obj);
        if (o->have_cb) { op_deliver(o, i, MI_TRUE, MI_RESULT_OK, NULL); inst_free(i); }
        else op_push(o, i);
    }
}

static void variant_from_mi(MI_Type type, const MI_Value *v, VARIANT *out)
{
    VariantInit(out);
    switch (type)
    {
    case MI_BOOLEAN: V_VT(out) = VT_BOOL; V_BOOL(out) = v->boolean ? VARIANT_TRUE : VARIANT_FALSE; break;
    case MI_UINT8: V_VT(out) = VT_UI1; V_UI1(out) = v->uint8; break;
    case MI_SINT8: V_VT(out) = VT_I1; V_I1(out) = v->sint8; break;
    case MI_UINT16: V_VT(out) = VT_I4; V_I4(out) = v->uint16; break;
    case MI_SINT16: V_VT(out) = VT_I2; V_I2(out) = v->sint16; break;
    case MI_UINT32: V_VT(out) = VT_UI4; V_UI4(out) = v->uint32; break;    /* wbemprox's Put refuses VT_I4 for a uint32 */
    case MI_SINT32: V_VT(out) = VT_I4; V_I4(out) = v->sint32; break;
    case MI_STRING: V_VT(out) = VT_BSTR; V_BSTR(out) = SysAllocString(v->string); break;
    default: break;
    }
}

static HRESULT run_invoke(struct operation *o, IWbemServices *svc, struct inst **result)
{
    IWbemClassObject *cls = NULL, *in_sig = NULL, *in = NULL, *out = NULL;
    BSTR path, meth;
    HRESULT hr;
    MI_Uint32 n;
    WCHAR clsname[256];

    lstrcpynW(clsname, o->cls ? o->cls : (o->in_inst ? o->in_inst->cls : L""), 256);
    path = SysAllocString(o->in_inst && o->in_inst->relpath ? o->in_inst->relpath : clsname);
    meth = SysAllocString(o->method);
    { BSTR c = SysAllocString(clsname); hr = IWbemServices_GetObject(svc, c, 0, NULL, &cls, NULL); SysFreeString(c); }
    if (FAILED(hr)) goto done;
    hr = IWbemClassObject_GetMethod(cls, o->method, 0, &in_sig, NULL);
    if (FAILED(hr)) goto done;
    if (in_sig)
    {
        hr = IWbemClassObject_SpawnInstance(in_sig, 0, &in);
        if (FAILED(hr)) goto done;
        if (o->in_params)
            for (n = 0; n < o->in_params->count; n++)
            {
                VARIANT v;
                if (o->in_params->el[n].flags & MI_FLAG_NULL) continue;
                variant_from_mi(o->in_params->el[n].type, &o->in_params->el[n].val, &v);
                IWbemClassObject_Put(in, o->in_params->el[n].name, 0, &v, 0);
                VariantClear(&v);
            }
    }
    hr = IWbemServices_ExecMethod(svc, path, meth, 0, NULL, in, &out, NULL);
    if (SUCCEEDED(hr) && out) *result = inst_from_wbem(out, o->ns, NULL);
done:
    SysFreeString(path); SysFreeString(meth);
    if (out) IWbemClassObject_Release(out);
    if (in) IWbemClassObject_Release(in);
    if (in_sig) IWbemClassObject_Release(in_sig);
    if (cls) IWbemClassObject_Release(cls);
    return hr;
}

static void op_release(struct operation *o)
{
    int n;
    if (InterlockedDecrement(&o->refs)) return;
    for (n = o->qhead; n < o->qn; n++) inst_free(o->queue[n]);
    free(o->queue); free(o->ns); free(o->cls); free(o->query); free(o->method); free(o->errmsg);
    if (o->in_inst) inst_free(o->in_inst);
    if (o->in_params) inst_free(o->in_params);
    CloseHandle(o->avail); CloseHandle(o->ack);
    DeleteCriticalSection(&o->cs);
    free(o);
}

static DWORD WINAPI op_thread(void *arg)
{
    struct operation *o = arg;
    IWbemServices *svc;
    IEnumWbemClassObject *en = NULL;
    HRESULT hr, init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    BSTR a, b;

    svc = (o->kind == K_NOOP) ? NULL : mi_connect_ns(o->ns, &hr);
    if (o->kind == K_NOOP) hr = S_OK;
    if (svc)
    {
        if (o->kind == K_ENUM)
        {
            a = SysAllocString(o->cls);
            hr = IWbemServices_CreateInstanceEnum(svc, a, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &en);
            SysFreeString(a);
            if (SUCCEEDED(hr)) run_enum(o, svc, en);
        }
        else if (o->kind == K_QUERY)
        {
            a = SysAllocString(L"WQL"); b = SysAllocString(o->query);
            hr = IWbemServices_ExecQuery(svc, a, b, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &en);
            SysFreeString(a); SysFreeString(b);
            if (SUCCEEDED(hr)) run_enum(o, svc, en);
        }
        else if (o->kind == K_INVOKE)
        {
            struct inst *res = NULL;
            hr = run_invoke(o, svc, &res);
            if (res) { if (o->have_cb) { op_deliver(o, res, MI_TRUE, MI_RESULT_OK, NULL); inst_free(res); } else op_push(o, res); }
        }
        else hr = WBEM_E_NOT_SUPPORTED;
        if (en) IEnumWbemClassObject_Release(en);
        IWbemServices_Release(svc);
    }
    mi_trace("op kind=%d ns=%ls cls=%ls q=%ls -> hr=%08lx", o->kind, o->ns, o->cls ? o->cls : L"", o->query ? o->query : L"", (unsigned long)hr);
    o->result = FAILED(hr) ? hr_to_result(hr) : MI_RESULT_OK;
    if (FAILED(hr))
    {
        WCHAR msg[64];
        swprintf(msg, 64, L"WMI error 0x%08lx", (unsigned long)hr);
        o->errmsg = wdup(msg);
    }
    o->done = TRUE;
    SetEvent(o->avail);
    if (o->have_cb) op_deliver(o, NULL, MI_FALSE, o->result, o->errmsg);
    if (SUCCEEDED(init)) CoUninitialize();
    op_release(o);
    return 0;
}

static void start_op(MI_Session *s, enum kind kind, const MI_Char *ns, const MI_Char *cls, const MI_Char *query,
        const MI_Char *method, const MI_Instance *in_inst, const MI_Instance *in_params,
        MI_OperationCallbacks *cb, MI_Operation *op)
{
    struct operation *o = calloc(1, sizeof(*o));
    mi_trace("start_op kind=%d ns=%ls cls=%ls q=%ls m=%ls cb=%p", kind, ns ? ns : L"-", cls ? cls : L"-",
             query ? query : L"-", method ? method : L"-", (void *)cb);
    o->handle = op;
    o->kind = kind;
    o->ns = wdup(ns);
    o->cls = wdup(cls);
    o->query = wdup(query);
    o->method = wdup(method);
    if (in_inst) impl_MI_InstanceFT_Clone(in_inst, (MI_Instance **)&o->in_inst);
    if (in_params) impl_MI_InstanceFT_Clone(in_params, (MI_Instance **)&o->in_params);
    if (cb) { o->cb = *cb; o->have_cb = cb->instanceResult != NULL; }
    InitializeCriticalSection(&o->cs);
    o->avail = CreateEventW(NULL, FALSE, FALSE, NULL);
    o->ack = CreateEventW(NULL, FALSE, FALSE, NULL);
    o->session = *s;
    o->refs = 2;   /* the handle and the thread */
    memset(op, 0, sizeof(*op));
    op->reserved2 = (ptrdiff_t)o;
    op->ft = &mi_tbl_MI_OperationFT;
    o->thread = CreateThread(NULL, 0, op_thread, o, 0, NULL);
}

void MI_CALL impl_MI_SessionFT_EnumerateInstances(MI_Session *s, MI_Uint32 flags, MI_OperationOptions *opts, const MI_Char *ns,
        const MI_Char *cls, MI_Boolean keysOnly, MI_OperationCallbacks *cb, MI_Operation *op)
{ start_op(s, K_ENUM, ns, cls, NULL, NULL, NULL, NULL, cb, op); }

void MI_CALL impl_MI_SessionFT_QueryInstances(MI_Session *s, MI_Uint32 flags, MI_OperationOptions *opts, const MI_Char *ns,
        const MI_Char *dialect, const MI_Char *query, MI_OperationCallbacks *cb, MI_Operation *op)
{ start_op(s, K_QUERY, ns, NULL, query, NULL, NULL, NULL, cb, op); }

void MI_CALL impl_MI_SessionFT_Invoke(MI_Session *s, MI_Uint32 flags, MI_OperationOptions *opts, const MI_Char *ns,
        const MI_Char *cls, const MI_Char *method, const MI_Instance *inst, const MI_Instance *params,
        MI_OperationCallbacks *cb, MI_Operation *op)
{ start_op(s, K_INVOKE, ns, cls, NULL, method, inst, params, cb, op); }

MI_Result MI_CALL impl_MI_OperationFT_Close(MI_Operation *op)
{
    struct operation *o = (struct operation *)op->reserved2;
    if (o) { o->cancel = TRUE; op->reserved2 = 0; op_release(o); }
    return MI_RESULT_OK;
}
MI_Result MI_CALL impl_MI_OperationFT_Cancel(MI_Operation *op, MI_CancellationReason r)
{ struct operation *o = (struct operation *)op->reserved2; if (o) o->cancel = TRUE; return MI_RESULT_OK; }
MI_Result MI_CALL impl_MI_OperationFT_GetSession(const MI_Operation *op, MI_Session *s)
{ struct operation *o = (struct operation *)op->reserved2; if (!o) return MI_RESULT_INVALID_PARAMETER; *s = o->session; return MI_RESULT_OK; }

MI_Result MI_CALL impl_MI_OperationFT_GetInstance(MI_Operation *op, const MI_Instance **instance, MI_Boolean *more,
        MI_Result *result, const MI_Char **errmsg, const MI_Instance **details)
{
    struct operation *o = (struct operation *)op->reserved2;
    mi_trace("Operation_GetInstance o=%p", (void *)o);
    if (!o) return MI_RESULT_INVALID_PARAMETER;
    for (;;)
    {
        EnterCriticalSection(&o->cs);
        if (o->qhead < o->qn)
        {
            *instance = &o->queue[o->qhead++]->base;
            if (more) *more = MI_TRUE;
            if (result) *result = MI_RESULT_OK;
            if (errmsg) *errmsg = NULL;
            if (details) *details = NULL;
            LeaveCriticalSection(&o->cs);
            return MI_RESULT_OK;
        }
        LeaveCriticalSection(&o->cs);
        if (o->done)
        {
            *instance = NULL;
            if (more) *more = MI_FALSE;
            if (result) *result = o->result;
            if (errmsg) *errmsg = o->errmsg;
            if (details) *details = NULL;
            return MI_RESULT_OK;
        }
        WaitForSingleObject(o->avail, 1000);
    }
}

void MI_CALL impl_MI_SessionFT_TestConnection(MI_Session *s, MI_Uint32 flags, MI_OperationCallbacks *cb, MI_Operation *op)
{ start_op(s, K_NOOP, NULL, NULL, NULL, NULL, NULL, NULL, cb, op); }
