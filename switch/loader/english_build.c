// SonicAdventureRe-nx: the English layer's folder, made on the Switch from the player's US disc
// (the loader runs it on the first launch, after the game's image). The same work as
// tools/english/make_english.py, which stays the reference: its output and this one's are
// compared byte for byte (switch/tools/english_check.sh).
//
//   us/       the US disc's files (EVENT_ADX_US.AFS, EV*.PRS, STG*.PRS, *_E.*), any depth
//   jp/       the Japanese disc's files, the same way
//   out/      becomes english/: EVENT_ADX_US.AFS, voice_map.txt, subtitles.txt, hints.txt,
//             files.txt, SONICADV/ (see make_english.py)
//
// The maps (tools/english/*_map.txt, numbers only) are compiled in: english_maps.c, which
// switch/tools/maps2c.py writes at build time.
#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "english_build.h"

typedef uint8_t u8;
typedef uint32_t u32;

// the maps (english_maps.c)
struct subtitle_row {
    const char *event; // "ev0006"
    u32 crc;
    int table, first, count;
};
struct hint_row {
    const char *module; // "stg01"
    u32 crc;
    int block, run, page;
};
extern const struct subtitle_row english_subtitle_map[];
extern const int english_subtitle_rows;
extern const struct hint_row english_hint_map[];
extern const int english_hint_rows;
extern const short english_voice_map[][2]; // Japanese clip, US clip
extern const int english_voice_rows;
extern const u32 english_maps_crc;

static english_say_fn say;

// --- bytes ---

struct buf {
    u8 *p;
    size_t n, room;
};

static void buf_free(struct buf *b)
{
    free(b->p);
    b->p = NULL;
    b->n = b->room = 0;
}

static int buf_put(struct buf *b, const void *data, size_t n)
{
    if (b->n + n > b->room) {
        size_t room = b->room ? b->room : 4096;
        while (room < b->n + n)
            room *= 2;
        u8 *p = realloc(b->p, room);
        if (!p)
            return -1;
        b->p = p;
        b->room = room;
    }
    if (n)
        memcpy(b->p + b->n, data, n);
    b->n += n;
    return 0;
}

static int buf_text(struct buf *b, const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    return n < 0 ? -1 : buf_put(b, line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
}

static int load(const char *path, struct buf *b)
{
    memset(b, 0, sizeof *b);
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b->p = malloc(n > 0 ? (size_t)n : 1);
    if (!b->p || (n > 0 && fread(b->p, 1, (size_t)n, f) != (size_t)n)) {
        fclose(f);
        buf_free(b);
        return -1;
    }
    fclose(f);
    b->n = b->room = (size_t)n;
    return 0;
}

static int save(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    int ok = fwrite(data, 1, n, f) == n;
    ok = !fclose(f) && ok;
    return ok ? 0 : -1;
}

static u32 le32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static void put_le32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

// Sega's PRS, into a growing buffer
static int prs_decode(const struct buf *src, struct buf *out)
{
    memset(out, 0, sizeof *out);
    size_t in = 0;
    u32 flags = 0, left = 0;
#define NEXT_BIT(var)                                                                                          \
    do {                                                                                                       \
        if (!left) {                                                                                           \
            if (in >= src->n)                                                                                  \
                goto done;                                                                                     \
            flags = src->p[in++];                                                                              \
            left = 8;                                                                                          \
        }                                                                                                      \
        var = flags & 1;                                                                                       \
        flags >>= 1;                                                                                           \
        left--;                                                                                                \
    } while (0)
    while (in < src->n) {
        u32 bit;
        NEXT_BIT(bit);
        if (bit) {
            if (in >= src->n || buf_put(out, &src->p[in++], 1))
                goto fail;
            continue;
        }
        long offset;
        size_t size;
        NEXT_BIT(bit);
        if (bit) {
            if (in + 1 >= src->n)
                break;
            u32 word = src->p[in] | src->p[in + 1] << 8;
            in += 2;
            if (!word)
                break;
            offset = (long)(word >> 3) - 0x2000;
            size = word & 7;
            if (size)
                size += 2;
            else {
                if (in >= src->n)
                    goto fail;
                size = (size_t)src->p[in++] + 1;
            }
        } else {
            u32 hi, lo;
            NEXT_BIT(hi);
            NEXT_BIT(lo);
            size = (hi << 1 | lo) + 2;
            if (in >= src->n)
                goto fail;
            offset = (long)src->p[in++] - 0x100;
        }
        if ((long)out->n + offset < 0)
            goto fail;
        for (size_t i = 0; i < size; i++) {
            u8 c = out->p[out->n + offset];
            if (buf_put(out, &c, 1))
                goto fail;
        }
    }
done:
#undef NEXT_BIT
    return 0;
fail:
    buf_free(out);
    return -1;
}

// PRS of literals only (make_english.py's common.prs_store, byte for byte)
static int prs_store(const u8 *data, size_t n, struct buf *out)
{
    memset(out, 0, sizeof *out);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        u8 f = 0xff;
        if (buf_put(out, &f, 1) || buf_put(out, data + i, 8))
            return -1;
    }
    size_t rest = n - i;
    if (!rest) {
        static const u8 end[3] = {0x02, 0, 0};
        return buf_put(out, end, 3);
    }
    // the last literals, then flags 0 and 1 (a long copy) and a zero word
    u8 bits[10];
    size_t nb = 0;
    for (size_t k = 0; k < rest; k++)
        bits[nb++] = 1;
    bits[nb++] = 0;
    bits[nb++] = 1;
    u8 f1 = 0;
    for (size_t k = 0; k < nb && k < 8; k++)
        f1 |= bits[k] << k;
    if (buf_put(out, &f1, 1) || buf_put(out, data + i, rest))
        return -1;
    if (nb > 8) {
        u8 f2 = 0;
        for (size_t k = 8; k < nb; k++)
            f2 |= bits[k] << (k - 8);
        if (buf_put(out, &f2, 1))
            return -1;
    }
    static const u8 zero[2] = {0, 0};
    return buf_put(out, zero, 2);
}

