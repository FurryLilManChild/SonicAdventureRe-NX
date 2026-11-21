// SonicAdventureRe-nx: the English layer. The Japanese game, with English from the player's own
// US disc, which tools/english/make_english.py turns into sdmc:/switch/SonicAdventureRe-nx/english/:
//
//   EVENT_ADX_US.AFS  the US cutscene voices in English
//   voice_map.txt     which English clip each Japanese clip plays (Japanese index, US index)
//   subtitles.txt     each cutscene subtitle in English: cutscene, CRC-32 of the Japanese line,
//                     then the English parts, tab-separated
//   files.txt         the Japanese files this folder holds English versions of, under the same
//                     names: menus, HUD, results and mission textures, and the fields' messages
//                     ("text NAME"); later the sound units ("voices NAME")
//
// A field's messages name their voices as the Japanese game plays them; a voice the Japanese game
// has no clip for is 0x2000 plus its US number, which plays only with English voices.
//
// The decomp calls three hooks (patch 0011), which the link wraps (switch/build.sh):
//   native_hook_voice_file      a cutscene voice: the English clip in the Japanese one's place
//   native_hook_event_text      a subtitle: the English line in the Japanese one's place
//   native_hook_event_text_next where the US split a line in parts, the next part once its share
//                               of the voice has played (event_wait polls it each step)
// and loader_read_prs is wrapped to know which cutscene is loaded (ev%04x.prs): the same Japanese
// line can read differently in English from one cutscene to another.
//
// Voices and text are settings of their own (settings.txt "voices" and "text", the port menu's
// VOICES and TEXT), English by default where the folder holds the files. A clip or a line with no
// English stays Japanese.
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#ifndef ENGLISH
#define ENGLISH "sdmc:/switch/SonicAdventureRe-nx/english"
#endif
#define MOST_PARTS 4

int nx_voices_english = 1; // settings.txt "voices english|japanese"
int nx_text_english = 1;   // settings.txt "text english|japanese"

// --- what the folder holds, read once ---

static int loaded, have_voices, have_text;

// the English voices: the archive's table, and each Japanese clip's English one (-1: none)
static FILE *voices;
static uint32_t *clip_offset, *clip_size;
static uint32_t clips;
static int16_t voice_map[4096];
static uint8_t *clip;
static size_t clip_room;

// the subtitles, sorted by cutscene and CRC
struct line {
    uint32_t key; // cutscene number << 32 would not fit: the CRC alone, with event beside it
    int32_t event;
    int parts;
    char *part[MOST_PARTS];
};
static struct line *lines;
static int line_count;

// the files read from this folder in place of the disc's
struct file {
    char name[40];
    int voices; // 1 for a sound unit (the VOICES setting), 0 for text
};
static struct file *swaps;
static int swap_count;

static uint32_t crc_table[256];

