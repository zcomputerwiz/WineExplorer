/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * StatusNotifierWatcher/Host and dbusmenu. No GTK dependency or state polling.
 */
#if 0
#pragma makedep unix
#endif
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <dlfcn.h>
#include <glob.h>
#include <unistd.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <winerror.h>
#include "wexhost_unix.h"
#include "wine/debug.h"
WINE_DEFAULT_DEBUG_CHANNEL(wexhost);

#ifdef SONAME_LIBDBUS_1
#include <dbus/dbus.h>
#define DBUS_FUNCS \
    X(dbus_threads_init_default) X(dbus_bus_get_private) X(dbus_bus_request_name) \
    X(dbus_bus_get_unique_name) X(dbus_bus_add_match) X(dbus_connection_set_exit_on_disconnect) \
    X(dbus_connection_close) X(dbus_connection_unref) X(dbus_connection_read_write) \
    X(dbus_connection_pop_message) X(dbus_connection_send) X(dbus_connection_flush) \
    X(dbus_connection_send_with_reply_and_block) X(dbus_message_new_method_call) \
    X(dbus_message_new_method_return) X(dbus_message_new_error) X(dbus_message_new_signal) \
    X(dbus_message_unref) X(dbus_message_append_args) X(dbus_message_get_sender) \
    X(dbus_message_get_path) X(dbus_message_get_interface) X(dbus_message_get_member) \
    X(dbus_message_get_type) X(dbus_message_set_no_reply) X(dbus_error_init) X(dbus_error_free) \
    X(dbus_message_iter_init) X(dbus_message_iter_init_append) X(dbus_message_iter_next) \
    X(dbus_message_iter_recurse) X(dbus_message_iter_get_arg_type) X(dbus_message_iter_get_basic) \
    X(dbus_message_iter_get_fixed_array) X(dbus_message_iter_append_basic) \
    X(dbus_message_iter_open_container) X(dbus_message_iter_close_container) \
    X(dbus_validate_bus_name) X(dbus_validate_path)
