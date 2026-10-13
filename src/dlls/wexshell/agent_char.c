/*
 * WineExplorer Desktop Environment - animated characters (Microsoft Agent .acs files)
 *
 * Reads the characters of Windows' Search Companion (Rover, Courtney, Earl ...) and plays their animations the way
 * Microsoft Agent did: frames with a duration in 1/100 s, random branches between frames, exit branches or a
 * return animation to get back to rest, and the idle animations of the character when nothing else plays.
 * Written from the published descriptions of the file format; speech, sounds and mouth overlays are not used.
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "wine/debug.h"
#include "agent_char.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define ACS_SIGNATURE   0xabcdabc3
#define ACS_VOICE       0x00000020
#define ACS_BALLOON     0x00000200
#define NAME_LEN        64

enum transition { RETURN_ANIMATION, EXIT_BRANCHES, NO_TRANSITION };

struct frame_image { DWORD index; short x, y; };
struct branch { WORD frame, probability; };

struct frame
{
    struct frame_image *images;
    unsigned int image_count;
    unsigned int duration;    /* 1/100 s */
    int exit;                 /* frame to go to when leaving the animation, < 0: the next one */
    struct branch branches[3];
    unsigned int branch_count;
};

struct animation
{
    WCHAR name[NAME_LEN];
    DWORD offset, size;
    BOOL loaded, bad;
    enum transition transition;
    WCHAR return_name[NAME_LEN];
    struct frame *frames;
    unsigned int frame_count;
};

struct state
{
    WCHAR name[NAME_LEN];
    WCHAR (*animations)[NAME_LEN];
    unsigned int count;
};

struct image
{
    DWORD offset, size;
    BYTE *bits;               /* decoded 8-bit rows, bottom-up, DWORD aligned */
    unsigned int width, height;
    BOOL bad;
};

struct agent_char
{
    BYTE *data;
    DWORD data_size;
    unsigned int width, height;
    BYTE transparent;
    RGBQUAD palette[256];
    struct animation *animations;
    unsigned int animation_count;
    struct image *images;
    unsigned int image_count;
    struct state *states;
    unsigned int state_count;
    DWORD names_offset, names_size;   /* localized name, description and greeting */

    /* playback */
    struct animation *current, *pending;
    int frame;
    BOOL exiting;
    DWORD frame_end;          /* tick count at which the current frame is over */
    struct animation *shown_anim;
    int shown_frame;          /* what the picture shows (empty end frames keep the one before) */
    DWORD *pixels;            /* the picture, premultiplied BGRA, top-down */
    HBITMAP bitmap;
    BOOL dirty;
};

/* ---- reading ---- */

struct reader { const BYTE *data; DWORD size, pos; BOOL bad; };

static BOOL need(struct reader *r, DWORD n)
{
    if (r->bad || n > r->size || r->pos > r->size - n) { r->bad = TRUE; return FALSE; }
    return TRUE;
}

static BYTE u8(struct reader *r)
{
    return need(r, 1) ? r->data[r->pos++] : 0;
}

static WORD u16(struct reader *r)
{
    WORD v = 0;
    if (need(r, 2)) { memcpy(&v, r->data + r->pos, 2); r->pos += 2; }
    return v;
}

static DWORD u32(struct reader *r)
{
    DWORD v = 0;
    if (need(r, 4)) { memcpy(&v, r->data + r->pos, 4); r->pos += 4; }
    return v;
}

static void skip(struct reader *r, DWORD n)
{
    if (need(r, n)) r->pos += n;
}

/* a counted UTF-16 string (stored with a terminator when not empty), cut to the buffer */
static void string(struct reader *r, WCHAR *out, unsigned int len)
{
    DWORD n = u32(r), i;

    if (out) out[0] = 0;
    if (!n || n > 0x10000 || !need(r, (n + 1) * 2)) return;
    if (out)
    {
        for (i = 0; i < n && i < len - 1; i++) memcpy(&out[i], r->data + r->pos + i * 2, 2);
        out[i] = 0;
    }
    r->pos += (n + 1) * 2;
}

static struct reader reader_at(const struct agent_char *agent, DWORD offset, DWORD size)
{
    struct reader r = { agent->data, agent->data_size, offset, FALSE };
    if (offset > agent->data_size || size > agent->data_size - offset) r.bad = TRUE;
    else r.size = offset + size;
    return r;
}

/* ---- the compression of the pictures ----
 * A 0 byte, then a bit stream read from the low bit up: 0 + 8 bits is a literal byte; 1 starts a copy from earlier
 * output, with an offset of 6, 9, 12 or 20 bits (prefixes 0, 10, 110, 111; bases 1, 65, 577, 4673) and a length of
 * 2 (3 for the 20 bit offsets) plus a count of 1 bits n ending in a 0 and n more bits: 2^n - 1 + those bits.  The
 * 20 bit offset 0xfffff ends the stream. */
