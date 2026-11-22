/* Copyright 2026 M5Stack Technology CO LTD. SPDX-License-Identifier: MIT */
#include "app.h"
#include "asset_manager.h"
#include "linux_input.h"
#include "lvgl.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <thread>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#if USE_DESKTOP
#include "desktop_simulator_frame.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif

namespace app {
namespace {

// The secret is passed only through an anonymous pipe, never argv or logs.
bool invoke(const char* action, const std::string* input, std::string* output) {
    const char* path=getenv("CARDPUTER_VOICE_SETTINGS_BIN");
    if(!path || path[0]!='/') path="/usr/lib/voice-input/voice-input-settings";
    int in[2], out[2];
    if (pipe(in) != 0) return false;
    if (pipe(out) != 0) { close(in[0]); close(in[1]); return false; }
    const pid_t pid=fork();
    if (pid<0) { close(in[0]);close(in[1]);close(out[0]);close(out[1]);return false; }
    if(pid==0) {
        dup2(in[0],STDIN_FILENO);dup2(out[1],STDOUT_FILENO);
        const int devnull=open("/dev/null",O_WRONLY);if(devnull>=0) {dup2(devnull,STDERR_FILENO);close(devnull);}
        close(in[0]);close(in[1]);close(out[0]);close(out[1]);
        execl(path,path,action,static_cast<char*>(nullptr));_exit(127);
    }
    close(in[0]);close(out[1]);
    bool written=true;
    if(input) { const char* p=input->data();size_t left=input->size();while(left) {ssize_t n=write(in[1],p,left);if(n<0 && errno==EINTR) continue;if(n<=0){written=false;break;}p+=n;left-=static_cast<size_t>(n);} }
    close(in[1]);
    std::string response;char buf[256];ssize_t n;
    while((n=read(out[0],buf,sizeof buf))>0) {if(response.size()<4096) response.append(buf,static_cast<size_t>(n));}
    close(out[0]);int status=0;while(waitpid(pid,&status,0)<0) {if(errno!=EINTR)return false;}
    if(output) *output=response;
    return written && WIFEXITED(status) && WEXITSTATUS(status)==0;
}

class SettingsView {
public:
    SettingsView(lv_display_t* display, AssetManager& assets): assets_(assets) {
        root_=lv_obj_create(nullptr);lv_obj_remove_style_all(root_);
        lv_obj_set_size(root_,320,170);lv_obj_set_style_bg_color(root_,lv_color_hex(0x101827),0);
        lv_obj_set_style_bg_opa(root_,LV_OPA_COVER,0);lv_obj_clear_flag(root_,LV_OBJ_FLAG_SCROLLABLE);
        const auto* font=assets_.load_font("/usr/share/APPLaunch/share/font/AlibabaPuHuiTi-3-55-Regular.ttf",14);
#if defined(__APPLE__)
        if(!font) font=assets_.load_font("/System/Library/Fonts/Supplemental/Songti.ttc",14);
#endif
        if(!font) font=&lv_font_montserrat_14;
        lv_obj_set_style_text_font(root_,font,0);lv_obj_set_style_text_color(root_,lv_color_hex(0xe9efff),0);
        title_=label(8,7,304);body_=label(8,34,304);footer_=label(8,112,304);
        summary_panel_=lv_obj_create(root_);lv_obj_remove_style_all(summary_panel_);
        lv_obj_set_pos(summary_panel_,8,29);lv_obj_set_size(summary_panel_,304,70);
        lv_obj_set_style_bg_color(summary_panel_,lv_color_hex(0x19283a),0);lv_obj_set_style_bg_opa(summary_panel_,LV_OPA_COVER,0);
        lv_obj_set_style_border_color(summary_panel_,lv_color_hex(0x334c62),0);lv_obj_set_style_border_width(summary_panel_,1,0);
        lv_obj_set_style_radius(summary_panel_,7,0);lv_obj_clear_flag(summary_panel_,LV_OBJ_FLAG_SCROLLABLE);lv_obj_move_background(summary_panel_);
        key_entry_=lv_textarea_create(root_);lv_obj_set_pos(key_entry_,8,76);lv_obj_set_size(key_entry_,304,29);
        lv_textarea_set_one_line(key_entry_,true);lv_textarea_set_password_mode(key_entry_,false);
        lv_textarea_set_placeholder_text(key_entry_,"Paste or type an API key");
        lv_obj_set_style_bg_color(key_entry_,lv_color_hex(0x202c40),0);
        lv_obj_set_style_text_color(key_entry_,lv_color_hex(0xe9efff),0);
        lv_obj_set_style_border_color(key_entry_,lv_color_hex(0x69849e),0);
        lv_obj_set_style_border_width(key_entry_,1,0);lv_obj_set_style_radius(key_entry_,5,0);
        lv_obj_set_style_pad_left(key_entry_,6,0);lv_obj_set_style_pad_right(key_entry_,6,0);
        lv_obj_set_style_pad_top(key_entry_,4,0);lv_obj_set_style_pad_bottom(key_entry_,4,0);
        lv_obj_add_flag(key_entry_,LV_OBJ_FLAG_HIDDEN);
        // Align the footer actions with physical keys 4, 5, 6 and 8.
        key_action_=label(17,142,64);help_action_=label(241,142,64);provider_action_=label(120,142,80);service_action_=label(73,142,64);
        for(auto* action : {key_action_,help_action_,provider_action_,service_action_}) {
            lv_obj_set_style_text_align(action,LV_TEXT_ALIGN_CENTER,0);
            lv_obj_set_style_text_color(action,lv_color_hex(0xe9efff),0);
        }
        key_tick_=tick(49);help_tick_=tick(273);provider_tick_=tick(160);service_tick_=tick(105);
        create_help();
        provider_badge_=label(208,2,110);
        lv_obj_set_style_bg_color(provider_badge_,lv_color_hex(0x2456a6),0);
        lv_obj_set_style_bg_opa(provider_badge_,LV_OPA_COVER,0);
        lv_obj_add_flag(provider_badge_,LV_OBJ_FLAG_HIDDEN);
        badge_timer_=lv_timer_create(badge_tick,200,this);
        lv_obj_set_style_text_color(title_,lv_color_hex(0x68d6e6),0);
        lv_obj_set_style_text_color(footer_,lv_color_hex(0xa7b9d4),0);
        lv_display_set_default(display);lv_screen_load(root_);refresh();
        platform::set_global_key_listener(key_event,this);
    }
    ~SettingsView() { platform::clear_global_key_listener(key_event,this);if(timer_)lv_timer_delete(timer_);if(badge_timer_)lv_timer_delete(badge_timer_);if(worker_.joinable())worker_.join();erase_key(); }
    bool running() const { return running_; }
#ifdef VOICE_UI_SELFTEST
    bool self_test() {
        if(std::strstr(lv_label_get_text(body_),"Running")==nullptr) return false;
        if(std::strcmp(lv_label_get_text(key_action_),"Set key") || std::strcmp(lv_label_get_text(service_action_),"Stop")) return false;
        if(std::strcmp(lv_label_get_text(help_action_),"Help")) return false;
        handle('8');
        if(!helping_ || lv_obj_has_flag(help_,LV_OBJ_FLAG_HIDDEN) || lv_obj_get_child_count(help_)<10) return false;
        if(const char* path=std::getenv("CARDPUTER_HELP_SCREENSHOT")) {
            lv_obj_update_layout(help_);
            auto* image=lv_snapshot_take(help_,LV_COLOR_FORMAT_ARGB8888);
            if(!image) return false;
            FILE* f=std::fopen(path,"wb");if(!f){lv_draw_buf_destroy(image);return false;}
            std::fprintf(f,"P6\n%u %u\n255\n",image->header.w,image->header.h);
            for(uint32_t y=0;y<image->header.h;y++) for(uint32_t x=0;x<image->header.w;x++) {
                const uint8_t* p=image->data+y*image->header.stride+x*4;std::fputc(p[2],f);std::fputc(p[1],f);std::fputc(p[0],f);
            }
            std::fclose(f);lv_draw_buf_destroy(image);
        }
        handle(LV_KEY_ESC);
        if(helping_ || !lv_obj_has_flag(help_,LV_OBJ_FLAG_HIDDEN)) return false;
        const char* runtime=std::getenv("XDG_RUNTIME_DIR");
        if(!runtime || !badge_timer_) return false;
        const std::string status_path=std::string(runtime)+"/voice-input/status.json";
        if(FILE* f=std::fopen(status_path.c_str(),"w")) {std::fputs("{\"state\":\"provider_openai\"}",f);std::fclose(f);} else return false;
        badge_tick(badge_timer_);
        if(lv_obj_has_flag(provider_badge_,LV_OBJ_FLAG_HIDDEN) || std::strcmp(lv_label_get_text(provider_badge_),"OpenAI")) return false;
        if(FILE* f=std::fopen(status_path.c_str(),"w")) {std::fputs("{\"state\":\"idle\"}",f);std::fclose(f);} else return false;
        badge_tick(badge_timer_);
        if(!lv_obj_has_flag(provider_badge_,LV_OBJ_FLAG_HIDDEN)) return false;
        handle('6');
        if(std::strstr(lv_label_get_text(body_),"OpenAI")==nullptr) return false;
        handle('6');
        if(std::strstr(lv_label_get_text(body_),"DeepSeek")==nullptr) return false;
        const char* screenshot=std::getenv("CARDPUTER_UI_SCREENSHOT");
        if(screenshot) {
            lv_obj_update_layout(root_);
            auto* image=lv_snapshot_take(root_,LV_COLOR_FORMAT_ARGB8888);
            if(!image) return false;
            FILE* f=std::fopen(screenshot,"wb");if(!f) {lv_draw_buf_destroy(image);return false;}
            std::fprintf(f,"P6\n%u %u\n255\n",image->header.w,image->header.h);
            for(uint32_t y=0;y<image->header.h;y++) for(uint32_t x=0;x<image->header.w;x++) {
                const uint8_t* p=image->data+y*image->header.stride+x*4;std::fputc(p[2],f);std::fputc(p[1],f);std::fputc(p[0],f);
            }
            std::fclose(f);lv_draw_buf_destroy(image);
        }
        const std::string unconfigured="{\"provider\":\"deepseek\",\"key_configured\":false,\"voice_active\":true,\"text_active\":true,\"launcher_compatible\":true}";
        refresh(&unconfigured,true);
        if(std::strstr(lv_label_get_text(body_),"API key: Not configured")==nullptr) return false;
        handle('4');
        if(std::strstr(lv_label_get_text(body_),"No key saved")==nullptr) return false;
        handle(LV_KEY_ENTER);
        if(!editing_ || std::strcmp(lv_label_get_text(footer_),"Enter an API key")) return false;
        handle(LV_KEY_ESC);
        if(editing_ || lv_label_get_text(footer_)[0]!='\0') return false;
        handle('4');
        for(int i=0;i<160;i++) handle('a');
        if(std::strlen(lv_textarea_get_text(key_entry_))!=160) return false;
        handle(LV_KEY_ESC);
        if(lv_textarea_get_text(key_entry_)[0]!='\0') return false;
        handle('4');handle('s');handle('k');handle('-');handle('4');handle('8');
        if(std::strcmp(lv_textarea_get_text(key_entry_),"sk-48")) return false;
        if(std::strstr(lv_label_get_text(body_),"Current key: sk-...mple")==nullptr) return false;
        handle(LV_KEY_ENTER);
        if(editing_ || lv_textarea_get_text(key_entry_)[0]!='\0') return false;
        handle('5');
        if(!busy_ || std::strstr(lv_label_get_text(footer_),"Stopping")==nullptr) return false;
        const std::string first_frame=lv_label_get_text(footer_);
        handle('5');
        for(int i=0;i<20 && busy_;++i) {lv_timer_handler();lv_delay_ms(20);}
        if(!busy_ || first_frame==lv_label_get_text(footer_)) return false;
        for(int i=0;i<250 && busy_;++i) {lv_timer_handler();lv_delay_ms(20);}
        return !busy_ && std::strstr(lv_label_get_text(body_),"Stopped")!=nullptr;
    }
#endif
private:
    lv_obj_t* label(int x,int y,int width) {auto* l=lv_label_create(root_);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,width);lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);return l;}
    lv_obj_t* tick(int x) {auto* t=lv_obj_create(root_);lv_obj_remove_style_all(t);lv_obj_set_pos(t,x-1,164);lv_obj_set_size(t,2,6);lv_obj_set_style_bg_color(t,lv_color_hex(0x68d6e6),0);lv_obj_set_style_bg_opa(t,LV_OPA_COVER,0);return t;}
    void show_actions(bool visible) {for(auto* o : {key_action_,help_action_,provider_action_,service_action_,key_tick_,help_tick_,provider_tick_,service_tick_}) {if(visible)lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);}}
    lv_obj_t* help_label(int x,int y,int width,const char* text) {
        auto* l=lv_label_create(help_);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,width);lv_label_set_text(l,text);return l;
    }
    void keycap(int x,int y,int width,const char* text,bool accent=false) {
        auto* box=lv_obj_create(help_);lv_obj_remove_style_all(box);lv_obj_set_pos(box,x,y);lv_obj_set_size(box,width,23);
        const bool fn_key=std::strcmp(text,"Fn")==0;
        lv_obj_set_style_radius(box,8,0);lv_obj_set_style_bg_color(box,lv_color_hex(fn_key?0xe65a24:(accent?0x8e22a7:0x555d69)),0);
        lv_obj_set_style_bg_opa(box,LV_OPA_COVER,0);lv_obj_set_style_border_width(box,1,0);
        lv_obj_set_style_border_color(box,lv_color_hex(fn_key?0xffa276:(accent?0xd273dc:0x9ca5af)),0);lv_obj_clear_flag(box,LV_OBJ_FLAG_SCROLLABLE);
        auto* l=lv_label_create(box);lv_label_set_text(l,text);lv_obj_center(l);
    }
    void help_badge(int x,int y,const char* text,uint32_t color) {
        auto* l=lv_label_create(help_);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,40);lv_label_set_text(l,text);
        lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_bg_color(l,lv_color_hex(color),0);lv_obj_set_style_bg_opa(l,LV_OPA_COVER,0);
        lv_obj_set_style_text_color(l,lv_color_hex(0xffffff),0);
    }
    void create_help() {
        help_=lv_obj_create(root_);lv_obj_remove_style_all(help_);lv_obj_set_size(help_,320,170);
        lv_obj_set_style_bg_color(help_,lv_color_hex(0x101827),0);lv_obj_set_style_bg_opa(help_,LV_OPA_COVER,0);
        lv_obj_clear_flag(help_,LV_OBJ_FLAG_SCROLLABLE);
        auto* title=help_label(8,3,304,"Voice Input  /  Help");lv_obj_set_style_text_color(title,lv_color_hex(0x68d6e6),0);
        keycap(8,23,36,"Fn",true);help_label(49,26,12,"+");keycap(64,23,27,"V");help_label(104,26,207,"Start / stop recording");
        keycap(8,50,50,"Shift",true);help_label(62,53,12,"+");keycap(77,50,36,"Fn",true);help_label(117,53,12,"+");keycap(132,50,27,"V");
        help_label(170,53,142,"Switch ASR");
        help_label(8,78,304,"Rec max 15s | ASR timeout 10s");
        help_badge(8,99,"REC",0xc90000);help_label(56,99,96,"Recording");
        help_badge(160,99,"ASR",0x2456a6);help_label(208,99,104,"Recognizing");
        help_badge(8,120,"OK",0x16823b);help_label(56,120,96,"Inserted");
        help_badge(160,120,"FAIL",0xc90000);help_label(208,120,104,"Failed");
        keycap(8,145,39,"Esc");help_label(56,148,180,"Back");
        lv_obj_add_flag(help_,LV_OBJ_FLAG_HIDDEN);
    }
    void erase_key() { std::fill(key_.begin(),key_.end(),'\0');key_.clear();lv_textarea_set_text(key_entry_," ");lv_textarea_set_text(key_entry_,""); }
    void refresh(const std::string* known_status=nullptr, bool known_ok=false) {
        if(editing_) {
            show_actions(false);lv_obj_add_flag(summary_panel_,LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(body_,8,34);lv_obj_set_width(body_,304);
            const std::string title=editing_provider_+" API Key";
            lv_label_set_text(title_,title.c_str());
            const std::string description=editing_key_configured_ ?
                "Current key: "+(editing_key_hint_.empty()?std::string("Saved"):editing_key_hint_)+"\nEnter a new key to replace it" :
                "No key saved\nEnter an API key";
            lv_label_set_text(body_,description.c_str());
            lv_obj_remove_flag(key_entry_,LV_OBJ_FLAG_HIDDEN);
            lv_textarea_set_text(key_entry_,key_.c_str());
            lv_textarea_set_cursor_pos(key_entry_,LV_TEXTAREA_CURSOR_LAST);
            lv_label_set_text(footer_,message_.empty()?"Enter: Save    Esc: Cancel":message_.c_str());return;
        }
        lv_obj_add_flag(key_entry_,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(summary_panel_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(body_,18,35);lv_obj_set_width(body_,284);
        lv_label_set_text(title_,"Voice Input Settings");
        show_actions(true);
        std::string status;
        const bool ok=known_status?(status=*known_status,known_ok):invoke("status",nullptr,&status);
        const bool voice_active=ok && status.find("\"voice_active\":true")!=std::string::npos;
        const bool text_active=ok && status.find("\"text_active\":true")!=std::string::npos;
        enabled_=voice_active && text_active;
        const char* service=(!ok?"Unavailable":(enabled_?"Running":((voice_active || text_active)?"Partially running":"Stopped")));
        provider_name_=status.find("\"provider\":\"openai\"")!=std::string::npos?"OpenAI":"DeepSeek";
        key_configured_=ok && status.find("\"key_configured\":true")!=std::string::npos;
        key_hint_.clear();
        const std::string marker="\"key_hint\":\"";const auto start=status.find(marker);
        if(ok && start!=std::string::npos) {const auto first=start+marker.size(),end=status.find('"',first);
            if(end!=std::string::npos && end-first<=16) {
                const auto hint=status.substr(first,end-first);
                if(std::all_of(hint.begin(),hint.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.';})) key_hint_=hint;
            }
        }
        const bool compatible=ok && status.find("\"launcher_compatible\":true")!=std::string::npos;
        compatible_=compatible;
        std::string text=std::string("ASR: ")+provider_name_+"\nAPI key: "+(key_configured_?(key_hint_.empty()?"Configured":key_hint_.c_str()):"Not configured")+"\nService: "+service;
        const bool restart=ok && status.find("\"restart_required\":true")!=std::string::npos;
        lv_label_set_text(body_,text.c_str());
        lv_label_set_text(footer_,!message_.empty()?message_.c_str():(restart?"Restart device to finish update":(!ok?"Status unavailable":(!compatible?"Launcher not supported":""))));
        lv_label_set_text(key_action_,"Set key");lv_label_set_text(help_action_,"Help");lv_label_set_text(provider_action_,"ASR");lv_label_set_text(service_action_,enabled_?"Stop":"Start");
    }
    static void badge_tick(lv_timer_t* timer) {
        auto* self=static_cast<SettingsView*>(lv_timer_get_user_data(timer));
        const char* runtime=std::getenv("XDG_RUNTIME_DIR");char path[512],data[512]={};
        if(runtime && runtime[0]=='/' && std::snprintf(path,sizeof(path),"%s/voice-input/status.json",runtime)<(int)sizeof(path)) {
            int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
            if(fd>=0) {ssize_t n=read(fd,data,sizeof(data)-1);close(fd);if(n<=0)data[0]=0;}
        }
        const char* text=nullptr;
        if(std::strstr(data,"\"state\":\"provider_deepseek_missing\""))text="DeepSeek !";
        else if(std::strstr(data,"\"state\":\"provider_openai_missing\""))text="OpenAI !";
        else if(std::strstr(data,"\"state\":\"provider_deepseek\""))text="DeepSeek";
        else if(std::strstr(data,"\"state\":\"provider_openai\""))text="OpenAI";
        else if(std::strstr(data,"\"state\":\"provider_error\""))text="ASR error";
        else if(std::strstr(data,"\"state\":\"provider_none\""))text="No ASR key";
        if(text && self->last_notice_!=text) {
            self->last_notice_=text;
            if(!self->editing_ && !self->busy_) self->refresh();
        } else if(!text) self->last_notice_.clear();
        if(text) {
            lv_label_set_text(self->provider_badge_,text);
            lv_obj_set_style_bg_color(self->provider_badge_,lv_color_hex(std::strchr(text,'!') || std::strstr(text,"error") || std::strstr(text,"No ASR")?0xc90000:0x2456a6),0);
            lv_obj_remove_flag(self->provider_badge_,LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(self->provider_badge_);
        } else lv_obj_add_flag(self->provider_badge_,LV_OBJ_FLAG_HIDDEN);
    }
    void update_busy_text() {
        const unsigned dots=(lv_tick_elaps(busy_since_)/300)%4;
        std::string text=starting_?"Starting":"Stopping";
        text.append(dots+1,'.');
        lv_label_set_text(footer_,text.c_str());
    }
    static void busy_tick(lv_timer_t* timer) {
        auto* self=static_cast<SettingsView*>(lv_timer_get_user_data(timer));
        if(!self->done_.load(std::memory_order_acquire)) {self->update_busy_text();return;}
        self->worker_.join();self->busy_=false;self->timer_=nullptr;lv_timer_delete(timer);
        const bool voice=self->worker_status_.find("\"voice_active\":true")!=std::string::npos;
        const bool text=self->worker_status_.find("\"text_active\":true")!=std::string::npos;
        const bool reached=self->starting_?(voice&&text):(!voice&&!text);
        self->message_=!self->worker_ok_?"Action failed":(!self->worker_status_ok_?"Status unavailable":(reached?"":"Check service status"));
        self->refresh(&self->worker_status_,self->worker_status_ok_);
    }
    void toggle_service() {
        if(!enabled_ && !compatible_) {message_="Launcher not supported";refresh();return;}
        if(!enabled_ && !key_configured_) {message_="Set an API key first";refresh();return;}
        starting_=!enabled_;busy_=true;done_.store(false,std::memory_order_relaxed);
        busy_since_=lv_tick_get();message_.clear();show_actions(false);update_busy_text();
        timer_=lv_timer_create(busy_tick,100,this);
        if(!timer_) {busy_=false;message_="Could not start action";refresh();return;}
        try {
            worker_=std::thread([this] {
                worker_ok_=invoke(starting_?"enable":"disable",nullptr,nullptr);
                worker_status_.clear();worker_status_ok_=invoke("status",nullptr,&worker_status_);
                done_.store(true,std::memory_order_release);
            });
        } catch(...) {lv_timer_delete(timer_);timer_=nullptr;busy_=false;message_="Could not start action";refresh();}
    }
    static bool key_event(uint32_t key,const char*,bool repeated,void* context) {
        if(repeated) return true;
        return static_cast<SettingsView*>(context)->handle(key);
    }
    bool handle(uint32_t key) {
        if(busy_) return true;
        if(editing_) {
            if(key==LV_KEY_ESC) {editing_=false;erase_key();message_.clear();}
            else if(key==LV_KEY_ENTER) {
                if(key_.empty()) {message_="Enter an API key";refresh();return true;}
                bool ok=invoke(editing_provider_=="OpenAI"?"save-key-openai":"save-key-deepseek",&key_,nullptr);
                if(ok) {erase_key();editing_=false;message_.clear();} else message_="Could not save key";
            }
            else if(key==LV_KEY_BACKSPACE) {if(!key_.empty())key_.pop_back();message_.clear();}
            else if(key<128 && ((key>='a'&&key<='z')||(key>='A'&&key<='Z')||(key>='0'&&key<='9')||key=='-'||key=='_'||key=='.')) {if(key_.size()<1024)key_.push_back(static_cast<char>(key));message_.clear();}
            refresh();return true;
        }
        if(helping_) {if(key==LV_KEY_ESC || key=='8') {helping_=false;lv_obj_add_flag(help_,LV_OBJ_FLAG_HIDDEN);refresh();}return true;}
        if(key==LV_KEY_ESC) {running_=false;return true;}
        if(key=='8') {helping_=true;lv_obj_remove_flag(help_,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(help_);return true;}
        if(key=='4') {message_.clear();editing_provider_=provider_name_;editing_key_configured_=key_configured_;editing_key_hint_=key_hint_;editing_=true;erase_key();refresh();return true;}
        if(key=='6') {const char* next=provider_name_=="DeepSeek"?"select-openai":"select-deepseek";message_=invoke(next,nullptr,nullptr)?"":"ASR change failed";refresh();return true;}
        if(key=='5') {toggle_service();return true;}
        return true;
    }
    AssetManager& assets_;lv_obj_t *root_{nullptr},*title_{nullptr},*body_{nullptr},*footer_{nullptr};
    lv_obj_t *key_action_{nullptr},*help_action_{nullptr},*provider_action_{nullptr},*service_action_{nullptr},*key_tick_{nullptr},*help_tick_{nullptr},*provider_tick_{nullptr},*service_tick_{nullptr},*provider_badge_{nullptr};
    lv_obj_t* help_{nullptr};lv_obj_t* key_entry_{nullptr};lv_obj_t* summary_panel_{nullptr};
    bool helping_{false},key_configured_{false},editing_key_configured_{false};
    bool running_{true},editing_{false},enabled_{false},compatible_{false};std::string key_,message_,provider_name_,editing_provider_,key_hint_,editing_key_hint_,last_notice_;
    bool busy_{false},starting_{false},worker_ok_{false},worker_status_ok_{false};
    std::atomic<bool> done_{false};std::string worker_status_;std::thread worker_;lv_timer_t* timer_{nullptr};lv_timer_t* badge_timer_{nullptr};uint32_t busy_since_{0};
};
}

int Application::run() {
    signal(SIGPIPE,SIG_IGN);lv_init();AssetManager assets;
#if USE_DESKTOP
    DesktopSimulatorFrame simulator(assets);auto* display=simulator.display();
#else
    auto* display=lv_linux_fbdev_create();if(!display || lv_linux_fbdev_set_file(display,"/dev/fb0")!=LV_RESULT_OK)return 1;
    platform::init_key_input(display);
#endif
    if(!display)return 1;
    SettingsView view(display,assets);
#ifdef VOICE_UI_SELFTEST
    return view.self_test()?0:1;
#endif
    while(view.running()
#if USE_DESKTOP
          && simulator.process_events()
#endif
    ) {lv_timer_handler();lv_delay_ms(5);}
    return 0;
}
}
