"""Boss-leader damage by turn T vs outcome. All valid our-engine-family games vs BOSS 3 (incl. variants), and baseline-only. Within-map check."""
import json
from collections import defaultdict
A=[g for g in json.load(open('boss_games.json')) if g['me'] and g['score_ok']]
BASE=('41709358','41710268','41711135','cd28(=41710268)','ce73(41709358+lazyQ)')
def sc(g):
    my=1-g['boss_seat'];m=g['final'][my]-g['final'][1-my];return 1 if m>0 else .5 if m==0 else 0
def mean(x):return sum(x)/len(x) if x else float('nan')
def hp(g,T,who):
    my=1-g['boss_seat'];p=my if who=='us' else 1-my;s=g['states'][min(T,len(g['states'])-1)];return s[4*p+1]
for name,G in (('baseline-family',[g for g in A if g['version'] in BASE]),('all our variants',A)):
    print('\n##',name,len(G),'games')
    for T in (20,30,40):
        for seat in (0,1):
            R=[g for g in G if 1-g['boss_seat']==seat]
            line='T=%d seat %d:'%(T,seat)
            for lab,lo,hi in (('bossL>=9',9,10),('7-8',7,8),('<=6',0,6)):
                X=[g for g in R if lo<=hp(g,T,'boss')<=hi];line+='  %s n=%d score %.0f%%'%(lab,len(X),100*mean([sc(g) for g in X]))
            line+='  || ourL>=9 '
            for lab,lo,hi in (('>=9',9,10),('7-8',7,8),('<=6',0,6)):
                X=[g for g in R if lo<=hp(g,T,'us')<=hi];line+=' %s n=%d %.0f%%'%(lab,len(X),100*mean([sc(g) for g in X]))
            print(line)
    # within-map: pairs on the same (seed,seat) with different boss leader hp at T=30
    by=defaultdict(list)
    for g in G:by[(g['seed'],g['boss_seat'])].append(g)
    num=den=0;conc=disc=tie=0
    for k,gs in by.items():
        for i in range(len(gs)):
            for j in range(i+1,len(gs)):
                a,b=gs[i],gs[j];da=hp(a,30,'boss');db=hp(b,30,'boss')
                if da==db:continue
                lo,hi=(a,b) if da<db else (b,a)   # lo = game where boss leader more damaged
                if sc(lo)>sc(hi):conc+=1
                elif sc(lo)<sc(hi):disc+=1
                else:tie+=1
    print('within-map pairs (same seed+seat, boss leader HP@30 differs): more-damaged game scored better %d, worse %d, same %d ; maps with repeats %d'%(conc,disc,tie,sum(len(v)>1 for v in by.values())))