struct bits { const BYTE *data; DWORD size, pos; ULONGLONG buf; unsigned int count; };

static DWORD get_bits(struct bits *b, unsigned int n)
{
    DWORD v;
    while (b->count < n)
    {
        b->buf |= (ULONGLONG)(b->pos < b->size ? b->data[b->pos] : 0) << b->count;
        b->pos++;
        b->count += 8;
    }
    v = (DWORD)(b->buf & ((1u << n) - 1));
    b->buf >>= n;
    b->count -= n;
    return v;
}

static BOOL decompress(const BYTE *src, DWORD src_size, BYTE *out, DWORD out_size)
{
    static const struct { unsigned int width, base; } offsets[4] = { { 6, 1 }, { 9, 65 }, { 12, 577 }, { 20, 4673 } };
    struct bits b = { src, src_size, 1, 0, 0 };
    DWORD pos = 0;

    if (!src_size || src[0]) return FALSE;
    while (pos < out_size)
    {
        DWORD value, offset, length;
        unsigned int n = 0, k = 0;

        if (b.pos > src_size + 8) return FALSE;   /* ran off the end of the data */
        if (!get_bits(&b, 1))
        {
            out[pos++] = get_bits(&b, 8);
            continue;
        }
        while (n < 3 && get_bits(&b, 1)) n++;
        value = get_bits(&b, offsets[n].width);
        length = 2;
        if (n == 3)
        {
            if (value == 0xfffff) break;
            length = 3;
        }
        offset = value + offsets[n].base;
        while (k < 11 && get_bits(&b, 1)) k++;
        if (k == 11 && get_bits(&b, 1)) return FALSE;
        if (k) length += (1u << k) - 1 + get_bits(&b, k);
        if (offset > pos) return FALSE;
        while (length-- && pos < out_size)
        {
            out[pos] = out[pos - offset];
            pos++;
        }
    }
    return pos == out_size;
}

static struct image *load_image(struct agent_char *agent, DWORD index)
{
    struct image *image;
    struct reader r;
    DWORD stride, size, data_size;
    BOOL compressed;

    if (index >= agent->image_count) return NULL;
    image = &agent->images[index];
    if (image->bits || image->bad) return image->bits ? image : NULL;

    r = reader_at(agent, image->offset, image->size);
    u8(&r);
    image->width = u16(&r);
    image->height = u16(&r);
    compressed = u8(&r);
    data_size = u32(&r);
    stride = (image->width + 3) & ~3;
    size = stride * image->height;
    if (r.bad || !size || size > 4096 * 4096 || !need(&r, data_size) || !(image->bits = malloc(size)))
    {
        image->bad = TRUE;
        return NULL;
    }
    if (compressed) image->bad = !decompress(r.data + r.pos, data_size, image->bits, size);
    else if (data_size >= size) memcpy(image->bits, r.data + r.pos, size);
    else image->bad = TRUE;
    if (image->bad)
    {
        WARN("bad picture %lu\n", index);
        free(image->bits);
        image->bits = NULL;
        return NULL;
    }
    return image;
}

static void free_animation(struct animation *anim)
{
    unsigned int i;
    for (i = 0; i < anim->frame_count; i++) free(anim->frames[i].images);
    free(anim->frames);
    anim->frames = NULL;
    anim->frame_count = 0;
}

static BOOL load_animation(struct agent_char *agent, struct animation *anim)
{
    struct reader r;
    unsigned int i, j;

    if (anim->loaded) return !anim->bad;
    anim->loaded = TRUE;
    r = reader_at(agent, anim->offset, anim->size);
    string(&r, NULL, 0);
    anim->transition = u8(&r);
    string(&r, anim->return_name, NAME_LEN);
    anim->frame_count = u16(&r);
    if (r.bad || !(anim->frames = calloc(max(anim->frame_count, 1), sizeof(*anim->frames)))) goto bad;
    for (i = 0; i < anim->frame_count; i++)
    {
        struct frame *f = &anim->frames[i];

        f->image_count = u16(&r);
        if (r.bad || !(f->images = calloc(max(f->image_count, 1), sizeof(*f->images)))) goto bad;
        for (j = 0; j < f->image_count; j++)
        {
            f->images[j].index = u32(&r);
            f->images[j].x = (short)u16(&r);
            f->images[j].y = (short)u16(&r);
        }
        u16(&r);   /* sound */
        f->duration = u16(&r);
        f->exit = (short)u16(&r);
        f->branch_count = u8(&r);
        for (j = 0; j < f->branch_count; j++)
        {
            WORD frame = u16(&r), probability = u16(&r);
            if (j < ARRAY_SIZE(f->branches)) { f->branches[j].frame = frame; f->branches[j].probability = probability; }
        }
        f->branch_count = min(f->branch_count, ARRAY_SIZE(f->branches));
        for (j = u8(&r); j && !r.bad; j--)   /* mouth overlays: not used */
        {
            BYTE has_region;
            skip(&r, 5);
            has_region = u8(&r);
            skip(&r, 8);
            if (has_region) skip(&r, u32(&r));
        }
        if (r.bad) goto bad;
    }
    return TRUE;

bad:
    WARN("bad animation %s\n", debugstr_w(anim->name));
    free_animation(anim);
    anim->bad = TRUE;
    return FALSE;
}