// --- the discs' files, by name whatever the case or folder ---

struct entry {
    char name[48]; // upper case
    char path[256];
};
struct listing {
    struct entry *e;
    int n, room;
};

static void list_files(const char *dir, struct listing *l, int depth)
{
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        char path[256];
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(path, &st))
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < 3)
                list_files(path, l, depth + 1);
            continue;
        }
        if (strlen(de->d_name) >= sizeof l->e->name)
            continue;
        if (l->n == l->room) {
            l->room = l->room ? l->room * 2 : 512;
            struct entry *e = realloc(l->e, (size_t)l->room * sizeof *e);
            if (!e)
                break;
            l->e = e;
        }
        struct entry *e = &l->e[l->n++];
        for (size_t i = 0; i <= strlen(de->d_name); i++)
            e->name[i] = (char)(de->d_name[i] >= 'a' && de->d_name[i] <= 'z' ? de->d_name[i] - 32 : de->d_name[i]);
        snprintf(e->path, sizeof e->path, "%s", path);
    }
    closedir(d);
}

static const char *find(const struct listing *l, const char *name)
{
    for (int i = 0; i < l->n; i++)
        if (!strcasecmp(l->e[i].name, name))
            return l->e[i].path;
    return NULL;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(((const struct entry *)a)->name, ((const struct entry *)b)->name);
}

// a file's bytes, PRS-decoded where its name ends .PRS
static int load_decoded(const char *path, struct buf *out)
{
    struct buf raw;
    if (load(path, &raw))
        return -1;
    int r = prs_decode(&raw, out);
    buf_free(&raw);
    return r;
}

// --- cutscene subtitles (common.language_tables, english_lines) ---

#define EVENT_BASE 0x0cb80000u

static const u8 *string_at(const struct buf *d, u32 ptr, size_t *len)
{
    if (!ptr || ptr < EVENT_BASE || ptr - EVENT_BASE >= d->n)
        return NULL;
    const u8 *s = d->p + (ptr - EVENT_BASE);
    const u8 *e = memchr(s, 0, d->n - (ptr - EVENT_BASE));
    if (!e)
        return NULL;
    *len = (size_t)(e - s);
    return s;
}