#define X(f) static typeof(f) *p_##f;
DBUS_FUNCS
#undef X
#define WATCHER "org.kde.StatusNotifierWatcher"
#define WATCH_PATH "/StatusNotifierWatcher"
#define ITEM_IFACE "org.kde.StatusNotifierItem"
#define PROPS "org.freedesktop.DBus.Properties"
#define MENU_IFACE "com.canonical.dbusmenu"
#define ITEM_MAX 128
struct item
{
    DWORD token;
    BOOL dirty, removed;
    char service[256], owner[256], path[256], menu[256];
    WEX_TRAY_ITEM data;
};
static struct item items[ITEM_MAX];
static DBusConnection *bus;
static BOOL watcher;
static DWORD next_token;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static void copy_string(char *dst, const char *src, size_t size)
{
    snprintf(dst, size, "%s", src ? src : "");
}
static const char *string_value(DBusMessageIter *it)
{
    const char *s = "";
    int type = p_dbus_message_iter_get_arg_type(it);
    if (type == DBUS_TYPE_STRING || type == DBUS_TYPE_OBJECT_PATH)
        p_dbus_message_iter_get_basic(it, &s);
    return s;
}
static int int_value(DBusMessageIter *it, int fallback)
{
    dbus_int32_t n;
    if (p_dbus_message_iter_get_arg_type(it) != DBUS_TYPE_INT32) return fallback;
    p_dbus_message_iter_get_basic(it, &n);
    return n;
}
static BOOL bool_value(DBusMessageIter *it, BOOL fallback)
{
    dbus_bool_t n;
    if (p_dbus_message_iter_get_arg_type(it) != DBUS_TYPE_BOOLEAN) return fallback;
    p_dbus_message_iter_get_basic(it, &n);
    return n;
}
static DBusMessage *call(DBusMessage *msg)
{
    DBusMessage *reply;
    DBusError error;
    if (!msg) return NULL;
    p_dbus_error_init(&error);
    reply = p_dbus_connection_send_with_reply_and_block(bus, msg, 1500, &error);
    if (!reply) TRACE("tray D-Bus: %s\n", error.message ? error.message : "no reply");
    p_dbus_error_free(&error);
    p_dbus_message_unref(msg);
    return reply;
}
static void send_message(DBusMessage *msg)
{
    if (!msg) return;
    p_dbus_connection_send(bus, msg, NULL);
    p_dbus_message_unref(msg);
}
static void signal_item(const char *member, struct item *item)
{
    DBusMessage *msg = p_dbus_message_new_signal(WATCH_PATH, WATCHER, member);
    char identity[520];
    const char *s = identity;
    snprintf(identity, sizeof(identity), "%s%s", item->service, item->path);
    if (msg) p_dbus_message_append_args(msg, DBUS_TYPE_STRING, &s, DBUS_TYPE_INVALID);
    send_message(msg);
}
static void register_item(const char *service, const char *sender)
{
    char dest[256], path[256];
    struct item *item = NULL;
    DBusMessage *msg, *reply;
    DBusMessageIter it;
    const char *arg;
    unsigned int i;
    if (!service || !sender) return;
    if (*service == '/')
    {
        copy_string(dest, sender, sizeof(dest));
        copy_string(path, service, sizeof(path));
    }
    else
    {
        const char *slash = strchr(service, '/');
        snprintf(dest, sizeof(dest), "%.*s", (int)(slash ? slash - service : strlen(service)), service);
        copy_string(path, slash ? slash : "/StatusNotifierItem", sizeof(path));
    }
    if (!p_dbus_validate_bus_name(dest, NULL) || !p_dbus_validate_path(path, NULL)) return;
    for (i = 0; i < ITEM_MAX; i++)
    {
        if (items[i].token && !strcmp(items[i].service, dest) && !strcmp(items[i].path, path)) return;
        if (!items[i].token && !item) item = &items[i];
    }
    if (!item) return;
    /* Bind lifetime and signal matching to the unique bus owner, never just a
     * reusable well-known name (libayatana normally registers such a name). */
    msg = p_dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "GetNameOwner");
    arg = dest;
    if (msg) p_dbus_message_append_args(msg, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID);
    if (!(reply = call(msg))) return;
    if (p_dbus_message_iter_init(reply, &it)) copy_string(item->owner, string_value(&it), sizeof(item->owner));
    p_dbus_message_unref(reply);
    if (!item->owner[0]) return;
    item->token = ++next_token;
    copy_string(item->service, dest, sizeof(item->service));
    copy_string(item->path, path, sizeof(item->path));
    item->dirty = TRUE;
    if (watcher) signal_item("StatusNotifierItemRegistered", item);
}
static void append_property(DBusMessageIter *out, const char *name)
{
    DBusMessageIter array;
    dbus_bool_t yes = TRUE;
    dbus_int32_t version = 0;
    unsigned int i;
    if (!strcmp(name, "RegisteredStatusNotifierItems"))
    {
        p_dbus_message_iter_open_container(out, DBUS_TYPE_ARRAY, "s", &array);
        for (i = 0; i < ITEM_MAX; i++) if (items[i].token && !items[i].removed)
        {
            char identity[520];
            const char *s = identity;
            snprintf(identity, sizeof(identity), "%s%s", items[i].service, items[i].path);
            p_dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &s);
        }
        p_dbus_message_iter_close_container(out, &array);
    }
    else if (!strcmp(name, "ProtocolVersion")) p_dbus_message_iter_append_basic(out, DBUS_TYPE_INT32, &version);
    else p_dbus_message_iter_append_basic(out, DBUS_TYPE_BOOLEAN, &yes);
}
static const char *property_signature(const char *name)
{
    if (!strcmp(name, "RegisteredStatusNotifierItems")) return "as";
    if (!strcmp(name, "ProtocolVersion")) return "i";
    if (!strcmp(name, "IsStatusNotifierHostRegistered")) return "b";
    return NULL;
}
static void watcher_method(DBusMessage *msg)
{
    const char *iface = p_dbus_message_get_interface(msg), *member = p_dbus_message_get_member(msg);
    DBusMessage *reply = NULL;
    DBusMessageIter it, out, var, array, dict;
    if (!iface || !member) return;
    if (!strcmp(iface, "org.freedesktop.DBus.Introspectable") && !strcmp(member, "Introspect"))
    {
        const char *xml = "<node><interface name='org.kde.StatusNotifierWatcher'>"
            "<method name='RegisterStatusNotifierItem'><arg type='s' direction='in'/></method>"
            "<method name='RegisterStatusNotifierHost'><arg type='s' direction='in'/></method>"
            "<property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
            "<property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
            "<property name='ProtocolVersion' type='i' access='read'/>"
            "<signal name='StatusNotifierItemRegistered'><arg type='s'/></signal>"
            "<signal name='StatusNotifierItemUnregistered'><arg type='s'/></signal>"
            "<signal name='StatusNotifierHostRegistered'/></interface>"
            "<interface name='org.freedesktop.DBus.Properties'>"
            "<method name='Get'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='out'/></method>"
            "<method name='GetAll'><arg type='s' direction='in'/><arg type='a{sv}' direction='out'/></method>"
            "</interface></node>";
        reply = p_dbus_message_new_method_return(msg);
        if (reply) p_dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
        send_message(reply); return;
    }
    if (!strcmp(iface, WATCHER) &&
        (!strcmp(member, "RegisterStatusNotifierItem") || !strcmp(member, "RegisterStatusNotifierHost")))
    {
        const char *service = NULL;
        if (p_dbus_message_iter_init(msg, &it) && p_dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING)
            service = string_value(&it);
        if (!service) { send_message(p_dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "Expected service or object path")); return; }
        /* Registration reply precedes GetAll, so the client can dispatch our property calls. */
        send_message(p_dbus_message_new_method_return(msg));
        p_dbus_connection_flush(bus);
        if (!strcmp(member, "RegisterStatusNotifierItem")) register_item(service, p_dbus_message_get_sender(msg));
        return;
    }
    if (!strcmp(iface, PROPS) && p_dbus_message_iter_init(msg, &it) && !strcmp(string_value(&it), WATCHER))
    {
        if (!strcmp(member, "Get"))
        {
            const char *name, *signature;
            p_dbus_message_iter_next(&it);
            name = string_value(&it);
            if (!(signature = property_signature(name)))
                reply = p_dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown watcher property");
            else
            {
                reply = p_dbus_message_new_method_return(msg);
                p_dbus_message_iter_init_append(reply, &out);
                p_dbus_message_iter_open_container(&out, DBUS_TYPE_VARIANT, signature, &var);
                append_property(&var, name);
                p_dbus_message_iter_close_container(&out, &var);
            }
        }
        else if (!strcmp(member, "GetAll"))
        {
            static const char *names[] = {"RegisteredStatusNotifierItems", "IsStatusNotifierHostRegistered", "ProtocolVersion"};
            unsigned int i;
            reply = p_dbus_message_new_method_return(msg);
            p_dbus_message_iter_init_append(reply, &out);
            p_dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "{sv}", &array);
            for (i = 0; i < ARRAY_SIZE(names); i++)
            {
                p_dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict);
                p_dbus_message_iter_append_basic(&dict, DBUS_TYPE_STRING, &names[i]);
                p_dbus_message_iter_open_container(&dict, DBUS_TYPE_VARIANT, property_signature(names[i]), &var);
                append_property(&var, names[i]);
                p_dbus_message_iter_close_container(&dict, &var);
                p_dbus_message_iter_close_container(&array, &dict);
            }
            p_dbus_message_iter_close_container(&out, &array);
        }
    }
    if (!reply) reply = p_dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, "Unknown watcher method");
    send_message(reply);
}
static void handle_message(DBusMessage *msg)
{
    const char *member = p_dbus_message_get_member(msg), *iface = p_dbus_message_get_interface(msg);
    const char *sender = p_dbus_message_get_sender(msg), *path = p_dbus_message_get_path(msg);
    DBusMessageIter it;
    unsigned int i;
    if (!member || !iface) return;
    if (watcher && path && !strcmp(path, WATCH_PATH) && p_dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL)
    { watcher_method(msg); return; }
    if (p_dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL) return;
    if (!strcmp(iface, DBUS_INTERFACE_DBUS) && !strcmp(member, "NameOwnerChanged") && p_dbus_message_iter_init(msg, &it))
    {
        const char *name = string_value(&it), *old, *now;
        p_dbus_message_iter_next(&it); old = string_value(&it);
        p_dbus_message_iter_next(&it); now = string_value(&it);
        for (i = 0; i < ITEM_MAX; i++) if (items[i].token && !items[i].removed &&
            ((!strcmp(items[i].owner, name) && !*now) || (!strcmp(items[i].service, name) && strcmp(old, now))))
        {
            items[i].removed = TRUE;
            if (watcher) signal_item("StatusNotifierItemUnregistered", &items[i]);
        }
    }
    else if (!strcmp(iface, WATCHER) && !strcmp(member, "StatusNotifierItemRegistered") && !watcher && p_dbus_message_iter_init(msg, &it))
        register_item(string_value(&it), sender);
    else if (!strcmp(iface, WATCHER) && !strcmp(member, "StatusNotifierItemUnregistered") && !watcher && p_dbus_message_iter_init(msg, &it))
    {
        const char *identity = string_value(&it);
        for (i = 0; i < ITEM_MAX; i++) if (items[i].token)
        {
            char key[520];
            snprintf(key, sizeof(key), "%s%s", items[i].service, items[i].path);
            if (!strcmp(key, identity)) items[i].removed = TRUE;
        }
    }
    else if ((!strcmp(iface, ITEM_IFACE) || !strcmp(iface, PROPS)) && sender && path)
        for (i = 0; i < ITEM_MAX; i++) if (items[i].token && !strcmp(items[i].owner, sender) && !strcmp(items[i].path, path)) items[i].dirty = TRUE;
}
/* Prefer the smallest supplied pixmap at least 24 px wide, otherwise the
 * largest smaller one; reject inconsistent lengths before reading pixels. */
