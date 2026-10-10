#!/usr/bin/env python3
"""Offline P620 timing QA: frame-held-out summaries and cross-run drift. No address inference."""
import argparse,csv,json,math,random,statistics
from pathlib import Path
from collections import Counter

REQUIRED={'run_id','pair_id','frame_index','frame_base_phys','numa_node','paddr_a','paddr_b','trials','mean','median','p10','p90','min','max'}
def q(xs,f):
    xs=sorted(xs)
    return xs[min(len(xs)-1,int((len(xs)-1)*f))] if xs else None
def stats(xs,rng):
    if not xs:return dict(n=0)
    ci=sorted(statistics.fmean(rng.choices(xs,k=len(xs))) for _ in range(400))
    return dict(n=len(xs),mean=statistics.fmean(xs),median=q(xs,.5),p10=q(xs,.1),p90=q(xs,.9),p99=q(xs,.99),bootstrap_mean_ci95=[q(ci,.025),q(ci,.975)])
def analyze(csvpath,manifestpath,metric,seed):
    m=json.loads(Path(manifestpath).read_text())
    frames={int(f['base_phys'],0):f for f in m['frames']}
    if len(frames)!=len(m['frames']):raise ValueError('duplicate physical frames')
    rows=[];bad=[];ids=set()
    with open(csvpath,newline='') as f:
        rd=csv.DictReader(f)
        if not REQUIRED.issubset(rd.fieldnames or []):raise ValueError('missing CSV columns')
        for line,r in enumerate(rd,2):
            try:
                b=int(r['frame_base_phys'],0);x=int(r['paddr_a'],0);y=int(r['paddr_b'],0)
                if b not in frames or b%(1<<30) or x>>30 != b>>30 or y>>30 != b>>30:raise ValueError('frame mismatch')
                if x==y or x%64 or y%64:raise ValueError('unaligned/identical addresses')
                if r['run_id']!=m['run_id']:raise ValueError('run ID mismatch')
                pair=(r['run_id'],r['pair_id'])
                if pair in ids:raise ValueError('duplicate pair');ids.add(pair)
                if int(r['frame_index'])!=int(frames[b]['index']) or int(r['numa_node'])!=int(frames[b]['numa_node']):raise ValueError('index/NUMA mismatch')
                lo,p10,med,p90,hi=[float(r[k]) for k in ('min','p10','median','p90','max')]
                avg=float(r['mean']);n=int(r['trials'])
                if n<1 or not all(math.isfinite(z) for z in [lo,p10,med,p90,hi,avg]) or not 0<=lo<=p10<=med<=p90<=hi or not lo<=avg<=hi:raise ValueError('timing inconsistency')
                rows.append(dict(frame=b,value=float(r[metric])))
            except (ValueError,KeyError) as e:bad.append(dict(line=line,error=str(e)))
    rng=random.Random(seed)
    ids=sorted(set(r['frame'] for r in rows));rng.shuffle(ids)
    cutoff=max(1,min(len(ids)-1,round(len(ids)*.5))) if len(ids)>=2 else 0
    train=set(ids[:cutoff])
    tr=[r['value'] for r in rows if r['frame'] in train]
    te=[r['value'] for r in rows if r['frame'] not in train]
    per=[dict(frame_base_phys=hex(b),partition='train' if b in train else 'heldout',numa_node=frames[b]['numa_node'],**stats([r['value'] for r in rows if r['frame']==b],rng)) for b in sorted(set(r['frame'] for r in rows))]
    return dict(run_id=m['run_id'],source_revision=m.get('source_revision'),metric=metric,quality=dict(valid_pairs=len(rows),invalid_pairs=len(bad),errors=bad[:100],expected_pairs=m['pages']*m['pairs_per_frame'],complete=len(bad)==0 and len(rows)==m['pages']*m['pairs_per_frame'],observed_cpu_migrations=m.get('observed_cpu_migrations'),numa_unknown=sum(x.get('numa_node',-1)<0 for x in m['frames'])),split=dict(unit='physical_1GiB_frame',train_frames=[hex(b) for b in sorted(train)],heldout_frames=[hex(b) for b in sorted(set(ids)-train)],train=stats(tr,rng),heldout=stats(te,rng)),per_frame=per)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--summary',nargs='+',required=True);p.add_argument('--manifest',nargs='+',required=True);p.add_argument('--output',required=True)
    p.add_argument('--metric',choices=['mean','median'],default='median');p.add_argument('--seed',type=int,default=20261010)
    a=p.parse_args()
    if len(a.summary)!=len(a.manifest):p.error('one manifest per CSV needed')
    runs=[analyze(c,m,a.metric,a.seed+i) for i,(c,m) in enumerate(zip(a.summary,a.manifest))]
    count=Counter(f['frame_base_phys'] for r in runs for f in r['per_frame'])
    overlap=sorted(k for k,n in count.items() if n>1)
    medians=[r['split']['heldout'].get('median') for r in runs]
    report=dict(format_version=1,runs=runs,cross_run=dict(overlapping_physical_frames=overlap,heldout_median_drift=[dict(run_id=r['run_id'],delta_from_first=(v-medians[0] if v is not None and medians[0] is not None else None)) for r,v in zip(runs[1:],medians[1:])]),interpretation='Passive timing quality only; not physical DRAM wordline, ECC, or TRR evidence.')
    dest=Path(a.output);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(json.dumps(report,indent=2)+'\n')
    print('Wrote',dest,'runs',len(runs),'frame overlaps',len(overlap))
    for r in runs:print(r['run_id'],'valid',r['quality']['valid_pairs'],'invalid',r['quality']['invalid_pairs'])
    return 0 if all(r['quality']['complete'] for r in runs) else 3
if __name__=='__main__':raise SystemExit(main())
