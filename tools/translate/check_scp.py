import sys,glob,os,re
sys.path.insert(0,os.path.dirname(__file__)); import scp
base=os.environ.get('GC1_EXTRACTED','GrandChase1-extracted')
for lang in ('ko','pt'):
    ok=bad=0; msgs=[]
    for f in sorted(glob.glob(f'{base}/{lang}/jar/[nm]*.scp')):
        n=os.path.basename(f)[:-4]
        if not re.fullmatch(r'[nm]\d+',n): continue
        code=open(f,'rb').read(); dat=open(f[:-4]+'.dat','rb').read()
        try: refs=scp.text_refs(code)
        except ValueError as e: bad+=1; msgs.append((n,str(e))); continue
        cov=bytearray(len(dat)); over=0
        for r in refs:
            if r['off']<0 or r['off']+r['len']>len(dat): over+=1; continue
            for i in range(r['off'],r['off']+r['len']): cov[i]+=1
        gaps=sum(1 for c in cov if c==0); dup=sum(1 for c in cov if c>1)
        if over or gaps or dup: msgs.append((n,f'refs={len(refs)} over={over} gaps={gaps} dup={dup} datlen={len(dat)}'))
        else: ok+=1
    print(lang,'ok',ok,'bad',bad,'issues',len(msgs))
    for m in msgs[:15]: print('  ',m)