static BOOL read_character(struct agent_char *agent, DWORD offset, DWORD size)
{
    struct reader r = reader_at(agent, offset, size);
    DWORD flags, count, i, j;

    skip(&r, 4);                             /* version */
    agent->names_offset = u32(&r);
    agent->names_size = u32(&r);
    skip(&r, 16);                            /* GUID */
    agent->width = u16(&r);
    agent->height = u16(&r);
    agent->transparent = u8(&r);
    flags = u32(&r);
    skip(&r, 4);                             /* animation set version */
    if (flags & ACS_VOICE)
    {
        skip(&r, 16 + 16 + 4 + 2);
        if (u8(&r))
        {
            skip(&r, 2);
            string(&r, NULL, 0);
            skip(&r, 4);
            string(&r, NULL, 0);
        }
    }
    if (flags & ACS_BALLOON)
    {
        skip(&r, 2 + 12);
        string(&r, NULL, 0);
        skip(&r, 4 + 4 + 2);
    }
    count = u32(&r);
    if (count > 256 || !need(&r, count * 4)) return FALSE;
    memcpy(agent->palette, r.data + r.pos, count * 4);
    r.pos += count * 4;
    if (u8(&r))                              /* tray icon */
    {
        skip(&r, u32(&r));
        skip(&r, u32(&r));
    }
    count = u16(&r);
    if (r.bad || !(agent->states = calloc(max(count, 1), sizeof(*agent->states)))) return FALSE;
    for (i = 0; i < count && !r.bad; i++)
    {
        struct state *state = &agent->states[i];
        string(&r, state->name, NAME_LEN);
        state->count = u16(&r);
        if (!(state->animations = calloc(max(state->count, 1), sizeof(*state->animations)))) return FALSE;
        for (j = 0; j < state->count; j++) string(&r, state->animations[j], NAME_LEN);
        agent->state_count++;
    }
    return !r.bad && agent->width && agent->height && agent->width <= 1024 && agent->height <= 1024;
}

struct agent_char *agent_open(const WCHAR *path)
{
    struct agent_char *agent;
    HANDLE file;
    DWORD read, header[9], i;
    struct reader r;

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    if (!(agent = calloc(1, sizeof(*agent))) || (agent->data_size = GetFileSize(file, NULL)) == INVALID_FILE_SIZE ||
        agent->data_size < sizeof(header) || agent->data_size > 64 << 20 || !(agent->data = malloc(agent->data_size)) ||
        !ReadFile(file, agent->data, agent->data_size, &read, NULL) || read != agent->data_size)
    {
        CloseHandle(file);
        agent_close(agent);
        return NULL;
    }
    CloseHandle(file);

    memcpy(header, agent->data, sizeof(header));
    if (header[0] != ACS_SIGNATURE || !read_character(agent, header[1], header[2])) goto bad;

    r = reader_at(agent, header[3], header[4]);
    agent->animation_count = u32(&r);
    if (r.bad || agent->animation_count > 4096 ||
        !(agent->animations = calloc(max(agent->animation_count, 1), sizeof(*agent->animations)))) goto bad;
    for (i = 0; i < agent->animation_count; i++)
    {
        string(&r, agent->animations[i].name, NAME_LEN);
        agent->animations[i].offset = u32(&r);
        agent->animations[i].size = u32(&r);
    }

    if (r.bad) goto bad;
    r = reader_at(agent, header[5], header[6]);
    agent->image_count = u32(&r);
    if (r.bad || agent->image_count > 65536 || !(agent->images = calloc(max(agent->image_count, 1), sizeof(*agent->images))))
        goto bad;
    for (i = 0; i < agent->image_count; i++)
    {
        agent->images[i].offset = u32(&r);
        agent->images[i].size = u32(&r);
        u32(&r);   /* checksum */
    }
    if (r.bad) goto bad;

