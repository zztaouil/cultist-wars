"""Package audited source and persist experiment provenance after evaluation."""
from experiment import *
def main():
    candidate()
    exe=ROOT/'build/standalone'
    subprocess.run(['cc','-O3','-std=c11',str(ROOT/'standalone.c'),'-o',str(exe)],check=True,capture_output=True)
    script=(REPO/'tests/test_protocol.py').read_text().replace("['./bot']",repr([str(exe)]))
    path=ROOT/'build/protocol.py';path.write_text(script)
    p=subprocess.run([__import__('sys').executable,str(path)],capture_output=True,text=True)
    save('protocol.json',dict(returncode=p.returncode,stdout=p.stdout,stderr=p.stderr));assert p.returncode==0,p.stderr
    files=['baseline.c','candidate.c','residual.h','weights.h','standalone.c','data.npz','records.json','model.pt','collection.json','training.json','checks.json','throughput.json','tactical.json','matches.json','games.json','match-maps.json','protocol.json','experiment.py','collect.py','train.py','check.py','evaluate.py','finalize.py']
    save('manifest.json',dict(source_sha256={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in files},budget_ms=48,correction_bound=150,production_matches_frozen=hashlib.sha256((REPO/'bot.c').read_bytes()).hexdigest()==hashlib.sha256((ROOT/'baseline.c').read_bytes()).hexdigest(),standalone_bytes=(ROOT/'standalone.c').stat().st_size))
    print(p.stdout)
if __name__=='__main__':main()
