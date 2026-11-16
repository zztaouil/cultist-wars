import json,hashlib,lib
maps=json.load(open(lib.ROOT/'validation/arena-verify/results/maps.json'))
g=lambda m:int(hashlib.md5(str(m[0]).encode()).hexdigest(),16)%5
tr=[m for m in maps if g(m)!=0];ga=[m for m in maps if g(m)==0]
json.dump(tr,open('maps_train.json','w'));json.dump(ga,open('maps_gate.json','w'));print(len(tr),len(ga))
