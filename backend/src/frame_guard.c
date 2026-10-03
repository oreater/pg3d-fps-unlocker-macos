// Enforce rendering budgets in the existing Unity player loop, immediately
// before UpdateAllRenderers. Keep every existing system and call the original
// stage exactly once. No executable code patching or per-frame file I/O.
//
// Optional stage timing (PG3D_STAGE_PROFILE=1): every native leaf stage is
// wrapped with a shim that forwards to its original function exactly once and
// adds the elapsed time to a per-stage counter. Frames are classified by
// whether mouse motion reached the game since the previous frame, so the log
// shows which stages get slower while the camera turns.
#include <limits.h>
#include <mach/mach_time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <time.h>

typedef void *(*Resolver)(const char *);
extern void pg3d_optimizer_frame(void);
extern void pg3d_latency_frame(void);
extern void pg3d_presentation_log(const char *);
extern volatile uint64_t pg3d_motion_events;
static void *(*domain_get)(void);
static const void **(*assemblies_get)(void *, size_t *);
static void *(*image_get)(const void *);
static void *(*class_find)(void *, const char *, const char *);
static void *(*nested_get)(void *, void **);
static const char *(*class_name)(void *);
static const void *(*class_type)(void *);
static void *(*type_object)(const void *);
static void *(*field_find)(void *, const char *);
static size_t (*field_offset)(void *);
static int32_t (*value_size)(void *, uint32_t *);
static uint32_t (*object_header)(void), (*array_header)(void);
static uintptr_t (*array_length)(void *);
static uint32_t (*root_new)(void *, bool);
static void (*root_free)(uint32_t);
static void *(*system_type_class)(void *);
static void *(*declaring_class)(void *);
static void *(*get_loop)(void);
static void (*set_loop)(void *);
static void (*original_stage)(void);
static uint64_t frame_calls, frame_ns;
static bool installed, attempted;
static void before_render(void) {
    struct timespec a,b;
    clock_gettime(CLOCK_MONOTONIC,&a);
    pg3d_optimizer_frame();
    pg3d_latency_frame();
    clock_gettime(CLOCK_MONOTONIC,&b);
    frame_ns += (uint64_t)((b.tv_sec-a.tv_sec)*1000000000LL+b.tv_nsec-a.tv_nsec);
    ++frame_calls;
    original_stage();
}
static void (*guard_slot)(void) = before_render;

// ---- Stage timing -------------------------------------------------------
enum { MOVING, STILL, TRANSITION, KINDS };
enum { STAGE_LIMIT = 192, QUIET_FRAMES = 3 };
typedef struct {
    void (*original)(void);
    void (**variable)(void);       // Unity's own function variable, read at every call.
    void (*slot)(void);            // Target for pointer-style update functions.
    char name[72];
    bool looped;                   // Under a system with a loop condition (FixedUpdate).
    uint64_t ticks[KINDS], session[KINDS];
} Stage;
typedef struct { uint64_t frames[KINDS], loop[KINDS], between[KINDS]; } FrameTotals;
static Stage stages[STAGE_LIMIT];
static unsigned stage_count, frame_start_stage = UINT_MAX, skipped_stages;
static FrameTotals window_totals, session_totals;
static int frame_kind = TRANSITION;
static uint64_t frame_start, loop_end, seen_motion, quiet_run;
static unsigned samples_logged;
static bool profiling;

