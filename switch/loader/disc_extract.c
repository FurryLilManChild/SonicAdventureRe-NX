// SonicAdventureRe-nx: a GD-ROM image (.gdi or Redump .cue, raw 2352 or cooked 2048 sectors)
// unpacked into the folder it sits in, once.
//
// The game's files are an ISO9660 filesystem in the high-density area (the first data track at
// LBA 45000 or later). Retail discs record absolute LBAs there; a plain ISO's relative LBAs are
// handled too. Every file is written under the folder by its disc path (1ST_READ.BIN,
// SONICADV/...), then extracted.txt is written last, so an interrupted unpacking starts over.

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "disc_extract.h"

#define HD_LBA 45000
#define MOST_TRACKS 99
#define CHUNK_SECTORS 256

struct span {
    char path[512];
    uint32_t lba, sectors, sector_size;
    int data;
};

static struct span spans[MOST_TRACKS];
static int nspans;
static FILE *open_f;
static int open_i = -1;
static disc_say_fn say;
static uint8_t *chunk;

static int ends_with(const char *s, const char *ext)
{
    size_t a = strlen(s), b = strlen(ext);
    return a >= b && !strcasecmp(s + a - b, ext);
}

static long long file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) ? -1 : (long long)st.st_size;
}

// the next token of a line: a "quoted string" or a run of non-spaces
static const char *token(const char *p, char *out, size_t room)
{
    while (*p == ' ' || *p == '\t')
        p++;
    size_t n = 0;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            if (n + 1 < room)
                out[n++] = *p;
            p++;
        }
        if (*p == '"')
            p++;
    } else {
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            if (n + 1 < room)
                out[n++] = *p;
            p++;
        }
    }
    out[n] = 0;
    return p;
}

