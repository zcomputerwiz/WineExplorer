/*
 * mi.dll: MI_Class, MI_ParameterSet and MI_QualifierSet over a WMI class definition (docs/POWERSHELL.md)
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

/*
 * What PowerShell reads about a class before it invokes a method: the methods, and for each the parameters with their
 * types and the "in" and "out" qualifiers. The class is read from the WMI class object when it is asked for.
 */
#define COBJMACROS
#include <stdlib.h>
#include <wbemcli.h>
#include "mi_priv.h"

struct param { WCHAR *name; MI_Type type; BOOL in, out; };

struct method
{
    WCHAR *name;
    struct param *params;
    MI_Uint32 count;
};

struct prop { WCHAR *name; MI_Type type; };

struct cls
{
    MI_Class base;
    WCHAR *name, *ns;
    struct prop *props;
    MI_Uint32 nprops;
    struct method *methods;
    MI_Uint32 nmethods;
};

struct pset { struct method *m; };
struct qset { MI_Uint32 count; const WCHAR *names[2]; };

static const struct qset qs_empty = { 0, { NULL, NULL } };
static const struct qset qs_in = { 1, { L"in", NULL } };
static const struct qset qs_out = { 1, { L"out", NULL } };
static const struct qset qs_inout = { 2, { L"in", L"out" } };

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

static MI_Type cim_to_mi(CIMTYPE ct)
{
    BOOL arr = (ct & CIM_FLAG_ARRAY) != 0;
    MI_Type t;
    switch (ct & ~CIM_FLAG_ARRAY)
    {
    case CIM_BOOLEAN: t = MI_BOOLEAN; break;
    case CIM_UINT8: t = MI_UINT8; break;
    case CIM_SINT8: t = MI_SINT8; break;
    case CIM_UINT16: t = MI_UINT16; break;
    case CIM_SINT16: t = MI_SINT16; break;
    case CIM_UINT32: t = MI_UINT32; break;
    case CIM_SINT32: t = MI_SINT32; break;
    case CIM_UINT64: t = MI_UINT64; break;
    case CIM_SINT64: t = MI_SINT64; break;
    case CIM_REAL32: t = MI_REAL32; break;
    case CIM_REAL64: t = MI_REAL64; break;
    case CIM_CHAR16: t = MI_CHAR16; break;
    case CIM_DATETIME: t = MI_DATETIME; break;
    case CIM_REFERENCE: t = MI_REFERENCE; break;
    case CIM_OBJECT: t = MI_INSTANCE; break;
    default: t = MI_STRING; break;
    }
    return arr ? (MI_Type)(t + 16) : t;
}

static void read_sig(IWbemClassObject *sig, BOOL in, struct method *m)
{
    BSTR name;
    CIMTYPE ct;
    VARIANT v;
    MI_Uint32 n;

    if (!sig) return;
    IWbemClassObject_BeginEnumeration(sig, WBEM_FLAG_NONSYSTEM_ONLY);
    while (IWbemClassObject_Next(sig, 0, &name, &v, &ct, NULL) == S_OK)
    {
        VariantClear(&v);
        if (!in && !lstrcmpiW(name, L"ReturnValue")) { SysFreeString(name); continue; }
        for (n = 0; n < m->count; n++) if (!lstrcmpiW(m->params[n].name, name)) break;
        if (n == m->count)
        {
            m->params = realloc(m->params, (m->count + 1) * sizeof(*m->params));
            m->params[m->count].name = wdup(name);
            m->params[m->count].type = cim_to_mi(ct);
            m->params[m->count].in = m->params[m->count].out = FALSE;
            m->count++;
        }
        if (in) m->params[n].in = TRUE; else m->params[n].out = TRUE;
        SysFreeString(name);
    }
    IWbemClassObject_EndEnumeration(sig);
}

