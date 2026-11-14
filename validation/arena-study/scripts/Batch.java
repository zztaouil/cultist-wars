package model;
import java.util.*;
import java.io.*;
public class Batch {
 static Board b; static int turn; static int[] last=new int[14];
 static String dump(Action a){
  StringBuilder s=new StringBuilder("[");
  for(Unit u:b.getUnits()){int i=u.getUnitId();if(u.getTile()!=null)last[i]=u.getRow()*13+u.getCol();s.append(last[i]+","+u.getHp()+","+u.getPlayerId()+","+(u instanceof CultLeader?1:0)+",");}
  int mask=0;for(Unit u:b.getNeutralUnits())mask|=1<<u.getUnitId();
  s.append((turn%2)+","+(150-turn)+","+Random.seed+","+mask+",1,0],[");
  if(a==null||a.getCommand()==Action.Command.WAIT)s.append("0,0,0");
  else if(a instanceof MoveAction){MoveAction m=(MoveAction)a;s.append("1,"+m.getUnitId()+","+(m.getY()*13+m.getX()));}
  else {SpecialAction m=(SpecialAction)a;s.append((a.getCommand()==Action.Command.SHOOT?2:3)+","+m.getUnitId()+","+m.getTargetId());}
  return s.append("]").toString();
 }
 public static void main(String[] args) throws Exception{
  BufferedReader in=new BufferedReader(new InputStreamReader(System.in));PrintStream out=new PrintStream(new BufferedOutputStream(System.out),false);
  String line;
  while((line=in.readLine())!=null){
   String[] h=line.split(" ");String id=h[1];long seed=Long.parseLong(h[2]);int n=Integer.parseInt(h[3]);
   E.random=new SplittableRandom(seed);Random.seed=42;b=new Board(false);turn=0;last=new int[14];
   StringBuilder w=new StringBuilder();for(int p=0;p<91;p++)w.append(b.getTile(p%13,p/13).getType()==Tile.Type.FLOOR?'0':'1');
   out.println("G "+id+" "+w);
   boolean bad=false;
   for(int k=0;k<n;k++){
    String l=in.readLine();if(bad)continue;
    try{Action a=Action.parseAction(l.trim().split(" "));b.checkActionValidity(a,turn%2);a=b.getFinalAction(a);out.println("S "+dump(a));
     if(a.getCommand()!=Action.Command.WAIT)b.update(b.getUnit(a.getUnitId()),a);
     List<Unit> neutral=b.getNeutralUnits();if(!neutral.isEmpty()){int idx=Random.rand(12);if(idx<neutral.size()){Unit u=neutral.get(idx);List<Action> aa=b.getValidActionsOfNeutralUnit(u);if(!aa.isEmpty())b.update(u,aa.get(Random.rand(aa.size())));}}
     b.removeDeadUnitsFromTiles();turn++;
    }catch(Exception e){out.println("X "+k+" "+e);bad=true;}
   }
   if(!bad)out.println("S "+dump(null));
   out.println("E");
  }
  out.flush();
 }
}