static uint32_t crc32_of(const uint8_t *p, size_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
    uint32_t c = 0xffffffffu;
    while (n--)
        c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

static uint32_t little32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// one escaped part of subtitles.txt, in place: \n \t \a \\ \xNN
static char *unescape(char *s)
{
    char *out = s, *in = s;
    while (*in) {
        if (*in != '\\') {
            *out++ = *in++;
            continue;
        }
        in++;
        switch (*in) {
        case 'n': *out++ = '\n'; in++; break;
        case 't': *out++ = '\t'; in++; break;
        case 'a': *out++ = '\a'; in++; break;
        case 'x': {
            char hex[3] = {in[1], in[1] ? in[2] : 0, 0};
            *out++ = (char)strtoul(hex, NULL, 16);
            in += 3;
            break;
        }
        case 0: break;
        default: *out++ = *in++; break;
        }
    }
    *out = 0;
    return s;
}

static int by_key(const void *a, const void *b)
{
    const struct line *x = a, *y = b;
    if (x->key != y->key)
        return x->key < y->key ? -1 : 1;
    return x->event < y->event ? -1 : x->event > y->event;
}

static void load_voices(void)
{
    FILE *f = fopen(ENGLISH "/voice_map.txt", "r");
    memset(voice_map, 0xff, sizeof voice_map);
    int mapped = 0;
    if (f) {
        char row[64];
        while (fgets(row, sizeof row, f)) {
            if (row[0] == '#')
                continue;
            char *end;
            long jp = strtol(row, &end, 10), us = strtol(end, NULL, 10);
            if (end != row && jp >= 0 && jp < 4096 && us >= 0 && us < 32768) {
                voice_map[jp] = (int16_t)us;
                mapped++;
            }
        }
        fclose(f);
    }
    voices = fopen(ENGLISH "/EVENT_ADX_US.AFS", "rb");
    uint8_t head[8];
    if (voices && fread(head, 1, 8, voices) == 8 && !memcmp(head, "AFS", 3)) {
        clips = little32(head + 4);
        clip_offset = malloc(clips * sizeof *clip_offset);
        clip_size = malloc(clips * sizeof *clip_size);
        uint8_t *table = malloc((size_t)clips * 8);
        if (clip_offset && clip_size && table && fread(table, 8, clips, voices) == clips) {
            for (uint32_t i = 0; i < clips; i++) {
                clip_offset[i] = little32(table + 8 * i);
                clip_size[i] = little32(table + 8 * i + 4);
            }
            have_voices = mapped > 0;
        }
        free(table);
    }
    fprintf(stderr, "english: voices: %s (%d Japanese clips mapped, %u English clips)\n",
            have_voices ? "ready" : "NOT FOUND (english/EVENT_ADX_US.AFS and voice_map.txt)", mapped, clips);
}

static void load_text(void)
{
    FILE *f = fopen(ENGLISH "/subtitles.txt", "rb");
    if (!f) {
        fprintf(stderr, "english: subtitles: NOT FOUND (english/subtitles.txt)\n");
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    if (!text || fread(text, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(text);
        return;
    }
    fclose(f);
    text[size] = 0;
    int rows = 0;
    for (long i = 0; i < size; i++)
        rows += text[i] == '\n';
    lines = calloc((size_t)rows + 1, sizeof *lines);
    if (!lines)
        return;
    // each row: evNNNN \t crc \t part \t part ... (kept in text, which stays allocated)
    for (char *row = text, *next; row && *row; row = next) {
        next = strchr(row, '\n');
        if (next)
            *next++ = 0;
        size_t n = strlen(row);
        if (n && row[n - 1] == '\r')
            row[n - 1] = 0;
        if (row[0] == '#' || strncmp(row, "ev", 2))
            continue;
        struct line l = {0};
        char *field = row, *tab;
        int k = 0;
        while (field) {
            tab = strchr(field, '\t');
            if (tab)
                *tab++ = 0;
            if (k == 0)
                l.event = (int32_t)strtol(field + 2, NULL, 16);
            else if (k == 1)
                l.key = (uint32_t)strtoul(field, NULL, 16);
            else if (l.parts < MOST_PARTS)
                l.part[l.parts++] = unescape(field);
            k++;
            field = tab;
        }
        if (l.parts)
            lines[line_count++] = l;
    }
    qsort(lines, (size_t)line_count, sizeof *lines, by_key);
    have_text = line_count > 0;
    fprintf(stderr, "english: subtitles: %d lines\n", line_count);
}

static void load_files(void)
{
    FILE *f = fopen(ENGLISH "/files.txt", "r");
    if (!f) {
        fprintf(stderr, "english: files: none (english/files.txt)\n");
        return;
    }
    char row[96];
    int room = 0;
    while (fgets(row, sizeof row, f)) {
        // "kind NAME" (no sscanf: the game's C brings its own, which the link may pick)
        if (row[0] == '#')
            continue;
        row[strcspn(row, "\r\n")] = 0;
        char *space = strchr(row, ' ');
        if (!space)
            continue;
        *space = 0;
        const char *kind = row, *name = space + 1;
        if (!*name || strlen(name) >= sizeof swaps->name)
            continue;
        if (swap_count == room) {
            room = room ? room * 2 : 128;
            struct file *more = realloc(swaps, (size_t)room * sizeof *swaps);
            if (!more)
                break;
            swaps = more;
        }
        struct file *s = &swaps[swap_count++];
        snprintf(s->name, sizeof s->name, "%s", name);
        s->voices = !strcmp(kind, "voices");
    }
    fclose(f);
    fprintf(stderr, "english: files: %d\n", swap_count);
}

static void load(void)
{
    if (loaded)
        return;
    loaded = 1;
    load_voices();
    load_text();
    load_files();
}

// a disc file the game opens: the English one in its place, by the settings
int __wrap_native_hook_disc_file(const char *name, char *out, size_t size)
{
    if (!name || (!nx_text_english && !nx_voices_english))
        return 0;
    load();
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    for (int i = 0; i < swap_count; i++)
        if (!strcasecmp(swaps[i].name, base)) {
            if (swaps[i].voices ? !nx_voices_english : !nx_text_english)
                return 0;
            snprintf(out, size, "%s/%s", ENGLISH, swaps[i].name);
            static int told;
            if (told++ < 40)
                fprintf(stderr, "english: %s from the english folder\n", swaps[i].name);
            return 1;
        }
    return 0;
}

// whether the folder has each (for the menu and the settings' log)
int nx_english_has_voices(void)
{
    load();
    return have_voices;
}

int nx_english_has_text(void)
{
    load();
    return have_text;
}

// --- the cutscene loaded ---

static int32_t event_now = -1;

int __real_loader_read_prs(char *name, void *dest);
int __wrap_loader_read_prs(char *name, void *dest)
{
    if (name && tolower((unsigned char)name[0]) == 'e' && tolower((unsigned char)name[1]) == 'v' &&
        isxdigit((unsigned char)name[2]))
        event_now = (int32_t)strtol(name + 2, NULL, 16);
    return __real_loader_read_prs(name, dest);
}

// --- voices ---

static u64 voice_started;  // when the last English clip started
static double voice_seconds; // and how long it plays

int __wrap_native_hook_voice_file(const char *archive, int fid, const void **data, size_t *size)
{
    if (!nx_voices_english || !archive || fid < 0)
        return 0;
    size_t n = strlen(archive);
    if (n < 13 || strcasecmp(archive + n - 13, "event_adx.afs"))
        return 0;
    load();
    // 0x2000 and up: a US clip the Japanese game has none for (an English field message's voice)
    int us = fid >= 0x2000 ? fid - 0x2000 : fid < 4096 ? voice_map[fid] : -1;
    if (!have_voices || us < 0 || (uint32_t)us >= clips)
        return 0;
    size_t want = clip_size[us];
    if (want > clip_room) {
        uint8_t *more = realloc(clip, want);
        if (!more)
            return 0;
        clip = more;
        clip_room = want;
    }
    if (fseek(voices, (long)clip_offset[us], SEEK_SET) || fread(clip, 1, want, voices) != want)
        return 0;
    // ADX: big-endian sample rate at 8, sample count at 12
    if (want >= 16) {
        uint32_t rate = (uint32_t)clip[8] << 24 | (uint32_t)clip[9] << 16 | (uint32_t)clip[10] << 8 | clip[11];
        uint32_t samples = (uint32_t)clip[12] << 24 | (uint32_t)clip[13] << 16 | (uint32_t)clip[14] << 8 | clip[15];
        voice_seconds = rate ? (double)samples / rate : 0.0;
    }
    voice_started = armGetSystemTick();
    *data = clip;
    *size = want;
    return 1;
}

// --- subtitles ---

static const struct line *pending; // a line shown in parts, and its next part
static int pending_next;
static u64 shown_at;

static double since(u64 t)
{
    return (double)(armGetSystemTick() - t) / (double)armGetSystemTickFreq();
}

static const struct line *find(const char *text)
{
    uint32_t crc = crc32_of((const uint8_t *)text, strlen(text));
    struct line probe = {.key = crc, .event = event_now};
    const struct line *hit = bsearch(&probe, lines, (size_t)line_count, sizeof *lines, by_key);
    if (hit)
        return hit;
    // another cutscene's English for the same Japanese line
    for (int i = 0; i < line_count; i++)
        if (lines[i].key == crc)
            return &lines[i];
    return NULL;
}

const char *__wrap_native_hook_event_text(const char *text)
{
    pending = NULL;
    if (!text || !nx_text_english)
        return text;
    load();
    if (!have_text)
        return text;
    const struct line *l = find(text);
    if (!l)
        return text;
    if (l->parts > 1) {
        pending = l;
        pending_next = 1;
        shown_at = armGetSystemTick();
    }
    return l->part[0];
}

// a part's share of the line, by its letters
static size_t letters(const struct line *l, int upto)
{
    size_t n = 0;
    for (int i = 0; i < upto && i < l->parts; i++)
        n += strlen(l->part[i]);
    return n;
}

const char *__wrap_native_hook_event_text_next(void)
{
    if (!pending || pending_next >= pending->parts)
        return NULL;
    // the voice the line goes with: one that started about when it showed, else 3 s a part
    double whole = voice_seconds > 0.5 && voice_started && since(voice_started) < since(shown_at) + 1.5
                       ? voice_seconds
                       : 3.0 * pending->parts;
    double due = whole * (double)letters(pending, pending_next) / (double)(letters(pending, pending->parts) | 1);
    if (since(shown_at) < due)
        return NULL;
    const char *next = pending->part[pending_next++];
    if (pending_next >= pending->parts)
        pending = NULL;
    return next;
}
