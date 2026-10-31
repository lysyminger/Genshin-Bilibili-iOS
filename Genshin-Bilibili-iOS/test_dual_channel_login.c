#define GB_DUAL_LOGIN 1
#define main existing_combo_fixture
#include "test_combo_login.c"
#undef main

int main(void) {
    assert(existing_combo_fixture() == 0);
    reset();
    combo_channel_active = 1; channel = 14;
    combo_bili_uid = str("synthetic-bili-uid");
    bridge.phase = GB_CHOOSING;
    bridge_resume_official(0, 0);
    assert(bridge.phase == GB_IDLE && bridge.channel == GB_OFFICIAL);
    assert(channel == 1 && !combo_channel_active && !combo_bili_uid);
    assert(official_calls == 1 && requests == 0 && callbacks == 0);
    assert(pending_self == 0 && pending_args == 0 && !combo_dispatch_route_active());
    Obj value = combo_return(0, 0, str("info_get_channel_id"), 0);
    assert(!strcmp(((struct Value *)value)->text, "1"));
    return 0;
}