static void begin_frame(uint64_t now) {
    if (frame_start && loop_end >= frame_start) {
        window_totals.loop[frame_kind] += loop_end - frame_start;
        session_totals.loop[frame_kind] += loop_end - frame_start;
    }
    uint64_t motion = pg3d_motion_events;
    bool moved = motion != seen_motion;
    seen_motion = motion;
    quiet_run = moved ? 0 : quiet_run + 1;
    frame_kind = moved ? MOVING : quiet_run >= QUIET_FRAMES ? STILL : TRANSITION;
    // Time since the previous frame's last stage: AppKit events, run-loop
    // observers and timers that ran before this frame.
    if (loop_end && now >= loop_end) {
        window_totals.between[frame_kind] += now - loop_end;
        session_totals.between[frame_kind] += now - loop_end;
    }
    ++window_totals.frames[frame_kind];
    ++session_totals.frames[frame_kind];
    frame_start = now;
}
static inline void timed_stage(unsigned index) {
    Stage *stage = &stages[index];
    uint64_t start = mach_absolute_time();
    if (index == frame_start_stage) begin_frame(start);
    void (*function)(void) = stage->variable ? *stage->variable : stage->original;
    if (function) function();
    uint64_t end = mach_absolute_time();
    stage->ticks[frame_kind] += end - start;
    stage->session[frame_kind] += end - start;
    loop_end = end;
}
#define W1(b,o) static void stage_##b##_##o(void) { timed_stage((b)*8+(o)); }
#define W8(b) W1(b,0) W1(b,1) W1(b,2) W1(b,3) W1(b,4) W1(b,5) W1(b,6) W1(b,7)
W8(0) W8(1) W8(2) W8(3) W8(4) W8(5) W8(6) W8(7) W8(8) W8(9) W8(10) W8(11)
W8(12) W8(13) W8(14) W8(15) W8(16) W8(17) W8(18) W8(19) W8(20) W8(21) W8(22) W8(23)
#define R8(b) stage_##b##_0, stage_##b##_1, stage_##b##_2, stage_##b##_3, \
              stage_##b##_4, stage_##b##_5, stage_##b##_6, stage_##b##_7
static void (*const stage_wrappers[STAGE_LIMIT])(void) = {
    R8(0), R8(1), R8(2), R8(3), R8(4), R8(5), R8(6), R8(7), R8(8), R8(9), R8(10), R8(11),
    R8(12), R8(13), R8(14), R8(15), R8(16), R8(17), R8(18), R8(19), R8(20), R8(21), R8(22), R8(23)};
#undef W1
#undef W8
#undef R8

