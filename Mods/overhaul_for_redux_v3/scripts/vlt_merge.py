"""Merge the Overhaul's vault changes onto Redux v3, record by record, from Attribulator YAML exports.

  python Mods/overhaul_for_redux_v3/scripts/vlt_merge.py [--exclude junkman ...]

Reads  Exports/vlt/{vanilla, redux, overhaul13}  (see vlt_changes_report.py for how they are made)
Writes Exports/ReduxV3 + Overhaul/yaml/         the merged YAML tree (then packed by Attribulator, see agent_docs/project.md)
       Exports/ReduxV3 + Overhaul/merge_report.txt

How: the two mods change disjoint values (checked: no value is changed by both), so the unit is the record (collection):
  - a record only the Overhaul changed or added  -> the Overhaul's text block replaces / is inserted into Redux's file
  - a record both changed                        -> the Overhaul's changed top-level fields are inserted into Redux's block
                                                    (only if Redux's block lacks them; otherwise the script stops)
  - everything else                              -> Redux's text, untouched
  - classes in --exclude (default: junkman) keep Redux's text, which is vanilla for them.
Text blocks are copied, never re-serialised, so the formatting Attribulator expects is kept. A check at the end compares every
value of the result against what it should be.
"""
import argparse
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vlt_changes_report as rep

yaml = rep.yaml
ROOT = rep.ROOT
OUT = os.path.normpath(ROOT + '../ReduxV3 + Overhaul') + os.sep


def split_blocks(text):
    lines = text.splitlines(keepends=True)
    starts = [i for i, l in enumerate(lines) if l.startswith('- ParentName:')]
    if not starts:
        return [], []
    blocks = [''.join(lines[a:b]) for a, b in zip(starts, starts[1:] + [len(lines)])]
    return blocks, [''.join(lines[:starts[0]])]


def keys_of(text):
    doc = yaml.load(text, Loader=rep.LOADER)
    keys, seen = [], set()
    for i, c in enumerate(doc):
        k = str(c.get('Name'))
        keys.append(k if k not in seen else f'{k}#{i}')
        seen.add(k)
    return keys, doc


