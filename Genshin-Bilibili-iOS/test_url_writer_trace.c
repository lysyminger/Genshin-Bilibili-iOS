#define GB_ENABLE_URL_WRITER 1
#define main existing_stack_tests
#include "test_dispatch_stack.c"
#undef main

static unsigned short managed_chars[4096];
static int managed_length;
static int get_length(Obj value) { assert(value == (Obj)99); return managed_length; }
static const unsigned short *get_chars(Obj value) { assert(value == (Obj)99); return managed_chars; }
static void set_url(const char *value) {
    managed_length = (int)strlen(value);
    assert(managed_length < 4096);
    for (int i = 0; i < managed_length; ++i) managed_chars[i] = (unsigned char)value[i];
}
int main(void) {
    assert(existing_stack_tests() == 0);
    trace_calls = 0; no_frames = 0; captured[0] = 0;
    stack_backtrace = trace; writer_string_length = get_length; writer_string_chars = get_chars;
    stack_attempts[1][0] = stack_attempts[1][1] = 0;
    set_url("https://cnqddispatch.yuanshen.com/query_cur_region?dispatchSeed=PRIVATE&aid=PRIVATE");
    GBDispatchURLWriteTrace(0, (Obj)99);
    assert(trace_calls == 1 && strstr(captured, "phase=url_write kind=cur_region"));
    assert(!strstr(captured, "PRIVATE") && !strstr(captured, "dispatchSeed"));
    GBDispatchURLWriteTrace(0, (Obj)99); GBDispatchURLWriteTrace(0, (Obj)99);
    assert(trace_calls == 2);
    set_url("http://cnqddispatch.yuanshen.com/query_cur_region");
    GBDispatchURLWriteTrace(0, (Obj)99); assert(trace_calls == 2);
    set_url("https://unrelated.example/query_region_list");
    GBDispatchURLWriteTrace(0, (Obj)99); assert(trace_calls == 2);
    set_url("https://dispatchcnglobal.yuanshen.com/query_region_list?PRIVATE");
    managed_chars[managed_length - 1] = 0xffff;  /* Query contents are never inspected. */
    GBDispatchURLWriteTrace(0, (Obj)99); assert(trace_calls == 3);
    managed_length = 4097; GBDispatchURLWriteTrace(0, (Obj)99); assert(trace_calls == 3);
    managed_length = 0; GBDispatchURLWriteTrace(0, (Obj)99); assert(trace_calls == 3);
    GBDispatchURLWriteTrace(0, 0); assert(trace_calls == 3);
    writer_string_length = 0; GBDispatchURLWriteTrace(0, (Obj)99); assert(trace_calls == 3);
    return 0;
}
