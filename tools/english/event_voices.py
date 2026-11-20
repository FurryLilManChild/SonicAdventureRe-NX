#!/usr/bin/env python3
"""Map the Japanese disc's cutscene voice clips to the US disc's English ones (SonicAdventureRe-nx).

The Japanese release's EVENT_ADX.AFS holds 2,088 clips. The US disc holds two archives of 2,044
clips in the same order: EVENT_ADX.AFS (its Japanese track) and EVENT_ADX_US.AFS (English). So a
Japanese clip that also appears in the US Japanese archive has its English line at the same index
of EVENT_ADX_US.AFS. The two Japanese archives are not in the same order, so each clip is found:

  1. by its sample count, where only one US clip has it;
  2. where several do, by the shape of its first second of sound (an energy envelope);
  3. failing both, by envelope alone among US clips of about the same length.

Clips with no match are lines the US release rewrote or cut; they keep the Japanese voice.

  event_voices.py JP_EVENT_ADX.AFS US_EVENT_ADX.AFS OUT.json

OUT.json maps a Japanese clip index to a US clip index (both as numbers).
Measured on HDR-0001 v1.007 against the US disc: 1,869 of 2,088 clips mapped; 80 spot checks by
sound all agreed.
"""
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import adx  # noqa: E402


def clip(data, entries, i):
    off, size = entries[i]
    return data[off:off + size]


def envelope(data, seconds):
    h = adx.header(data)
    s = adx.decode(data, int(h[1] * seconds))
    step = max(1, h[1] // 20)
    return [sum(abs(x) for x in s[k:k + step]) // step for k in range(0, len(s) - step + 1, step)]


def distance(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / (sum(a) + 1)


def main():
    jp_path, us_path, out_path = sys.argv[1:4]
    jp = open(jp_path, 'rb').read()
    us = open(us_path, 'rb').read()
    ej, eu = adx.afs_entries(jp), adx.afs_entries(us)
    hj = [adx.header(clip(jp, ej, i)) for i in range(len(ej))]
    hu = [adx.header(clip(us, eu, i)) for i in range(len(eu))]
    by_samples = collections.defaultdict(list)
    for i, h in enumerate(hu):
        by_samples[h[2]].append(i)

    env_us = {}

    def us_env(k, seconds):
        if (k, seconds) not in env_us:
            env_us[(k, seconds)] = envelope(clip(us, eu, k), seconds)
        return env_us[(k, seconds)]

    mapping = {}
    for i, h in enumerate(hj):
        same = by_samples.get(h[2], [])
        if len(same) == 1:
            mapping[i] = same[0]
        elif same:
            mine = envelope(clip(jp, ej, i), 0.2)
            mapping[i] = min(same, key=lambda k: distance(mine, us_env(k, 0.2)))
    for i, h in enumerate(hj):
        if i in mapping:
            continue
        mine = envelope(clip(jp, ej, i), 2)
        secs = h[2] / h[1]
        near = [k for k, u in enumerate(hu) if abs(u[2] / u[1] - secs) < 0.15 * secs + 0.1]
        best = min(near, key=lambda k: distance(mine, us_env(k, 2)), default=None)
        if best is not None and distance(mine, us_env(best, 2)) < 0.15:
            mapping[i] = best
    json.dump({str(k): v for k, v in sorted(mapping.items())}, open(out_path, 'w'), indent=0)
    print(f'{len(mapping)} of {len(hj)} Japanese clips mapped to a US clip')


if __name__ == '__main__':
    main()
