from experiment import *
import random
names=sys.argv[1:] or ['baseline','neutral','ray','metadata','packed','undo']
libs={n:build(n) for n in names};rng=__import__('random').Random(872034);count=0
for board in range(10):
 walls=[int(rng.random()<.12) for _ in range(91)]
 for lib in libs.values():lib.setup(W(*walls))
 for trial in range(100):
  pos=rng.sample([p for p in range(91) if not walls[p]],14);s=[]
  for i,p in enumerate(pos):s.extend([p,rng.randrange(11),i if i<2 else rng.randrange(3),int(i<2)])
  s.extend([rng.randrange(2),rng.randrange(1,151),rng.randrange(1<<31),sum(1<<i for i in range(2,14) if s[4*i+2]==2),rng.randrange(2),0])
  actions=[]
  for n,lib in libs.items():
   out=(C.c_int*(253*4))();nact=lib.actions(A(*s),out);actions.append(list(out)[:nact*4])
  assert all(a==actions[0] for a in actions)
  for i in range(len(actions[0])//4):
   outputs=[]
   for name,lib in libs.items():
    for prepared in [0,1]:
     out=A();lib.transition(A(*s),i,prepared,out);outputs.append(list(out))
   assert all(o==outputs[0] for o in outputs),(board,trial,i)
   count+=1
print(json.dumps(dict(random_states=1000,transitions=count,variants=names,prepared_unprepared=True)),flush=True)
