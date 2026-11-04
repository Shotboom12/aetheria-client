#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Aetheria FSR object motion: generate velocity vertex variants of OUR translated skinned vertex modules.

For a module M (SPIR-V 1.0 vertex shader written by Tools/VulkanShaders, regs block at set 1 binding 0) the variant V:
  - runs M's own code as before (current clip position),
  - additionally runs a CLONE of main and of every function it calls against a second register file `regsPrev`
    (same Regs block type, set 2 binding 0) whose only effect is to write its gl_Position into a new output,
  - outputs: location 16 = current clip position (the final gl_Position value), location 17 = previous clip position.
The velocity fragment shader (GLSL, velocity.frag) turns those two into a motion vector. Everything is validated with spirv-val.

Usage: velocity_variants.py --in <VulkanShaders/vertex> --manifest <VulkanShaders/manifest.txt> --out <VulkanShaders/velocity/own/vertex>
         --index <VulkanShaders/velocity/velocity-index.txt> [--tools <folder with spirv-dis/spirv-as/spirv-val>] [--limit N] [--only md5]
"""
import argparse, os, re, subprocess, sys, tempfile

def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)

class Fail(Exception):
    pass

def transform(asm_lines):
    text = [l.rstrip('\n') for l in asm_lines]
    # ---- parse -----------------------------------------------------------------------------------------------------------
    def_re = re.compile(r'^\s*%(\d+)\s*=\s*(Op\w+)(.*)$')
    ids = set()
    for l in text:
        for m in re.finditer(r'%(\d+)', l):
            ids.add(int(m.group(1)))
    bound = max(ids) + 1
    # entry point
    entry = None
    entry_idx = None
    for i, l in enumerate(text):
        m = re.match(r'^\s*OpEntryPoint Vertex %(\d+) "main"(.*)$', l)
        if m:
            entry = int(m.group(1)); entry_idx = i; break
    if entry is None:
        raise Fail('no vertex entry point')
    # functions
    funcs = {}      # id -> (start, end)
    order = []
    i = 0
    while i < len(text):
        m = def_re.match(text[i])
        if m and m.group(2) == 'OpFunction':
            fid = int(m.group(1)); j = i
            while 'OpFunctionEnd' not in text[j]:
                j += 1
            funcs[fid] = (i, j); order.append(fid); i = j + 1
        else:
            i += 1
    if entry not in funcs:
        raise Fail('entry function body not found')
    # call graph
    def callees(fid):
        s, e = funcs[fid]; out = set()
        for l in text[s:e + 1]:
            m = re.search(r'OpFunctionCall %\d+ %(\d+)', l)
            if m:
                out.add(int(m.group(1)))
        return out
    clone_set = set(); stack = [entry]
    while stack:
        f = stack.pop()
        if f in clone_set:
            continue
        clone_set.add(f); stack.extend(callees(f))
    # module-level variables
    vars_ = {}   # id -> (type id, storage)
    for l in text:
        m = re.match(r'^\s*%(\d+)\s*=\s*OpVariable %(\d+) (\w+)', l)
        if m:
            vars_[int(m.group(1))] = (int(m.group(2)), m.group(3))
    types = {}
    for l in text:
        m = re.match(r'^\s*%(\d+)\s*=\s*OpTypePointer (\w+) %(\d+)', l)
        if m:
            types[int(m.group(1))] = (m.group(2), int(m.group(3)))
    # regs variable: DescriptorSet 1 / Binding 0 in Uniform storage
    dset = {}; dbind = {}
    for l in text:
        m = re.match(r'^\s*OpDecorate %(\d+) DescriptorSet (\d+)', l)
        if m: dset[int(m.group(1))] = int(m.group(2))
        m = re.match(r'^\s*OpDecorate %(\d+) Binding (\d+)', l)
        if m: dbind[int(m.group(1))] = int(m.group(2))
    regs = None
    for vid, (tid, sc) in vars_.items():
        if sc == 'Uniform' and dset.get(vid) == 1 and dbind.get(vid) == 0:
            regs = vid
    if regs is None:
        raise Fail('no regs block at set 1 binding 0')
    regs_ptr_type = vars_[regs][0]
    out_vars = {vid for vid, (tid, sc) in vars_.items() if sc == 'Output'}
    # position output: Output var whose struct has member 0 BuiltIn Position
    pos_struct = None
    for l in text:
        m = re.match(r'^\s*OpMemberDecorate %(\d+) 0 BuiltIn Position', l)
        if m: pos_struct = int(m.group(1))
    pos_var = None
    for vid in out_vars:
        tid = vars_[vid][0]
        if types.get(tid, ('', 0))[1] == pos_struct:
            pos_var = vid
    if pos_var is None:
        raise Fail('no gl_Position output')
    # vec4 float type and Output pointer to it
    vec4 = None
    for i, l in enumerate(text):
        m = re.match(r'^\s*%(\d+)\s*=\s*OpTypeVector %(\d+) 4$', l)
        if m:
            fid = int(m.group(2))
            if any(re.match(r'^\s*%%%d\s*=\s*OpTypeFloat 32$' % fid, t) for t in text):
                vec4 = int(m.group(1)); break
    if vec4 is None:
        raise Fail('no vec4 float type')
    out_ptr4 = None
    for tid, (sc, tgt) in types.items():
        if sc == 'Output' and tgt == vec4:
            out_ptr4 = tid
    nid = [bound]
    def new_id():
        v = nid[0]; nid[0] += 1; return v
    new_types = []
    if out_ptr4 is None:
        out_ptr4 = new_id(); new_types.append('%%%d = OpTypePointer Output %%%d' % (out_ptr4, vec4))
    cur_clip = new_id(); prev_clip = new_id(); regs_prev = new_id()
    new_vars = ['%%%d = OpVariable %%%d Output' % (cur_clip, out_ptr4),
                '%%%d = OpVariable %%%d Output' % (prev_clip, out_ptr4),
                '%%%d = OpVariable %%%d Uniform' % (regs_prev, regs_ptr_type)]
    # ---- clone ------------------------------------------------------------------------------------------------------------
    idmap = {}
    for f in clone_set:
        s, e = funcs[f]
        for l in text[s:e + 1]:
            m = def_re.match(l)
            if m:
                idmap[int(m.group(1))] = new_id()
    def remap_line(l):
        def sub(m):
            v = int(m.group(1))
            if v == regs: return '%%%d' % regs_prev
            return '%%%d' % idmap.get(v, v)
        return re.sub(r'%(\d+)', sub, l)
    # which chains in a function point into outputs
    def clone_function(f, is_entry):
        s, e = funcs[f]
        body = text[s:e + 1]
        out_chain = {}
        for l in body:
            m = re.match(r'^\s*%(\d+)\s*=\s*OpAccessChain %\d+ %(\d+)(.*)$', l)
            if m and int(m.group(2)) in out_vars:
                out_chain[int(m.group(1))] = int(m.group(2))
        result = []
        for l in body:
            if is_entry:
                m = re.match(r'^\s*OpStore %(\d+) %(\d+)\s*$', l)
                if m:
                    p = int(m.group(1)); v = int(m.group(2))
                    if p in out_vars:
                        continue
                    if p in out_chain:
                        if out_chain[p] == pos_var:
                            result.append('               OpStore %%%d %%%d' % (prev_clip, idmap.get(v, v))); continue
                        continue
            result.append(remap_line(l))
        return result
    cloned = []
    for f in order:
        if f in clone_set:
            cloned.extend(clone_function(f, f == entry))
    cloned_entry = idmap[entry]
    # ---- patch the original main ------------------------------------------------------------------------------------------
    s, e = funcs[entry]
    body = text[s:e + 1]
    # position store in the original main
    out_chain = {}
    for l in body:
        m = re.match(r'^\s*%(\d+)\s*=\s*OpAccessChain %\d+ %(\d+)(.*)$', l)
        if m and int(m.group(2)) in out_vars:
            out_chain[int(m.group(1))] = int(m.group(2))
    patched = []; got_store = False; call_done = False; seen_label = False
    void_ty = re.match(r'^\s*%\d+\s*=\s*OpFunction %(\d+)', body[0]).group(1)
    for l in body:
        if not call_done:
            if seen_label and not re.match(r'^\s*%\d+\s*=\s*OpVariable', l):
                patched.append('        %%%d = OpFunctionCall %%%s %%%d' % (new_id(), void_ty, cloned_entry)); call_done = True
            if re.match(r'^\s*%\d+\s*=\s*OpLabel', l):
                seen_label = True
        patched.append(l)
        m = re.match(r'^\s*OpStore %(\d+) %(\d+)\s*$', l)
        if m and int(m.group(1)) in out_chain and out_chain[int(m.group(1))] == pos_var:
            patched.append('               OpStore %%%d %%%s' % (cur_clip, m.group(2))); got_store = True
    if not got_store:
        raise Fail('position store not found in main')
    if not call_done:
        raise Fail('call site not placed')
    # ---- assemble the new text ------------------------------------------------------------------------------------------
    out = []
    first_func = min(funcs[f][0] for f in funcs)
    annotations_done = False
    for i, l in enumerate(text):
        if i == entry_idx:
            l = l.rstrip() + ' %%%d %%%d' % (cur_clip, prev_clip)
        if not annotations_done and re.match(r'^\s*%\d+\s*=\s*OpType', l):
            out.append('               OpDecorate %%%d Location 16' % cur_clip)
            out.append('               OpDecorate %%%d Location 17' % prev_clip)
            out.append('               OpDecorate %%%d DescriptorSet 2' % regs_prev)
            out.append('               OpDecorate %%%d Binding 0' % regs_prev)
            annotations_done = True
        if i == first_func:
            for t in new_types: out.append('          ' + t)
            for v in new_vars: out.append('          ' + v)
        if i in range(funcs[entry][0], funcs[entry][1] + 1):
            if i == funcs[entry][0]:
                out.extend(patched)
            continue
        out.append(l)
    # the clones go at the end of the module (functions may appear in any order)
    out.extend(cloned)
    return '\n'.join(out) + '\n'

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--in', dest='indir', required=True)
    ap.add_argument('--manifest', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--tools', default=r'D:\Tools\spirv-tools\bin')
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--only', default='')
    ap.add_argument('--index', default='', help='write the ownvel lines of velocity-index.txt here')
    ap.add_argument('--rel', default='velocity/own/vertex', help='--out relative to the VulkanShaders folder, as written into the index')
    a = ap.parse_args()
    dis = os.path.join(a.tools, 'spirv-dis.exe'); asm = os.path.join(a.tools, 'spirv-as.exe'); val = os.path.join(a.tools, 'spirv-val.exe')
    os.makedirs(a.out, exist_ok=True)
    # skinned vertex records: inputs include blendweight and blendindices
    skinned = []
    block = []
    def flush():
        if not block: return
        rec = None; ins = set(); ok = False
        for l in block:
            p = l.split()
            if len(p) >= 3 and p[0] == 'record' and p[2] == 'vertex': rec = p[1]; ok = True
            if p and p[0] == 'input': ins.add(p[1])
        if ok and any(i.startswith('blendindices') for i in ins):
            skinned.append(rec)
    for l in open(a.manifest, encoding='utf-8', errors='replace'):
        l = l.split('#')[0].rstrip()
        if not l.strip():
            flush(); block = []
        else:
            block.append(l)
    flush()
    if a.only: skinned = [m for m in skinned if m == a.only]
    if a.limit: skinned = skinned[:a.limit]
    print('skinned vertex records: %d' % len(skinned))
    good = bad = 0; reasons = {}; made = []
    for md5 in skinned:
        src = os.path.join(a.indir, md5 + '.spv')
        if not os.path.exists(src):
            reasons['missing'] = reasons.get('missing', 0) + 1; bad += 1; continue
        with tempfile.TemporaryDirectory() as td:
            asmfile = os.path.join(td, 'm.spvasm'); outspv = os.path.join(a.out, md5 + '.vel.spv')
            r = run([dis, '--raw-id', '--no-header', src, '-o', asmfile])
            if r.returncode:
                reasons['dis'] = reasons.get('dis', 0) + 1; bad += 1; continue
            try:
                new = transform(open(asmfile, encoding='utf-8').readlines())
            except Fail as e:
                reasons[str(e)] = reasons.get(str(e), 0) + 1; bad += 1; continue
            nf = os.path.join(td, 'v.spvasm'); open(nf, 'w', encoding='utf-8').write(new)
            r = run([asm, '--preserve-numeric-ids', '--target-env', 'vulkan1.0', nf, '-o', outspv])
            if r.returncode:
                reasons['asm: ' + r.stderr.strip().splitlines()[0][:80]] = reasons.get('asm', 0) + 1; bad += 1
                if os.path.exists(outspv): os.remove(outspv)
                continue
            r = run([val, '--target-env', 'vulkan1.0', outspv])
            if r.returncode:
                key = 'val: ' + (r.stderr.strip().splitlines()[0][:100] if r.stderr.strip() else '?')
                reasons[key] = reasons.get(key, 0) + 1; bad += 1; os.remove(outspv); continue
            good += 1; made.append(md5)
    print('generated and validated: %d, failed: %d' % (good, bad))
    if a.index:
        with open(a.index, 'w', encoding='utf-8', newline='\n') as f:
            f.write('# ownvel <md5 of the original D3D9 shader> <path relative to VulkanShaders>, written by velocity_variants.py\n')
            for m in made:
                f.write('ownvel %s %s/%s.vel.spv\n' % (m, a.rel.strip('/'), m))
        print('index written: %s (%d lines)' % (a.index, len(made)))
    for k, v in sorted(reasons.items(), key=lambda x: -x[1])[:10]:
        print('  %5d  %s' % (v, k))

if __name__ == '__main__':
    main()