// the English strings (slot first .. first+count-1, empty slots left out) of the table-th
// language table; gives how many
static int event_english(const struct buf *d, int table, int first, int count, const u8 **parts, size_t *lens,
                         int most)
{
    if (d->n < 20)
        return 0;
    size_t words = d->n / 4;
#define W(k) le32(d->p + 4 * (k))
#define INSIDE(x) ((x) >= EVENT_BASE && (x) < EVENT_BASE + d->n)
    int found = 0;
    for (size_t k = 0; k + 4 < words; k++) {
        u32 w[5];
        int ok = 1;
        for (int i = 0; i < 5; i++) {
            w[i] = W(k + i);
            if (!INSIDE(w[i]))
                ok = 0;
        }
        if (!ok)
            continue;
        long stride = (long)w[1] - (long)w[0];
        if (stride <= 0 || stride % 4)
            continue;
        for (int i = 0; i < 4; i++)
            if ((long)w[i + 1] - (long)w[i] != stride)
                ok = 0;
        if (!ok)
            continue;
        long n = stride / 4;
        int any_english = 0;
        for (int a = 0; a < 5 && ok; a++)
            for (long i = 0; i < n; i++) {
                size_t at = w[a] - EVENT_BASE + 4 * (size_t)i;
                if (at + 4 > d->n) {
                    ok = 0;
                    break;
                }
                u32 p = le32(d->p + at);
                if (p && !INSIDE(p)) {
                    ok = 0;
                    break;
                }
                if (a == 1 && p)
                    any_english = 1;
            }
        if (!ok || !any_english)
            continue;
        if (found++ != table)
            continue;
        int np = 0;
        for (long i = first; i < first + count && i < n && np < most; i++) {
            u32 p = le32(d->p + (w[1] - EVENT_BASE) + 4 * (size_t)i);
            size_t len;
            const u8 *s = string_at(d, p, &len);
            if (s) {
                parts[np] = s;
                lens[np++] = len;
            }
        }
        return np;
    }
#undef W
#undef INSIDE
    return 0;
}

// make_english.py's escape
static int put_escaped(struct buf *b, const u8 *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        u8 c = s[i];
        char t[8];
        if (c == 0x5c)
            strcpy(t, "\\\\");
        else if (c == 10)
            strcpy(t, "\\n");
        else if (c == 9)
            strcpy(t, "\\t");
        else if (c == 7)
            strcpy(t, "\\a");
        else if (c < 0x20 || c >= 0x7f)
            snprintf(t, sizeof t, "\\x%02x", c);
        else {
            t[0] = (char)c;
            t[1] = 0;
        }
        if (buf_put(b, t, strlen(t)))
            return -1;
    }
    return 0;
}

// --- stage hints (common.hint_runs, hint_blocks, hint_english) ---

// where the module loads: a stage or area at 0x0c900000, a garden or the Chao race at
// 0x0cb80000 (above AL_MAIN, the code they share)
static u32 MODULE_BASE = 0x0c900000u;

static u32 module_base(const char *module)
{
    return strncasecmp(module, "al_garden", 9) == 0 || strncasecmp(module, "al_race", 7) == 0
               ? 0x0cb80000u
               : 0x0c900000u;
}
#define MOST_RUNS (1 << 17)
#define MOST_PAGE_REFS (1 << 19)
#define MOST_PAGES 64

// a run's pages lie in the pool, from its first
struct page_ref {
    const u8 *text;
    size_t len;
};
static struct page_ref *pool;
static int pool_used;

struct run {
    size_t at;
    int pages, first;
};
#define RTEXT(r, i) (pool[(r)->first + (i)].text)
#define RLEN(r, i) (pool[(r)->first + (i)].len)

static const u8 *module_text(const struct buf *d, u32 p, size_t *len)
{
    if (p < MODULE_BASE || p - MODULE_BASE >= d->n)
        return NULL;
    size_t o = p - MODULE_BASE;
    const u8 *e = memchr(d->p + o, 0, d->n - o);
    if (!e || e == d->p + o)
        return NULL;
    *len = (size_t)(e - (d->p + o));
    return d->p + o;
}