static struct cls *cls_load(const WCHAR *name, const WCHAR *ns)
{
    IWbemServices *svc;
    IWbemClassObject *obj = NULL, *in, *out;
    HRESULT hr;
    BSTR b, mname;
    CIMTYPE ct;
    VARIANT v;
    struct cls *c;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    svc = mi_connect_ns(ns, &hr);
    if (!svc) return NULL;
    b = SysAllocString(name);
    hr = IWbemServices_GetObject(svc, b, 0, NULL, &obj, NULL);
    SysFreeString(b);
    IWbemServices_Release(svc);
    if (FAILED(hr)) return NULL;

    c = calloc(1, sizeof(*c));
    c->base.ft = &mi_tbl_MI_ClassFT;
    c->name = wdup(name);
    c->ns = wdup(ns && *ns ? ns : L"root/cimv2");
    c->base.namespaceName = c->ns;

    IWbemClassObject_BeginEnumeration(obj, WBEM_FLAG_NONSYSTEM_ONLY);
    while (IWbemClassObject_Next(obj, 0, &b, &v, &ct, NULL) == S_OK)
    {
        c->props = realloc(c->props, (c->nprops + 1) * sizeof(*c->props));
        c->props[c->nprops].name = wdup(b);
        c->props[c->nprops].type = cim_to_mi(ct);
        c->nprops++;
        SysFreeString(b);
        VariantClear(&v);
    }
    IWbemClassObject_EndEnumeration(obj);

    IWbemClassObject_BeginMethodEnumeration(obj, 0);
    while (IWbemClassObject_NextMethod(obj, 0, &mname, &in, &out) == S_OK)
    {
        struct method *m;
        c->methods = realloc(c->methods, (c->nmethods + 1) * sizeof(*c->methods));
        m = &c->methods[c->nmethods++];
        memset(m, 0, sizeof(*m));
        m->name = wdup(mname);
        read_sig(in, TRUE, m);
        read_sig(out, FALSE, m);
        if (in) IWbemClassObject_Release(in);
        if (out) IWbemClassObject_Release(out);
        SysFreeString(mname);
    }
    IWbemClassObject_EndMethodEnumeration(obj);
    IWbemClassObject_Release(obj);
    return c;
}

MI_Class *mi_class_load(const WCHAR *name, const WCHAR *ns)
{
    struct cls *c = cls_load(name, ns);
    return c ? &c->base : NULL;
}

static void cls_free(struct cls *c)
{
    MI_Uint32 i, j;
    for (i = 0; i < c->nprops; i++) free(c->props[i].name);
    for (i = 0; i < c->nmethods; i++)
    {
        for (j = 0; j < c->methods[i].count; j++) free(c->methods[i].params[j].name);
        free(c->methods[i].params);
        free(c->methods[i].name);
    }
    free(c->props); free(c->methods); free(c->name); free(c->ns);
    free(c);
}

/* ------------------------------------------------------------- qualifiers */

static void set_qs(MI_QualifierSet *q, const struct qset *s)
{
    if (!q) return;
    q->reserved1 = 0;
    q->reserved2 = (ptrdiff_t)s;
    q->ft = &mi_tbl_MI_QualifierSetFT;
}

MI_Result MI_CALL impl_MI_QualifierSetFT_GetQualifierCount(const MI_QualifierSet *self, MI_Uint32 *count)
{ *count = ((const struct qset *)self->reserved2)->count; return MI_RESULT_OK; }

MI_Result MI_CALL impl_MI_QualifierSetFT_GetQualifierAt(const MI_QualifierSet *self, MI_Uint32 index, const MI_Char **name,
        MI_Type *type, MI_Uint32 *flags, MI_Value *value)
{
    const struct qset *s = (const struct qset *)self->reserved2;
    if (index >= s->count) return MI_RESULT_NOT_FOUND;
    *name = s->names[index];
    *type = MI_BOOLEAN;
    *flags = 0;
    memset(value, 0, sizeof(*value));
    value->boolean = MI_TRUE;
    return MI_RESULT_OK;
}

