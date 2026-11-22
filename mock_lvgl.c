#define _GNU_SOURCE
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
int mock_context=1, mock_generation=1, mock_paused=0, mock_running=1, mock_textarea_class;
static int screen, top, textarea, page, event;
static int badge, badge_active;
struct badge_color { uint8_t blue, green, red; };
void *mock_label_create(void *parent) { if(parent!=&top || badge_active) abort(); badge_active=1; const char *path=getenv("MOCK_BADGE_CREATED"); if(path) { FILE *f=fopen(path,"w"); if(!f) abort(); fclose(f); } return &badge; }
void mock_label_set_text(void *p,const char *text) { if(p!=&badge || !text) abort(); const char *path=getenv("MOCK_BADGE_TEXT"); if(path) {FILE *f=fopen(path,"w"); if(!f) abort(); fputs(text,f); fclose(f);} }
void mock_obj_delete(void *p) { if(p!=&badge) abort(); badge_active=0; }
void mock_obj_set_pos(void *p,int32_t x,int32_t y) { (void)p; const char *path=getenv("MOCK_BADGE_POS"); if(path) {FILE *f=fopen(path,"w"); if(!f) abort(); fprintf(f,"%d,%d",x,y);fclose(f);} }
void mock_obj_set_color(void *p,struct badge_color c,uint32_t s) { (void)p; (void)c; (void)s; }
void mock_obj_set_opa(void *p,uint8_t v,uint32_t s) { (void)p; (void)v; (void)s; }
struct badge_color mock_color_hex(uint32_t x) { struct badge_color c={(uint8_t)x,(uint8_t)(x>>8),(uint8_t)(x>>16)};return c; }
static int terminal_mode(void) { return getenv("MOCK_TERMINAL")!=NULL; }
void *mock_screen(void) { return &screen; }
void *mock_top(void) { return &top; }
unsigned mock_get_child_count(void *p) { return p==&top ? (unsigned)(!terminal_mode()+badge_active) : 0; }
void *mock_get_child(void *p,int32_t i) { if(p!=&top) return NULL; if(!terminal_mode() && i==0) return &textarea; if(badge_active && i==!terminal_mode()) return &badge; return NULL; }
int mock_obj_check_type(void *p,void *class_) { return p==&textarea && class_==&mock_textarea_class; }
int mock_obj_has_state(void *p,uint32_t state) { return p==&textarea && state==8; }
int mock_obj_is_visible(void *p) { return p==&textarea || (p==&badge && badge_active); }
int mock_get_password_mode(void *p) { (void)p; return 0; }
void mock_add_text(void *p,const char *text) {
    if(p!=&textarea) abort();
    FILE *f=fopen(getenv("MOCK_OUTPUT"),"a"); if(!f) abort(); fputs(text,f); fclose(f);
}
void mock_terminal_callback(void) {}
unsigned mock_event_count(void *p) { return p==&screen && terminal_mode() ? 1 : 0; }
void *mock_event_dsc(void *p,unsigned i) { return p==&screen && i==0 && terminal_mode() ? &event : NULL; }
void *mock_event_cb(void *p) { return p==&event ? (void *)mock_terminal_callback : NULL; }
void *mock_event_data(void *p) { return p==&event ? &page : NULL; }
int mock_help_visible(void *p) { return p!=&page; }
int mock_terminal_pty(uint64_t *id,uint64_t *start) { *id=42; *start=99; return 1; }
int mock_pty_write(void *p,const char *text,size_t len) { if(p!=&page) return -1; mock_add_text(&textarea,text); return (int)len; }
int main(void) {
    if(terminal_mode()) mock_context=0;
    sem_t sem; if(sem_init(&sem,0,0)) return 1;
    for(int i=0;i<2000;i++) {
        struct timespec end; clock_gettime(CLOCK_MONOTONIC,&end);
        end.tv_nsec+=10000000; if(end.tv_nsec>=1000000000) { ++end.tv_sec; end.tv_nsec-=1000000000; }
        sem_clockwait(&sem,CLOCK_MONOTONIC,&end);
        if(access(getenv("MOCK_INVALIDATE"),F_OK)==0) mock_generation=2;
    }
    sem_destroy(&sem); return 0;
}
