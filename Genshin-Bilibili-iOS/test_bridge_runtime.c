#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
#define __attribute__(attributes)
#define GB_ENABLE_BRIDGE 1
#include "src/GBDiagnostic.c"
#undef __attribute__

void *dlsym(void *handle, const char *name) {
    (void)handle;
    (void)name;
    return 0;
}
static int forwarded, retained, released, scheduled;
static Obj received_self, received_function, received_args;
static Sel received_selector;
static int received_index;
static Obj mock_class(const char *name) { return (Obj)name; }
static Sel mock_selector(const char *name) { return (Sel)name; }
static Obj mock_message(Obj self, Sel cmd, Obj argument) {
    (void)argument;
    if (strcmp((const char *)cmd, "isKindOfClass:") == 0) return (Obj)1;
    if (strcmp((const char *)cmd, "UTF8String") == 0) return self;
    return 0;
}
static Obj mock_retain(Obj value) { ++retained; return value; }
static void mock_release(Obj value) { (void)value; ++released; }
static void mock_dispatch(void *queue, void *context, void (*fn)(void *)) {
    (void)queue;
    assert(context == 0 && fn == bridge_present);
    ++scheduled;
}
static void mock_original(Obj self, Sel cmd, Obj function, Obj args, int index) {
    ++forwarded;
    received_self = self;
    received_selector = cmd;
    received_function = function;
    received_args = args;
    received_index = index;
}
int main(void) {
    get_class = mock_class;
    selector = mock_selector;
    message = (Imp)mock_message;
    bridge_retain = mock_retain;
    bridge_release = mock_release;
    bridge_dispatch = mock_dispatch;
    bridge_next_invoke = mock_original;
    Obj login = (Obj)"login_login";
    Obj other = (Obj)"info_set_game_version";
    bridge_invoke((Obj)10, (Sel)20, other, (Obj)30, 40);
    assert(forwarded == 1 && scheduled == 0);
    assert(received_function == other && received_index == 40);
    bridge_invoke((Obj)10, (Sel)20, login, (Obj)30, 50);
    assert(forwarded == 1 && scheduled == 1 && retained == 3);
    assert(bridge.phase == GB_CHOOSING);
    assert(gb_select(&bridge, GB_BILIBILI, 0) == GB_NOT_READY);
    bridge_resume_official(0, 0);
    assert(forwarded == 2 && released == 3 && bridge.phase == GB_IDLE);
    assert(received_self == (Obj)10 && received_selector == (Sel)20);
    assert(received_function == login && received_args == (Obj)30 && received_index == 50);
    bridge_resume_official(0, 0);
    assert(forwarded == 2 && released == 3);
    /* Missing presenter takes the original path rather than stranding login. */
    bridge_invoke((Obj)10, (Sel)20, login, (Obj)30, 60);
    bridge_present(0);
    assert(forwarded == 3 && received_index == 60 && retained == 6 && released == 6);
    return 0;
}
