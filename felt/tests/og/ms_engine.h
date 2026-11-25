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
#include "felt_makeup.h"
#include "source_makeup.h"
static const int16_t ms_steps[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int8_t ms_indices[8]={-1,-1,-1,-1,2,4,6,8};
/* All state belongs to this engine; another plugin cannot share its voice caches. */
typedef struct {
    uint32_t frame,frac,block,amp;
    int32_t sample,pending,gate,lp,bass;
    uint32_t phase;
    int16_t decoded[MS_FRAMES+1];
    uint8_t note,display_note,display_candidate,display_count,source_shift;
    int32_t pad_gain,lofi_speed,lofi_held,start_gain;
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
static volatile uint8_t ms_lofi_octaves[8];
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
static int ms_select(int note) {
    int chosen=-1,n=0;
    for(unsigned i=0;i<MS_SAMPLE_COUNT;i++)
        if(note>=ms_samples[i].low && note<=ms_samples[i].high)n++;
    if(!n)return -1;
    int wanted=ms_rr[note]%n;
    ms_rr[note]=(uint8_t)((wanted+1)%n);
    for(unsigned i=0;i<MS_SAMPLE_COUNT;i++)
        if(note>=ms_samples[i].low && note<=ms_samples[i].high && wanted--==0){chosen=(int)i;break;}
    return chosen;
}
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
/* Editor words: blue sample start, green loop start, white loop end.
 * p7==0 identifies the prior two-marker layout until main-loop migration. */
static void ms_editor_values(const int16_t*p,unsigned *attack,unsigned *start,unsigned *end){
 if(!p[7]){*attack=0;*start=(uint16_t)p[5];*end=(uint16_t)p[6];}
 else{*attack=(uint16_t)p[5];*start=(uint16_t)p[6];*end=(uint16_t)p[7];}
 if(*attack<=1)*attack=0;if(*start<=1)*start=0;
}
static void ms_bounds_preferred(unsigned si,unsigned start_trim,unsigned end_trim,unsigned moving_end,uint32_t *lo,uint32_t *hi){
 uint32_t guard=MS_RATE/4u,limit=ms_loop_limit[si];
 if(start_trim>32767)start_trim=32767;if(end_trim>32767)end_trim=32767;
 uint32_t room=limit-guard-MS_RATE;
 uint32_t a=start_trim?guard+((room*start_trim)>>15):ms_loop_start[si];
 uint32_t b=end_trim==32767u?ms_loop_end[si]:guard+MS_RATE+((room*end_trim)>>15);
 /* Clamp the marker being moved; never push its stationary neighbour. */
 if(a+MS_RATE>b){if(moving_end)b=a+MS_RATE;else a=b-MS_RATE;}
 *lo=a;*hi=b;
}
static void ms_bounds(unsigned si,unsigned start_trim,unsigned end_trim,uint32_t *lo,uint32_t *hi){ms_bounds_preferred(si,start_trim,end_trim,0,lo,hi);}
static uint32_t ms_editor_cookie(int slot);
static uint32_t ms_marker_cookie[8];
typedef struct {uint32_t start_word,end_word,lo,hi;uint8_t valid;} ms_marker_value_t;
typedef struct {volatile uint32_t sequence;ms_marker_value_t value;} ms_marker_cache_t;
static ms_marker_cache_t ms_markers[8][MS_SAMPLE_COUNT];
static void ms_patch_bounds(unsigned si,const int16_t*p,int slot,int commit,uint32_t*lo,uint32_t*hi){
 unsigned attack,start,end;ms_editor_values(p,&attack,&start,&end);
 slot&=7;uint32_t cookie=ms_editor_cookie(slot);int fresh=ms_marker_cookie[slot]!=cookie;
 if(commit&&fresh){for(unsigned i=0;i<MS_SAMPLE_COUNT;i++)ms_markers[slot][i].value.valid=0;ms_marker_cookie[slot]=cookie;fresh=0;}
 ms_marker_cache_t*c=&ms_markers[slot][si];ms_marker_value_t previous={0};uint32_t a,b;
 /* One audio writer, bounded GUI snapshots; never spin in either callback. */
 for(unsigned attempt=0;attempt<2;attempt++){
  uint32_t sequence=c->sequence;if(sequence&1u)continue;
  __asm__ __volatile__("" ::: "memory");ms_marker_value_t copy=c->value;
  __asm__ __volatile__("" ::: "memory");if(sequence==c->sequence){previous=copy;break;}
 }
 ms_bounds_preferred(si,start,end,0,&a,&b);
 if(previous.valid&&!fresh){
  if(start==previous.start_word&&end==previous.end_word){a=previous.lo;b=previous.hi;}
  else if(end==previous.end_word){
   /* Green alone moved: retain the actual white marker, even after a clamp. */
   b=previous.hi;uint32_t guard=MS_RATE/4u,room=ms_loop_limit[si]-guard-MS_RATE;
   a=start?guard+((room*start)>>15):ms_loop_start[si];if(a+MS_RATE>b)a=b-MS_RATE;
  }else if(start==previous.start_word){
   /* White alone moved: retain the actual green marker. */
   a=previous.lo;uint32_t guard=MS_RATE/4u,room=ms_loop_limit[si]-guard-MS_RATE;
   b=end==32767u?ms_loop_end[si]:guard+MS_RATE+((room*end)>>15);if(b<a+MS_RATE)b=a+MS_RATE;
  }
 }
 *lo=a;*hi=b;
 if(commit){
  c->sequence++;__asm__ __volatile__("" ::: "memory");
  c->value.start_word=start;c->value.end_word=end;c->value.lo=a;c->value.hi=b;c->value.valid=1;
  __asm__ __volatile__("" ::: "memory");c->sequence++;
 }
}
static uint32_t ms_sample_start(unsigned si,const int16_t*p,uint32_t loop_start){
 unsigned attack,start,end;ms_editor_values(p,&attack,&start,&end);
 uint32_t frame=(uint32_t)(((uint64_t)ms_samples[si].frames*attack)>>15);
 return frame>loop_start?loop_start:frame;
}
static int ms_loop_allowed(unsigned si,const int16_t*p){
 unsigned attack,start,end;ms_editor_values(p,&attack,&start,&end);
 return ms_loop_enabled[si]||start!=0||end<32767u;
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
    v->start_gain=32767;v->pending=1;v->gate=1;v->sample=-1;v->lp=0;v->bass=0;v->phase=0;
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
        v->source_shift=ms_lofi_octaves[op1_current_slot()&7];
        int source_note=(int)v->note+12*(int)v->source_shift;if(source_note>127)source_note=127;
        v->sample=ms_select(source_note);v->pending=0;
        ms_last_sample=v->sample;ms_last_voice=op1_voice_index(self);
        if(v->sample>=0){ms_patch_bounds(v->sample,p,op1_current_slot()&7,1,&v->loop_start,&v->loop_end);v->target_start=v->loop_start;v->target_end=v->loop_end;v->loop_valid=(uint8_t)ms_loop_allowed(v->sample,p);ms_last_take=ms_samples[v->sample].take;v->wash_frame=ms_loop_start[v->sample];
            v->frame=ms_sample_start(v->sample,p,v->loop_start);v->start_gain=v->frame?0:32767;}
    }
    int looping=(uint16_t)p[1]!=0||(uint16_t)p[3]!=0;
    if(v->sample>=0&&(!looping||!v->loop_valid)&&v->frame>=ms_samples[v->sample].frames)v->amp=0;
    if(v->sample<0 || !v->amp) {for(int i=0;i<OP1_BLOCK;i++)out[i]=0;return;}
    const ms_sample_t *s=&ms_samples[v->sample];
    uint32_t next_start,next_end;
    ms_patch_bounds(v->sample,p,op1_current_slot()&7,1,&next_start,&next_end);
    v->target_start=next_start;v->target_end=next_end;
    v->loop_valid=(uint8_t)ms_loop_allowed(v->sample,p);
    if(next_start!=v->loop_start||next_end!=v->loop_end){
        if(looping&&v->loop_valid){if(!v->edit_phase)v->edit_phase=1;}
        else{v->loop_start=next_start;v->loop_end=next_end;}
    }
    /* Input rate is defined by the prepared bank. Multiplication/division only once per 128-frame block. */
    uint32_t inc=(uint32_t)OP1_PITCH_INC[0];
    if(inc!=v->cached_inc){
        v->cached_inc=inc;
        v->cached_rate=(uint32_t)(((uint64_t)inc*(MS_RATE*65536u/44100u))/s->root_inc);
        if(v->cached_rate<256)v->cached_rate=256;
        if(v->cached_rate>262144)v->cached_rate=262144;
    }
    uint32_t rate=v->cached_rate;
    unsigned felt_index=(uint16_t)p[0]>>13,blend=(uint16_t)p[0]&8191u;
    int32_t ga=ms_felt_makeup[v->sample][felt_index],gb=ms_felt_makeup[v->sample][felt_index+1];
    int32_t felt_gain=ga+(((gb-ga)*(int32_t)blend)>>13);
    int32_t felt=(uint16_t)p[0];
    int32_t tone=32767-((felt*29567)>>15);
    /* FELT: tone, decay/release, tape, wash. */

    int32_t lofi=(uint16_t)p[2],wash=(uint16_t)p[3];
    /* root_inc already retunes the selected higher recording to the original input pitch.
     * Do NOT apply another octave divisor: that would lower the played note again. */
    v->lofi_speed=32768;
    unsigned crush=(unsigned)(lofi*6+16383)>>15;
    unsigned hold=1u+((unsigned)(lofi*5+16383)>>15);
    /* Wash reshapes this same stream: darker, slow attack, extended release. */
    tone-=((tone*wash)>>16);
    /* Green == 0: forward one-shot, internal release bypassed; stock T2 still applies.
     * Above zero: green sets approximately 0.2..20 seconds to -60dB, independent of Wash.
     * Precompute a per-sample decrement once per block: no division in the sample loop. */
    uint32_t release_ms=200u+((uint32_t)(uint16_t)p[1]*19800u>>15)+((uint32_t)(uint16_t)p[3]*40000u>>15);
    uint32_t release_coef=657000u/release_ms;
    uint32_t release_drop=((v->release_level>>15)*release_coef)>>7;
    if(!release_drop)release_drop=1;
    /* Wash raises the retained tail gently, with block smoothing, in the same decoder stream. */
    int32_t pad_target=8192;
    if(wash&&v->frame>=v->loop_start)pad_target+=((wash*12288)>>15);
    v->pad_gain+=(pad_target-v->pad_gain)>>4;
    for(int i=0;i<OP1_BLOCK;i++) {
        if(v->frame>=s->frames) {out[i]=0;if(v->edit_phase)ms_edit_tick(v);continue;}
        int32_t value=ms_read(v,s,v->frame,v->frac,0);
        if(v->start_gain<32767){value=(value*v->start_gain)>>15;v->start_gain+=512;if(v->start_gain>32767)v->start_gain=32767;}
        if(wash){
            if(v->wash_env<32767u)v->wash_env++;
            int32_t attack=32767-((wash*(32767-(int32_t)v->wash_env))>>15);
            value=(value*attack)>>15;
        }
        if(!felt&&!wash)v->lp=value;else v->lp+=((value-v->lp)*tone)>>15;
        value=(v->lp*(int32_t)v->amp)>>15;
        if(wash)value=op1_sat16((value*v->pad_gain)>>13);
        if(lofi){
            /* 16 -> 10 bit staircase, plus sample-and-hold down to ~14.7 kHz on the voice bus.
             * No makeup gain/compressor. Silent input stays silent apart from low-level hiss. */
            if(!v->lofi_count){
                v->lofi_held=(value>>crush)*(1<<crush);v->lofi_count=(uint8_t)hold;
            }
            v->lofi_count--;value=v->lofi_held;
            v->noise=v->noise*1664525u+1013904223u;
            int32_t hiss=((int32_t)(v->noise>>16)-32768);
            value+=((((hiss*lofi)>>15)*((int32_t)v->amp>>8))>>15);
        }else v->lofi_count=0;
        if(v->edit_phase){
            value=(value*v->edit_gain)>>15;
            ms_edit_tick(v);
        }
        value=(value*felt_gain)>>12;
        /* Fixed measured makeup; no pitch/rate changes or extra decoder stream. */
        value=(value*(int32_t)ms_source_makeup[v->note][v->source_shift])>>12;
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
