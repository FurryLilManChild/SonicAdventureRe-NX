// SonicAdventureRe-nx: the English layer's folder, made from the player's US disc (english_build.c)
#ifndef ENGLISH_BUILD_H
#define ENGLISH_BUILD_H

typedef void (*english_say_fn)(const char *fmt, ...);

// us_dir: the US disc's files; jp_dir: the Japanese disc's; out: the english folder. 0 when made
int english_build(const char *us_dir, const char *jp_dir, const char *out, english_say_fn say);
// 1 where out holds a folder these maps made (its made.txt)
int english_current(const char *out);

#endif
