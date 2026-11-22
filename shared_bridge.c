#define _GNU_SOURCE
#include <dlfcn.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <termios.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* Version-locked experiment for installed launcher and ZClaw builds. No system binary patched. */
static const unsigned char supported_id[20] = {0xea,0xde,0xf4,0x81,0x0b,0xc7,0xf6,0xda,0x27,0x6d,0x07,0xb3,0xc5,0x98,0x1c,0xe9,0x43,0xc3,0xee,0x45};
static const unsigned char launcher_id[20] = {0x32,0x5e,0x7b,0xa6,0x3a,0x58,0x3a,0x9e,0x3f,0xfe,0x5f,0x12,0x61,0xa9,0x16,0xd0,0xc0,0xc7,0xfd,0x2f};
static const unsigned char *expected_id=supported_id;
static int launcher_profile;
static uintptr_t base;
static int eligible, listen_fd=-1, client_fd=-1;
static uint64_t nonce;
static char socket_path[108];
static struct timespec accepted_at;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static int (*real_clockwait)(sem_t*,clockid_t,const struct timespec*);
static int (*real_wait)(sem_t*);
static uint32_t (*real_key)(void*);
static int (*real_key_state)(void*);
static int fn_down, consumed_v;
static void *status_overlay;

static int visit_executable(struct dl_phdr_info *i, size_t size, void *data) {
    (void)size; (void)data;
    if (i->dlpi_name && *i->dlpi_name) return 0;
    for (int n=0;n<i->dlpi_phnum;n++) {
        if (i->dlpi_phdr[n].p_type != PT_NOTE) continue;
        const unsigned char *p=(void *)(i->dlpi_addr+i->dlpi_phdr[n].p_vaddr);
        const unsigned char *end=p+i->dlpi_phdr[n].p_memsz;
        while (p+sizeof(Elf64_Nhdr)<=end) {
            Elf64_Nhdr h; memcpy(&h,p,sizeof h); p+=sizeof h;
            size_t a=(h.n_namesz+3u)&~3u, b=(h.n_descsz+3u)&~3u;
            if (a>(size_t)(end-p) || b>(size_t)(end-p-a)) break;
            if (h.n_type==NT_GNU_BUILD_ID && h.n_namesz==4 && h.n_descsz==20 && !memcmp(p,"GNU",4) && !memcmp(p+a,expected_id,20)) {
                base=i->dlpi_addr; eligible=1; return 1;
            }
            p+=a+b;
        }
    }
    return 1;
}
static void init(void) {
    real_clockwait=dlsym(RTLD_NEXT,"sem_clockwait"); real_wait=dlsym(RTLD_NEXT,"sem_wait");
    real_key=dlsym(RTLD_NEXT,"libinput_event_keyboard_get_key");
    real_key_state=dlsym(RTLD_NEXT,"libinput_event_keyboard_get_key_state");
#ifdef BRIDGE_TEST
    eligible=1; nonce=0x12345678; launcher_profile=getenv("MOCK_TERMINAL")!=NULL; return;
#endif
    char exe[512]; ssize_t n=readlink("/proc/self/exe",exe,sizeof(exe)-1);
    if(n<0) return;
    exe[n]=0;
    if(!strcmp(exe,"/usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch")) { launcher_profile=1; expected_id=launcher_id; }
    else if(strcmp(exe,"/usr/share/APPLaunch/bin/ZClaw")) return;
    dl_iterate_phdr(visit_executable,NULL);
    if(!eligible || getrandom(&nonce,sizeof nonce,0)!=(ssize_t)sizeof nonce) { eligible=0; return; }
}
#ifdef BRIDGE_TEST
static uintptr_t test_address(uintptr_t off) {
    const char *name=NULL;
    switch(off) {
    case 0x4919c0: name="mock_context"; break;
    case 0x490040: name="mock_generation"; break;
    case 0x491964: name="mock_paused"; break;
    case 0x49003c: name="mock_running"; break;
    case 0x48cf40: name="mock_textarea_class"; break;
    case 0x1f1c78: name="mock_obj_check_type"; break;
    case 0x1f1b10: name="mock_obj_has_state"; break;
    case 0x1e7f70: name="mock_obj_is_visible"; break;
    case 0x1c471c: name="mock_get_password_mode"; break;
    case 0x1e3348: name="mock_get_child_count"; break;
    case 0x1e3018: name="mock_get_child"; break;
    case 0x1d73f8: name="mock_screen"; break;
    case 0x1d7410: name="mock_top"; break;
    case 0x1c3590: name="mock_add_text"; break;
    case 0x4d95b0: name="mock_event_count"; break; case 0x4d9600: name="mock_event_dsc"; break;
    case 0x42bf74: name="mock_event_cb"; break; case 0x42bfa0: name="mock_event_data"; break;
    case 0xaa6c0: name="mock_terminal_callback"; break; case 0xa9924: name="mock_help_visible"; break;
    case 0x9cd94: name="mock_pty_write"; break;
    case 0x1cbebc: name="mock_label_create"; break;
    case 0x1cbef0: name="mock_label_set_text"; break;
    case 0x1e25e8: name="mock_obj_delete"; break;
    case 0x1e45ec: name="mock_obj_set_pos"; break;
    case 0x1eac20: name="mock_obj_set_color"; break;
    case 0x1eb7a0: name="mock_obj_set_color"; break;
    case 0x1eac70: name="mock_obj_set_opa"; break;
    case 0x149b90: name="mock_color_hex"; break;
    }
    return (uintptr_t)dlsym(RTLD_DEFAULT,name);
}
#define ADDRESS(off) test_address(off)
#else
static uintptr_t runtime_address(uintptr_t off) {
    if(!launcher_profile) return base+off;
    switch(off) {
    case 0x4919c0: off=0x7b5bf0; break; case 0x490040: off=0x7b3e80; break;
    case 0x491964: off=0x7b5b94; break; case 0x49003c: off=0x7b3e7c; break;
    case 0x48cf40: off=0x7ad288; break; case 0x1f1c78: off=0x4d1320; break;
    case 0x1f1b10: off=0x4d11b8; break; case 0x1e7f70: off=0x4c859c; break;
    case 0x1c471c: off=0x4a12bc; break; case 0x1e3348: off=0x4c3974; break;
    case 0x1e3018: off=0x4c3644; break; case 0x1d73f8: off=0x4b7864; break;
    case 0x1d7410: off=0x4b787c; break; case 0x1c3590: off=0x4a0130; break;
    case 0x1cbebc: off=0x4ac2ec; break; case 0x1cbef0: off=0x4ac320; break;
    case 0x1e25e8: off=0x4c2c14; break; case 0x1e45ec: off=0x4c4c18; break;
    case 0x1eac20: off=0x4ca248; break; case 0x1eb7a0: off=0x4cadc8; break;
    case 0x1eac70: off=0x4ca298; break; case 0x149b90: off=0x428754; break;
    case 0x4d95b0: case 0x4d9600: case 0x42bf74: case 0x42bfa0:
    case 0xaa6c0: case 0xa9924: case 0x9cd94: break;
    default: return 0;
    }
    return base+off;
}
#define ADDRESS(off) runtime_address(off)
#endif
#define FN(off,type) ((type)(ADDRESS(off)))
#define CTX (*(volatile uint32_t *)ADDRESS(0x4919c0))
#define GEN (*(volatile uint32_t *)ADDRESS(0x490040))
#define PAUSED (*(volatile uint32_t *)ADDRESS(0x491964))
#define RUNNING (*(volatile int *)ADDRESS(0x49003c))
typedef void *(*root_fn)(void);
typedef unsigned (*count_fn)(const void *);
typedef void *(*child_fn)(const void *,int32_t);
typedef int (*type_fn)(const void *,const void *);
typedef int (*state_fn)(const void *,uint32_t);
typedef int (*flag_fn)(const void *);
typedef void (*insert_fn)(void *,const char *);
struct identity { uint64_t nonce; uint64_t screen; uint64_t top; uint64_t object; uint64_t topology; uint64_t pty_id; uint64_t process_start; uint32_t generation; uint32_t kind; };

