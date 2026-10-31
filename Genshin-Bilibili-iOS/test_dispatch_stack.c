#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#define __attribute__(attributes)
#define GB_ENABLE_DISPATCH_TRACE 1
#define GB_ENABLE_DISPATCH_STACK 1
#include "src/GBDiagnostic.c"
#undef __attribute__

static char scheme_text[] = "https";
static const char *host_text = "cnqddispatch.yuanshen.com";
static const char *path_text = "/query_cur_region";
static char captured[8192];
static int original_calls, trace_calls, installed, no_frames, symbol_missing, bad_abi;
static Obj url_object = (Obj)50;
static void *userdata_object = (void *)60;
static Obj original_init(Obj self, Sel cmd, Obj url, void *userdata) {
    assert(self == (Obj)10 && cmd == (Sel)20 && url == url_object && userdata == userdata_object);
    ++original_calls; return (Obj)70;
}
static int trace(void **frames, int size) {
    assert(size == 48); ++trace_calls;
    frames[0] = (void *)0x701000; frames[1] = (void *)0x101234;
    frames[2] = (void *)0x800123; frames[3] = (void *)0x103456;
    return no_frames ? 0 : 4;
}
static int image(const void *pc, StackImage *info) {
    if (pc == (const void *)original_init || pc == (void *)0x101234 || pc == (void *)0x103456) {
        info->base = (void *)0x100000; info->name = "/PRIVATE/Payload/Yuanshen.app/Yuanshen";
        info->symbol = "PRIVATE_SYMBOL"; info->symbol_address = (void *)0x100100; return 1;
    }
    info->base = (void *)0x700000; return 1;
}
static Imp implementation(void *method) { assert(method == (void *)1); return (Imp)original_init; }
void *dlsym(void *handle, const char *name) {
    (void)handle;
    if (!strcmp(name, "backtrace")) return symbol_missing ? 0 : (void *)trace;
    if (!strcmp(name, "dladdr")) return (void *)image;
    if (!strcmp(name, "method_getImplementation")) return (void *)implementation;
    assert(!"Unexpected symbol"); return 0;
}
static Obj klass(const char *name) { assert(!strcmp(name, "UnityWWWConnectionDelegate")); return (Obj)30; }
static Sel sel(const char *name) { return (Sel)name; }
static void *method(Obj cls, Sel command) {
    assert(cls == (Obj)30 && !strcmp(command, "initWithURL:udata:")); return (void *)1;
}
static const char *types(void *entry) { assert(entry == (void *)1); return bad_abi ? "wrong" : "@32@0:8@16^v24"; }
static Imp replace(void *entry, Imp imp) {
    assert(entry == (void *)1 && imp == (Imp)stack_init); ++installed; return (Imp)original_init;
}
static Obj msg(Obj object, Sel command, ...) {
    if (!object) return 0;
    if (!strcmp(command, "respondsToSelector:")) return (Obj)1;
    if (!strcmp(command, "scheme")) { assert(object == url_object); return scheme_text; }
    if (!strcmp(command, "host")) { assert(object == url_object); return (Obj)host_text; }
    if (!strcmp(command, "path")) { assert(object == url_object); return (Obj)path_text; }
    if (!strcmp(command, "UTF8String")) return object;
    assert(!"Only URL scheme/host/path may be inspected"); return 0;
}
static int open_log(const char *path, int flags, ...) { (void)path; (void)flags; return 7; }
static long write_log(int fd, const void *data, Size size) {
    assert(fd == 7 && strlen(captured) + size < sizeof(captured));
    strncat(captured, data, size); return (long)size;
}
static int close_log(int fd) { assert(fd == 7); return 0; }
int main(void) {
    get_class = klass; selector = sel; get_method = method; method_types = types; replace_method = replace;
    message = (Imp)msg; file_open = open_log; file_write = write_log; file_close = close_log;
    strcpy(log_path, "test-only");
    symbol_missing = 1; dispatch_stack_install(); assert(installed == 0);
    symbol_missing = 0; bad_abi = 1; dispatch_stack_install(); assert(installed == 0);
    bad_abi = 0; dispatch_stack_install(); assert(installed == 1 && stack_game_base == (void *)0x100000);
    captured[0] = 0;
    assert(stack_init((Obj)10, (Sel)20, url_object, userdata_object) == (Obj)70);
    assert(original_calls == 1 && trace_calls == 1);
    assert(strstr(captured, "kind=cur_region attempt=1 frame=1 Yuanshen+0x1234"));
    assert(strstr(captured, "kind=cur_region attempt=1 frame=3 Yuanshen+0x3456"));
    assert(!strstr(captured, "PRIVATE") && !strstr(captured, "701000") && !strstr(captured, "800123"));
    stack_init((Obj)10, (Sel)20, url_object, userdata_object);
    stack_init((Obj)10, (Sel)20, url_object, userdata_object);
    assert(original_calls == 3 && trace_calls == 2);
    host_text = "unrelated.example"; stack_init((Obj)10, (Sel)20, url_object, userdata_object);
    assert(original_calls == 4 && trace_calls == 2);
    host_text = "cnqddispatch.yuanshen.com"; strcpy(scheme_text, "http");
    stack_init((Obj)10, (Sel)20, url_object, userdata_object);
    assert(original_calls == 5 && trace_calls == 2);
    strcpy(scheme_text, "https"); host_text = "dispatchcnglobal.yuanshen.com"; path_text = "/query_region_list";
    no_frames = 1; captured[0] = 0;
    stack_init((Obj)10, (Sel)20, url_object, userdata_object);
    assert(original_calls == 6 && trace_calls == 3 && strstr(captured, "no game frames"));
    stack_backtrace = 0; stack_init((Obj)10, (Sel)20, url_object, userdata_object);
    assert(original_calls == 7 && trace_calls == 3);
    return 0;
}