static void pixmap(DBusMessageIter *value, WEX_TRAY_ITEM *data)
{
    DBusMessageIter array, entry, bytes;
    int score = 0x7fffffff;
    if (p_dbus_message_iter_get_arg_type(value) != DBUS_TYPE_ARRAY) return;
    p_dbus_message_iter_recurse(value, &array);
    while (p_dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT)
    {
        const unsigned char *pixels;
        int width, height, length, rank;
        unsigned int x, y;
        p_dbus_message_iter_recurse(&array, &entry);
        width = int_value(&entry, 0); p_dbus_message_iter_next(&entry);
        height = int_value(&entry, 0); p_dbus_message_iter_next(&entry);
        if (width > 0 && height > 0 && width <= 1024 && height <= 1024 && p_dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_ARRAY)
        {
            p_dbus_message_iter_recurse(&entry, &bytes);
            pixels = NULL; length = 0;
            if (p_dbus_message_iter_get_arg_type(&bytes) == DBUS_TYPE_BYTE)
                p_dbus_message_iter_get_fixed_array(&bytes, &pixels, &length);
            rank = width >= 24 ? width - 24 : 2048 - width;
            if (length == width * height * 4 && rank < score)
            {
                score = rank;
                data->width = min(width, 64); data->height = min(height, 64);
                for (y = 0; y < data->height; y++) for (x = 0; x < data->width; x++)
                {
                    const unsigned char *p = pixels + 4 * ((y * height / data->height) * width + x * width / data->width);
                    data->pixels[y * data->width + x] = ((DWORD)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
                }
            }
        }
        p_dbus_message_iter_next(&array);
    }
}

/* GdkPixbuf is only an image decoder here; loading it does not initialize GTK,
 * open a display, or create a second tray. SVG decoding follows its installed
 * loader modules just as the desktop application launcher does. */
static BOOL decode_icon(const char *path, WEX_TRAY_ITEM *data)
{
    static BOOL tried;
    static void *(*load)(const char *, int, int, int, void **);
    static int (*width)(void *), (*height)(void *), (*stride)(void *), (*channels)(void *);
    static unsigned char *(*pixels)(void *);
    static void (*unref)(void *), (*error_free)(void *);
    void *image, *error = NULL;
    unsigned int x, y, w, h, row, n;
    unsigned char *bytes;
    if (!tried)
    {
        void *lib = dlopen("libgdk_pixbuf-2.0.so.0", RTLD_NOW);
        tried = TRUE;
        if (lib)
        {
            load = dlsym(lib, "gdk_pixbuf_new_from_file_at_scale");
            width = dlsym(lib, "gdk_pixbuf_get_width"); height = dlsym(lib, "gdk_pixbuf_get_height");
            stride = dlsym(lib, "gdk_pixbuf_get_rowstride"); channels = dlsym(lib, "gdk_pixbuf_get_n_channels");
            pixels = dlsym(lib, "gdk_pixbuf_get_pixels"); unref = dlsym(lib, "g_object_unref");
            error_free = dlsym(lib, "g_error_free");
        }
    }
    if (!load || !width || !height || !stride || !channels || !pixels || !unref || !error_free) return FALSE;
    image = load(path, 24, 24, TRUE, &error);
    if (error) error_free(error);
    if (!image) return FALSE;
    w = width(image); h = height(image); row = stride(image); n = channels(image); bytes = pixels(image);
    if (!w || !h || w > 64 || h > 64 || (n != 3 && n != 4)) { unref(image); return FALSE; }
    data->width = w; data->height = h;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++)
    {
        const unsigned char *p = bytes + y * row + x * n;
        data->pixels[y * w + x] = ((DWORD)(n == 4 ? p[3] : 255) << 24) | (p[0] << 16) | (p[1] << 8) | p[2];
    }
    unref(image);
    return TRUE;
}
static BOOL search_icon(const char *root, const char *name, WEX_TRAY_ITEM *data)
{
    static const char *patterns[] = {"%s/%s", "%s/24x24/*/%s", "%s/16x16/*/%s", "%s/*/24x24/*/%s",
        "%s/*/16x16/*/%s", "%s/*/*/%s", "%s/*/*/*/%s"};
    static const char *suffixes[] = {"", ".png", ".svg", ".xpm"};
    unsigned int a, b;
    size_t j;
    for (a = 0; a < ARRAY_SIZE(patterns); a++) for (b = 0; b < ARRAY_SIZE(suffixes); b++)
    {
        char pattern[4096], file[512];
        glob_t matches = {0};
        snprintf(file, sizeof(file), "%s%s", name, suffixes[b]);
        snprintf(pattern, sizeof(pattern), patterns[a], root, file);
        if (!glob(pattern, 0, NULL, &matches)) for (j = 0; j < matches.gl_pathc; j++)
            if (decode_icon(matches.gl_pathv[j], data)) { globfree(&matches); return TRUE; }
        globfree(&matches);
    }
    return FALSE;
}
static void named_icon(const char *name, const char *custom, WEX_TRAY_ITEM *data)
{
    const char *home = getenv("HOME"), *xdg = getenv("XDG_DATA_HOME"), *dirs = getenv("XDG_DATA_DIRS");
    char root[4096], *paths, *part, *save;
    if (!*name || strpbrk(name, "*?[]")) return;
    if (*name == '/') { decode_icon(name, data); return; }
    if (strchr(name, '/')) return;
    if (*custom && search_icon(custom, name, data)) return;
    if (home)
    {
        snprintf(root, sizeof(root), "%s/.icons", home);
        if (search_icon(root, name, data)) return;
        if (xdg && *xdg) snprintf(root, sizeof(root), "%s/icons", xdg);
        else snprintf(root, sizeof(root), "%s/.local/share/icons", home);
        if (search_icon(root, name, data)) return;
    }
    if (!(paths = strdup(dirs && *dirs ? dirs : "/usr/local/share:/usr/share"))) return;
    for (part = strtok_r(paths, ":", &save); part; part = strtok_r(NULL, ":", &save))
    {
        if (*part != '/') continue;
        snprintf(root, sizeof(root), "%s/icons", part);
        if (search_icon(root, name, data)) break;
        snprintf(root, sizeof(root), "%s/pixmaps", part);
        if (search_icon(root, name, data)) break;
    }
    free(paths);
}
static void refresh(struct item *item)
{
    DBusMessage *msg, *reply;
    DBusMessageIter it, array, entry, value, tooltip;
    const char *iface = ITEM_IFACE;
    WEX_TRAY_ITEM attention = {0};
    char name[256] = "", attention_name[256] = "", theme_path[1024] = "";
    memset(&item->data, 0, sizeof(item->data));
    item->data.token = item->token; item->data.event = WEX_TRAY_CHANGED;
    item->menu[0] = 0;
    msg = p_dbus_message_new_method_call(item->service, item->path, PROPS, "GetAll");
    if (msg) p_dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);
    reply = call(msg);
    if (!reply) { item->removed = TRUE; return; }
    if (p_dbus_message_iter_init(reply, &it) && p_dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY)
    {
        p_dbus_message_iter_recurse(&it, &array);
        while (p_dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_DICT_ENTRY)
        {
            const char *key;
            p_dbus_message_iter_recurse(&array, &entry);
            key = string_value(&entry);
            p_dbus_message_iter_next(&entry);
            if (p_dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) break;
            p_dbus_message_iter_recurse(&entry, &value);
            if (!strcmp(key, "Id")) copy_string(item->data.id, string_value(&value), sizeof(item->data.id));
            else if (!strcmp(key, "Title")) copy_string(item->data.title, string_value(&value), sizeof(item->data.title));
            else if (!strcmp(key, "Menu")) copy_string(item->menu, string_value(&value), sizeof(item->menu));
            else if (!strcmp(key, "ItemIsMenu") && bool_value(&value, FALSE)) item->data.flags |= WEX_TRAY_ITEM_MENU;
            else if (!strcmp(key, "Status"))
            {
                if (!strcmp(string_value(&value), "Passive")) item->data.flags |= WEX_TRAY_PASSIVE;
                if (!strcmp(string_value(&value), "NeedsAttention")) item->data.flags |= WEX_TRAY_ATTENTION;
            }
            else if (!strcmp(key, "IconPixmap")) pixmap(&value, &item->data);
            else if (!strcmp(key, "AttentionIconPixmap")) pixmap(&value, &attention);
            else if (!strcmp(key, "IconName")) copy_string(name, string_value(&value), sizeof(name));
            else if (!strcmp(key, "AttentionIconName")) copy_string(attention_name, string_value(&value), sizeof(attention_name));
            else if (!strcmp(key, "IconThemePath")) copy_string(theme_path, string_value(&value), sizeof(theme_path));
            else if (!strcmp(key, "ToolTip") && p_dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRUCT)
            {
                const char *title, *description;
                p_dbus_message_iter_recurse(&value, &tooltip);
                p_dbus_message_iter_next(&tooltip); p_dbus_message_iter_next(&tooltip);
                title = string_value(&tooltip); p_dbus_message_iter_next(&tooltip);
                description = string_value(&tooltip);
                snprintf(item->data.tooltip, sizeof(item->data.tooltip), "%s%s%s", title, *title && *description ? "\n" : "", description);
            }
            p_dbus_message_iter_next(&array);
        }
    }
    if (!item->data.width) named_icon(name, theme_path, &item->data);
    if (item->menu[0] && !p_dbus_validate_path(item->menu, NULL)) item->menu[0] = 0;
    if ((item->data.flags & WEX_TRAY_ATTENTION) && !attention.width) named_icon(attention_name, theme_path, &attention);
    if ((item->data.flags & WEX_TRAY_ATTENTION) && attention.width)
    {
        item->data.width = attention.width; item->data.height = attention.height;
        memcpy(item->data.pixels, attention.pixels, sizeof(attention.pixels));
    }
    p_dbus_message_unref(reply);
    item->dirty = FALSE;
}
static struct item *find_item(DWORD token)
{
    unsigned int i;
    for (i = 0; i < ITEM_MAX; i++) if (items[i].token == token && !items[i].removed) return &items[i];
    return NULL;
}
NTSTATUS tray_start(void *args)
{
    struct init_params *params = args;
    static BOOL loaded;
    void *lib;
    DBusError error;
    DBusMessage *msg, *reply;
    DBusMessageIter it, array;
    const char *iface = WATCHER, *prop = "RegisteredStatusNotifierItems", *host;
    int result;
    params->err = ERROR_NOT_SUPPORTED;
    pthread_mutex_lock(&mutex);
    if (bus) { params->err = ERROR_ALREADY_EXISTS; goto done; }
    if (!loaded)
    {
        if (!(lib = dlopen(SONAME_LIBDBUS_1, RTLD_NOW))) goto done;
#define X(f) if (!(p_##f = dlsym(lib, #f))) goto done;
        DBUS_FUNCS
#undef X
        p_dbus_threads_init_default(); loaded = TRUE;
    }
    p_dbus_error_init(&error);
    bus = p_dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    p_dbus_error_free(&error);
    if (!bus) goto done;
    p_dbus_connection_set_exit_on_disconnect(bus, FALSE);
    p_dbus_error_init(&error);
    result = p_dbus_bus_request_name(bus, WATCHER, DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
    p_dbus_error_free(&error);
    watcher = result == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER;
    p_dbus_bus_add_match(bus, "type='signal',interface='org.freedesktop.DBus',member='NameOwnerChanged'", NULL);
    p_dbus_bus_add_match(bus, "type='signal',interface='org.kde.StatusNotifierItem'", NULL);
    p_dbus_bus_add_match(bus, "type='signal',interface='org.freedesktop.DBus.Properties'", NULL);
    p_dbus_bus_add_match(bus, "type='signal',interface='org.kde.StatusNotifierWatcher'", NULL);
    if (watcher)
        send_message(p_dbus_message_new_signal(WATCH_PATH, WATCHER, "StatusNotifierHostRegistered"));
    else
    {
        host = p_dbus_bus_get_unique_name(bus);
        msg = p_dbus_message_new_method_call(WATCHER, WATCH_PATH, WATCHER, "RegisterStatusNotifierHost");
        if (msg) p_dbus_message_append_args(msg, DBUS_TYPE_STRING, &host, DBUS_TYPE_INVALID);
        if ((reply = call(msg))) p_dbus_message_unref(reply);
        msg = p_dbus_message_new_method_call(WATCHER, WATCH_PATH, PROPS, "Get");
        if (msg) p_dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
        if ((reply = call(msg)))
        {
            if (p_dbus_message_iter_init(reply, &it) && p_dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_VARIANT)
            {
                p_dbus_message_iter_recurse(&it, &array);
                if (p_dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_ARRAY)
                {
                    p_dbus_message_iter_recurse(&array, &it);
                    while (p_dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING)
                    { register_item(string_value(&it), WATCHER); p_dbus_message_iter_next(&it); }
                }
            }
            p_dbus_message_unref(reply);
        }
    }
    p_dbus_connection_flush(bus);
    params->err = 0;
done:
    pthread_mutex_unlock(&mutex);
    return STATUS_SUCCESS;
}
NTSTATUS tray_next(void *args)
{
    struct tray_params *params = args;
    DBusMessage *msg;
    unsigned int i;
    memset(&params->item, 0, sizeof(params->item));
    params->err = 0;
    pthread_mutex_lock(&mutex);
    if (!bus || !p_dbus_connection_read_write(bus, 100)) { params->err = ERROR_BROKEN_PIPE; goto done; }
    /* Bound each dispatch batch so a noisy client cannot starve shutdown/UI. */
    for (i = 0; i < 64 && (msg = p_dbus_connection_pop_message(bus)); i++)
    { handle_message(msg); p_dbus_message_unref(msg); }
    for (i = 0; i < ITEM_MAX; i++) if (items[i].token)
    {
        if (items[i].dirty && !items[i].removed) refresh(&items[i]);
        if (items[i].removed)
        {
            params->item.token = items[i].token; params->item.event = WEX_TRAY_REMOVED;
            memset(&items[i], 0, sizeof(items[i])); break;
        }
        if (items[i].data.event)
        { params->item = items[i].data; items[i].data.event = 0; break; }
    }
    if (!params->item.event) tray_x11_next(&params->item);
    p_dbus_connection_flush(bus);
done:
    pthread_mutex_unlock(&mutex);
    return STATUS_SUCCESS;
}
NTSTATUS tray_stop(void *args)
{
    pthread_mutex_lock(&mutex);
    tray_x11_stop();
    if (bus) { p_dbus_connection_close(bus); p_dbus_connection_unref(bus); bus = NULL; }
    memset(items, 0, sizeof(items));
    ((struct init_params *)args)->err = 0;
    pthread_mutex_unlock(&mutex);
    return STATUS_SUCCESS;
}
NTSTATUS tray_action(void *args)
{
    struct tray_action_params *params = args;
    struct item *item;
    DBusMessage *msg = NULL;
    dbus_int32_t x = params->x, y = params->y, value = params->value;
    pthread_mutex_lock(&mutex);
    params->err = ERROR_NOT_FOUND;
    if (tray_x11_action(params)) goto done;
    if (!bus || !(item = find_item(params->token))) goto done;
    switch (params->action)
    {
    case WEX_TRAY_ACTIVATE: case WEX_TRAY_SECONDARY: case WEX_TRAY_CONTEXT:
        msg = p_dbus_message_new_method_call(item->service, item->path, ITEM_IFACE,
            params->action == WEX_TRAY_ACTIVATE ? "Activate" : params->action == WEX_TRAY_SECONDARY ? "SecondaryActivate" : "ContextMenu");
        if (msg) p_dbus_message_append_args(msg, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y, DBUS_TYPE_INVALID);
        break;
    case WEX_TRAY_SCROLL:
    {
        const char *orientation = params->y ? "horizontal" : "vertical";
        msg = p_dbus_message_new_method_call(item->service, item->path, ITEM_IFACE, "Scroll");
        if (msg) p_dbus_message_append_args(msg, DBUS_TYPE_INT32, &value, DBUS_TYPE_STRING, &orientation, DBUS_TYPE_INVALID);
        break;
    }
    case WEX_TRAY_CLICK_MENU:
    {
        DBusMessageIter out, var;
        const char *event = "clicked";
        dbus_uint32_t timestamp = params->x;
        dbus_int32_t zero = 0;
        if (!item->menu[0]) break;
        msg = p_dbus_message_new_method_call(item->service, item->menu, MENU_IFACE, "Event");
        if (!msg) break;
        p_dbus_message_iter_init_append(msg, &out);
        p_dbus_message_iter_append_basic(&out, DBUS_TYPE_INT32, &value);
        p_dbus_message_iter_append_basic(&out, DBUS_TYPE_STRING, &event);
        p_dbus_message_iter_open_container(&out, DBUS_TYPE_VARIANT, "i", &var);
        p_dbus_message_iter_append_basic(&var, DBUS_TYPE_INT32, &zero);
        p_dbus_message_iter_close_container(&out, &var);
        p_dbus_message_iter_append_basic(&out, DBUS_TYPE_UINT32, &timestamp);
        break;
    }
    }
    params->err = msg ? 0 : ERROR_NOT_SUPPORTED;
    if (msg) { p_dbus_message_set_no_reply(msg, TRUE); send_message(msg); p_dbus_connection_flush(bus); }
done:
    pthread_mutex_unlock(&mutex);
    return STATUS_SUCCESS;
}
static void parse_menu(DBusMessageIter *node, WEX_TRAY_MENU *menu, INT parent, unsigned int depth)
{
    DBusMessageIter it, properties, entry, value, children, child;
    WEX_TRAY_MENU_ENTRY *out;
    INT id;
    if (depth > 16 || menu->count == WEX_TRAY_MENU_MAX || p_dbus_message_iter_get_arg_type(node) != DBUS_TYPE_STRUCT) return;
    p_dbus_message_iter_recurse(node, &it);
    id = int_value(&it, -1);
    if (id < 0) return;
    out = &menu->entries[menu->count++];
    out->id = id; out->parent = parent; out->enabled = out->visible = TRUE;
    p_dbus_message_iter_next(&it);
    if (p_dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) return;
    p_dbus_message_iter_recurse(&it, &properties);
    while (p_dbus_message_iter_get_arg_type(&properties) == DBUS_TYPE_DICT_ENTRY)
    {
        const char *key;
        p_dbus_message_iter_recurse(&properties, &entry); key = string_value(&entry);
        p_dbus_message_iter_next(&entry);
        if (p_dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) break;
        p_dbus_message_iter_recurse(&entry, &value);
        if (!strcmp(key, "label")) copy_string(out->label, string_value(&value), sizeof(out->label));
        else if (!strcmp(key, "enabled")) out->enabled = bool_value(&value, TRUE);
        else if (!strcmp(key, "visible")) out->visible = bool_value(&value, TRUE);
        else if (!strcmp(key, "type")) out->separator = !strcmp(string_value(&value), "separator");
        else if (!strcmp(key, "toggle-type")) out->toggle = !strcmp(string_value(&value), "radio") ? 2 : !strcmp(string_value(&value), "checkmark");
        else if (!strcmp(key, "toggle-state")) out->checked = int_value(&value, 0) > 0;
        p_dbus_message_iter_next(&properties);
    }
    p_dbus_message_iter_next(&it);
    if (p_dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY) return;
    p_dbus_message_iter_recurse(&it, &children);
    while (p_dbus_message_iter_get_arg_type(&children) == DBUS_TYPE_VARIANT)
    {
        p_dbus_message_iter_recurse(&children, &child);
        parse_menu(&child, menu, id, depth + 1);
        p_dbus_message_iter_next(&children);
    }
}
NTSTATUS tray_menu(void *args)
{
    struct tray_menu_params *params = args;
    struct item *item;
    DBusMessage *msg, *reply;
    DBusMessageIter it, array;
    dbus_int32_t root = 0, depth = -1;
    pthread_mutex_lock(&mutex);
    memset(&params->menu, 0, sizeof(params->menu));
    params->err = ERROR_NOT_FOUND;
    if (!bus || !(item = find_item(params->token)) || !item->menu[0]) goto done;
    msg = p_dbus_message_new_method_call(item->service, item->menu, MENU_IFACE, "AboutToShow");
    if (msg) p_dbus_message_append_args(msg, DBUS_TYPE_INT32, &root, DBUS_TYPE_INVALID);
    if ((reply = call(msg))) p_dbus_message_unref(reply);
    msg = p_dbus_message_new_method_call(item->service, item->menu, MENU_IFACE, "GetLayout");
    if (msg)
    {
        p_dbus_message_iter_init_append(msg, &it);
        p_dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &root);
        p_dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &depth);
        p_dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &array);
        p_dbus_message_iter_close_container(&it, &array);
    }
    if ((reply = call(msg)))
    {
        if (p_dbus_message_iter_init(reply, &it)) { p_dbus_message_iter_next(&it); parse_menu(&it, &params->menu, -1, 0); }
        p_dbus_message_unref(reply);
        params->err = params->menu.count ? 0 : ERROR_INVALID_DATA;
    }
done:
    pthread_mutex_unlock(&mutex);
    return STATUS_SUCCESS;
}
#else
#define STUB(name, type) NTSTATUS name(void *args) { ((type *)args)->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }
STUB(tray_start, struct init_params)
STUB(tray_stop, struct init_params)
STUB(tray_next, struct tray_params)
STUB(tray_action, struct tray_action_params)
STUB(tray_menu, struct tray_menu_params)
#endif
