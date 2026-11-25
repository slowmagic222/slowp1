/* FELT / TINE experimental multisampler. GPL-3.0-or-later with OP-1 firmware permission.
 * Samples have separate licenses; see each instrument's SOURCES.md.
 * No disk operations, allocation, floating point, or per-sample division.
 * Two adjacent compressed blocks can be interpolated using the next block's predictor.
 */
#include "op1_plugin.h"
#define MS_FRAMES 128u
#define MS_BYTES 68u
typedef struct { uint32_t offset,frames,root_inc; uint8_t root,low,high,take; } ms_sample_t;
#include "bank.h"
#include "loop_metadata.h"
#include "bank_loader.h"
#include "source_makeup.h"


static const int16_t ms_steps[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int8_t ms_indices[8]={-1,-1,-1,-1,2,4,6,8};
/* All state belongs to this engine; another plugin cannot share its voice caches. */
typedef struct {
    uint32_t frame,frac,block,amp,play_start,start_fade;
    int32_t sample,pending,gate,lp,bass;
    uint32_t phase,natural_level;
    int16_t decoded[MS_FRAMES+1];
    uint8_t note,display_note,display_candidate,display_count,source_shift;
    int32_t pad_gain,lofi_speed,lofi_held;
    uint8_t lofi_count;
    uint32_t wash_frame,wash_frac,wash_env,noise,release_level;
    uint32_t loop_block[2];
    uint32_t cached_inc,cached_rate;
    int direction;
    uint32_t loop_start,loop_end;uint8_t loop_valid;
    uint32_t target_start,target_end,render_serial;int32_t edit_gain;uint8_t edit_phase;
    int16_t loop_cache[2][129];
} ms_voice_t;
static ms_voice_t ms_voices[OP1_VOICES];
/* Button octave choices are session state, per synth slot. */
static volatile uint8_t ms_source_mode[8];
static volatile uint8_t ms_trem_rate[8]={1,1,1,1,1,1,1,1};
static uint8_t ms_rr[128];
static volatile uint8_t ms_last_note,ms_last_take;
static volatile int ms_last_sample=-1,ms_last_voice;
static int16_t ms_le16(const uint8_t *p) { return (int16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8)); }
static void ms_decode(const uint8_t *p,int16_t *out) {
    int pred=ms_le16(p),idx=p[2];if(idx>88)idx=88;
    out[0]=(int16_t)pred;
    for(unsigned i=0;i<127;i++) {
        int code=(p[4+(i>>1)]>>((i&1)*4))&15;
        int step=ms_steps[idx],diff=step>>3;
        if(code&4)diff+=step;
        if(code&2)diff+=step>>1;
        if(code&1)diff+=step>>2;
        pred+=(code&8)?-diff:diff;
        if(pred>32767)pred=32767;else if(pred< -32768)pred=-32768;
        idx+=ms_indices[code&7];if(idx<0)idx=0;else if(idx>88)idx=88;
        out[i+1]=(int16_t)pred;
    }
}
static int ms_note(uint32_t inc){
    int lo=0,hi=127;
    while(lo<hi){int mid=(lo+hi)/2;if(ms_note_inc[mid]<inc)lo=mid+1;else hi=mid;}
    uint32_t value=ms_note_inc[lo],d=value>inc?value-inc:inc-value;
    if(lo){uint32_t prev=ms_note_inc[lo-1],pd=prev>inc?prev-inc:inc-prev;if(pd<=d)lo--;}
    return lo;
}
static int ms_select_for(int note,int counter) {
    int chosen=-1,n=0;
    for(unsigned i=0;i<MS_SAMPLE_COUNT;i++)
        if(note>=ms_samples[i].low && note<=ms_samples[i].high)n++;
    if(!n)return -1;
    int wanted=ms_rr[counter]%n;
    ms_rr[counter]=(uint8_t)((wanted+1)%n);
    for(unsigned i=0;i<MS_SAMPLE_COUNT;i++)
        if(note>=ms_samples[i].low && note<=ms_samples[i].high && wanted--==0){chosen=(int)i;break;}
    return chosen;
}
static int ms_select(int note){return ms_select_for(note,note);}
/* Two caches for one crossfaded sustain stream. No separate pad decoder. */
static int32_t ms_read(ms_voice_t *v,const ms_sample_t *s,uint32_t frame,uint32_t frac,int cache) {
    if(frame>=s->frames)return 0;
    uint32_t block=frame>>7;
    if(v->loop_block[cache]!=block){
        const uint8_t *b=ms_audio+s->offset+block*MS_BYTES;
        ms_decode(b,v->loop_cache[cache]);
        v->loop_cache[cache][128]=(block+1)*128<s->frames?ms_le16(b+MS_BYTES):v->loop_cache[cache][127];
        v->loop_block[cache]=block;
    }
    int32_t a=v->loop_cache[cache][frame&127u];
    return a+(((int32_t)v->loop_cache[cache][(frame&127u)+1]-a)*(int32_t)(frac>>1)>>15);
}
/* Page two: blue sample start, green loop start, white loop end. */
static uint32_t ms_sample_start(unsigned si,unsigned value){
 if(value>32767)value=32767;return (uint32_t)(((uint64_t)(ms_samples[si].frames-2)*value)/32767u);
}
static void ms_bounds(unsigned si,unsigned start_trim,unsigned end_trim,uint32_t *lo,uint32_t *hi){
 uint32_t limit=ms_loop_limit[si],minimum=MS_RATE*35u/100u;
 if(start_trim>32767)start_trim=32767;if(end_trim>32767)end_trim=32767;
 uint32_t automatic=ms_loop_start[si],top=limit-minimum;
 uint32_t a=start_trim<=16384u?(automatic*start_trim>>14):automatic+(uint32_t)(((uint64_t)(top-automatic)*(start_trim-16384u))/16383u);
 uint32_t b=end_trim==32767u?ms_loop_end[si]:minimum+(uint32_t)(((uint64_t)(limit-minimum)*end_trim)/32767u);
 if(b>limit)b=limit;if(b<minimum)b=minimum;
 if(a+minimum>b)a=b-minimum;*lo=a;*hi=b;
}
static int ms_loop_allowed(unsigned si,const int16_t *p){
 return ms_loop_enabled[si]||(uint16_t)p[6]!=16384u||(uint16_t)p[7]<32767u;
}
/* Long ping-pong loop, reflected fixed-point position; no extra decoder stream. */
static void ms_advance(ms_voice_t *v,uint32_t rate,int looping){
    uint32_t whole=rate>>16,part=rate&65535u;
    if(!looping)v->direction=1;
    if(v->direction>0){
        v->frac+=rate;v->frame+=v->frac>>16;v->frac&=65535u;
        if(v->edit_phase==1)return;
        if(looping&&v->loop_valid&&v->frame>=v->loop_end-1){
            uint32_t edge=v->loop_end-1;
            if(v->frame-edge>=v->loop_end-v->loop_start){v->frame=edge;v->frac=0;}
            else v->frame=edge-(v->frame-edge)-(v->frac?1u:0u);
            if(v->frac)v->frac=65536u-v->frac;
            v->direction=-1;
        }
    }else{
        if(v->frac<part){v->frame-=whole+1;v->frac+=65536u-part;}
        else{v->frame-=whole;v->frac-=part;}
        if(v->edit_phase==1)return;
        if(v->frame<v->loop_start){
            uint32_t edge=v->loop_start;
            if(edge-v->frame>=v->loop_end-v->loop_start){v->frame=edge;v->frac=0;}
            else v->frame=edge+(edge-v->frame)-(v->frac?1u:0u);
            if(v->frac)v->frac=65536u-v->frac;
            v->direction=1;
        }
    }
}
static void ms_edit_tick(ms_voice_t *v){
            if(v->edit_phase==1){
                v->edit_gain-=512;
                if(v->edit_gain<=0){
                    v->edit_gain=0;v->loop_start=v->target_start;v->loop_end=v->target_end;
                    if(v->frame>=v->loop_end-1){v->frame=v->loop_end-1;v->frac=0;v->direction=-1;}
                    else if(v->direction<0&&v->frame<v->loop_start){v->frame=v->loop_start;v->frac=0;v->direction=1;}
                    v->edit_phase=2;
                }
            }else{
                v->edit_gain+=512;if(v->edit_gain>=32767){v->edit_gain=32767;v->edit_phase=0;}
            }
}
static void ms_on(void *self,const int16_t *p) {
    (void)p;ms_voice_t *v=&ms_voices[op1_voice_index(self)];
    v->frame=0;v->frac=0;v->block=0xffffffffu;v->amp=32767;
    v->release_level=32767u<<16;v->wash_frame=0;v->wash_frac=0;v->wash_env=0;v->noise=0x1234567u+ms_rr[60]+(uint32_t)op1_voice_index(self)*1234567u;
    for(int c=0;c<2;c++)v->loop_block[c]=0xffffffffu;
    v->direction=1;v->loop_valid=0;v->edit_gain=32767;v->edit_phase=0;v->cached_inc=0xffffffffu;v->cached_rate=0;
    v->display_note=255;v->display_candidate=255;v->display_count=0;v->pad_gain=8192;v->lofi_speed=32768;v->lofi_held=0;v->lofi_count=0;
    v->natural_level=1u<<30;v->pending=1;v->gate=1;v->sample=-1;v->lp=0;v->bass=0;v->phase=0;
}
static void ms_off(void *self) { ms_voices[op1_voice_index(self)].gate=0; }
static int16_t ms_control(int16_t raw){
 int32_t v=(int32_t)(uint16_t)raw-256;if(v<=0)return 0;if(v>=32255)return 32767;return (int16_t)(v*32767/32255);
}
static void ms_controls(const int16_t*in,int16_t*out){for(int i=0;i<8;i++)out[i]=i<4?ms_control(in[i]):in[i];}
static void ms_render(void *self,int16_t *out,const int16_t *raw) {
    int16_t controls[8];ms_controls(raw,controls);const int16_t*p=controls;
    ms_voice_t *v=&ms_voices[op1_voice_index(self)];
    v->render_serial++;
    /* The GUI follows settled pitch blocks, never the once-latched sample region.
     * Recheck after a multi-block pitch ramp; three equal endings reject transient crossings. */
    uint8_t shown=(uint8_t)ms_note((uint32_t)OP1_PITCH_INC[OP1_BLOCK-1]);
    if(shown!=v->display_candidate){v->display_candidate=shown;v->display_count=1;}
    else if(v->display_count<3)v->display_count++;
    if(v->display_count>=3){v->display_note=shown;if(op1_voice_index(self)==ms_last_voice)ms_last_note=shown;}

    if(!ms_bank_ready()) {for(int i=0;i<OP1_BLOCK;i++)out[i]=0;return;}
    if(v->pending) {
        v->note=(uint8_t)ms_note((uint32_t)OP1_PITCH_INC[OP1_BLOCK-1]);
        /* Select a higher register's recording, but tune it using the ORIGINAL note.
         * Latch the mapping on note start: held voices never hot-swap sample buffers. */
        v->source_shift=ms_source_mode[op1_current_slot()&7];
        int source_note=(int)v->note+12*(int)v->source_shift;if(source_note>100)source_note=100;
        v->sample=(v->note<24||v->note>100)?-1:ms_select_for(source_note,v->note);v->pending=0;
        ms_last_sample=v->sample;ms_last_voice=op1_voice_index(self);
        if(v->sample>=0){ms_bounds(v->sample,(uint16_t)p[6],(uint16_t)p[7],&v->loop_start,&v->loop_end);v->target_start=v->loop_start;v->target_end=v->loop_end;v->loop_valid=(uint8_t)(ms_loop_allowed(v->sample,p)&&v->play_start<v->loop_end-1);ms_last_take=ms_samples[v->sample].take;v->wash_frame=ms_loop_start[v->sample];
            v->play_start=ms_sample_start(v->sample,(uint16_t)p[5]);v->frame=v->play_start;v->start_fade=v->frame?0u:32767u;}
    }
    int looping=(uint16_t)p[1]!=0||(uint16_t)p[3]!=0;
    if(v->sample>=0&&(!looping||!v->loop_valid)&&v->frame>=ms_samples[v->sample].frames)v->amp=0;
    if(v->sample<0 || !v->amp) {for(int i=0;i<OP1_BLOCK;i++)out[i]=0;return;}
    const ms_sample_t *s=&ms_samples[v->sample];
    uint32_t next_start,next_end;
    ms_bounds(v->sample,(uint16_t)p[6],(uint16_t)p[7],&next_start,&next_end);
    v->target_start=next_start;v->target_end=next_end;
    v->loop_valid=(uint8_t)(ms_loop_allowed(v->sample,p)&&v->play_start<next_end-1);
    if(next_start!=v->loop_start||next_end!=v->loop_end){
        if(looping&&v->loop_valid){if(!v->edit_phase)v->edit_phase=1;}
        else{v->loop_start=next_start;v->loop_end=next_end;}
    }
    /* Input rate is defined by the prepared bank. Multiplication/division only once per 128-frame block. */
    uint32_t inc=(uint32_t)OP1_PITCH_INC[0];
    if(inc!=v->cached_inc){
        v->cached_inc=inc;
        v->cached_rate=(uint32_t)(((uint64_t)inc*(MS_RATE*65536u/OP1_VOICE_RATE))/s->root_inc);
        if(v->cached_rate<256)v->cached_rate=256;
        if(v->cached_rate>262144)v->cached_rate=262144;
    }
    uint32_t rate=v->cached_rate;
    int32_t dark=(uint16_t)p[0],tone=32767-((dark*30000)>>15);
    int32_t lofi=(uint16_t)p[2],wash=(uint16_t)p[3];
    tone-=(((tone-1800)*wash)>>15);
    uint32_t release_ms=150u+((uint32_t)(uint16_t)p[1]*14850u>>15)+((uint32_t)wash*30000u>>15);
    /* Slow wow + a little flutter. Pitch excursion at full amount stays under ~0.3%. */
    int32_t wow=OP1_SINE_TABLE[v->phase>>19],flutter=OP1_SINE_TABLE[(v->phase*13u)>>19];
    int32_t warble=(((wow*64)>>15)+((flutter*16)>>15))*lofi>>15;
    rate=(uint32_t)(((uint64_t)rate*(uint32_t)(32768+warble))>>15);
    v->phase+=1350949u; /* ~0.217 Hz per 128-frame block at 88.2kHz */
    unsigned crush=(unsigned)(lofi*3+16383)>>15;
    uint32_t attack_step=32767u/(1u+((uint32_t)wash*60000u>>15));if(!attack_step)attack_step=1;
    uint32_t decay_coef=ms_loop_decay[v->sample]-(uint32_t)(((uint64_t)ms_loop_decay[v->sample]*wash)>>16);
    uint32_t release_coef=328500u/release_ms;
    uint32_t release_drop=((v->release_level>>15)*release_coef)>>7;if(!release_drop)release_drop=1;
    for(int i=0;i<OP1_BLOCK;i++) {
        if(v->frame>=s->frames) {out[i]=0;if(v->edit_phase)ms_edit_tick(v);continue;}
        int32_t value=ms_read(v,s,v->frame,v->frac,0);
        if(!dark&&!wash)v->lp=value;else v->lp+=((value-v->lp)*tone)>>15;
        value=(v->lp*(int32_t)v->amp)>>15;
        if(looping&&v->frame>=v->loop_start){
            value=(value*(int32_t)(v->natural_level>>15))>>15;
            uint32_t drop=((v->natural_level>>15)*decay_coef)>>15;
            v->natural_level=v->natural_level>drop?v->natural_level-drop:0;
        }
        /* Same decoder stream: soften the attack and darken the tail for Ambient. */
        if(wash){
            if(v->wash_env<32767u){v->wash_env+=attack_step;if(v->wash_env>32767u)v->wash_env=32767u;}
            int32_t envelope=32767-((wash*(32767-(int32_t)v->wash_env))>>15);
            value=(value*envelope)>>15;
        }
        if(lofi){
            value=(value>>crush)*(1<<crush);
            v->noise=v->noise*1664525u+1013904223u;
            int32_t hiss=(int32_t)(v->noise>>16)-32768;
            value+=(((hiss*lofi)>>15)*((int32_t)v->amp>>9))>>15;
        }
        if(v->start_fade<32767u){v->start_fade+=128u;if(v->start_fade>32767u)v->start_fade=32767u;value=(value*(int32_t)v->start_fade)>>15;}
        if(v->edit_phase){
            value=(value*v->edit_gain)>>15;
            ms_edit_tick(v);
        }
        value=(value*(int32_t)ms_source_makeup[v->note][v->source_shift])>>12;
        value=(value*16384)>>15; /* -6 dB master headroom, all modes/effects */
        if(value>24000)value=24000+((value-24000)>>2);
        if(value< -24000)value=-24000+((value+24000)>>2);
        out[i]=op1_sat16(value);
        if(!v->gate&&looping){
            if(v->release_level>release_drop)v->release_level-=release_drop;
            else v->release_level=0;
            if(v->release_level<65536u)v->release_level=0;
            v->amp=v->release_level>>16;
        }
        ms_advance(v,rate,looping);
    }
}
const op1_engine_t MS_ENGINE={ms_on,ms_render,ms_off,{OP1_PRESS_PAGE,OP1_PRESS_PAGE,OP1_PRESS_PAGE,0}};

#include "simple_art.h"
