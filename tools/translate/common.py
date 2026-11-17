import sys,os,glob,re
import sys as _s
_s.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0,os.path.dirname(__file__)); import scp
BASE=os.environ.get('GC1_EXTRACTED','GrandChase1-extracted').replace('\\','/').rstrip('/')+'/'   # pasta com ko/jar e pt/jar (jars extraidos; nao fazem parte do repositorio)
def jar(lang): return f'{BASE}{lang}/jar/'
def dec(lang,b): return b.decode('cp949' if lang=='ko' else 'latin1', errors='replace')
def script_names(lang='pt'):
    return sorted([os.path.basename(f)[:-4] for f in glob.glob(jar(lang)+'*.scp') if re.fullmatch(r'[nm]\d+',os.path.basename(f)[:-4])], key=lambda s:(s[0],int(s[1:])))
def lines(lang,n):
    code=open(f'{jar(lang)}{n}.scp','rb').read(); dat=open(f'{jar(lang)}{n}.dat','rb').read()
    return [(r, dec(lang,dat[r['off']:r['off']+r['len']])) for r in scp.text_refs(code)]
