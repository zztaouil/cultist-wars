from experiment import *

def neutral(s):
 a=s.index('int id = -1;\nfor (int i = 0; i < UNITS; ++i) if (s->neutral_mask')
 b=s.index('if (id < 0 || s->u[id].hp <= 0) return;',a)
 return s[:a]+'''unsigned mask = s->neutral_mask;
while (index-- && mask) mask &= mask - 1;
int id = mask ? __builtin_ctz(mask) : -1;
'''+s[b:]

def ray(s):
 a=s.index('const Ray *path = &rays[from][to];',s.index('static int fast_hit'))
 b=s.index('\n}',a)
 return s[:a]+'''int best = CELLS;
p = 0;
while (lo) { int q = __builtin_ctzll(lo); lo &= lo - 1;
if (taxi[from][q] < best) { best = taxi[from][q]; p = q; } }
while (hi) { int q = 64 + __builtin_ctzll(hi); hi &= hi - 1;
if (taxi[from][q] < best) { best = taxi[from][q]; p = q; } }'''+s[b:]

def meta(s,packed=False):
 # Append metadata: external/manual four-field actions initialize zero => recompute.
 typ='int kind, unit, target, order;'
 new='int kind, unit, target, order, hit;' if not packed else 'int8_t kind; uint8_t unit, target; int order; uint8_t hit;'
 s=s.replace(typ,new)
 s=s.replace('(Action){SHOOT, i, j, order}', '(Action){.kind=SHOOT,.unit=i,.target=j,.order=order,.hit=hit+1}')
 s=s.replace('if (s->ready) hit = fast_hit','if (a.hit) hit = a.hit - 1;\nelse if (s->ready) hit = fast_hit')
 # Only use metadata on the state that generated it. Tracker applies to OTHER candidates.
 s=s.replace('static void track_action(Tracker *t, Action a) {','static void track_action(Tracker *t, Action a) {\na.hit = 0;')
 s=s.replace('return a.kind==SHOOT?fast_hit(s,s->u[a.unit].pos,s->u[a.target].pos):-1;', 'return a.kind==SHOOT?(a.hit?a.hit-1:fast_hit(s,s->u[a.unit].pos,s->u[a.target].pos)):-1;')
 return s

def undo(s):
 at=s.index('static void move_neutral')
 s=s[:at]+'''typedef struct { Unit old[2]; int id[2], n; uint32_t rng; uint16_t neutral_mask; uint64_t key, metadata; } Undo;
static void save_unit(State *s, int id, Undo *u) {
if(u) { u->id[u->n]=id; u->old[u->n++]=s->u[id]; }
}
'''+s[at:]
 s=s.replace('static void move_neutral(State *s, int corpse)', 'static void move_neutral(State *s, int corpse, Undo *undo)')
 s=s.replace('if (n) { Unit u = s->u[id];','if (n) { save_unit(s,id,undo); Unit u = s->u[id];')
 a=s.index('static State apply(');b=s.index('static int same(',a)
 old=s[a:b]; body=old[old.index('int corpse'):old.index('return next;')]
 body=body.replace('next.', 's->').replace('&next','s')
 body=body.replace('if (a.kind == MOVE) {', 'if (a.kind == MOVE) { save_unit(s,a.unit,undo);')
 body=body.replace('else if (a.kind == CONVERT) {','else if (a.kind == CONVERT) {\nsave_unit(s,a.target,undo);')
 body=body.replace('if (hit >= 0) {','if (hit >= 0) {\nsave_unit(s,hit,undo);')
 body=body.replace('move_neutral(s, corpse);','move_neutral(s, corpse, undo);')
 body=body.replace('s->key ^= s->metadata ^ s->metadata;', 's->key ^= old_metadata ^ s->metadata;')
 replacement='''static void make(State *s, Action a, Undo *undo) {
uint64_t old_metadata=s->metadata;
if(undo){undo->n=0;undo->rng=s->rng;undo->neutral_mask=s->neutral_mask;undo->key=s->key;undo->metadata=s->metadata;}
'''+body+'''}
static void unmake(State *s, const Undo *undo) {
for(int i=undo->n-1;i>=0;--i) replace_unit(s,undo->id[i],undo->old[i]);
s->rng=undo->rng;s->neutral_mask=undo->neutral_mask;s->key=undo->key;s->metadata=undo->metadata;s->side^=1;++s->remaining;
}
static State apply(const State *s, Action a) {State next=*s;make(&next,a,0);return next;}
'''
 s=s[:a]+replacement+s[b:]
 # Mutate only function-local copies; every exit after recursion restores parent first.
 for name,end in [('quiescence','\n#define NF'),('search','\nstatic Action choose')]:
  a=s.index('static int '+name+'(');b=s.index(end,a);part=s[a:b]
  part=part.replace('State next = apply(s, actions[i]);','Undo undo; make((State *)s,actions[i],&undo);')
  part=part.replace('&next,','s,')
  part=part.replace('if (stopped) return 0;','unmake((State *)s,&undo);\nif (stopped) return 0;')
  if name=='search':
   part=part.replace('Undo undo; make', 'const State *saved_nn_root=nn_root; nn_root=0; Undo undo; make').replace('unmake((State *)s,&undo);', 'unmake((State *)s,&undo); nn_root=saved_nn_root;')
  s=s[:a]+part+s[b:]
 return s

if __name__=='__main__':
 variants={'neutral':neutral(BASE),'ray':ray(BASE),'metadata':meta(BASE),'packed':meta(BASE,True),'undo':undo(BASE)}
 for name,s in variants.items():(D/(name+'.c')).write_text(s)
 print({n:len(s) for n,s in variants.items()})

def rowray(s):
 s=s.replace('static Bits ray_bits[CELLS][CELLS];','static Bits ray_bits[CELLS][CELLS], row_bits[H];')
 s=s.replace('static void initialize(void) {','static void initialize(void) {\nmemset(row_bits,0,sizeof row_bits);for(int p=0;p<CELLS;p++)bit_toggle(&row_bits[p/W],p);')
 a=s.index('int p;\nif (!hi',s.index('static int fast_hit'));b=s.index('\nreturn (int)((s->occupants',a)
 s=s[:a]+'''int p;
if (from < to) {
p = lo ? __builtin_ctzll(lo) : 64 + __builtin_ctzll(hi);
if (from % W > to % W) {
Bits row = row_bits[p / W]; lo &= row.lo; hi &= row.hi;
p = hi ? 127 - __builtin_clzll(hi) : 63 - __builtin_clzll(lo);
}
} else {
p = hi ? 127 - __builtin_clzll(hi) : 63 - __builtin_clzll(lo);
if (from % W < to % W) {
Bits row = row_bits[p / W]; lo &= row.lo; hi &= row.hi;
p = lo ? __builtin_ctzll(lo) : 64 + __builtin_ctzll(hi);
}
}'''+s[b:]
 return s
if __name__=='__main__':
 (D/'rowray.c').write_text(rowray(BASE));(D/'rowneutral.c').write_text(neutral(rowray(BASE)))