MI_Result MI_CALL impl_MI_QualifierSetFT_GetQualifier(const MI_QualifierSet *self, const MI_Char *name, MI_Type *type,
        MI_Uint32 *flags, MI_Value *value, MI_Uint32 *index)
{
    const struct qset *s = (const struct qset *)self->reserved2;
    MI_Uint32 i;
    for (i = 0; i < s->count; i++)
        if (!lstrcmpiW(s->names[i], name))
        {
            const MI_Char *n;
            if (index) *index = i;
            return impl_MI_QualifierSetFT_GetQualifierAt(self, i, &n, type, flags, value);
        }
    return MI_RESULT_NO_SUCH_PROPERTY;
}

/* ----------------------------------------------------------- parameter set */

static const struct qset *param_qs(const struct param *p)
{ return (p->in && p->out) ? &qs_inout : p->in ? &qs_in : &qs_out; }

MI_Result MI_CALL impl_MI_ParameterSetFT_GetMethodReturnType(const MI_ParameterSet *self, MI_Type *type, MI_QualifierSet *qs)
{ *type = MI_UINT32; set_qs(qs, &qs_empty); return MI_RESULT_OK; }

MI_Result MI_CALL impl_MI_ParameterSetFT_GetParameterCount(const MI_ParameterSet *self, MI_Uint32 *count)
{ *count = ((struct pset *)self->reserved2)->m->count; return MI_RESULT_OK; }

MI_Result MI_CALL impl_MI_ParameterSetFT_GetParameterAt(const MI_ParameterSet *self, MI_Uint32 index, const MI_Char **name,
        MI_Type *type, MI_Char **refClass, MI_QualifierSet *qs)
{
    struct method *m = ((struct pset *)self->reserved2)->m;
    if (index >= m->count) return MI_RESULT_NOT_FOUND;
    *name = m->params[index].name;
    *type = m->params[index].type;
    if (refClass) *refClass = NULL;
    set_qs(qs, param_qs(&m->params[index]));
    return MI_RESULT_OK;
}

MI_Result MI_CALL impl_MI_ParameterSetFT_GetParameter(const MI_ParameterSet *self, const MI_Char *name, MI_Type *type,
        MI_Char **refClass, MI_QualifierSet *qs, MI_Uint32 *index)
{
    struct method *m = ((struct pset *)self->reserved2)->m;
    MI_Uint32 i;
    for (i = 0; i < m->count; i++)
        if (!lstrcmpiW(m->params[i].name, name))
        {
            const MI_Char *n;
            if (index) *index = i;
            return impl_MI_ParameterSetFT_GetParameterAt(self, i, &n, type, refClass, qs);
        }
    return MI_RESULT_NO_SUCH_PROPERTY;
}

/* the parameter sets live as long as the class: one per method, made when asked and kept in a small list */
static void set_pset(MI_ParameterSet *p, struct method *m)
{
    struct pset *s = malloc(sizeof(*s));    /* released with the process: a handful of bytes per call */
    s->m = m;
    if (!p) { free(s); return; }
    p->reserved1 = 0;
    p->reserved2 = (ptrdiff_t)s;
    p->ft = &mi_tbl_MI_ParameterSetFT;
}

/* ------------------------------------------------------------------ class */

MI_Result MI_CALL impl_MI_ClassFT_GetClassName(const MI_Class *self, const MI_Char **name)
{ *name = ((const struct cls *)self)->name; return MI_RESULT_OK; }
MI_Result MI_CALL impl_MI_ClassFT_GetNameSpace(const MI_Class *self, const MI_Char **ns)
{ *ns = ((const struct cls *)self)->ns; return MI_RESULT_OK; }
MI_Result MI_CALL impl_MI_ClassFT_GetServerName(const MI_Class *self, const MI_Char **name)
{ *name = NULL; return MI_RESULT_OK; }
MI_Result MI_CALL impl_MI_ClassFT_GetElementCount(const MI_Class *self, MI_Uint32 *count)
{ *count = ((const struct cls *)self)->nprops; return MI_RESULT_OK; }

