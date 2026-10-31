#define GB_ENABLE_BILI_COMPONENT 1
#define main bridge_runtime_main
#include "test_bridge_runtime.c"
#undef main

static int finishes, last_result;
static void capture_finish(void *queue, void *context, void (*fn)(void *)) {
    (void)queue;
    assert(fn == bili_finish_on_main);
    ++finishes;
    last_result = (int)(__UINTPTR_TYPE__)context;
}
static Obj provider_message(Obj self, Sel cmd, Obj arg) {
    (void)arg;
    if (strcmp((const char *)cmd, "isKindOfClass:") == 0) return (Obj)1;
    if (strcmp((const char *)cmd, "UTF8String") == 0) return self;
    return 0;
}
int main(void) {
    assert(bridge_runtime_main() == 0);
    struct { void *isa; unsigned long generation; } delegate = {0, 101};
    bili_delegate = &delegate;
    bili_generation_offset = sizeof(void *);
    bridge.generation = 101;
    bridge.phase = GB_SDK_LOGIN;
    bili_finishing = 0;
    bridge_dispatch = capture_finish;
    message = (Imp)provider_message;
    bili_success(&delegate, 0, (Obj)"123", (Obj)"synthetic-test-access-key");
    assert(finishes == 1 && last_result == 1 && bridge.phase == GB_VERIFYING);
    /* An SDK success is not game-server authentication. */
    assert(bridge.phase != GB_AUTHENTICATED);
    bili_success(&delegate, 0, (Obj)"123", (Obj)"synthetic-test-access-key");
    assert(finishes == 1);
    bili_finishing = 0;
    ++bridge.generation;
    bridge.phase = GB_SDK_LOGIN;
    bili_success(&delegate, 0, (Obj)"123", (Obj)"synthetic-test-access-key");
    assert(finishes == 1);
    delegate.generation = bridge.generation;
    bili_success(&delegate, 0, (Obj)"not-numeric", (Obj)"synthetic-test-access-key");
    assert(finishes == 2 && last_result == 0);
    bili_finishing = 0;
    bili_failure(&delegate, 0, 0);
    assert(finishes == 3 && last_result == 0);
    bili_finishing = 0;
    bili_timeout((void *)(__UINTPTR_TYPE__)(bridge.generation - 1));
    assert(finishes == 3);
    bili_timeout((void *)(__UINTPTR_TYPE__)bridge.generation);
    assert(finishes == 4 && last_result == 0);
    return 0;
}