static int is_alpha(u8 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static int hint_runs(const struct buf *d, struct run *runs)
{
    int n = 0;
    size_t i = 0;
    pool_used = 0;
    while (i + 8 <= d->n && n < MOST_RUNS) {
        struct run r = {.at = i, .first = pool_used};
        size_t k = i;
        int too_many = 0;
        while (k + 8 <= d->n) {
            u32 p = le32(d->p + k), v = le32(d->p + k + 4);
            if (p < MODULE_BASE || p >= MODULE_BASE + d->n || v > 0xffff)
                break;
            size_t len;
            const u8 *t = module_text(d, p, &len);
            if (!t || len < 3 || (t[0] != 7 && t[0] != 9 && t[0] != 0x81 && t[0] != 0x82 && t[0] != 0x83 && !is_alpha(t[0])))
                break;
            if (r.pages == MOST_PAGES || pool_used + r.pages >= MOST_PAGE_REFS) {
                too_many = 1;
                break;
            }
            pool[r.first + r.pages].text = t;
            pool[r.first + r.pages].len = len;
            r.pages++;
            k += 8;
        }
        if (!too_many && r.pages && k + 8 <= d->n && !le32(d->p + k) && !le32(d->p + k + 4)) {
            pool_used += r.pages;
            runs[n++] = r;
            i = k + 8;
        } else {
            i += 4;
        }
    }
    return n;
}

static int is_japanese(const u8 *t, size_t n)
{
    if (n <= 4 || (t[0] != 7 && t[0] != 9))
        return 0;
    size_t hi = 0;
    for (size_t i = 0; i < n; i++)
        hi += t[i] >= 0x81;
    return hi > n / 3;
}

static int run_japanese(const struct run *r)
{
    for (int i = 0; i < r->pages; i++)
        if (!is_japanese(RTEXT(r, i), RLEN(r, i)))
            return 0;
    return 1;
}

static size_t run_end(const struct run *r) { return r->at + 8 * ((size_t)r->pages + 1); }

struct block {
    int j, e, n; // the Japanese runs' first index, the English runs', how many
};

static int hint_blocks(const struct run *rs, int nr, struct block *out, int most)
{
    int nb = 0, i = 0;
    while (i < nr && nb < most) {
        if (!run_japanese(&rs[i])) {
            i++;
            continue;
        }
        int j = i + 1;
        while (j < nr && rs[j].at == run_end(&rs[j - 1]) && run_japanese(&rs[j]))
            j++;
        int n = j - i;
        int ok = j + n <= nr && rs[j].at == run_end(&rs[j - 1]);
        for (int k = 1; ok && k < n; k++)
            if (rs[j + k].at != run_end(&rs[j + k - 1]))
                ok = 0;
        for (int k = 0; ok && k < n; k++)
            for (int p = 0; p < rs[j + k].pages; p++)
                if (is_japanese(RTEXT(&rs[j + k], p), RLEN(&rs[j + k], p)))
                    ok = 0;
        if (ok)
            out[nb++] = (struct block){i, j, n};
        i = j;
    }
    return nb;
}

// the English parts of page p of run r of block b; how many, 0 for none
static int hint_english(const struct run *rs, const struct block *bl, int nb, int b, int r, int p, const u8 **parts,
                        size_t *lens, int most)
{
    if (b < 0 || b >= nb || r < 0 || r >= bl[b].n)
        return 0;
    const struct run *jr = &rs[bl[b].j + r], *er = &rs[bl[b].e + r];
    if (p < 0 || p >= jr->pages)
        return 0;
    int from, to;
    if (jr->pages == er->pages)
        from = p, to = p + 1;
    else if (jr->pages == 1)
        from = 0, to = er->pages;
    else if (er->pages > jr->pages)
        from = p, to = p < jr->pages - 1 ? p + 1 : er->pages;
    else
        return 0;
    int n = 0;
    for (int k = from; k < to && n < most; k++) {
        parts[n] = RTEXT(er, k);
        lens[n++] = RLEN(er, k);
    }
    return n;
}

// --- files: textures, sound units, field messages (common.swap_textures, swap_banks,
// messages_rebased) ---

struct chunk {
    size_t at, n;
};

// the PVRT chunks from start: GBIX, PVRT and PVPL walked in order (common.chunks)
static int pvrt_chunks(const u8 *d, size_t size, size_t start, struct chunk *out, int most)
{
    int n = 0;
    size_t pos = start;
    while (pos + 8 <= size) {
        const u8 *tag = d + pos;
        if (memcmp(tag, "GBIX", 4) && memcmp(tag, "PVRT", 4) && memcmp(tag, "PVPL", 4)) {
            size_t k = (size_t)-1;
            for (size_t s = pos; s + 4 <= size; s++)
                if (!memcmp(d + s, "GBIX", 4) || !memcmp(d + s, "PVRT", 4)) {
                    k = s;
                    break;
                }
            if (k == (size_t)-1)
                break;
            pos = k;
            continue;
        }
        size_t len = 8 + (size_t)le32(d + pos + 4);
        if (!memcmp(tag, "PVRT", 4) && n < most)
            out[n++] = (struct chunk){pos, len};
        pos += len;
    }
    return n;
}

static int swap_textures(const struct buf *jp, size_t sj, const struct buf *us, size_t su, struct buf *out, int *swapped,
                         int *total)
{
    static struct chunk cj[1024], cu[1024];
    int nj = pvrt_chunks(jp->p, jp->n, sj, cj, 1024), nu = pvrt_chunks(us->p, us->n, su, cu, 1024);
    memset(out, 0, sizeof *out);
    size_t last = 0;
    *swapped = 0;
    *total = nj;
    for (int i = 0; i < nj; i++) {
        size_t at = cj[i].at, n = cj[i].n;
        if (at + 16 > jp->n || at + n > jp->n)
            return -1;
        if (buf_put(out, jp->p + last, at - last))
            return -1;
        if (i < nu && cu[i].at + 16 <= us->n && cu[i].at + cu[i].n <= us->n &&
            !memcmp(jp->p + at + 12, us->p + cu[i].at + 12, 4)) {
            if (buf_put(out, us->p + cu[i].at, cu[i].n))
                return -1;
            (*swapped)++;
        } else if (buf_put(out, jp->p + at, n)) {
            return -1;
        }
        last = at + n;
    }
    return buf_put(out, jp->p + last, jp->n - last);
}

static int swap_banks(const struct buf *jp, const struct buf *us, struct buf *out, int *swapped, int *total)
{
    if (jp->n < 0x20 || us->n < 0x20)
        return -1;
    int nj = (int)le32(jp->p + 8), nu = (int)le32(us->p + 8);
    if (nj < 0 || nu < 0 || 0x20 + 0x20 * (size_t)nj > jp->n || 0x20 + 0x20 * (size_t)nu > us->n)
        return -1;
    memset(out, 0, sizeof *out);
    if (buf_put(out, jp->p, 0x20 + 0x20 * (size_t)nj))
        return -1;
    *swapped = 0;
    *total = 0;
    for (int i = 0; i < nj; i++) {
        const u8 *b = jp->p + 0x20 + 0x20 * i;
        int32_t bdata = (int32_t)le32(b + 16), blen = (int32_t)le32(b + 20);
        if (blen > 0)
            (*total)++;
        const u8 *u = NULL;
        for (int k = 0; k < nu; k++) {
            const u8 *c = us->p + 0x20 + 0x20 * k;
            if (!memcmp(c, b, 16)) {
                u = c;
                break;
            }
        }
        const u8 *blob = NULL;
        size_t n = 0;
        if (u && (int32_t)le32(u + 20) > 0 && blen > 0) {
            size_t at = le32(u + 16), len = le32(u + 20);
            if (at + len > us->n)
                return -1;
            blob = us->p + at;
            n = len;
            (*swapped)++;
        } else if (blen > 0) {
            if ((size_t)bdata + (size_t)blen > jp->n)
                return -1;
            blob = jp->p + bdata;
            n = (size_t)blen;
        }
        u8 *rec = out->p + 0x20 + 0x20 * i;
        if (blob) {
            put_le32(rec + 16, (u32)out->n);
            put_le32(rec + 20, (u32)n);
            if (buf_put(out, blob, n))
                return -1;
            rec = out->p + 0x20 + 0x20 * i;
            static const u8 zeros[32];
            if (buf_put(out, zeros, (32 - out->n % 32) % 32))
                return -1;
        } else {
            put_le32(rec + 16, (u32)bdata);
            put_le32(rec + 20, (u32)blen);
        }
    }
    return 0;
}

static short voice_of(int us)
{
    for (int i = 0; i < english_voice_rows; i++)
        if (english_voice_map[i][1] == us)
            return english_voice_map[i][0];
    return (short)(0x2000 + us);
}

static int s16(const u8 *p) { return (short)(p[0] | p[1] << 8); }

static int messages_rebased(const struct buf *us, u32 jp_base, struct buf *out)
{
    memset(out, 0, sizeof *out);
    if (us->n < 8 || buf_put(out, us->p, us->n))
        return -1;
    u8 *d = out->p;
    size_t n = out->n;
    u32 lo = 0xffffffffu;
    for (size_t k = 0; k + 4 <= n; k += 4) {
        u32 w = le32(d + k);
        if (w >= 0x0c000000u && w < 0x0d000000u && w < lo)
            lo = w;
    }
    if (lo == 0xffffffffu)
        return -1;
    u32 us_base = lo & ~0xfffu;
    long delta = (long)jp_base - (long)us_base;
#define IN(w) ((w) >= us_base && (w) < us_base + n)
    // the scripts, through the table, before anything moves
    u32 table = le32(d + 4);
    size_t scripts[4096];
    int ns = 0;
    long at = (long)table - (long)us_base;
    while (at >= 0 && (size_t)at + 8 <= n) {
        u32 s = le32(d + at), r = le32(d + at + 4);
        if (!((s == 0 || IN(s)) && (r == 0 || IN(r))) || (s == 0 && r == 0))
            break;
        if (s && ns < 4096) {
            size_t off = s - us_base;
            int seen = 0;
            for (int i = 0; i < ns; i++)
                if (scripts[i] == off)
                    seen = 1;
            if (!seen)
                scripts[ns++] = off;
        }
        at += 8;
    }
    for (int i = 0; i < ns; i++) {
        size_t k = scripts[i];
        for (int step = 0; step < 4096 && k + 2 <= n; step++) {
            int w = s16(d + k);
            if (w == -1)
                break;
            if (w == -4) {
                if (k + 4 > n)
                    break;
                int v = s16(d + k + 2);
                if (v >= 0) {
                    short j = voice_of(v);
                    d[k + 2] = (u8)j;
                    d[k + 3] = (u8)((unsigned short)j >> 8);
                }
                k += 4;
            } else if (w == -3 || w == -5) {
                k += 4;
            } else if (w == -6 || w == -7) {
                k += 2;
                while (k + 2 <= n && s16(d + k) >= 0)
                    k += 2;
            } else {
                k += 2;
            }
        }
    }
    for (size_t k = 0; k + 4 <= n; k += 4) {
        u32 w = le32(d + k);
        if (IN(w))
            put_le32(d + k, (u32)((long)w + delta));
    }
#undef IN
    return 0;
}

// --- the folder ---

static const char *KEEP_JAPANESE[] = {"AVA_GTITLE0.PRS"};

static int copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b = a ? fopen(to, "wb") : NULL;
    if (!a || !b) {
        if (a)
            fclose(a);
        return -1;
    }
    static u8 chunk[1 << 20];
    size_t n;
    int ok = 1;
    while ((n = fread(chunk, 1, sizeof chunk, a)) > 0)
        if (fwrite(chunk, 1, n, b) != n) {
            ok = 0;
            break;
        }
    fclose(a);
    ok = !fclose(b) && ok;
    return ok ? 0 : -1;
}

