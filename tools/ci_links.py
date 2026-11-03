#!/usr/bin/env python3
"""Linux (case-sensitive file system): creates links in src/compat_ci for the headers included with a different case.
Usage: python3 tools/ci_links.py   (run after setup_engine.sh; idempotent)"""
import os, re, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
dirs = ['src/compat', 'src/compat_ci', 'src/n64', 'src/gta2_re/Source', 'src/gta2_re']
dirs = [os.path.join(root, d) for d in dirs]
idx = {}
for d in dirs:
    if not os.path.isdir(d): continue
    for dp, dn, fn in os.walk(d):
        for f in fn:
            rel = os.path.relpath(os.path.join(dp, f), d).replace('\\', '/')
            idx.setdefault(rel.lower(), os.path.join(dp, f))
inc = re.compile(r'#\s*include\s*[<"]([^>"]+)[>"]')
need = set()
for sub in ['src', ]:
    for dp, dn, fn in os.walk(os.path.join(root, sub)):
        if '3rdParty' in dp: continue
        for f in fn:
            if f.endswith(('.cpp', '.hpp', '.h', '.c', '.inl')):
                try: txt = open(os.path.join(dp, f), errors='ignore').read()
                except: continue
                for m in inc.finditer(txt): need.add(m.group(1).replace('\\', '/'))
made = 0
for n in sorted(need):
    exact = any(os.path.exists(os.path.join(d, n)) for d in dirs)
    if exact: continue
    t = idx.get(n.lower())
    if not t: continue
    dst = os.path.join(root, 'src/compat_ci', n)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if not os.path.lexists(dst):
        os.symlink(t, dst); made += 1
print('links created:', made)
