"""10 eval variants on 41709358 base (LEADER_BONUS=420), 5 boss replays each (same maps/seats).
  python3 exp10.py        resumable; run repeatedly
  python3 exp10.py sum"""
import sys,time,iter
G=iter.BOSS_GAMES[:5];S='validation/race2/bot_z.c:LEADER_BONUS=420'
V=[('x01_lstep20','LSTEP=20'),('x02_lstep10','LSTEP=10'),('x03_npot150','NPOT=150'),('x04_npot300','NPOT=300'),
   ('x05_lnear300','LNEAR=300'),('x06_flead100','FLEAD=100'),('x07_lexpw20','LEXPW=20'),('x08_lhpw40','LHPW=40'),
   ('x09_lproxe50','LPROXE=50'),('x10_lneut120','LNEUT=120'),('x00_base420','')]
if sys.argv[1:]==['sum']:
  iter.bsum([t for t,_ in V]);sys.exit()
t0=time.time()
for tag,f in V:
  spec=S+(':'+f if f else '');c=iter.code(spec)
  for g in G:
    if time.time()-t0>150:print('chunk end');sys.exit()
    iter.one(tag,c,spec,g)
print('ALL DONE')
