import copy
import torch
from torch import nn
from experiment import *
torch.set_num_threads(2);torch.manual_seed(71)
class Net(nn.Module):
    def __init__(self):
        super().__init__();self.hidden=nn.Linear(16,8);self.out=nn.Linear(8,1,bias=False)
    def forward(self,x):
        return (self.out(self.hidden(x).relu()-self.hidden(-x).relu()).squeeze(-1)*75).clamp(-150,150)
def main():
    d=np.load(ROOT/'data.npz');x=torch.from_numpy(d['x']);base=torch.from_numpy(d['base']);y=torch.from_numpy(d['teacher']);outcome=torch.from_numpy(d['outcome'])
    ids=[torch.from_numpy(np.flatnonzero(d['split']==i)) for i in range(3)]
    model=Net();opt=torch.optim.AdamW(model.parameters(),lr=.005,weight_decay=.01);best=float('inf');history=[]
    def metrics(ii):
        with torch.no_grad():
            p=model(x[ii]);return dict(n=len(ii),base_mae=float((base[ii]-y[ii]).abs().mean()),residual_mae=float((base[ii]+p-y[ii]).abs().mean()),bounded_target_mse=float(((p-(y[ii]-base[ii]).clamp(-150,150))/150).square().mean()),mean_abs_correction=float(p.abs().mean()))
    for epoch in range(150):
        order=ids[0][torch.randperm(len(ids[0]))]
        for ii in order.split(256):
            pred=model(x[ii]);target=(y[ii]-base[ii]).clamp(-150,150)
            loss=((pred-target)/150).square().mean()
            ok=torch.isfinite(outcome[ii])
            if ok.any():loss=loss+.05*((base[ii][ok]+pred[ok])/1000).tanh().sub(outcome[ii][ok]).square().mean()
            opt.zero_grad();loss.backward();opt.step()
        m=metrics(ids[1]);history.append(dict(epoch=epoch,**m))
        if m['bounded_target_mse']<best:best=m['bounded_target_mse'];state=copy.deepcopy(model.state_dict());selected=epoch
    model.load_state_dict(state);torch.save(state,ROOT/'model.pt')
    w=state['hidden.weight'].numpy();b=state['hidden.bias'].numpy();o=state['out.weight'].numpy()[0]*150
    def arr(a):return '{'+','.join(arr(v) if np.ndim(v) else format(float(v),'.9g')+'f' if '.' in format(float(v),'.9g') or 'e' in format(float(v),'.9g') else format(float(v),'.9g')+'.0f' for v in a)+'}'
    (ROOT/'weights.h').write_text('static const float rw[8][16]='+arr(w)+';\nstatic const float rb[8]='+arr(b)+';\nstatic const float ro[8]='+arr(o)+';\n')
    report=dict(parameters=sum(p.numel() for p in model.parameters()),epoch=selected,train=metrics(ids[0]),validation=metrics(ids[1]),test=metrics(ids[2]),history=history)
    save('training.json',report);print(json.dumps({k:v for k,v in report.items() if k!='history'}))
if __name__=='__main__':main()
