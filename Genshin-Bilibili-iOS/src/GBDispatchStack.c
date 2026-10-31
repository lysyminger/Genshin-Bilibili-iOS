/* Trace only game return-address offsets at dispatch URL construction. */
typedef __UINTPTR_TYPE__ StackAddress;
typedef struct {
    const char *name;
    void *base;
    const char *symbol;
    void *symbol_address;
} StackImage;
static int (*stack_backtrace)(void **, int);
static int (*stack_image)(const void *, StackImage *);
static Obj (*stack_next_init)(Obj, Sel, Obj, void *);
static void *stack_game_base;
static unsigned stack_attempts[2][2];

static Size stack_hex(char *out, Size pos, Size capacity, StackAddress value) {
    static const char digits[] = "0123456789abcdef";
    char reversed[sizeof(value) * 2];
    Size count = 0;
    do { reversed[count++] = digits[value & 15]; value >>= 4; } while (value);
    while (count && pos + 1 < capacity) out[pos++] = reversed[--count];
    out[pos] = 0;
    return pos;
}
static int stack_dispatch_kind(Obj url) {
    if (!responds(url, "scheme") || !responds(url, "host") || !responds(url, "path")) return -1;
    if (!equal(utf8(send0(url, "scheme")), "https")) return -1;
    const char *host = utf8(send0(url, "host"));
    const char *path = utf8(send0(url, "path"));
    if (equal(host, "dispatchcnglobal.yuanshen.com") && equal(path, "/query_region_list")) return 0;
    if ((equal(host, "cngfdispatch.yuanshen.com") || equal(host, "cnqddispatch.yuanshen.com")) &&
        equal(path, "/query_cur_region")) return 1;
    return -1;
}
__attribute__((noinline)) static void stack_capture_at(int kind, int early) {
    if (kind < 0 || !stack_backtrace || !stack_image || !stack_game_base) return;
    unsigned attempt = __atomic_fetch_add(&stack_attempts[early][kind], 1, __ATOMIC_RELAXED);
    if (attempt >= 2) return;
    const char *label = kind ? "cur_region" : "region_list";
    void *frames[48];
    int count = stack_backtrace(frames, 48), emitted = 0;
    if (count < 0) count = 0;
    if (count > 48) count = 48;
    for (int i = 0; i < count; ++i) {
        StackImage image = {0};
        if (!stack_image(frames[i], &image) || image.base != stack_game_base ||
            (StackAddress)frames[i] < (StackAddress)stack_game_base) continue;
        char line[160];
        Size pos = append(line, 0, sizeof(line), early ? "dispatch stack phase=url_write kind=" : "dispatch stack kind=");
        pos = append(line, pos, sizeof(line), label);
        pos = append(line, pos, sizeof(line), " attempt=");
        pos = number(line, pos, sizeof(line), attempt + 1);
        pos = append(line, pos, sizeof(line), " frame=");
        pos = number(line, pos, sizeof(line), i);
        pos = append(line, pos, sizeof(line), " Yuanshen+0x");
        stack_hex(line, pos, sizeof(line), (StackAddress)frames[i] - (StackAddress)stack_game_base);
        record(line);
        ++emitted;
    }
    if (!emitted) record("dispatch stack has no game frames; generation source remains unknown");
}
static void stack_capture(int kind) { stack_capture_at(kind, 0); }
static Obj stack_init(Obj self, Sel cmd, Obj url, void *userdata) {
    stack_capture(stack_dispatch_kind(url));
    return stack_next_init(self, cmd, url, userdata);
}
static void dispatch_stack_install(void) {
    void *all = (void *)-2;
    stack_backtrace = resolve(all, "backtrace");
    stack_image = resolve(all, "dladdr");
    Imp (*implementation)(void *) = resolve(all, "method_getImplementation");
    void *method = get_method(get_class("UnityWWWConnectionDelegate"), selector("initWithURL:udata:"));
    if (!stack_backtrace || !stack_image || !implementation || !method ||
        !equal(method_types(method), "@32@0:8@16^v24")) {
        record("dispatch construction stack unavailable; original initializer retained"); return;
    }
    StackImage image = {0};
    if (!stack_image((const void *)implementation(method), &image) || !image.base || !image.name) return;
    const char *name = image.name;
    for (const char *p = name; *p; ++p) if (*p == '/') name = p + 1;
    if (!equal(name, "Yuanshen")) {
        record("dispatch initializer is outside the game image; original initializer retained"); return;
    }
    stack_game_base = image.base;
    stack_next_init = (void *)install("UnityWWWConnectionDelegate", "initWithURL:udata:",
        "@32@0:8@16^v24", (Imp)stack_init);
    if (stack_next_init) record("dispatch construction stack installed; game offsets only; two captures per kind");
}