static int write_subtitles(const struct listing *us, const char *out)
{
    struct buf txt = {0};
    buf_text(&txt, "# SonicAdventureRe-nx English subtitles, made by tools/english/make_english.py\n");
    char loaded_name[16] = "";
    struct buf mod = {0};
    int have = 0, written = 0;
    for (int i = 0; i < english_subtitle_rows; i++) {
        const struct subtitle_row *r = &english_subtitle_map[i];
        if (strcmp(loaded_name, r->event)) {
            buf_free(&mod);
            snprintf(loaded_name, sizeof loaded_name, "%s", r->event);
            char name[32];
            snprintf(name, sizeof name, "%s.PRS", r->event);
            const char *path = find(us, name);
            have = path && !load_decoded(path, &mod);
        }
        if (!have)
            continue;
        const u8 *parts[8];
        size_t lens[8];
        int np = event_english(&mod, r->table, r->first, r->count, parts, lens, 8);
        if (!np)
            continue;
        buf_text(&txt, "%s\t%08x", r->event, r->crc);
        for (int k = 0; k < np; k++) {
            buf_put(&txt, "\t", 1);
            put_escaped(&txt, parts[k], lens[k]);
        }
        buf_put(&txt, "\n", 1);
        written++;
    }
    buf_free(&mod);
    char path[300];
    snprintf(path, sizeof path, "%s/subtitles.txt", out);
    int r = save(path, txt.p, txt.n);
    buf_free(&txt);
    say("  subtitles: %d\n", written);
    return r;
}