    agent->frame = -1;
    agent->shown_frame = -1;
    TRACE("%s: %ux%u, %u animations, %u pictures\n", debugstr_w(path), agent->width, agent->height,
          agent->animation_count, agent->image_count);
    return agent;

bad:
    WARN("%s is not a character file\n", debugstr_w(path));
    agent_close(agent);
    return NULL;
}

void agent_close(struct agent_char *agent)
{
    unsigned int i;

    if (!agent) return;
    for (i = 0; i < agent->animation_count; i++) free_animation(&agent->animations[i]);
    for (i = 0; i < agent->image_count; i++) free(agent->images[i].bits);
    for (i = 0; i < agent->state_count; i++) free(agent->states[i].animations);
    if (agent->bitmap) DeleteObject(agent->bitmap);
    free(agent->animations);
    free(agent->images);
    free(agent->states);
    free(agent->data);
    free(agent);
}

SIZE agent_size(const struct agent_char *agent)
{
    SIZE size = { agent->width, agent->height };
    return size;
}

/* The character's name, description and greeting in the user's language, else in English, else the first ones */
BOOL agent_names(const struct agent_char *agent, WCHAR *name, WCHAR *description, WCHAR *greeting, unsigned int len)
{
    struct reader r = reader_at(agent, agent->names_offset, agent->names_size);
    LANGID user = GetUserDefaultUILanguage();
    unsigned int count = u16(&r), i, best = 0, best_score = 0;

    for (i = 0; i < count && !r.bad; i++)
    {
        LANGID lang = u16(&r);
        unsigned int score = lang == user ? 3 : PRIMARYLANGID(lang) == PRIMARYLANGID(user) ? 2 :
                             PRIMARYLANGID(lang) == LANG_ENGLISH ? 1 : 0;
        WCHAR n[NAME_LEN], d[256], g[256];

        string(&r, n, ARRAY_SIZE(n));
        string(&r, d, ARRAY_SIZE(d));
        string(&r, g, ARRAY_SIZE(g));
        if (r.bad || (i && score <= best_score)) continue;
        best = i + 1;
        best_score = score;
        lstrcpynW(name, n, len);
        lstrcpynW(description, d, len);
        lstrcpynW(greeting, g, len);
    }
    return best != 0;
}

/* ---- playback ---- */

static struct animation *find_animation(struct agent_char *agent, const WCHAR *name)
{
    unsigned int i;
    for (i = 0; i < agent->animation_count; i++)
        if (!wcsicmp(agent->animations[i].name, name))
            return load_animation(agent, &agent->animations[i]) ? &agent->animations[i] : NULL;
    return NULL;
}

static struct animation *state_animation(struct agent_char *agent, const WCHAR *state)
{
    unsigned int i;
    for (i = 0; i < agent->state_count; i++)
        if (!wcsicmp(agent->states[i].name, state) && agent->states[i].count)
            return find_animation(agent, agent->states[i].animations[rand() % agent->states[i].count]);
    return NULL;
}

static void start(struct agent_char *agent, struct animation *anim)
{
    agent->current = anim;
    agent->frame = -1;                   /* its first frame shows at the next advance */
    agent->exiting = FALSE;
    agent->frame_end = GetTickCount();
}

/* What to do once an animation is over: what was asked for next, the return animation, else idle */
static void finished(struct agent_char *agent)
{
    struct animation *done = agent->current, *next = agent->pending;

    agent->pending = NULL;
    if (!next && done && done->transition == RETURN_ANIMATION && done->return_name[0] && !agent->exiting)
        next = find_animation(agent, done->return_name);
    if (!next) next = state_animation(agent, L"IdlingLevel1");
    agent->current = NULL;
    if (next && next->frame_count) start(agent, next);
}

static BOOL request(struct agent_char *agent, struct animation *anim)
{
    if (!anim) return FALSE;
    if (anim == agent->current && !agent->exiting) return TRUE;   /* already playing */
    if (!agent->current || agent->frame < 0)
    {
        start(agent, anim);
        return TRUE;
    }
    agent->pending = anim;
    switch (agent->current->transition)
    {
    case EXIT_BRANCHES:
        agent->exiting = TRUE;   /* follow the exit frames back to rest */
        break;
    case RETURN_ANIMATION:
    {
        struct animation *ret = agent->current->return_name[0] ? find_animation(agent, agent->current->return_name) : NULL;
        if (ret && ret != agent->current)
        {
            start(agent, ret);
            agent->pending = anim;
            break;
        }
    }
    /* fall through */
    default:
        agent->pending = NULL;
        start(agent, anim);
        break;
    }
    return TRUE;
}

