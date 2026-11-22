#ifndef SARE_DISC_EXTRACT_H
#define SARE_DISC_EXTRACT_H

typedef void (*disc_say_fn)(const char *fmt, ...);

// If dir holds a .gdi or .cue (and no extracted.txt), unpacks the disc's files into dir.
// what names the disc in messages ("Japanese", "US"). 0: done or nothing to do; -1: failed (said).
int disc_extract(const char *dir, const char *what, disc_say_fn say);

#endif
