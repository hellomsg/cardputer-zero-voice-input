/* Test-only fake Linux framebuffer and keyboard capability ioctl. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

static int fb=-1, keyboard=-1;
int open(const char *path,int flags,...) {
    int (*real_open)(const char*,int,...)=dlsym(RTLD_NEXT,"open"); mode_t mode=0;
    if(flags&O_CREAT){va_list ap;va_start(ap,flags);mode=va_arg(ap,int);va_end(ap);}
    if(!strcmp(path,"/dev/fb0")){fb=real_open(getenv("FAKE_FB"),O_RDWR);return fb;}
    int fd=real_open(path,flags,mode);
    if(!strcmp(path,"/dev/input/event0") || !strcmp(path,"/dev/input/by-path/platform-3f804000.i2c-event")) keyboard=fd;
    return fd;
}
int open64(const char *path,int flags,...) {
    int (*real_open64)(const char*,int,...)=dlsym(RTLD_NEXT,"open64"); mode_t mode=0;
    if(flags&O_CREAT){va_list ap;va_start(ap,flags);mode=va_arg(ap,int);va_end(ap);}
    int fd=real_open64(path,flags,mode);
    if(!strcmp(path,"/dev/input/by-path/platform-3f804000.i2c-event")) keyboard=fd;
    return fd;
}
int ioctl(int fd,unsigned long req,...) {
    va_list ap;va_start(ap,req);void *arg=va_arg(ap,void*);va_end(ap);
    if(fd==fb) {
        if(req==FBIOBLANK || req==FBIOPUT_VSCREENINFO)return 0;
        if(req==FBIOGET_FSCREENINFO){struct fb_fix_screeninfo *f=arg;memset(f,0,sizeof(*f));f->line_length=640;f->smem_len=108800;f->visual=FB_VISUAL_TRUECOLOR;return 0;}
        if(req==FBIOGET_VSCREENINFO){struct fb_var_screeninfo *v=arg;memset(v,0,sizeof(*v));v->xres=v->xres_virtual=320;v->yres=v->yres_virtual=170;v->bits_per_pixel=16;v->red.offset=11;v->red.length=5;v->green.offset=5;v->green.length=6;v->blue.length=5;return 0;}
    }
    if(fd==keyboard && _IOC_TYPE(req)=='E' && _IOC_NR(req)==0x06) {strncpy(arg,"tca8418c",_IOC_SIZE(req));return 9;}
    if(fd==keyboard && _IOC_TYPE(req)=='E' && _IOC_NR(req)==0x20+EV_KEY) {
        unsigned long *bits=arg;memset(bits,0,_IOC_SIZE(req));
        bits[KEY_ESC/(8*sizeof(*bits))] |= 1UL<<(KEY_ESC%(8*sizeof(*bits)));return 0;
    }
    int (*real_ioctl)(int,unsigned long,...)=dlsym(RTLD_NEXT,"ioctl");return real_ioctl(fd,req,arg);
}