BOOL agent_play(struct agent_char *agent, const WCHAR *name)
{
    return agent && request(agent, find_animation(agent, name));
}

BOOL agent_play_state(struct agent_char *agent, const WCHAR *state)
{
    return agent && request(agent, state_animation(agent, state));
}

static int next_frame(struct agent_char *agent)
{
    const struct animation *anim = agent->current;
    const struct frame *f = &anim->frames[agent->frame];
    unsigned int i, roll, sum = 0;

    if (agent->exiting)
    {
        if (f->exit >= 0) return f->exit == agent->frame ? (int)anim->frame_count : f->exit;
        return agent->frame + 1;
    }
    roll = rand() % 100;
    for (i = 0; i < f->branch_count; i++)
    {
        sum += f->branches[i].probability;
        if (roll < sum) return f->branches[i].frame;
    }
    return agent->frame + 1;
}

DWORD agent_advance(struct agent_char *agent, BOOL *changed)
{
    DWORD now = GetTickCount();
    int guard = 64;   /* frames of no duration in a row */

    *changed = FALSE;
    if (!agent) return INFINITE;
    if (!agent->current) agent_play_state(agent, L"IdlingLevel1");

    while (agent->current && (int)(now - agent->frame_end) >= 0 && guard--)
    {
        const struct animation *anim = agent->current;
        int next = agent->frame < 0 ? 0 : next_frame(agent);

        if (next < 0 || next >= (int)anim->frame_count)
        {
            finished(agent);
            continue;
        }
        agent->frame = next;
        agent->frame_end += anim->frames[next].duration * 10;
        if ((int)(now - agent->frame_end) > 1000) agent->frame_end = now;   /* we were away for a while */
        if (anim->frames[next].image_count &&
            (agent->shown_anim != anim || agent->shown_frame != next))
        {
            agent->shown_anim = agent->current;
            agent->shown_frame = next;
            agent->dirty = *changed = TRUE;
        }
    }
    if (!agent->current) return INFINITE;
    return max(10, (int)(agent->frame_end - now));
}

/* ---- drawing ---- */

static void compose(struct agent_char *agent)
{
    const struct frame *f;
    int i;

    if (!agent->bitmap)
    {
        BITMAPINFO info = { { sizeof(BITMAPINFOHEADER), agent->width, -(int)agent->height, 1, 32, BI_RGB } };
        agent->bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&agent->pixels, NULL, 0);
        if (!agent->bitmap) return;
    }
    memset(agent->pixels, 0, agent->width * agent->height * 4);
    agent->dirty = FALSE;
    if (!agent->shown_anim || agent->shown_frame < 0) return;

    /* the first picture of a frame is on top */
    f = &agent->shown_anim->frames[agent->shown_frame];
    for (i = f->image_count - 1; i >= 0; i--)
    {
        const struct image *image = load_image(agent, f->images[i].index);
        unsigned int stride, row, col;

        if (!image) continue;
        stride = (image->width + 3) & ~3;
        for (row = 0; row < image->height; row++)
        {
            int y = f->images[i].y + row;
            const BYTE *src = image->bits + (image->height - 1 - row) * stride;
            if (y < 0 || y >= (int)agent->height) continue;
            for (col = 0; col < image->width; col++)
            {
                int x = f->images[i].x + col;
                const RGBQUAD *c;
                if (x < 0 || x >= (int)agent->width || src[col] == agent->transparent) continue;
                c = &agent->palette[src[col]];
                agent->pixels[y * agent->width + x] = 0xff000000 | (c->rgbRed << 16) | (c->rgbGreen << 8) | c->rgbBlue;
            }
        }
    }
}

void agent_draw(struct agent_char *agent, HDC hdc, int x, int y)
{
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    HDC mem;
    HBITMAP old;

    if (!agent) return;
    if (agent->dirty || !agent->bitmap) compose(agent);
    if (!agent->bitmap) return;
    mem = CreateCompatibleDC(hdc);
    old = SelectObject(mem, agent->bitmap);
    AlphaBlend(hdc, x, y, agent->width, agent->height, mem, 0, 0, agent->width, agent->height, blend);
    SelectObject(mem, old);
    DeleteDC(mem);
}

BOOL agent_hit(struct agent_char *agent, int x, int y)
{
    if (!agent || x < 0 || y < 0 || x >= (int)agent->width || y >= (int)agent->height) return FALSE;
    if (agent->dirty || !agent->bitmap) compose(agent);
    return agent->pixels && (agent->pixels[y * agent->width + x] >> 24);
}
