/* Native host test: exercise observer forwarding without an Apple runtime. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

/* Do not run the iOS constructor in the host test. */
#define __attribute__(attributes)
#include "src/GBDiagnostic.c"
#undef __attribute__

void *dlsym(void *handle, const char *name) {
    (void)handle;
    (void)name;
    return 0;
}

static int direct_calls, indexed_calls, notify_calls;
static Obj test_self = (Obj)1, test_function = (Obj)2;
static Obj test_response = (Obj)3;
static Sel test_command = (Sel)4;

static Obj mock_class(const char *name) {
    return (Obj)name;
}
static Sel mock_selector(const char *name) {
    return (Sel)name;
}
static Obj mock_message(Obj self, Sel cmd, Obj arg) {
    (void)self;
    (void)cmd;
    (void)arg;
    /* Objects are neither strings nor dictionaries; never inspect payloads. */
    return 0;
}
static void check_args(Obj self, Sel cmd, Obj function, Obj response) {
    assert(self == test_self);
    assert(cmd == test_command);
    assert(function == test_function);
    assert(response == test_response);
}
static void mock_direct(Obj self, Sel cmd, Obj function, Obj response) {
    check_args(self, cmd, function, response);
    ++direct_calls;
}
static void mock_indexed(Obj self, Sel cmd, Obj function, Obj response, int index) {
    check_args(self, cmd, function, response);
    assert(index == 77);
    ++indexed_calls;
}
static void mock_notify(Obj self, Sel cmd, Obj function, Obj response) {
    check_args(self, cmd, function, response);
    ++notify_calls;
}
int main(void) {
    get_class = mock_class;
    selector = mock_selector;
    message = (Imp)mock_message;
    original_callback_short = mock_direct;
    original_callback = mock_indexed;
    original_notify = mock_notify;
    trace_callback_short(test_self, test_command, test_function, test_response);
    trace_callback(test_self, test_command, test_function, test_response, 77);
    trace_notify(test_self, test_command, test_function, test_response);
    assert(direct_calls == 1 && indexed_calls == 1 && notify_calls == 1);
    return 0;
}