static int write_hints(const struct listing *us, const char *out)
{
    struct buf txt = {0};
    buf_text(&txt, "# SonicAdventureRe-nx English stage hints, made by tools/english/make_english.py\n");
    struct run *runs = malloc(MOST_RUNS * sizeof *runs);
    pool = malloc(MOST_PAGE_REFS * sizeof *pool);
    static struct block blocks[256];
    if (!runs || !pool) {
        free(runs);
        free(pool);
        pool = NULL;
        return -1;
    }
    char loaded_name[16] = "";
    struct buf mod = {0};
    int have = 0, nr = 0, nb = 0, written = 0;
    for (int i = 0; i < english_hint_rows; i++) {
        const struct hint_row *r = &english_hint_map[i];
        if (strcmp(loaded_name, r->module)) {
            buf_free(&mod);
            snprintf(loaded_name, sizeof loaded_name, "%s", r->module);
            char name[32];
            snprintf(name, sizeof name, "%s.PRS", r->module);
            const char *path = find(us, name);
            have = path && !load_decoded(path, &mod);
            MODULE_BASE = module_base(r->module);
            nr = have ? hint_runs(&mod, runs) : 0;
            nb = have ? hint_blocks(runs, nr, blocks, 256) : 0;
        }
        if (!have)
            continue;
        const u8 *parts[MOST_PAGES];
        size_t lens[MOST_PAGES];
        int np = hint_english(runs, blocks, nb, r->block, r->run, r->page, parts, lens, MOST_PAGES);
        if (!np)
            continue;
        buf_text(&txt, "%s\t%08x", r->module, r->crc);
        for (int k = 0; k < np; k++) {
            buf_put(&txt, "\t", 1);
            put_escaped(&txt, parts[k], lens[k]);
        }
        buf_put(&txt, "\n", 1);
        written++;
    }
    buf_free(&mod);
    free(runs);
    free(pool);
    pool = NULL;
    char path[300];
    snprintf(path, sizeof path, "%s/hints.txt", out);
    int r = save(path, txt.p, txt.n);
    buf_free(&txt);
    say("  hints: %d\n", written);
    return r;
}

