import json,os,time,urllib.request,urllib.error
API='https://www.codingame.com/api/v1'
def req(path,body=None):
  tok=open(os.path.expanduser('~/.cgtoken')).read().strip()
  r=urllib.request.Request(API+path,data=json.dumps(body).encode() if body is not None else None,headers={'Authorization':'Bearer '+tok,'Content-Type':'application/json'})
  for i in range(6):
    try:
      with urllib.request.urlopen(r,timeout=90) as f:return json.load(f)
    except urllib.error.HTTPError as e:
      try:d=json.load(e)
      except Exception:d={'http':e.code}
      if e.code==429:time.sleep(d.get('retry_after_seconds',30)+1);continue
      return d
    except Exception as e:
      time.sleep(10);d={'err':str(e)}
  return d