static void scan(void *o, int depth, void **found, int *count, uint64_t *hash, unsigned *visited) {
    if(!o || o==status_overlay || *count>1) return;
    if(depth>32 || ++*visited>512) { *count=2; return; }
    *hash=(*hash ^ (uintptr_t)o)*1099511628211ULL;
    if(FN(0x1f1c78,type_fn)(o,(void *)ADDRESS(0x48cf40)) &&
       FN(0x1f1b10,state_fn)(o,8) && FN(0x1e7f70,flag_fn)(o) &&
       !FN(0x1c471c,flag_fn)(o)) { *found=o; ++*count; }
    unsigned n=FN(0x1e3348,count_fn)(o);
    if(n>512) { *count=2; return; }
    unsigned visible_n=n;
    for(unsigned i=0;i<n;i++) if(FN(0x1e3018,child_fn)(o,(int32_t)i)==status_overlay && status_overlay) --visible_n;
    *hash=(*hash ^ visible_n)*1099511628211ULL;
    for(unsigned i=0;i<n && *count<=1;i++) scan(FN(0x1e3018,child_fn)(o,(int32_t)i),depth+1,found,count,hash,visited);
}
typedef void *(*event_dsc_fn)(const void *,uint32_t);
typedef void *(*event_data_fn)(void *);
typedef int (*pty_write_fn)(void *,const char *,size_t);
static uint64_t process_starttime(pid_t pid) {
    char path[64], line[2048]; snprintf(path,sizeof path,"/proc/%ld/stat",(long)pid);
    FILE *f=fopen(path,"r"); if(!f) return 0;
    char *read=fgets(line,sizeof line,f); fclose(f); if(!read) return 0;
    char *tail=strrchr(line,')'); if(!tail) return 0; char *save=NULL;
    char *token=strtok_r(tail+1," ", &save);
    for(int field=3;token && field<22;field++) token=strtok_r(NULL," ",&save);
    if(!token) return 0;
    char *end=NULL; unsigned long long value=strtoull(token,&end,10);
    return (end!=token && (*end==' ' || *end=='\0' || *end=='\n')) ? (uint64_t)value : 0;
}
static int terminal_pty(uint64_t *identity, uint64_t *start) {
#ifdef BRIDGE_TEST
    int (*mock)(uint64_t *,uint64_t *)=dlsym(RTLD_DEFAULT,"mock_terminal_pty");
    if(mock) return mock(identity,start);
#endif
    DIR *dir=opendir("/proc/self/fd"); if(!dir) return 0;
    struct dirent *entry; int count=0, target=-1;
    while((entry=readdir(dir))) {
        char *end=NULL; long fd=strtol(entry->d_name,&end,10);
        if(!*entry->d_name || *end || fd<0 || fd>INT32_MAX || fd==dirfd(dir)) continue;
        char path[64], link[64]; int n=snprintf(path,sizeof path,"/proc/self/fd/%ld",fd);
        if(n<0 || (size_t)n>=sizeof path) continue;
        ssize_t size=readlink(path,link,sizeof link-1); if(size<0) continue; link[size]=0;
        if(!strcmp(link,"/dev/ptmx")) { target=(int)fd; ++count; }
    }
    closedir(dir); if(count!=1) return 0;
    unsigned number=0; pid_t foreground=0;
    if(ioctl(target,TIOCGPTN,&number)<0 || ioctl(target,TIOCGPGRP,&foreground)<0 || foreground<=0) return 0;
    uint64_t started=process_starttime(foreground); if(!started) return 0;
    *identity=((uint64_t)number<<32)|(uint32_t)foreground; *start=started; return 1;
}
static void *terminal_page(void *screen,void *top) {
    unsigned children=FN(0x1e3348,count_fn)(top); if(children>512) return NULL;
    for(unsigned i=0;i<children;i++) {
        void *child=FN(0x1e3018,child_fn)(top,(int32_t)i);
        if(child==status_overlay) continue;
        if(child && FN(0x1e7f70,flag_fn)(child)) return NULL;
    }
    unsigned events=FN(0x4d95b0,count_fn)(screen); if(events>128) return NULL;
    void *page=NULL; unsigned found=0;
    for(unsigned i=0;i<events;i++) {
        void *event=FN(0x4d9600,event_dsc_fn)(screen,i);
        if(event && FN(0x42bf74,event_data_fn)(event)==(void *)ADDRESS(0xaa6c0)) {
            page=FN(0x42bfa0,event_data_fn)(event); ++found;
        }
    }
    if(found!=1 || !page || FN(0xa9924,flag_fn)(page)) return NULL;
    return page;
}
static int identify(struct identity *out) {
    if(!RUNNING || PAUSED) return 0;
    void *screen=FN(0x1d73f8,root_fn)(), *top=FN(0x1d7410,root_fn)(), *found=NULL;
    if(!screen || !top) return 0;
    int count=0; unsigned visited=0; uint64_t hash=1469598103934665603ULL;
    scan(screen,0,&found,&count,&hash,&visited); if(top!=screen) scan(top,0,&found,&count,&hash,&visited);
    unsigned top_n=FN(0x1e3348,count_fn)(top); if(top_n>512) return 0;
    for(unsigned i=0;i<top_n;i++) {
        void *child=FN(0x1e3018,child_fn)(top,(int32_t)i);
        if(child==status_overlay) continue;
        hash=(hash ^ (unsigned)(child && FN(0x1e7f70,flag_fn)(child)))*1099511628211ULL;
    }
    memset(out,0,sizeof *out); out->nonce=nonce; out->screen=(uintptr_t)screen; out->top=(uintptr_t)top;
    out->topology=hash; out->generation=GEN;
    if(count==1 && CTX==1) { out->object=(uintptr_t)found; out->kind=1; return 1; }
    if(count!=0 || !launcher_profile || CTX!=0) return 0;
    void *terminal=terminal_page(screen,top); if(!terminal || !terminal_pty(&out->pty_id,&out->process_start)) return 0;
    out->object=(uintptr_t)terminal; out->kind=2; return 1;
}
static void close_client(void) { if(client_fd>=0) close(client_fd); client_fd=-1; }
static void setup_socket(void) {
    const char *runtime=getenv("XDG_RUNTIME_DIR"); if(!runtime || runtime[0]!='/') { eligible=0; return; }
    char dir[108]; int n=snprintf(dir,sizeof dir,"%s/voice-input",runtime);
    if(n<0 || (size_t)n>=sizeof dir || (mkdir(dir,0700)<0 && errno!=EEXIST)) { eligible=0; return; }
    struct stat st; if(lstat(dir,&st)<0 || !S_ISDIR(st.st_mode) || st.st_uid!=getuid() || (st.st_mode&077)!=0) { eligible=0; return; }
    n=snprintf(socket_path,sizeof socket_path,"%s/adapter-%ld.native",dir,(long)getpid());
    if(n<0 || (size_t)n>=sizeof socket_path) { eligible=0; return; }
    listen_fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if(listen_fd<0) { eligible=0; return; }
    struct sockaddr_un addr={.sun_family=AF_UNIX}; memcpy(addr.sun_path,socket_path,strlen(socket_path)+1);
    unlink(socket_path);
    mode_t old=umask(0077); int ok=bind(listen_fd,(struct sockaddr *)&addr,sizeof addr); umask(old);
    if(ok<0 || listen(listen_fd,4)<0) { close(listen_fd); listen_fd=-1; eligible=0; return; }
}
static void handle_client(void) {
    unsigned char msg[8192]; ssize_t n=recv(client_fd,msg,sizeof msg,MSG_DONTWAIT);
    if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) {
        struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
        if(now.tv_sec-accepted_at.tv_sec>=2) close_client();
        return;
    }
    if(n<=0) { close_client(); return; }
    struct identity current, supplied; unsigned char response[1+sizeof current]; response[0]='E'; size_t out=1;
    if(msg[0]=='C' && n==1 && identify(&current)) {
        response[0]='O'; memcpy(response+1,&current,sizeof current); out=sizeof response;
    } else if(msg[0]=='I' && n>(ssize_t)(1+sizeof supplied) && n<(ssize_t)sizeof msg) {
        memcpy(&supplied,msg+1,sizeof supplied);
        size_t len=(size_t)n-1-sizeof supplied; char *text=(char *)msg+1+sizeof supplied;
        if(!memchr(text,0,len) && identify(&current) && !memcmp(&current,&supplied,sizeof current)) {
            int safe=1;
            if(current.kind==2) for(size_t i=0;i<len;i++) if((unsigned char)text[i]<32 || (unsigned char)text[i]==127) safe=0;
            if(safe) {
                text[len]=0;
                if(current.kind==1) { FN(0x1c3590,insert_fn)((void *)(uintptr_t)current.object,text); response[0]='O'; }
                else if(current.kind==2 && FN(0x9cd94,pty_write_fn)((void *)(uintptr_t)current.object,text,len)==(int)len) response[0]='O';
            }
        }
    }
    (void)send(client_fd,response,out,MSG_NOSIGNAL); close_client();
}
/* LVGL calls must stay on the application's UI thread. The badge is excluded
 * from input-target identity so showing it cannot invalidate a recording. */
