"""Tiny CodinGame API client. Token from $CG_TOKEN_FILE (never printed)."""
import json,os,time,urllib.request,urllib.error
API='https://www.codingame.com/api/v1'
def req(path,body=None,method=None):
  tok=open(os.environ.get('CG_TOKEN_FILE',os.path.expanduser('~/.cgtoken'))).read().strip()
  r=urllib.request.Request(API+path,data=json.dumps(body).encode() if body is not None else None,method=method,
    headers={'Authorization':'Bearer '+tok,'Content-Type':'application/json'})
  for i in range(4):
    try:
      with urllib.request.urlopen(r,timeout=70) as f: return json.load(f)
    except urllib.error.HTTPError as e:
      d=json.load(e)
      if e.code==429: time.sleep(d.get('retry_after_seconds',30)+1); continue
      return d
  return d
def battles(sid):
  out=[];before=None
  while True:
    q=f'/puzzles/cultist-wars/battles?submission={sid}&limit=100'+(f'&before={before}' if before else '')
    d=req(q);out+=d.get('battles',[]);before=d.get('next_before')
    if not before: return out