static int write_files(const struct listing *us, const struct listing *jp, const char *out)
{
    char dir[300];
    snprintf(dir, sizeof dir, "%s/SONICADV", out);
    mkdir(dir, 0777);
    struct buf list = {0};
    buf_text(&list, "# SonicAdventureRe-nx: the files read from this folder in place of the disc's\n");
    int text_files = 0, voice_files = 0, text_n = 0, text_total = 0, voice_n = 0, voice_total = 0;
    // in the order of the US names, as make_english.py's sorted()
    struct entry *sorted = malloc((size_t)(us->n ? us->n : 1) * sizeof *sorted);
    if (!sorted)
        return -1;
    memcpy(sorted, us->e, (size_t)us->n * sizeof *sorted);
    qsort(sorted, (size_t)us->n, sizeof *sorted, by_name);
    for (int i = 0; i < us->n; i++) {
        const char *name = sorted[i].name;
        if (i && !strcmp(name, sorted[i - 1].name))
            continue;
        const char *mark = NULL;
        for (const char *p = name; (p = strstr(p, "_E.")); p++)
            mark = p;
        if (!mark)
            continue;
        const char *ext = mark + 3;
        char base[48];
        snprintf(base, sizeof base, "%.*s.%s", (int)(mark - name), name, ext);
        if (strcmp(ext, "PRS") && strcmp(ext, "PVR") && strcmp(ext, "MLT") && strcmp(ext, "BIN"))
            continue;
        const char *jp_path = find(jp, base);
        if (!jp_path)
            continue;
        int keep = 0;
        for (size_t k = 0; k < sizeof KEEP_JAPANESE / sizeof *KEEP_JAPANESE; k++)
            if (!strcmp(base, KEEP_JAPANESE[k]))
                keep = 1;
        if (keep)
            continue;
        int voices = !strcmp(ext, "MLT");
        if (!strcmp(ext, "BIN") && !strstr(base, "MES"))
            continue;
        struct buf a, b, made = {0}, data = {0};
        if (load(jp_path, &a))
            continue;
        if (load(sorted[i].path, &b)) {
            buf_free(&a);
            continue;
        }
        int n = 0, total = 0, ok = 0;
        if (!strcmp(ext, "PRS")) {
            struct buf ja, ua;
            if (!prs_decode(&a, &ja)) {
                if (!prs_decode(&b, &ua)) {
                    if (ja.n > 8 && ua.n > 8 && !memcmp(ja.p, "PVMH", 4) && !memcmp(ua.p, "PVMH", 4) &&
                        !swap_textures(&ja, 8 + le32(ja.p + 4), &ua, 8 + le32(ua.p + 4), &made, &n, &total) &&
                        !prs_store(made.p, made.n, &data))
                        ok = 1;
                    buf_free(&ua);
                }
                buf_free(&ja);
            }
        } else if (!strcmp(ext, "PVR")) {
            if (!swap_textures(&a, 0, &b, 0, &data, &n, &total))
                ok = 1;
        } else if (voices) {
            if (a.n > 4 && b.n > 4 && !memcmp(a.p, "SMLT", 4) && !memcmp(b.p, "SMLT", 4) &&
                !swap_banks(&a, &b, &data, &n, &total) && n == total)
                ok = 1;
        } else {
            u32 lo = 0xffffffffu;
            for (size_t k = 0; k + 4 <= a.n; k += 4) {
                u32 w = le32(a.p + k);
                if (w >= 0x0c000000u && w < 0x0d000000u && w < lo)
                    lo = w;
            }
            if (lo != 0xffffffffu && !messages_rebased(&b, lo & ~0xfffu, &data))
                ok = 1, n = total = 1;
        }
        buf_free(&a);
        buf_free(&b);
        buf_free(&made);
        if (ok) {
            if (voices) {
                voice_n += n;
                voice_total += total;
            } else {
                text_n += n;
                text_total += total;
            }
        }
        if (ok && n) {
            char path[400];
            snprintf(path, sizeof path, "%s/%s", dir, base);
            if (!save(path, data.p, data.n)) {
                buf_text(&list, "%s %s\n", voices ? "voices" : "text", base);
                voices ? voice_files++ : text_files++;
            }
        }
        buf_free(&data);
    }
    free(sorted);
    char path[300];
    snprintf(path, sizeof path, "%s/files.txt", out);
    int r = save(path, list.p, list.n);
    buf_free(&list);
    say("  menus and messages: %d files (%d of %d pieces in English); voice banks: %d files (%d of %d)\n", text_files,
        text_n, text_total, voice_files, voice_n, voice_total);
    return r;
}

