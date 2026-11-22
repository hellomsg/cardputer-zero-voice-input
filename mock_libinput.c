#include <stdint.h>
struct mock_event {uint32_t key; int state;};
uint32_t libinput_event_keyboard_get_key(struct mock_event *e) { return e->key; }
int libinput_event_keyboard_get_key_state(struct mock_event *e) { return e->state; }
