#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#define __attribute__(attributes)
#define GB_ENABLE_DISPATCH_TRACE 1
#define GB_ENABLE_DISPATCH_ROUTE 1
#define GB_ENABLE_DISPATCH_PROFILE 1
static int route_active;
static int combo_dispatch_route_active(void) { return route_active; }
#include "src/GBDiagnostic.c"
#undef __attribute__

void *dlsym(void *handle, const char *name) { (void)handle; (void)name; return 0; }
enum { REQUEST = 1, URL, COMPONENTS, TEXT };
struct Value { int kind; const char *text; };
static struct Value request = {REQUEST}, edited = {REQUEST}, url = {URL};
static struct Value components = {COMPONENTS}, text_value = {TEXT, 0};
static struct Value host_value = {TEXT, 0}, path_value = {TEXT, 0}, absolute_value = {TEXT, 0};
static struct Value scheme_value = {TEXT, "https"};
static struct Value query_value = {TEXT, 0}, destination = {URL};
static char string_buffer[1024], host_buffer[128], path_buffer[64];
static char absolute_text[1024], query_text[1024], destination_text[2048];
static int copies, copy_failed, corrupt_applied, cancel_send, forwarded;
static Obj assigned_url;
static char captured[512];
static Obj klass(const char *name) { return (Obj)name; }
static Sel sel(const char *name) { return (Sel)name; }
static Obj msg(Obj object, Sel command, ...) {
    if (!object) return 0;
    const char *name = command;
    struct Value *v = object;
    va_list args; va_start(args, command);
    Obj result = 0;
    if (!strcmp(name, "respondsToSelector:")) result = (Obj)1;
    else if (!strcmp(name, "stringWithUTF8String:")) {
        const char *text = va_arg(args, const char *);
        assert(text && strlen(text) < sizeof(string_buffer));
        strcpy(string_buffer, text);
        text_value.text = string_buffer; result = &text_value;
    } else if (!strcmp(name, "componentsWithString:")) {
        Obj input = va_arg(args, Obj);
        assert(input == &text_value || input == &absolute_value); result = &components;
    } else if (!strcmp(name, "setPercentEncodedQuery:")) {
        assert(v == &components && v->kind == COMPONENTS);
        Obj text = va_arg(args, Obj);
        assert(text && !strcmp(((struct Value *)text)->text, string_buffer));
        strcpy(query_text, ((struct Value *)text)->text); result = 0;
    } else if (!strcmp(name, "percentEncodedQuery")) {
        assert(v == &components);
        query_value.text = corrupt_applied ? "changed" : query_text; result = &query_value;
    } else if (!strcmp(name, "URL")) {
        if (v == &components) {
            const char *mark = strchr(absolute_text, '?');
            assert(mark);
            size_t base = (size_t)(mark - absolute_text);
            memcpy(destination_text, absolute_text, base);
            destination_text[base] = '?';
            strcpy(destination_text + base + 1, query_text);
            destination.text = destination_text; result = &destination;
        } else if (v == &edited) result = &destination;
        else { assert(v == &request); absolute_value.text = absolute_text; result = &url; }
    } else if (!strcmp(name, "scheme")) { assert(v == &url || v == &destination); result = &scheme_value; }
    else if (!strcmp(name, "host")) { assert(v == &url || v == &destination); host_value.text = host_buffer; result = &host_value; }
    else if (!strcmp(name, "path")) { assert(v == &url || v == &destination); path_value.text = path_buffer; result = &path_value; }
    else if (!strcmp(name, "absoluteString")) { assert(v == &url || v == &destination); absolute_value.text = v == &url ? absolute_text : destination_text; result = &absolute_value; }
    else if (!strcmp(name, "queryItems")) { assert(v == &components); result = 0; }
    else if (!strcmp(name, "mutableCopy")) { assert(v == &request); ++copies; result = copy_failed ? 0 : &edited; }
    else if (!strcmp(name, "setURL:")) { assert(v == &edited); assigned_url = va_arg(args, Obj); }
    else if (!strcmp(name, "autorelease")) { assert(v == &edited); result = &edited; }
    else if (!strcmp(name, "UTF8String")) { assert(v->kind == TEXT); result = (Obj)v->text; }
    else assert(!"Unexpected selector");
    va_end(args); return result;
}
static int open_log(const char *path, int flags, ...) { (void)path; (void)flags; return 7; }
static long write_log(int fd, const void *data, Size size) {
    assert(fd == 7 && size < sizeof(captured)); memcpy(captured, data, size); captured[size] = 0; return (long)size;
}
static int close_log(int fd) { assert(fd == 7); return 0; }
static Obj original_send(Obj self, Sel cmd, Obj connection, Obj req, Obj response) {
    (void)self; (void)cmd; (void)connection; (void)response;
    ++forwarded; return cancel_send ? 0 : req;
}
static void reset(void) {
    copies = 0; copy_failed = 0; corrupt_applied = 0; assigned_url = 0; captured[0] = 0;
    cancel_send = 0; forwarded = 0; scheme_value.text = "https";
    request.kind = REQUEST; edited.kind = REQUEST; url.kind = URL; components.kind = COMPONENTS;
    strcpy(host_buffer, "dispatchcnglobal.yuanshen.com"); strcpy(path_buffer, "/query_region_list");
}
static void setup(const char *absolute) {
    strcpy(absolute_text, absolute); query_text[0] = 0;
}
int main(void) {
    get_class = klass; selector = sel; message = (Imp)msg;
    file_open = open_log; file_write = write_log; file_close = close_log;
    strcpy(log_path, "test-only");
    reset();
    setup("https://dispatchcnglobal.yuanshen.com/query_region_list?version=CNRELiOS7.1.0&lang=2"
          "&platform=1&binary=1&channel_id=1&sub_channel_id=0&aid=PRIVATE");
    assert(dispatch_profile_request(&request) == &edited);
    assert(copies == 1 && assigned_url == &destination);
    assert(!strcmp(destination_text, "https://dispatchcnglobal.yuanshen.com/query_region_list"
        "?version=CNRELAndroid7.1.0&lang=2&platform=1&binary=1&channel_id=14&sub_channel_id=0&aid=PRIVATE"));
    assert(strstr(captured, "region list request carries the B release profile"));
    reset(); route_active = 1; strcpy(host_buffer, "cnqddispatch.yuanshen.com"); strcpy(path_buffer, "/query_cur_region");
    setup("https://cnqddispatch.yuanshen.com/query_cur_region?version=CNRELiOS7.1.0&lang=2&platform=1"
          "&binary=1&channel_id=14&sub_channel_id=0&account_type=14&key_id=4&dispatchSeed=A%2Bb&aid=PRIVATE");
    assert(dispatch_profile_request(&request) == &edited && copies == 1);
    assert(!strcmp(destination_text, "https://cnqddispatch.yuanshen.com/query_cur_region"
        "?version=CNRELAndroid7.1.0&lang=2&platform=1&binary=1&channel_id=14&sub_channel_id=0"
        "&account_type=14&key_id=4&dispatchSeed=A%2Bb&aid=PRIVATE"));
    assert(strstr(captured, "region request carries the B release profile"));
    reset(); route_active = 0;
    strcpy(host_buffer, "cnqddispatch.yuanshen.com"); strcpy(path_buffer, "/query_cur_region");
    setup("https://cnqddispatch.yuanshen.com/query_cur_region?version=CNRELiOS7.1.0&channel_id=14");
    assert(dispatch_profile_request(&request) == &request && copies == 0);
    reset(); route_active = 1;
    strcpy(host_buffer, "cnqddispatch.yuanshen.com"); strcpy(path_buffer, "/query_cur_region");
    setup("https://cnqddispatch.yuanshen.com/query_cur_region?version=CNRELAndroid7.1.0&channel_id=14");
    assert(dispatch_profile_request(&request) == &request && copies == 0);
    reset(); strcpy(host_buffer, "unrelated.example");
    setup("https://unrelated.example/query_cur_region?version=CNRELiOS7.1.0");
    assert(dispatch_profile_request(&request) == &request);
    reset(); strcpy(path_buffer, "/login");
    setup("https://dispatchcnglobal.yuanshen.com/login?version=CNRELiOS7.1.0");
    assert(dispatch_profile_request(&request) == &request);
    reset();
    setup("https://dispatchcnglobal.yuanshen.com/query_region_list");
    assert(dispatch_profile_request(&request) == &request);
    reset();
    setup("https://dispatchcnglobal.yuanshen.com/query_region_list?version=CNRELWin7.1.0&channel_id=1");
    assert(dispatch_profile_request(&request) == &request);
    reset(); copy_failed = 1;
    setup("https://dispatchcnglobal.yuanshen.com/query_region_list?version=CNRELiOS7.1.0&channel_id=1");
    assert(dispatch_profile_request(&request) == &request && copies == 1);
    reset(); corrupt_applied = 1;
    setup("https://dispatchcnglobal.yuanshen.com/query_region_list?version=CNRELiOS7.1.0&channel_id=1");
    assert(dispatch_profile_request(&request) == &request);
    assert(strstr(captured, "rewrite unavailable"));
    reset();
    setup("https://dispatchcnglobal.yuanshen.com/query_region_list?version=CNRELiOS7.1.0&channel_id=14");
    assert(dispatch_profile_request(&request) == &edited && copies == 1);
    assert(strstr(destination_text, "channel_id=14") && strstr(destination_text, "version=CNRELAndroid7.1.0"));
    reset();
    setup("https://dispatchcnglobal.yuanshen.com/query_region_list?version=CNRELAndroid7.1.0&channel_id=1");
    assert(dispatch_profile_request(&request) == &request && copies == 0);
    reset();
    setup("https://dispatchcnglobal.yuanshen.com/query_region_list?channel_id=1&platform=1");
    assert(dispatch_profile_request(&request) == &request && copies == 0);
    assert(dispatch_profile_request(0) == 0);
    reset(); scheme_value.text = "http";
    setup("http://dispatchcnglobal.yuanshen.com/query_region_list?version=CNRELiOS7.1.0&channel_id=1");
    assert(dispatch_profile_request(&request) == &request && copies == 0);
    dispatch_next_unity_send = original_send;
    reset(); route_active = 0;
    setup("https://dispatchcnglobal.yuanshen.com/query_region_list?version=CNRELiOS7.1.0&channel_id=1");
    assert(dispatch_unity_send(0, 0, 0, &request, 0) == &edited && copies == 1 && forwarded == 1);
    assert(strstr(destination_text, "version=CNRELAndroid7.1.0&channel_id=14"));
    reset();
    setup("https://dispatchcnglobal.yuanshen.com/query_region_list?version=CNRELiOS7.1.0&channel_id=1");
    assert(dispatch_unity_send(0, 0, 0, &request, (Obj)101) == &request && copies == 0 && forwarded == 1);
    reset(); cancel_send = 1;
    assert(!dispatch_unity_send(0, 0, 0, &request, 0) && copies == 0 && forwarded == 1);
    return 0;
}