void pg3d_frame_guard_bind(void *handle) {
#define B(v,s) *(void **)(&v)=dlsym(handle,s)
    B(domain_get,"il2cpp_domain_get"); B(assemblies_get,"il2cpp_domain_get_assemblies");
    B(image_get,"il2cpp_assembly_get_image"); B(class_find,"il2cpp_class_from_name");
    B(nested_get,"il2cpp_class_get_nested_types"); B(class_name,"il2cpp_class_get_name");
    B(class_type,"il2cpp_class_get_type"); B(type_object,"il2cpp_type_get_object");
    B(field_find,"il2cpp_class_get_field_from_name"); B(field_offset,"il2cpp_field_get_offset");
    B(value_size,"il2cpp_class_value_size"); B(object_header,"il2cpp_object_header_size");
    B(array_header,"il2cpp_array_object_header_size"); B(array_length,"il2cpp_array_length");
    B(root_new,"il2cpp_gchandle_new"); B(root_free,"il2cpp_gchandle_free");
    B(system_type_class,"il2cpp_class_from_system_type"); B(declaring_class,"il2cpp_class_get_declaring_type");
#undef B
}
static void *find_class(const char *space,const char *name) {
    size_t n=0; const void **a=assemblies_get(domain_get(),&n);
    if(n>4096)return NULL;
    for(size_t i=0;i<n;i++){void *c=class_find(image_get(a[i]),space,name);if(c)return c;}
    return NULL;
}
static bool memory_access(void *p,size_t size,vm_prot_t required) {
    mach_vm_address_t address=(mach_vm_address_t)p;
    mach_vm_size_t length=0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object=MACH_PORT_NULL;
    kern_return_t result=mach_vm_region(mach_task_self(),&address,&length,
        VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object);
    if(object!=MACH_PORT_NULL)mach_port_deallocate(mach_task_self(),object);
    return result==KERN_SUCCESS && address<=(mach_vm_address_t)p &&
        (mach_vm_address_t)p-address<=length && size<=length-((mach_vm_address_t)p-address) &&
        (info.protection&required)==required;
}
static bool unity_function(void *p) {
    Dl_info info={0};
    return p && memory_access(p,1,VM_PROT_EXECUTE) && dladdr(p,&info) &&
        info.dli_fname && strstr(info.dli_fname,"/UnityPlayer.dylib");
}
static bool (*unity_code)(void *)=unity_function; // Replaced by unit tests only.
// Resolve an update callback into the Unity function it runs. Unity stores either
// the function itself or a pointer to a variable holding it; keep that form.
static bool resolve_callback(void *callback,void (**function)(void),bool *indirect) {
    if(unity_code(callback)){*function=(void (*)(void))callback;*indirect=false;return true;}
    if(!memory_access(callback,sizeof(void *),VM_PROT_READ))return false;
    void *target=NULL;memcpy(&target,callback,sizeof(target));
    if(!unity_code(target))return false;
    *function=(void (*)(void))target;*indirect=true;return true;
}
static void stage_name(void *type,char *out,size_t size) {
    void *klass=type&&system_type_class?system_type_class(type):NULL;
    const char *name=klass?class_name(klass):NULL;
    void *parent=klass&&declaring_class?declaring_class(klass):NULL;
    const char *parent_name=parent?class_name(parent):NULL;
    if(name&&parent_name)snprintf(out,size,"%s.%s",parent_name,name);
    else snprintf(out,size,"%s",name?name:"unnamed");
}
void pg3d_frame_guard_start(Resolver resolve,bool observe) {
    if(observe || attempted)return;
    attempted=true;
    if(!domain_get||!assemblies_get||!image_get||!class_find||!nested_get||!class_name||
       !class_type||!type_object||!field_find||!field_offset||!value_size||!object_header||
       !array_header||!array_length||!root_new||!root_free)goto unavailable;
    get_loop=(void *(*)(void))resolve("UnityEngine.LowLevel.PlayerLoop::GetCurrentPlayerLoopInternal");
    set_loop=(void (*)(void *))resolve("UnityEngine.LowLevel.PlayerLoop::SetPlayerLoopInternal");
    if(!get_loop||!set_loop)goto unavailable;
    void *layout=find_class("UnityEngine.LowLevel","PlayerLoopSystemInternal");
    void *post=find_class("UnityEngine.PlayerLoop","PostLateUpdate"), *stage=NULL;
    if(!layout||!post)goto unavailable;
    void *iterator=NULL,*nested;
    while((nested=nested_get(post,&iterator)))
        if(strcmp(class_name(nested),"UpdateAllRenderers")==0){stage=nested;break;}
    if(!stage)goto unavailable;
    const char *names[]={"type","updateDelegate","updateFunction","loopConditionFunction","numSubSystems"};
    size_t offsets[5];
    uint32_t alignment=0,header=object_header(),array_start=array_header();
    int32_t stride=value_size(layout,&alignment);
    if(stride!=40 || header!=16 || array_start<16 || array_start>128)goto unavailable;
    for(unsigned i=0;i<5;i++) {
        void *field=field_find(layout,names[i]); if(!field)goto unavailable;
        size_t offset=field_offset(field);
        if(offset<header)goto unavailable;
        offsets[i]=offset-header;
        if(offsets[i]!=i*8)goto unavailable;
    }
    const char *profile=getenv("PG3D_STAGE_PROFILE");
    bool want_profile=profile&&strcmp(profile,"1")==0&&system_type_class&&declaring_class;
    void *type=type_object(class_type(stage));
    void *array=get_loop(); if(!array)goto unavailable;
    uint32_t root=root_new(array,false); if(!root)goto unavailable;
    uintptr_t count=array_length(array);
    bool success=false;
    uintptr_t guard_index=UINTPTR_MAX;
    void (*guard_function)(void)=NULL; bool guard_indirect=false;
    if(count>0 && count<=2048) for(uintptr_t i=0;i<count;i++) {
        char *entry=(char *)array+array_start+i*(size_t)stride;
        void *entry_type=NULL,*callback=NULL,*delegate=NULL; int children=-1;
        memcpy(&entry_type,entry+offsets[0],sizeof(void *));
        if(entry_type!=type)continue;
        memcpy(&delegate,entry+offsets[1],sizeof(void *));
        memcpy(&callback,entry+offsets[2],sizeof(void *));
        memcpy(&children,entry+offsets[4],sizeof(children));
        if(delegate || children!=0 || !callback)break;
        if(resolve_callback(callback,&guard_function,&guard_indirect))guard_index=i;
        break;
    }
    if(guard_index!=UINTPTR_MAX) {
        original_stage=guard_function;
        unsigned planned=0,skipped=0;
        bool tree_valid=want_profile;
        static uintptr_t plan_entry[STAGE_LIMIT];
        static bool plan_indirect[STAGE_LIMIT];
        // Pre-order walk: entry 0 is the root and each entry lists its number of
        // direct children. Only leaves with a native Unity function are timed.
        int remaining[16]; bool loop_parent[16]; unsigned depth=0;
        for(uintptr_t i=0;tree_valid && i<count;i++) {
            char *entry=(char *)array+array_start+i*(size_t)stride;
            void *entry_type=NULL,*callback=NULL,*delegate=NULL,*condition=NULL; int children=-1;
            memcpy(&entry_type,entry+offsets[0],sizeof(void *));
            memcpy(&delegate,entry+offsets[1],sizeof(void *));
            memcpy(&callback,entry+offsets[2],sizeof(void *));
            memcpy(&condition,entry+offsets[3],sizeof(void *));
            memcpy(&children,entry+offsets[4],sizeof(children));
            while(depth>0&&remaining[depth-1]==0)--depth;
            if((i>0&&depth==0)||children<0||(uintptr_t)children>=count){tree_valid=false;break;}
            bool looped=depth>0&&loop_parent[depth-1];
            if(depth>0)--remaining[depth-1];
            if(children==0&&callback&&!delegate) {
                void (*function)(void)=NULL; bool indirect=false;
                if(planned<STAGE_LIMIT&&resolve_callback(callback,&function,&indirect)) {
                    Stage *s=&stages[planned];
                    // The render stage keeps its validated guard inside the timer.
                    s->original=i==guard_index?before_render:function; s->looped=looped;
                    s->variable=indirect&&i!=guard_index?(void (**)(void))callback:NULL;
                    stage_name(entry_type,s->name,sizeof(s->name));
                    plan_entry[planned]=i; plan_indirect[planned]=indirect;
                    ++planned;
                } else ++skipped;
            }
            if(children>0) {
                if(depth==16){tree_valid=false;break;}
                remaining[depth]=children; loop_parent[depth]=looped||condition!=NULL; ++depth;
            }
        }
        // Frames start at the first stage outside any looping system.
        if(tree_valid) for(unsigned k=0;k<planned;k++) if(!stages[k].looped){frame_start_stage=k;break;}
        bool guard_planned=false;
        for(unsigned k=0;k<planned;k++) guard_planned|=plan_entry[k]==guard_index;
        bool profiled=tree_valid&&frame_start_stage!=UINT_MAX&&guard_planned;
        if(profiled) {
            for(unsigned k=0;k<planned;k++) {
                char *entry=(char *)array+array_start+plan_entry[k]*(size_t)stride;
                stages[k].slot=stage_wrappers[k];
                void *replacement=plan_indirect[k]?(void *)&stages[k].slot:(void *)stage_wrappers[k];
                memcpy(entry+offsets[2],&replacement,sizeof(replacement));
            }
            stage_count=planned; skipped_stages=skipped; profiling=true;
        } else {
            char *entry=(char *)array+array_start+guard_index*(size_t)stride;
            void *replacement=guard_indirect?(void *)&guard_slot:(void *)before_render;
            memcpy(entry+offsets[2],&replacement,sizeof(replacement));
        }
        set_loop(array);
        success=true;installed=true;
        pg3d_presentation_log("optimizer frame guard installed: before UpdateAllRenderers; original stage preserved; no timer delay on camera discovery.");
        char line[200];
        if(profiled)snprintf(line,sizeof(line),"frame profiler installed: %u native stages timed, %u skipped; each original stage still runs exactly once.",stage_count,skipped_stages);
        else snprintf(line,sizeof(line),"frame profiler %s.",want_profile?"UNAVAILABLE: player-loop layout not recognized":"off");
        pg3d_presentation_log(line);
    }
    root_free(root);
    if(success)return;
unavailable:
    pg3d_presentation_log("optimizer frame guard UNAVAILABLE: player-loop validation failed; timed fallback only.");
}

