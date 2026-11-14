"""Validate diag engine transitions against Java reconstruction on all boss games."""
import json,eng
G=json.load(open('boss_games.json'));ok=bad=0
for g in G:
    eng.setup(g['walls'])
    for t in range(len(g['states'])-1):
        n=eng.step(g['states'][t],g['acts'][t])
        if n[:60]==g['states'][t+1][:60]:ok+=1
        else:
            bad+=1
            if bad<4:print('MISMATCH',g['id'],t,g['acts'][t],n[:60],g['states'][t+1][:60])
print('transitions ok',ok,'bad',bad)
