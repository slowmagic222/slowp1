// Simulates felt's page on the PC over a scripted performance (no text), writing PPM frames for felt/demo.gif.
// g++ -O2 -std=gnu++11 tests/demo_sim.cpp -o demo_sim && ./demo_sim frames 4
// ffmpeg -framerate 15 -start_number 83 -i frames/f%04d.ppm -vf "scale=960:440:flags=neighbor,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=none" demo.gif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../felt_scene.h"
static uint16_t px[scene::W*scene::H], fl[scene::W*scene::H], bs[scene::W*scene::H];
static void save(const char* p){FILE*f=fopen(p,"wb");fprintf(f,"P6\n%d %d\n255\n",scene::W,scene::H);
 for(int i=0;i<scene::W*scene::H;++i){uint16_t c=px[i];unsigned char rgb[3]={(unsigned char)((c>>11)<<3),(unsigned char)(((c>>5)&63)<<2),(unsigned char)((c&31)<<3)};fwrite(rgb,1,3,f);}fclose(f);}
static int white_key(unsigned note){static const unsigned char W12[12]={0,0,1,1,2,3,3,4,4,5,5,6};
 int k=(int)((note/12)*7+W12[note%12])-28; while(k<0)k+=15; while(k>=15)k-=15; return k;}
struct Voice{unsigned note; int on_until; int active;};
int main(int argc,char**argv){
 const char* dir=argv[1]; int step=atoi(argv[2]); // render every step-th paint frame (60 fps paint)
 scene::State st; memset(&st,0,sizeof st);
 unsigned life=0,garden=0,quiet=0; Voice v[6]={}; int vn=0;
 srand(7); st.seed=scene::hash(12345u*2654435761u+1u)|1u;
 const int T=60*26; int out=0;
 static const int VIEW[3]={50176,53248,56320};
 unsigned scale[]={60,62,64,65,67,69,71,72,74,76,77,79};
 int style=0;
 for(int f=0;f<T;++f){
  double s=f/60.0;
  // knobs: tone, release, wash
  st.tone=73; st.release=40+(s>9&&s<13?(int)((s-9)*10):0); if(s>=13)st.release=80;
  st.wash=s<4?20:(s<10?20+(int)((s-4)*13):100);
  if(s>=14&&s<20)style=1; else if(s>=20)style=2; else style=0;
  st.stretch=style; st.start=10;st.loop_in=40;st.loop_out=85;st.width=60;
  int target=VIEW[style],at=st.angle?(int)st.angle:target,gap=target-at,sp=gap/10;
  if(gap&&sp==0)sp=gap>0?1:-1; if(sp>0&&sp<64&&gap>=64)sp=64; if(sp<0&&sp>-64&&gap<=-64)sp=-64; st.angle=(uint32_t)(at+sp);
  st.time+=1;
  // note rate: quiet start, busy middle, calm end
  double rate = s<3?1.0 : s<5?3.0 : s<19?7.0 : s<22?3.0 : 0.6;
  int newnote = (rand()%10000) < (int)(rate/60.0*10000);
  if(newnote){ Voice&x=v[vn]; vn=(vn+1)%6; x.note=scale[rand()%12]-(rand()%3==0?12:0); x.on_until=f+20+rand()%50; x.active=1;
   unsigned h=scene::hash(x.note*2654435761u+f); st.pulse[h%12]=255; if(life>24000)st.pulse[(h>>8)%12]=255;
   life=life+2400>65535?65535:life+2400; garden=garden+16>4096?4096:garden+16; quiet=0;}
  st.keys=0; st.sounding=0;
  for(int i=0;i<6;++i){ if(v[i].active&&f<v[i].on_until){st.keys|=1u<<white_key(v[i].note);++st.sounding;} }
  life-=life>>10; if(++quiet>180&&garden&&(quiet&1))garden--;
  st.garden=garden; st.life=life>>8;
  int fade=10-st.life/32; for(int i=0;i<12;++i)st.pulse[i]=st.pulse[i]>fade?st.pulse[i]-fade:0;
  if(f%2==0) scene::render(px,st,fl,bs);
  if(f%step==0){char p[600];snprintf(p,sizeof p,"%s/f%04d.ppm",dir,out++);save(p);}
 }
 fprintf(stderr,"%d frames, garden %u life %u\n",out,garden,life);
}
