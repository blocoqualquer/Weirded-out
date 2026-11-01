#define HOST_TEST 1
#include "../main.c"
long g_draw_calls, g_vtx;
unsigned char msx[2048];
static unsigned long long rs = 88172645463325252ULL;
static unsigned rr(void){ rs ^= rs<<13; rs ^= rs>>7; rs ^= rs<<17; return (unsigned)(rs>>11); }
int main(int argc,char**argv){
  (void)argc;(void)argv;
  game_init(0,NULL);
  strcpy(savepath,"/tmp/wg_test_save.dat"); remove(savepath);
  has_save=0;
  SceCtrlData pad; memset(&pad,0,sizeof pad); pad.Lx=128; pad.Ly=128;
  unsigned old=0; long battles=0, deaths_seen=0, wins=0, floors=0; int prev=state;
  unsigned dirs[4]={PSP_CTRL_UP,PSP_CTRL_DOWN,PSP_CTRL_LEFT,PSP_CTRL_RIGHT};
  for(long it=0; it<600000; it++){
    unsigned b=0;
    if(state==S_TITLE){ b = PSP_CTRL_CROSS; }
    else if(state==S_EXPLORE){
      int tx=stairx, ty=stairy;
      if((P.hp*100/P.maxhp<55 || P.hunger<30) && rr()%100<70){ tx=firex; ty=firey; }
      if(P.hunger<35 && P.rations>0 && rr()%100<30) b|=PSP_CTRL_SQUARE;
      if(P.legInj||P.armInj){ if(rr()%100<30) b|=PSP_CTRL_TRIANGLE; }
      if(P.x==tx&&P.y==ty){ b|=PSP_CTRL_CROSS; }
      else {
        static int d2[MH][MW]; static int qx[MW*MH],qy[MW*MH];
        for(int j=0;j<MH;j++)for(int i=0;i<MW;i++)d2[j][i]=-1;
        int h=0,t=0; qx[t]=tx;qy[t]=ty;t++; d2[ty][tx]=0;
        while(h<t){int cx=qx[h],cy=qy[h];h++; int DX[4]={0,0,-1,1},DY[4]={-1,1,0,0};
          for(int d=0;d<4;d++){int nx=cx+DX[d],ny=cy+DY[d]; if(nx<0||ny<0||nx>=MW||ny>=MH||map[ny][nx]==T_WALL||d2[ny][nx]>=0)continue; d2[ny][nx]=d2[cy][cx]+1; qx[t]=nx;qy[t]=ny;t++;}}
        int best=-1,bd=1<<30; int DX[4]={0,0,-1,1},DY[4]={-1,1,0,0};
        for(int d=0;d<4;d++){int nx=P.x+DX[d],ny=P.y+DY[d]; if(nx<0||ny<0||nx>=MW||ny>=MH||map[ny][nx]==T_WALL||d2[ny][nx]<0)continue; if(d2[ny][nx]<bd){bd=d2[ny][nx];best=d;}}
        if(rr()%100<10) best=rr()%4;
        if(best>=0) b|=dirs[best];
      }
    } else if(state==S_BATTLE){
      int r=rr()%100;
      if(bphase==1) b=PSP_CTRL_CROSS; else { int hpq=P.hp*100/P.maxhp; if(hpq<35 && rr()%100<60){ b = (rr()%2)? PSP_CTRL_CROSS : 0; bmenu = (P.bandages>0)?2:((P.rations>0)?1:3);} else if(rr()%100<85) bmenu=0; else bmenu=rr()%4; b=PSP_CTRL_CROSS; }
    } else if(state==S_DEAD){ b=PSP_CTRL_CROSS; }
    if(it&1) b&=~(PSP_CTRL_CROSS|PSP_CTRL_SQUARE|PSP_CTRL_TRIANGLE);
    pad.Buttons=b; unsigned pressed=b&~old; old=b;
    update_game(&pad,pressed);
    if(state==S_BATTLE && prev!=S_BATTLE) battles++;
    if(state==S_DEAD && prev!=S_DEAD) deaths_seen++;
    if(prev==S_BATTLE && state==S_EXPLORE) wins++;
    if(it%7==0) render_game();      /* exercita todo o desenho */
    /* invariantes */
    if(state!=S_TITLE){
      if(P.x<0||P.y<0||P.x>=MW||P.y>=MH||map[P.y][P.x]==T_WALL){ printf("ERRO: jogador em posicao invalida (%d,%d) it=%ld\n",P.x,P.y,it); return 1; }
      if(P.hp>P.maxhp||P.hunger>100||P.hunger<0||P.rations<0||P.bandages<0){ printf("ERRO: estado invalido hp=%d/%d fome=%d rac=%d band=%d\n",P.hp,P.maxhp,P.hunger,P.rations,P.bandages); return 1; }
      for(int i=0;i<nen;i++){ Enemy*e=&en[i]; if(e->alive && (e->x<0||e->y<0||e->x>=MW||e->y>=MH||map[e->y][e->x]==T_WALL)){ printf("ERRO: inimigo invalido\n"); return 1; } }
    }
    if(P.floor>floors) floors=P.floor;
    prev=state;
  }
  printf("OK: iteracoes=600000 batalhas=%ld vitorias/fugas=%ld mortes=%ld andar_max=%ld draw_calls=%ld vertices=%ld\n",battles,wins,deaths_seen,floors,g_draw_calls,g_vtx);
  return 0;
}