static int parse_gdi(const char *dir, const char *name)
{
    char path[512], line[1024], tok[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    nspans = 0;
    if (!fgets(line, sizeof line, f)) {
        fclose(f);
        return -1;
    }
    while (fgets(line, sizeof line, f) && nspans < MOST_TRACKS) {
        const char *p = line;
        unsigned num, lba, type, size;
        p = token(p, tok, sizeof tok);
        if (!*tok)
            continue;
        num = (unsigned)strtoul(tok, NULL, 10);
        p = token(p, tok, sizeof tok);
        lba = (unsigned)strtoul(tok, NULL, 10);
        p = token(p, tok, sizeof tok);
        type = (unsigned)strtoul(tok, NULL, 10);
        p = token(p, tok, sizeof tok);
        size = (unsigned)strtoul(tok, NULL, 10);
        p = token(p, tok, sizeof tok);
        (void)num;
        struct span *s = &spans[nspans];
        snprintf(s->path, sizeof s->path, "%s/%s", dir, tok);
        long long bytes = file_size(s->path);
        if (bytes < 0 || (size != 2352 && size != 2048)) {
            say("  the disc image's track file is missing: %s\n", tok);
            fclose(f);
            return -1;
        }
        s->lba = lba;
        s->sector_size = size;
        s->sectors = (uint32_t)(bytes / size);
        s->data = type == 4;
        nspans++;
    }
    fclose(f);
    return nspans ? 0 : -1;
}

// Redump's GD-ROM cues: one .bin a track, the high-density area from LBA 45000; each file's
// first sector sits at the running LBA (pregaps are inside the files)
static int parse_cue(const char *dir, const char *name)
{
    char path[512], line[1024], tok[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    nspans = 0;
    uint32_t cursor = 0;
    struct span *cur = NULL;
    while (fgets(line, sizeof line, f)) {
        const char *p = token(line, tok, sizeof tok);
        if (!strcasecmp(tok, "REM")) {
            if (strstr(line, "HIGH-DENSITY"))
                cursor = HD_LBA;
        } else if (!strcasecmp(tok, "FILE") && nspans < MOST_TRACKS) {
            token(p, tok, sizeof tok);
            cur = &spans[nspans++];
            snprintf(cur->path, sizeof cur->path, "%s/%s", dir, tok);
            cur->lba = cursor;
            cur->sector_size = 0;
            cur->data = 0;
        } else if (!strcasecmp(tok, "TRACK") && cur && !cur->sector_size) {
            p = token(p, tok, sizeof tok);
            token(p, tok, sizeof tok);
            if (!strncasecmp(tok, "MODE1/2048", 10)) {
                cur->sector_size = 2048;
                cur->data = 1;
            } else {
                cur->sector_size = 2352;
                cur->data = !strncasecmp(tok, "MODE", 4);
            }
            long long bytes = file_size(cur->path);
            if (bytes < 0) {
                say("  the disc image's track file is missing: %s\n", strrchr(cur->path, '/') + 1);
                fclose(f);
                return -1;
            }
            cur->sectors = (uint32_t)(bytes / cur->sector_size);
            cursor = cur->lba + cur->sectors;
        }
    }
    fclose(f);
    return nspans ? 0 : -1;
}

static int span_of(uint32_t lba)
{
    for (int i = 0; i < nspans; i++)
        if (spans[i].sector_size && lba >= spans[i].lba && lba < spans[i].lba + spans[i].sectors)
            return i;
    return -1;
}

// count sectors from lba (all in one track) into out, 2048 bytes each
static int read_sectors(uint32_t lba, uint32_t count, uint8_t *out)
{
    while (count) {
        int i = span_of(lba);
        if (i < 0)
            return -1;
        struct span *s = &spans[i];
        uint32_t n = s->lba + s->sectors - lba;
        if (n > count)
            n = count;
        if (n > CHUNK_SECTORS)
            n = CHUNK_SECTORS;
        if (open_i != i) {
            if (open_f)
                fclose(open_f);
            open_f = fopen(s->path, "rb");
            open_i = open_f ? i : -1;
            if (!open_f)
                return -1;
        }
        if (fseeko(open_f, (off_t)(lba - s->lba) * s->sector_size, SEEK_SET) ||
            fread(chunk, s->sector_size, n, open_f) != n)
            return -1;
        if (s->sector_size == 2048)
            memcpy(out, chunk, (size_t)n * 2048);
        else
            for (uint32_t k = 0; k < n; k++)
                memcpy(out + (size_t)k * 2048, chunk + (size_t)k * 2352 + 16, 2048);
        out += (size_t)n * 2048;
        lba += n;
        count -= n;
    }
    return 0;
}

static uint32_t le32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

struct dir_job {
    uint32_t lba, size;
    char path[256];
};

static int unpack(const char *dir, int *files, long long *bytes)
{
    int hd = -1;
    for (int i = 0; i < nspans && hd < 0; i++)
        if (spans[i].data && spans[i].lba >= HD_LBA)
            hd = i;
    for (int i = 0; i < nspans && hd < 0; i++)
        if (spans[i].data)
            hd = i;
    if (hd < 0) {
        say("  the disc image has no data track\n");
        return -1;
    }
    static uint8_t pvd[2048];
    if (read_sectors(spans[hd].lba + 16, 1, pvd) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5)) {
        say("  no game filesystem on the disc image (is it a Dreamcast disc?)\n");
        return -1;
    }
    uint32_t root_lba = le32(pvd + 156 + 2), root_size = le32(pvd + 156 + 10);
    uint32_t offset = root_lba >= spans[hd].lba ? 0 : spans[hd].lba;

    static struct dir_job stack[256];
    int top = 0;
    stack[top].lba = root_lba;
    stack[top].size = root_size;
    stack[top].path[0] = 0;
    top++;
    uint8_t *data = malloc((size_t)CHUNK_SECTORS * 2048);
    uint8_t *listing = NULL;
    if (!data)
        return -1;
    int rc = 0;
    while (top && !rc) {
        struct dir_job d = stack[--top];
        uint32_t sectors = (d.size + 2047) / 2048;
        free(listing);
        listing = malloc((size_t)sectors * 2048 + 1);
        if (!listing || read_sectors(d.lba + offset, sectors, listing)) {
            say("  can't read a folder of the disc image\n");
            rc = -1;
            break;
        }
        uint32_t pos = 0;
        while (pos < d.size && !rc) {
            uint8_t len = listing[pos];
            if (!len) {
                pos = (pos / 2048 + 1) * 2048;
                continue;
            }
            const uint8_t *r = listing + pos;
            pos += len;
            uint32_t lba = le32(r + 2), size = le32(r + 10);
            uint8_t flags = r[25], nlen = r[32];
            if (nlen == 1 && (r[33] == 0 || r[33] == 1))
                continue;
            char name[128];
            int n = 0;
            for (int k = 0; k < nlen && n < 127 && r[33 + k] != ';'; k++)
                name[n++] = (char)r[33 + k];
            if (n && name[n - 1] == '.')
                n--;
            name[n] = 0;
            char rel[256], full[600];
            snprintf(rel, sizeof rel, "%s%s%s", d.path, d.path[0] ? "/" : "", name);
            snprintf(full, sizeof full, "%s/%s", dir, rel);
            if (flags & 2) {
                mkdir(full, 0777);
                if (top < 256) {
                    stack[top].lba = lba;
                    stack[top].size = size;
                    snprintf(stack[top].path, sizeof stack[top].path, "%s", rel);
                    top++;
                }
                continue;
            }
            FILE *o = fopen(full, "wb");
            if (!o) {
                say("  can't write %s\n", full);
                rc = -1;
                break;
            }
            uint32_t left = size, at = lba + offset;
            while (left) {
                uint32_t want = left > CHUNK_SECTORS * 2048 ? CHUNK_SECTORS * 2048 : left;
                uint32_t secs = (want + 2047) / 2048;
                if (read_sectors(at, secs, data) || fwrite(data, 1, want, o) != want) {
                    say("  can't copy %s out of the disc image\n", rel);
                    rc = -1;
                    break;
                }
                at += secs;
                left -= want;
            }
            if (fclose(o))
                rc = -1;
            (*files)++;
            *bytes += size;
            if (*files % 200 == 0)
                say("  %d files, %lld MB\n", *files, *bytes >> 20);
        }
    }
    free(listing);
    free(data);
    return rc;
}

int disc_extract(const char *dir, const char *what, disc_say_fn say_fn)
{
    say = say_fn;
    char marker[600];
    snprintf(marker, sizeof marker, "%s/extracted.txt", dir);
    if (file_size(marker) >= 0)
        return 0;
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    char image[256] = "";
    int cue = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (ends_with(e->d_name, ".gdi") || (!image[0] && ends_with(e->d_name, ".cue"))) {
            snprintf(image, sizeof image, "%s", e->d_name);
            cue = ends_with(e->d_name, ".cue");
        }
    }
    closedir(d);
    if (!image[0])
        return 0;  // no image: the disc's files are there as they are, or nothing is
    say("Unpacking the %s disc image (%s), once:\n", what, image);
    chunk = malloc((size_t)CHUNK_SECTORS * 2352);
    if (!chunk)
        return -1;
    int rc = cue ? parse_cue(dir, image) : parse_gdi(dir, image);
    if (rc)
        say("  can't read %s\n", image);
    int files = 0;
    long long bytes = 0;
    if (!rc)
        rc = unpack(dir, &files, &bytes);
    if (open_f)
        fclose(open_f);
    open_f = NULL;
    open_i = -1;
    free(chunk);
    chunk = NULL;
    if (rc)
        return -1;
    FILE *m = fopen(marker, "w");
    if (m) {
        fprintf(m, "%s\n%d files, %lld bytes\n", image, files, bytes);
        fclose(m);
    }
    say("  %d files, %lld MB: done\n", files, bytes >> 20);
    return 0;
}
