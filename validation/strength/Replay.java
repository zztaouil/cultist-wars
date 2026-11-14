package model;
import java.util.*;
/** Headless adapter around the unmodified supplied Java model. */
public class Replay {
 static Board b; static int turn=0; static int[] last=new int[14];
 static void dump(Action a) {
  StringBuilder s=new StringBuilder("{\"state\":[");
  for(Unit u:b.getUnits()) {int i=u.getUnitId();if(u.getTile()!=null)last[i]=u.getRow()*13+u.getCol();s.append(last[i]+","+u.getHp()+","+u.getPlayerId()+","+(u instanceof CultLeader?1:0)+",");}
  int mask=0;for(Unit u:b.getNeutralUnits())mask|=1<<u.getUnitId();
  s.append((turn%2)+","+(150-turn)+","+Random.seed+","+mask+",1,0],\"action\":[");
  if(a==null||a.getCommand()==Action.Command.WAIT)s.append("0,0,0");
  else if(a instanceof MoveAction){MoveAction m=(MoveAction)a;s.append("1,"+m.getUnitId()+","+(m.getY()*13+m.getX()));}
  else {SpecialAction m=(SpecialAction)a;s.append((a.getCommand()==Action.Command.SHOOT?2:3)+","+m.getUnitId()+","+m.getTargetId());}
  s.append("]}");System.out.println(s);
 }
 public static void main(String[] args){
  E.random=new SplittableRandom(Long.parseLong(args[0]));b=new Board(false);
  StringJoiner w=new StringJoiner(",","{\"walls\":[","]}");for(int p=0;p<91;p++)w.add(b.getTile(p%13,p/13).getType()==Tile.Type.FLOOR?"0":"1");System.out.println(w);
  Scanner in=new Scanner(System.in);
  while(in.hasNextLine()){
   String line=in.nextLine();Action a=Action.parseAction(line.split(" "));b.checkActionValidity(a,turn%2);a=b.getFinalAction(a);dump(a);
   if(a.getCommand()!=Action.Command.WAIT)b.update(b.getUnit(a.getUnitId()),a);
   List<Unit> neutral=b.getNeutralUnits();if(!neutral.isEmpty()){int idx=Random.rand(12);if(idx<neutral.size()){Unit u=neutral.get(idx);List<Action> aa=b.getValidActionsOfNeutralUnit(u);if(!aa.isEmpty())b.update(u,aa.get(Random.rand(aa.size())));}}
   b.removeDeadUnitsFromTiles();turn++;
  }dump(null);
 }
}
