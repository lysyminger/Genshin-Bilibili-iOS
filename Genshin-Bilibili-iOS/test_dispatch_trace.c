#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#define __attribute__(attributes)
#define GB_ENABLE_DISPATCH_TRACE 1
#define GB_ENABLE_DISPATCH_ROUTE 1
static int route_active;
static int combo_dispatch_route_active(void) { return route_active; }
#include "src/GBDiagnostic.c"
#undef __attribute__

void *dlsym(void *handle, const char *name) { (void)handle; (void)name; return 0; }
enum { TASK = 1, REQUEST, URL, COMPONENTS, ITEMS, ITEM, TEXT, UNITY };
struct Value { int kind; const char *text; Obj name, value; };
static struct Value task = {TASK}, request = {REQUEST}, url = {URL};
static struct Value unity = {UNITY}, redirected = {REQUEST};
static struct Value routed = {REQUEST}, destination = {URL};
static struct Value components = {COMPONENTS}, items = {ITEMS};
static struct Value scheme = {TEXT, "https"}, destination_host = {TEXT, "cnqddispatch.yuanshen.com"};
static struct Value routed_absolute = {TEXT, "https://cnqddispatch.yuanshen.com/query_cur_region?uid=PRIVATE"};
static struct Value encoded_query = {TEXT, "channel_id=14&account_type=14&dispatchSeed=A%2Bb&uid=PRIVATE"};
static struct Value bad_query = {TEXT, "changed"};
static Obj parts_host, copied_from, assigned_url;
static int copies, autoreleases, copy_failed, corrupt_query;
static Size query_count = 5;
static struct Value texts[] = {
    {TEXT, "dispatchcnglobal.yuanshen.com"}, {TEXT, "/query_region_list"},
    {TEXT, "https://dispatchcnglobal.yuanshen.com/query_region_list?uid=PRIVATE"},
    {TEXT, "version"}, {TEXT, "CNRELiOS7.1.0"}, {TEXT, "channel_id"}, {TEXT, "14"},
    {TEXT, "uid"}, {TEXT, "PRIVATE_UID"}, {TEXT, "combo_token"}, {TEXT, "PRIVATE_TOKEN"},
    {TEXT, "region"}, {TEXT, "cn_qd01\nFORGED=1"},
    {TEXT, "account_type"}, {TEXT, "14"},
};
static struct Value query[] = {
    {ITEM, 0, &texts[3], &texts[4]}, {ITEM, 0, &texts[5], &texts[6]},
    {ITEM, 0, &texts[7], &texts[8]}, {ITEM, 0, &texts[9], &texts[10]},
    {ITEM, 0, &texts[11], &texts[12]},
    {ITEM, 0, &texts[13], &texts[14]},
};
static char captured[1024];
static int forwarded, use_original_request, unity_started, unity_sent, cancel_send;
static Obj expected_response = (Obj)101;
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
        assert(!strcmp(va_arg(args, const char *), destination_host.text)); result = &destination_host;
    }
    else if (!strcmp(name, "componentsWithString:")) {
        Obj input = va_arg(args, Obj); assert(input == &texts[2] || input == &routed_absolute);
        parts_host = input == &texts[2] ? &texts[0] : &destination_host; result = &components;
    } else if (!strcmp(name, "currentRequest")) result = use_original_request ? 0 : &request;
    else if (!strcmp(name, "originalRequest")) result = &request;
    else if (!strcmp(name, "request")) { assert(v == &unity); result = &request; }
    else if (!strcmp(name, "URL")) {
        if(v == &components) result = &destination;
        else if(v == &routed) result = assigned_url;
        else { assert(v == &request || v == &redirected); result = &url; }
    }
    else if (!strcmp(name, "scheme")) { assert(v == &url); result = &scheme; }
    else if (!strcmp(name, "host")) { assert(v == &url || v == &destination); result = v == &url ? &texts[0] : &destination_host; }
    else if (!strcmp(name, "path")) { assert(v == &url || v == &destination); result = &texts[1]; }
    else if (!strcmp(name, "absoluteString")) { assert(v == &url || v == &destination); result = v == &url ? &texts[2] : &routed_absolute; }
    else if (!strcmp(name, "setHost:")) { assert(v == &components); parts_host = va_arg(args, Obj); }
    else if (!strcmp(name, "percentEncodedQuery")) {
        assert(v == &components); result = corrupt_query && parts_host == &destination_host ? &bad_query : &encoded_query;
    }
    else if (!strcmp(name, "mutableCopy")) {
        assert(v == &request || v == &redirected); copied_from = v; ++copies; result = copy_failed ? 0 : &routed;
    }
    else if (!strcmp(name, "setURL:")) { assert(v == &routed); assigned_url = va_arg(args, Obj); }
    else if (!strcmp(name, "autorelease")) { assert(v == &routed); ++autoreleases; result = v; }
    else if (!strcmp(name, "queryItems")) { assert(v == &components); result = &items; }
    else if (!strcmp(name, "count")) { assert(v == &items); result = (Obj)(__UINTPTR_TYPE__)query_count; }
    else if (!strcmp(name, "objectAtIndex:")) {
        Size i = va_arg(args, Size); assert(v == &items && i < 6); result = &query[i];
    } else if (!strcmp(name, "name")) { assert(v->kind == ITEM); result = v->name; }
    else if (!strcmp(name, "value")) { assert(v->kind == ITEM); result = v->value; }
    else if (!strcmp(name, "UTF8String")) { assert(v->kind == TEXT); result = (Obj)v->text; }
    else assert(!"Unexpected selector; requests must not be mutated");
    va_end(args); return result;
}
static int open_log(const char *path, int flags, ...) { (void)path; (void)flags; return 7; }
static long write_log(int fd, const void *data, Size size) {
    assert(fd == 7 && size < sizeof(captured));
    memcpy(captured, data, size); captured[size] = 0; return (long)size;
}
static int close_log(int fd) { assert(fd == 7); return 0; }
static void original_resume(Obj self, Sel command) {
    assert(self == &task && !strcmp(command, "resume")); ++forwarded;
}
static void original_unity_start(Obj self, Sel command) {
    assert(self == &unity && !strcmp(command, "startConnection")); ++unity_started;
}
static Obj original_unity_send(Obj self, Sel command, Obj connection, Obj req, Obj response) {
    assert(self == &unity && !strcmp(command, "connection:willSendRequest:redirectResponse:"));
    assert(connection == (Obj)100 && req == &request && response == expected_response);
    ++unity_sent; return cancel_send ? 0 : &redirected;
}
int main(void) {
    get_class = klass; selector = sel; message = (Imp)msg;
    file_open = open_log; file_write = write_log; file_close = close_log;
    strcpy(log_path, "test-only"); dispatch_next_resume = original_resume;
    dispatch_resume(&task, "resume");
    assert(forwarded == 1);
    assert(strstr(captured, "version=CNRELiOS7.1.0 channel_id=14"));
    assert(strstr(captured, "region=cn_qd01?FORGED?1"));
    assert(!strstr(captured, "PRIVATE") && !strstr(captured, "uid=") && !strstr(captured, "combo_token"));
    assert(strchr(captured, '\n') == captured + strlen(captured) - 1);
    use_original_request = 1;
    dispatch_resume(&task, "resume"); assert(forwarded == 2);
    texts[0].text = "unrelated.example"; captured[0] = 0;
    dispatch_resume(&task, "resume"); assert(forwarded == 3 && !captured[0]);
    texts[0].text = "dispatchcnglobal.yuanshen.com"; texts[1].text = "/login";
    dispatch_resume(&task, "resume"); assert(forwarded == 4 && !captured[0]);
    char bounded[8];
    assert(dispatch_value(bounded, 0, sizeof(bounded), "123456789") == 7);
    assert(!strcmp(bounded, "1234567"));
    texts[1].text = "/query_cur_region";
    dispatch_next_unity_start = original_unity_start;
    dispatch_unity_start(&unity, "startConnection");
    assert(unity_started == 1 && strstr(captured, "path=/query_cur_region"));
    assert(!strstr(captured, "PRIVATE"));
    dispatch_next_unity_send = original_unity_send;
    assert(dispatch_unity_send(&unity, "connection:willSendRequest:redirectResponse:",
        (Obj)100, &request, (Obj)101) == &redirected);
    assert(unity_sent == 1 && strstr(captured, "path=/query_cur_region"));
    cancel_send = 1; captured[0] = 0;
    assert(!dispatch_unity_send(&unity, "connection:willSendRequest:redirectResponse:",
        (Obj)100, &request, (Obj)101));
    assert(unity_sent == 2 && !captured[0]);
    cancel_send = 0; texts[0].text = "cngfdispatch.yuanshen.com";
    texts[2].text = "https://cngfdispatch.yuanshen.com/query_cur_region?uid=PRIVATE";
    query_count = 6;
    assert(dispatch_bili_request(&redirected) == &redirected && !copies);
    route_active = 1;
    assert(dispatch_unity_send(&unity, "connection:willSendRequest:redirectResponse:",
        (Obj)100, &request, (Obj)101) == &redirected && !copies);
    expected_response = 0;
    assert(dispatch_unity_send(&unity, "connection:willSendRequest:redirectResponse:",
        (Obj)100, &request, 0) == &routed);
    assert(copies == 1 && copied_from == &redirected && assigned_url == &destination && autoreleases == 1);
    assert(!strcmp(texts[0].text, "cngfdispatch.yuanshen.com"));
    assert(strstr(captured, "host=cnqddispatch.yuanshen.com") && !strstr(captured, "PRIVATE"));
    cancel_send = 1;
    assert(!dispatch_unity_send(&unity, "connection:willSendRequest:redirectResponse:",
        (Obj)100, &request, 0) && copies == 1);
    cancel_send = 0;
    texts[6].text = "1"; assert(dispatch_bili_request(&redirected) == &redirected && copies == 1);
    texts[6].text = "14"; texts[14].text = "1";
    assert(dispatch_bili_request(&redirected) == &redirected && copies == 1);
    texts[14].text = "14"; query_count = 5;
    assert(dispatch_bili_request(&redirected) == &redirected && copies == 1);
    query_count = 6; query[5].name = &texts[5];
    assert(dispatch_bili_request(&redirected) == &redirected && copies == 1);
    query[5].name = &texts[13]; texts[1].text = "/query_region_list";
    assert(dispatch_bili_request(&redirected) == &redirected && copies == 1);
    texts[1].text = "/query_cur_region"; texts[0].text = "cnqddispatch.yuanshen.com";
    assert(dispatch_bili_request(&redirected) == &redirected && copies == 1);
    texts[0].text = "unrelated.example";
    assert(dispatch_bili_request(&redirected) == &redirected && copies == 1);
    texts[0].text = "cngfdispatch.yuanshen.com"; scheme.text = "http";
    assert(dispatch_bili_request(&redirected) == &redirected && copies == 1);
    scheme.text = "https"; query_count = 129;
    assert(dispatch_bili_request(&redirected) == &redirected && copies == 1);
    query_count = 6; corrupt_query = 1;
    assert(dispatch_bili_request(&redirected) == &redirected && copies == 1);
    corrupt_query = 0; copy_failed = 1;
    assert(dispatch_bili_request(&redirected) == &redirected && copies == 2 && autoreleases == 1);
    return 0;
}