def top_fields(block):
    """The top level fields of a record's Data (4-space indent) -> {name: text}, plus the text before the first one."""
    lines = block.splitlines(keepends=True)
    start = next(i for i, l in enumerate(lines) if l.startswith('  Data:'))
    fields, cur, order = {}, None, []
    for l in lines[start + 1:]:
        m = re.match(r'^    ([^\s].*?):(?: .*)?\s*$', l)
        if m and not l.startswith('     '):
            cur = m.group(1)
            fields[cur] = l
            order.append(cur)
        elif cur is not None:
            fields[cur] += l
    return fields, order


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exclude', nargs='*', default=['junkman'], help='classes that keep Redux\'s (= vanilla) values')
    args = ap.parse_args()

    sets = {k: rep.load_set(v) for k, v in rep.SETS.items()}
    rows, added = rep.collect()
    o_rows, r_rows = {}, {}
    for r in rows:
        key = (r['file'], r['coll'])
        if r['o'] != r['v']:
            o_rows.setdefault(key, []).append(r)
        if r['r'] != r['v']:
            r_rows.setdefault(key, []).append(r)
    o_added = {(a['file'], a['coll']) for a in added if 'o' in a['by']}
    r_added = {(a['file'], a['coll']) for a in added if 'r' in a['by']}

    if os.path.exists(OUT):
        shutil.rmtree(OUT)
    shutil.copytree(ROOT + 'redux', OUT + 'yaml')

    report = []
    stats = {'replaced': 0, 'inserted': 0, 'patched': 0, 'skipped_excluded': 0}
    files = sorted({k[0] for k in o_rows} | {k[0] for k in o_added})
    for rel in files:
        cls = rep.cls_of(rel)
        if cls in args.exclude:
            n = sum(1 for k in list(o_rows) + list(o_added) if k[0] == rel)
            stats['skipped_excluded'] += n
            report.append(f'EXCLUDED  {rel}: {n} Overhaul record(s) left as in Redux')
            continue
        rtext = open(ROOT + 'redux/' + rel, encoding='utf-8').read()
        otext = open(ROOT + 'overhaul13/' + rel, encoding='utf-8').read()
        rblocks, rhead = split_blocks(rtext)
        oblocks, _ = split_blocks(otext)
        rkeys, _ = keys_of(rtext)
        okeys, _ = keys_of(otext)
        oby = dict(zip(okeys, oblocks))
        out_blocks = dict(zip(rkeys, rblocks))
        order = list(rkeys)
        wanted = sorted({k[1] for k in o_rows if k[0] == rel} | {k[1] for k in o_added if k[0] == rel})
        for name in wanted:
            key = (rel, name)
            if name not in oby:
                raise SystemExit(f'{rel}: record {name} is not in the Overhaul file')
            if name not in out_blocks:  # only the Overhaul has it: insert after the record before it in the Overhaul's file
                idx = okeys.index(name)
                prev = next((okeys[j] for j in range(idx - 1, -1, -1) if okeys[j] in out_blocks), None)
                order.insert(order.index(prev) + 1 if prev else len(order), name)
                out_blocks[name] = oby[name]
                stats['inserted'] += 1
                report.append(f'INSERTED  {rel}: {name}')
                continue
            if key not in r_rows and key not in r_added:  # Redux left it as it was in vanilla
                out_blocks[name] = oby[name]
                stats['replaced'] += 1
                report.append(f'REPLACED  {rel}: {name} ({len(o_rows.get(key, []))} value(s))')
                continue
            # both mods change this record: add the Overhaul's top level fields to Redux's block
            ofields, _ = top_fields(oby[name])
            rfields, _ = top_fields(out_blocks[name])
            changed_tops = {r['field'].split('/')[0].split('[')[0] for r in o_rows.get(key, [])}
            clash = [f for f in changed_tops if f in rfields]
            if clash:
                raise SystemExit(f'{rel}: {name}: the Overhaul changes {clash}, which Redux\'s record also has: merge this one by hand')
            add = ''.join(ofields[f] for f in sorted(changed_tops))
            blk = out_blocks[name]
            out_blocks[name] = blk + ('' if blk.endswith('\n') else '\n') + add
            stats['patched'] += 1
            report.append(f'PATCHED   {rel}: {name}: added {sorted(changed_tops)} from the Overhaul to Redux\'s record')
        text = ''.join(rhead) + ''.join(out_blocks[k] for k in order)
        open(OUT + 'yaml/' + rel, 'w', encoding='utf-8', newline='').write(text)

    # check: every value of the result is what it should be
    merged = {}
    base = OUT + 'yaml/main'
    for dp, dn, fn in os.walk(base):
        for f in fn:
            if f.endswith('.yml'):
                p = os.path.join(dp, f)
                rel = os.path.relpath(p, OUT + 'yaml').replace(os.sep, '/')
                doc = yaml.load(open(p, encoding='utf-8'), Loader=rep.LOADER)
                colls = {}
                for i, c in enumerate(doc):
                    k = str(c.get('Name'))
                    colls[k if k not in colls else f'{k}#{i}'] = c
                merged[rel] = colls
    bad = 0
    checked = 0
    for rel in sorted(set(sets['v']) | set(sets['r']) | set(sets['o'])):
        cls = rep.cls_of(rel)
        V, R, O, M = (s.get(rel, {}) for s in (sets['v'], sets['r'], sets['o'], merged))
        for name in set(V) | set(R) | set(O) | set(M):
            # expected record: Redux's, except that the Overhaul's changed values win
            fv, fr, fo, fm = {}, {}, {}, {}
            for d, out in ((V, fv), (R, fr), (O, fo), (M, fm)):
                if name in d:
                    rep.flat([], d[name].get('Data', {}), out)
            exp = dict(fr)
            if cls not in args.exclude and name in O:
                for k in set(fv) | set(fo):
                    if fo.get(k) != fv.get(k):
                        if fo.get(k) is None:
                            exp.pop(k, None)
                        else:
                            exp[k] = fo[k]
            if name not in R and (name not in O or cls in args.exclude):
                if name in M:
                    bad += 1
                    report.append(f'CHECK FAILED {rel}: {name} should not exist')
                continue
            if exp != fm:
                bad += 1
                diff = [k for k in set(exp) | set(fm) if exp.get(k) != fm.get(k)][:3]
                report.append(f'CHECK FAILED {rel}: {name}: {diff}')
            checked += 1
    report.insert(0, f'Merged: {stats}; value check: {checked} records compared, {bad} wrong')
    open(OUT + 'merge_report.txt', 'w', encoding='utf-8').write('\n'.join(report) + '\n')
    print(report[0])
    if bad:
        sys.exit(1)


if __name__ == '__main__':
    main()