MI_Result MI_CALL impl_MI_ClassFT_GetElementAt(const MI_Class *self, MI_Uint32 index, const MI_Char **name, MI_Value *value,
        MI_Boolean *exists, MI_Type *type, MI_Char **refClass, MI_QualifierSet *qs, MI_Uint32 *flags)
{
    const struct cls *c = (const struct cls *)self;
    if (index >= c->nprops) return MI_RESULT_NOT_FOUND;
    if (name) *name = c->props[index].name;
    if (value) memset(value, 0, sizeof(*value));
    if (exists) *exists = MI_FALSE;
    if (type) *type = c->props[index].type;
    if (refClass) *refClass = NULL;
    set_qs(qs, &qs_empty);
    if (flags) *flags = MI_FLAG_PROPERTY;
    return MI_RESULT_OK;
}

MI_Result MI_CALL impl_MI_ClassFT_GetElement(const MI_Class *self, const MI_Char *name, MI_Value *value, MI_Boolean *exists,
        MI_Type *type, MI_Char **refClass, MI_QualifierSet *qs, MI_Uint32 *flags, MI_Uint32 *index)
{
    const struct cls *c = (const struct cls *)self;
    MI_Uint32 i;
    for (i = 0; i < c->nprops; i++)
        if (!lstrcmpiW(c->props[i].name, name))
        {
            if (index) *index = i;
            return impl_MI_ClassFT_GetElementAt(self, i, NULL, value, exists, type, refClass, qs, flags);
        }
    return MI_RESULT_NO_SUCH_PROPERTY;
}

MI_Result MI_CALL impl_MI_ClassFT_GetClassQualifierSet(const MI_Class *self, MI_QualifierSet *qs)
{ set_qs(qs, &qs_empty); return MI_RESULT_OK; }
MI_Result MI_CALL impl_MI_ClassFT_GetMethodCount(const MI_Class *self, MI_Uint32 *count)
{ *count = ((const struct cls *)self)->nmethods; return MI_RESULT_OK; }

MI_Result MI_CALL impl_MI_ClassFT_GetMethodAt(const MI_Class *self, MI_Uint32 index, const MI_Char **name, MI_QualifierSet *qs,
        MI_ParameterSet *ps)
{
    struct cls *c = (struct cls *)self;
    if (index >= c->nmethods) return MI_RESULT_NOT_FOUND;
    *name = c->methods[index].name;
    set_qs(qs, &qs_empty);
    set_pset(ps, &c->methods[index]);
    return MI_RESULT_OK;
}

MI_Result MI_CALL impl_MI_ClassFT_GetMethod(const MI_Class *self, const MI_Char *name, MI_QualifierSet *qs, MI_ParameterSet *ps,
        MI_Uint32 *index)
{
    const struct cls *c = (const struct cls *)self;
    MI_Uint32 i;
    const MI_Char *n;
    for (i = 0; i < c->nmethods; i++)
        if (!lstrcmpiW(c->methods[i].name, name))
        {
            if (index) *index = i;
            return impl_MI_ClassFT_GetMethodAt(self, i, &n, qs, ps);
        }
    return MI_RESULT_METHOD_NOT_FOUND;
}

MI_Result MI_CALL impl_MI_ClassFT_GetParentClassName(const MI_Class *self, const MI_Char **name)
{ *name = NULL; return MI_RESULT_NOT_FOUND; }
MI_Result MI_CALL impl_MI_ClassFT_Delete(MI_Class *self)
{ cls_free((struct cls *)self); return MI_RESULT_OK; }

MI_Result MI_CALL impl_MI_ClassFT_Clone(const MI_Class *self, MI_Class **out)
{
    const struct cls *c = (const struct cls *)self;
    struct cls *n = cls_load(c->name, c->ns);
    if (!n) return MI_RESULT_FAILED;
    *out = &n->base;
    return MI_RESULT_OK;
}

MI_Result MI_CALL impl_MI_InstanceFT_GetClass(const MI_Instance *self, MI_Class **out)
{
    struct cls *c = cls_load(mi_instance_class_name(self), self->nameSpace);
    mi_trace("Instance_GetClass %ls -> %p", mi_instance_class_name(self), (void *)c);
    if (!c) return MI_RESULT_NOT_FOUND;
    *out = &c->base;
    return MI_RESULT_OK;
}