struct badge_color { uint8_t blue, green, red; };
typedef struct badge_color (*color_fn)(uint32_t);
typedef void *(*label_create_fn)(void *);
typedef void (*label_text_fn)(void *,const char *);
typedef void (*obj_delete_fn)(void *);
typedef void (*position_fn)(void *,int32_t,int32_t);
typedef void (*style_color_fn)(void *,struct badge_color,uint32_t);
typedef void (*style_opa_fn)(void *,uint8_t,uint32_t);
static void update_badge(void) {
    static struct timespec last;
    static int displayed;
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    if((now.tv_sec-last.tv_sec)*1000000000LL+now.tv_nsec-last.tv_nsec<200000000LL) return;
    last=now;
    const char *runtime=getenv("XDG_RUNTIME_DIR"); char path[256], data[512]={0}; int state=0;
    if(runtime && snprintf(path,sizeof path,"%s/voice-input/status.json",runtime)<(int)sizeof path) {
        int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
        if(fd>=0) { ssize_t n=read(fd,data,sizeof data-1); close(fd); if(n>0) {
            if(strstr(data,"\"state\":\"recording\"")) state=1;
            else if(strstr(data,"\"state\":\"transcribing\"")) state=2;
            else if(strstr(data,"\"state\":\"success\"")) state=9;
            else if(strstr(data,"\"state\":\"failure\"")) state=10;
            else if(strstr(data,"\"state\":\"provider_deepseek_missing\"")) state=5;
            else if(strstr(data,"\"state\":\"provider_openai_missing\"")) state=6;
            else if(strstr(data,"\"state\":\"provider_deepseek\"")) state=3;
            else if(strstr(data,"\"state\":\"provider_openai\"")) state=4;
            else if(strstr(data,"\"state\":\"provider_error\"")) state=7;
            else if(strstr(data,"\"state\":\"provider_none\"")) state=8;
        }}
    }
    if(!state) { if(status_overlay) { FN(0x1e25e8,obj_delete_fn)(status_overlay); status_overlay=NULL; } displayed=0; return; }
    if(!status_overlay) {
        void *top=FN(0x1d7410,root_fn)(); if(!top) return;
        status_overlay=FN(0x1cbebc,label_create_fn)(top); if(!status_overlay) return;
        FN(0x1eac70,style_opa_fn)(status_overlay,255,0);
        displayed=0;
    }
    if(state!=displayed) {
        const char *text=state==1?"REC":state==2?"ASR":state==3?"DeepSeek":state==4?"OpenAI":state==5?"DeepSeek !":state==6?"OpenAI !":state==7?"ASR error":state==8?"No ASR key":state==9?"OK":"FAIL";
        FN(0x1cbef0,label_text_fn)(status_overlay,text);
        FN(0x1e45ec,position_fn)(status_overlay,(state<=2 || state>=9)?2:208,2);
        FN(0x1eac20,style_color_fn)(status_overlay,FN(0x149b90,color_fn)(state==9?0x16823b:(state==1 || (state>=5 && state<=8) || state==10)?0xc90000:0x2456a6),0);
        FN(0x1eb7a0,style_color_fn)(status_overlay,FN(0x149b90,color_fn)(0xffffff),0);
        displayed=state;
    }
}
static void tick(void) {
    if(!eligible || (pid_t)syscall(SYS_gettid)!=getpid()) return;
    update_badge();
    if(listen_fd<0) setup_socket();
    if(!eligible) return;
    if(client_fd<0) {
        client_fd=accept4(listen_fd,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);
        if(client_fd>=0) {
            struct ucred cred; socklen_t len=sizeof cred;
            if(getsockopt(client_fd,SOL_SOCKET,SO_PEERCRED,&cred,&len)<0 || cred.uid!=getuid()) close_client();
            else clock_gettime(CLOCK_MONOTONIC,&accepted_at);
        }
    }
    if(client_fd>=0) handle_client();
}
int sem_clockwait(sem_t *sem,clockid_t clock,const struct timespec *deadline) {
    pthread_once(&once,init); if(!real_clockwait) { errno=ENOSYS; return -1; }
    int r=real_clockwait(sem,clock,deadline); int saved=errno; tick(); errno=saved; return r;
}
int sem_wait(sem_t *sem) {
    pthread_once(&once,init); if(!real_wait) { errno=ENOSYS; return -1; }
    int r=real_wait(sem); int saved=errno; tick(); errno=saved; return r;
}
static int voice_service_running(void) {
#ifdef BRIDGE_TEST
    if(!getenv("VOICE_TEST_REAL_LOCK")) return getenv("VOICE_DISABLED_TEST")==NULL;
#endif
    const char *runtime=getenv("XDG_RUNTIME_DIR"); char path[256];
    if(!runtime || runtime[0]!='/' || snprintf(path,sizeof path,"%s/voice-input/recorder.lock",runtime)>=(int)sizeof path) return 0;
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW); if(fd<0) return 0;
    int locked=flock(fd,LOCK_EX|LOCK_NB); int saved=errno;
    if(locked==0) flock(fd,LOCK_UN);
    close(fd); return locked<0 && (saved==EWOULDBLOCK || saved==EAGAIN);
}
/* Suppress only the voice chord in supported applications; the independent evdev listener still
 * receives it. Return KEY_RESERVED so the app discards both edges of V. */
uint32_t libinput_event_keyboard_get_key(void *event) {
    pthread_once(&once,init);
    if(!real_key) return 0;
    uint32_t key=real_key(event);
    if(!eligible || !real_key_state) return key;
    int down=real_key_state(event)!=0;
    if(key==389) fn_down=down;
    if(key==47) {
        if(down && fn_down && voice_service_running()) consumed_v=1;
        if(consumed_v) { if(!down) consumed_v=0; return 0; }
    }
    return key;
}
__attribute__((destructor)) static void cleanup(void) {
    close_client(); if(listen_fd>=0) close(listen_fd); if(socket_path[0]) unlink(socket_path);
}
