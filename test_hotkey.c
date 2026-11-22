#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
struct mock_event {uint32_t key; int state;};
uint32_t libinput_event_keyboard_get_key(struct mock_event *);
static int check(uint32_t key,int state,uint32_t expected) {
    struct mock_event event={key,state}; return libinput_event_keyboard_get_key(&event)==expected;
}
int main(void) {
    if(getenv("VOICE_DISABLED_TEST")) {
        if(!check(389,1,389) || !check(47,1,47) || !check(47,0,47) || !check(389,0,389)) return 1;
        puts("Fn+V is not suppressed when voice is disabled"); return 0;
    }
    if(!check(47,1,47) || !check(47,0,47) || !check(389,1,389) ||
       !check(47,1,0) || !check(389,0,389) || !check(47,0,0) ||
       !check(47,1,47) || !check(47,0,47)) return 1;
    puts("ordinary V preserved; Fn+V consumed on both edges"); return 0;
}
