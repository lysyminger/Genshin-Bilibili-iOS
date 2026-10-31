#define GB_ENABLE_LOGIN_SHAPE 1
#define GB_COMBO_VERIFY_ONLY 1
#define main unused_combo_fixture
#include "test_combo_login.c"
#undef main

static char shape_log[16000];
static Size shape_log_size;
static int shape_open(const char *path, int flags, ...) { (void)path; (void)flags; return 3; }
static long shape_write(int fd, const void *data, Size size) {
    (void)fd;
    assert(shape_log_size + size < sizeof(shape_log));
    memcpy(shape_log + shape_log_size, data, size);
    shape_log_size += size; shape_log[shape_log_size] = 0;
    return (long)size;
}
static int shape_close(int fd) { (void)fd; return 0; }
static void shape_reset(void) {
    reset();
    shape_log_size = 0; shape_log[0] = 0;
    file_open = shape_open; file_write = shape_write; file_close = shape_close;
    strcpy(log_path, "test-only");
}
int main(void) {
    shape_reset();
    Obj wrapper = response(0, 1, 1);
    Obj verified = get(get(wrapper, "data"), "data");
    put(verified, "channel_id", num(14)); put(verified, "app_id", num(4));
    record_login_shape("fixture", wrapper);
    assert(strstr(shape_log, "field=open_id type=string nonempty=1"));
    assert(strstr(shape_log, "field=channel_id type=number value=14"));
    assert(strstr(shape_log, "field=channel_token type=missing"));
    assert(!strstr(shape_log, "synthetic-open-id") && !strstr(shape_log, "synthetic-combo-token"));
    assert(shape_payload(str("malformed JSON")) == 0);
    shape_log_size = 0; shape_log[0] = 0;
    trace_login_shape(str("unrelated_event"), verified);
    assert(shape_log_size == 0);
    trace_login_shape(str("MiHoYoChannelModulelogin"), wrapper);
    assert(strstr(shape_log, "stage=host_callback"));

    shape_reset();
    combo_begin(bili_delegate, str("123"), str("synthetic-access-key"));
    assert(requests == 1 && channel == 14);
    combo_network_callback(&retained_block, response(0, 1, 1), 0, 0);
    assert(callbacks == 0 && server_side_effects == 0 && active_combo == 0);
    assert(official_calls == 1 && channel == 1 && bridge.channel == GB_OFFICIAL);
    assert(bridge.phase == GB_IDLE && pending_self == 0);
    assert(strstr(shape_log, "stage=bili_verified") && strstr(shape_log, "stage=bili_normalized"));
    assert(strstr(shape_log, "combo verify-only sample complete"));
    assert(!strstr(shape_log, "synthetic-access-key") && !strstr(shape_log, "synthetic-combo-token"));
    assert(!strstr(shape_log, "synthetic-open-id") && !strstr(shape_log, "synthetic-device-id"));

    shape_reset();
    combo_begin(bili_delegate, str("123"), str("synthetic-access-key"));
    combo_network_callback(&retained_block, response(-1, 1, 1), 0, 0);
    assert(callbacks == 0 && server_side_effects == 0 && official_calls == 1);
    assert(!strstr(shape_log, "combo verify-only sample complete"));
    return 0;
}