int english_build(const char *us_dir, const char *jp_dir, const char *out, english_say_fn say_fn)
{
    say = say_fn;
    struct listing us = {0}, jp = {0};
    list_files(us_dir, &us, 0);
    list_files(jp_dir, &jp, 0);
    const char *afs = find(&us, "EVENT_ADX_US.AFS");
    if (!afs) {
        say("no EVENT_ADX_US.AFS in %s: is it the US disc's files?\n", us_dir);
        free(us.e);
        free(jp.e);
        return -1;
    }
    mkdir(out, 0777);
    say("Making the English layer from your US disc (once):\n");
    char path[300];
    snprintf(path, sizeof path, "%s/EVENT_ADX_US.AFS", out);
    if (copy_file(afs, path)) {
        say("couldn't copy the US voices to %s\n", path);
        free(us.e);
        free(jp.e);
        return -1;
    }
    say("  cutscene voices copied\n");
    struct buf vm = {0};
    buf_text(&vm, "# SonicAdventureRe-nx: Japanese EVENT_ADX.AFS index, EVENT_ADX_US.AFS index\n");
    for (int i = 0; i < english_voice_rows; i++)
        buf_text(&vm, "%d %d\n", english_voice_map[i][0], english_voice_map[i][1]);
    snprintf(path, sizeof path, "%s/voice_map.txt", out);
    int r = save(path, vm.p, vm.n);
    buf_free(&vm);
    r = r || write_subtitles(&us, out);
    r = r || write_hints(&us, out);
    r = r || write_files(&us, &jp, out);
    if (!r) {
        struct buf stamp = {0};
        buf_text(&stamp, "%08x\n", english_maps_crc);
        snprintf(path, sizeof path, "%s/made.txt", out);
        r = save(path, stamp.p, stamp.n);
        buf_free(&stamp);
    }
    free(us.e);
    free(jp.e);
    return r;
}

// whether out holds a folder these maps made
int english_current(const char *out)
{
    char path[300], line[32] = "";
    snprintf(path, sizeof path, "%s/made.txt", out);
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    int ok = fgets(line, sizeof line, f) && strtoul(line, NULL, 16) == english_maps_crc;
    fclose(f);
    return ok;
}