static double ticks_ms(uint64_t ticks) {
    static mach_timebase_info_data_t base;
    if(base.denom==0)mach_timebase_info(&base);
    return (double)ticks*base.numer/base.denom/1e6;
}
typedef struct { unsigned index; double moving, still; } Delta;
static int by_delta(const void *a,const void *b) {
    double x=((const Delta *)a)->moving-((const Delta *)a)->still;
    double y=((const Delta *)b)->moving-((const Delta *)b)->still;
    return (y>x)-(y<x);
}
// One summary line and one stage line: which stages cost more per frame on
// frames with mouse motion than on frames without it.
static void log_profile(const char *label,FrameTotals *t,bool session) {
    char line[1400];
    uint64_t moving=t->frames[MOVING],still=t->frames[STILL];
    double frame_moving=moving?ticks_ms(t->loop[MOVING]+t->between[MOVING])/moving:0;
    double frame_still=still?ticks_ms(t->loop[STILL]+t->between[STILL])/still:0;
    snprintf(line,sizeof(line),"%s: frames moving/still/other=%llu/%llu/%llu; frame_ms moving=%.3f still=%.3f; between_frames_ms moving=%.3f still=%.3f.",
        label,(unsigned long long)moving,(unsigned long long)still,(unsigned long long)t->frames[TRANSITION],
        frame_moving,frame_still,moving?ticks_ms(t->between[MOVING])/moving:0,still?ticks_ms(t->between[STILL])/still:0);
    pg3d_presentation_log(line);
    if(moving<30||still<30)return;
    Delta deltas[STAGE_LIMIT];
    for(unsigned i=0;i<stage_count;i++) {
        uint64_t *ticks=session?stages[i].session:stages[i].ticks;
        deltas[i]=(Delta){i,ticks_ms(ticks[MOVING])/moving,ticks_ms(ticks[STILL])/still};
    }
    qsort(deltas,stage_count,sizeof(Delta),by_delta);
    int used=snprintf(line,sizeof(line),"%s stages (ms/frame moving vs still):",label);
    for(unsigned i=0;i<stage_count&&i<10&&used>0&&(size_t)used<sizeof(line)-120;i++)
        used+=snprintf(line+used,sizeof(line)-used," %s %+.3f (%.3f/%.3f);",stages[deltas[i].index].name,
            deltas[i].moving-deltas[i].still,deltas[i].moving,deltas[i].still);
    pg3d_presentation_log(line);
}
void pg3d_frame_guard_sample(void) {
    char line[180];
    snprintf(line,sizeof(line),"optimizer frame guard: installed=%s; calls=%llu; mean_us=%.2f.",
        installed?"yes":"no",(unsigned long long)frame_calls,frame_calls?(double)frame_ns/frame_calls/1000.0:0);
    pg3d_presentation_log(line);
    frame_calls=frame_ns=0;
    if(!profiling)return;
    log_profile("frame profile",&window_totals,false);
    if(++samples_logged%12==0)log_profile("frame profile session",&session_totals,true);
    memset(&window_totals,0,sizeof(window_totals));
    for(unsigned i=0;i<stage_count;i++)memset(stages[i].ticks,0,sizeof(stages[i].ticks));
}
