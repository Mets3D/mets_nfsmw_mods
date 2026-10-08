"""Compare the article repo's function names (docs/renames.csv + sdk_addrs.json) with
the decomp's PC 1.3 symbol map, by address."""
import csv
import json
import re
from collections import Counter

re_csv = 'nfsmw-2005-re/docs/renames.csv'
decomp = json.load(open('nfsmw-decomp/config/SPEED_EXE_1_3/symbols.json'))['functions']
sdk = json.load(open('nfsmw-2005-sdk/data/sdk_addrs.json'))

PLACEHOLDER = re.compile(r'^(sub|nullsub|loc|j|unk|nullsub)_?[0-9A-Fa-f]*$|^(sub|nullsub)_')
by_addr = {int(s['start'], 16): s for s in decomp}
starts = sorted(by_addr)


def tokens(name):
    """Lower-case word tokens from CamelCase / snake / mangled names."""
    name = re.sub(r'__\d+', ' ', name)           # gcc mangling count markers
    name = re.sub(r'[?@$]', ' ', name)
    words = re.findall(r'[A-Z]?[a-z]+|[A-Z]+(?![a-z])|\d+', name)
    return {w.lower() for w in words if len(w) > 2}


def classify(addr, art_name):
    s = by_addr.get(addr)
    if s is None:
        return 'no decomp entry at this address'
    if PLACEHOLDER.match(s['name']):
        return 'decomp has only a placeholder (sub_/nullsub_)'
    a, d = tokens(art_name), tokens(s['name'])
    if a and d and (a & d):
        return 'both named, names overlap'
    return 'both named, names differ'


def run(label, rows):
    c = Counter()
    examples = {}
    for addr, name in rows:
        k = classify(addr, name)
        c[k] += 1
        examples.setdefault(k, []).append((hex(addr), name, by_addr.get(addr, {}).get('name')))
    n = sum(c.values())
    print(f'\n== {label}: {n} entries')
    for k, v in c.most_common():
        print(f'{v:6} {100*v/n:5.1f}%  {k}')
    return examples


rows = []
for r in csv.DictReader(open(re_csv)):
    rows.append((int(r['address'], 16), r['name']))
ex = run('article renames.csv', rows)
ex2 = run('article sdk_addrs.json (functions only)',
          [(int(a['addr'], 16), a['fullname'] or '') for a in sdk if a['kind'] == 'function' and a['fullname']])
import random
random.seed(3)
for k in ('both named, names overlap', 'both named, names differ', 'decomp has only a placeholder (sub_/nullsub_)', 'no decomp entry at this address'):
    print('\n--', k)
    for e in random.sample(ex.get(k, []), min(6, len(ex.get(k, [])))):
        print('  ', *e)


# ---- refinement: what are the "no decomp entry" ones, and do the named pairs really agree? ----
import bisect
ends = {a: a + int(by_addr[a]['size'], 16) for a in starts}
GENERIC = re.compile(r'^(Unwind@|FUN_|thunk_|SEH_|__unwind|switchD|LAB_|DAT_)')
c = Counter()
miss = []
for addr, name in rows:
    if addr in by_addr:
        continue
    if GENERIC.match(name):
        c['article label is a compiler stub (Unwind@/FUN_/...), not a real name'] += 1
        continue
    i = bisect.bisect_right(starts, addr) - 1
    if i >= 0 and starts[i] <= addr < ends[starts[i]]:
        c['inside a decomp function (different start address)'] += 1
    else:
        c['outside every decomp function (data/gap)'] += 1
        miss.append((hex(addr), name))
print('\n== breakdown of article entries with no decomp entry at the same address')
for k, v in c.most_common():
    print(f'{v:6}  {k}')
print('sample of "outside every function":', miss[:10])
print('address ranges of those:', Counter((int(a, 16) >> 20) for a, _ in miss).most_common(5))

random.seed(11)
pairs = [(hex(a), n, by_addr[a]['name']) for a, n in rows
         if a in by_addr and not PLACEHOLDER.match(by_addr[a]['name'])
         and not n.startswith('_') and not by_addr[a]['name'].startswith('_')]
print(f'\n{len(pairs)} named-on-both-sides pairs (library/CRT names excluded). 20 random, judge by eye:')
for p in random.sample(pairs, 20):
    print('  ', *p)


# ---- clean table: real article names only ----
STUB = re.compile(r'^(Unwind|Catch)@|^FUN_|^thunk_')
real = [(a, n) for a, n in rows if not STUB.match(n)]
c = Counter()
for addr, name in real:
    s = by_addr.get(addr)
    if s is not None:
        c['decomp: same address, REAL name' if not PLACEHOLDER.match(s['name']) else 'decomp: same address, only placeholder (sub_/nullsub_)'] += 1
    else:
        i = bisect.bisect_right(starts, addr) - 1
        c['decomp: address is inside a function with another start' if (i >= 0 and starts[i] <= addr < ends[starts[i]]) else 'decomp: no function at address'] += 1
print(f'\n== CLEAN: article has {len(real)} real names (the other {len(rows)-len(real)} of {len(rows)} are compiler Unwind@/Catch@ stubs)')
for k, v in c.most_common():
    print(f'{v:6} {100*v/len(real):5.1f}%  {k}')
# the reverse direction: decomp real names the article lacks
art_addrs = {a for a, _ in real}
dec_real = [s for s in decomp if not PLACEHOLDER.match(s['name']) and not s['lib'] and not s['thunk']
            and not re.match(r'^(Unwind|Catch)@|^\?\?|^_', s['name'])]
only = [s for s in dec_real if int(s['start'], 16) not in art_addrs]
print(f'\ndecomp real-named non-library functions: {len(dec_real)}; of those the article has no name at the same address for {len(only)} ({100*len(only)/len(dec_real):.0f}%)')
