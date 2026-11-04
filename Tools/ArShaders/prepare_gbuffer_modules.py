#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Aetheria lamp surfaces: prepare the G-buffer shader modules (<client>/VulkanShaders/gbuffer/{vertex,pixel}).

Mado's Azeroth Remastered release pack (WoW-HD-2026-10-28.zip) carries, for most shaders, a "gbuf" variant that writes the six colour endpoints of the
geometry replay (scene colour, albedo, normal, light, two shadow endpoints). They are written against Mado's register layout. This script keeps every
instruction as it is and only changes the uniform offsets (p20 @0, misc @320, v @336) and the vertex-register array length to the native 64/512/1024-row
class of OUR translated shader of the same key (the md5 of the original D3D9 shader).

Usage: prepare_gbuffer_modules.py --pack WoW-HD-2026-10-28.zip --manifest <client>/VulkanShaders/manifest.txt --out <client>/VulkanShaders/gbuffer
No GPU and no spirv tools needed. (Optional check: spirv-val --target-env vulkan1.1 on the results.)
"""
import argparse, hashlib, re, struct, sys, zipfile
from pathlib import Path

PREFIX = 'client/Vulkan/shaders/translated/'

def instructions(words):
    i = 5
    while i < len(words):
        n = words[i] >> 16
        if not n or i + n > len(words):
            raise ValueError('Malformed instruction size')
        yield words[i] & 65535, list(words[i + 1:i + n])
        i += n

def blocks(text):
    out = {}
    for b in re.split(r'(?=^record )', text, flags=re.M):
        m = re.match(r'record ([0-9a-f]{32}) (vertex|pixel) \S+ (\S+).*?vconst=(\d+)', b)
        if m:
            out[m[1]] = {'kind': m[2], 'path': m[3], 'vertex_rows': int(m[4]), 'block': b}
    return out

def adapt(raw, vertex_rows):
    if vertex_rows not in (64, 512, 1024):
        raise ValueError('Unsupported native row class')
    if len(raw) % 4:
        raise ValueError('Truncated module')
    words = list(struct.unpack('<%dI' % (len(raw) // 4), raw))
    if len(words) < 5 or words[0] != 0x07230203 or words[1] not in (0x10000, 0x10300):
        raise ValueError('Unexpected module header')
    ops = list(instructions(words)); decor = {}; ptr = {}; structs = {}; arrays = {}; constants = {}
    for op, a in ops:
        if op == 71 and len(a) >= 3: decor.setdefault(a[0], {})[a[1]] = a[2]
        elif op == 32: ptr[a[0]] = (a[1], a[2])
        elif op == 30: structs[a[0]] = a[1:]
        elif op == 28: arrays[a[0]] = (a[1], a[2])
        elif op == 43 and len(a) == 3: constants[a[1]] = (a[0], a[2])
    targets = [ptr[a[0]][1] for op, a in ops if op == 59 and a[2] == 2 and decor.get(a[1], {}).get(34) == 1 and decor.get(a[1], {}).get(33) == 0]
    if len(targets) != 1:
        raise ValueError('Expected one native uniform block')
    target = targets[0]; members = structs[target]
    layout = {a[1]: a[3] for op, a in ops if op == 72 and len(a) == 4 and a[0] == target and a[2] == 35}
    old = [layout.get(i) for i in range(len(members))]
    shapes = {(272,): ([336], 0), (0, 256): ([0, 320], None), (0, 256, 272): ([0, 320, 336], 2)}
    if tuple(old) not in shapes:
        raise ValueError('Unreviewed uniform layout: ' + str(old))
    offsets, vindex = shapes[tuple(old)]; fresh = None; oldrows = None
    if vindex is not None:
        array = members[vindex]
        if array not in arrays or arrays[array][1] not in constants:
            raise ValueError('Unknown vertex array')
        typ, oldrows = constants[arrays[array][1]]
        if oldrows not in (64, 256, 512, 1024) or oldrows > vertex_rows:
            raise ValueError('Cannot shrink vertex array')
        if oldrows != vertex_rows:
            fresh = words[3]; words[3] += 1   # a new constant: never mutate one shared with unrelated arrays or logic
    out = []; inserted = False
    for op, a in ops:
        a = a.copy()
        if op == 72 and len(a) == 4 and a[0] == target and a[2] == 35: a[3] = offsets[a[1]]
        if fresh is not None and op == 28 and a[0] == members[vindex]:
            out.append((43, [typ, fresh, vertex_rows])); a[2] = fresh; inserted = True
        out.append((op, a))
    if fresh is not None and not inserted:
        raise ValueError('Vertex array declaration missing')
    result = words[:5]
    for op, a in out:
        result.extend([((len(a) + 1) << 16) | op, *a])
    return struct.pack('<%dI' % len(result), *result)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pack', required=True, help="Mado's WoW-HD-2026-10-28.zip")
    ap.add_argument('--manifest', required=True, help='our translated manifest.txt (VulkanShaders/manifest.txt)')
    ap.add_argument('--out', required=True, help='<client>/VulkanShaders/gbuffer')
    a = ap.parse_args()
    native = blocks(Path(a.manifest).read_text(encoding='utf-8', errors='replace'))
    out = Path(a.out); done = skipped = nogbuf = 0; reasons = {}
    with zipfile.ZipFile(a.pack) as z:
        upstream = blocks(z.read(PREFIX + 'manifest.txt').decode('utf-8', errors='replace'))
        for key, r in native.items():
            u = upstream.get(key)
            g = re.search(r'^gbuf (\S+)', u['block'], re.M) if u else None
            if not g:
                nogbuf += 1; continue
            try:
                data = adapt(z.read(PREFIX + g[1]), r['vertex_rows'])
            except (ValueError, KeyError) as e:
                skipped += 1; reasons[str(e)] = reasons.get(str(e), 0) + 1; continue
            p = out / g[1]; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(data); done += 1
    print('prepared %d modules into %s; %d shaders have no G-buffer variant in the pack; %d refused' % (done, out, nogbuf, skipped))
    for k, v in sorted(reasons.items(), key=lambda x: -x[1])[:6]:
        print('  %5d  %s' % (v, k))
    return 0 if done else 1

if __name__ == '__main__':
    sys.exit(main())
