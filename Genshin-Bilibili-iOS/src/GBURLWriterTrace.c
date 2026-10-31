/* Observe the original managed URL string; keep its contents out of the log. */
static int (*writer_string_length)(Obj);
static const unsigned short *(*writer_string_chars)(Obj);
__attribute__((visibility("default"))) void GBDispatchURLWriteTrace(Obj self, Obj value) {
    (void)self;
    if (!value || !writer_string_length || !writer_string_chars || !stack_game_base) return;
    int length = writer_string_length(value);
    if (length <= 0 || length > 4096) return;
    const unsigned short *chars = writer_string_chars(value);
    if (!chars) return;
    char path[128];
    int size = 0;
    /* Inspect only scheme/host/path, stopping before all query parameters. */
    while (size < length && size < (int)sizeof(path) - 1) {
        unsigned short c = chars[size];
        if (c == '?' || c == '#') break;
        if (c < 33 || c > 126) return;
        path[size++] = (char)c;
    }
    if (size == (int)sizeof(path) - 1) return;
    path[size] = 0;
    int kind = -1;
    if (equal(path, "https://dispatchcnglobal.yuanshen.com/query_region_list")) kind = 0;
    if (equal(path, "https://cnqddispatch.yuanshen.com/query_cur_region") ||
        equal(path, "https://cngfdispatch.yuanshen.com/query_cur_region")) kind = 1;
    if (kind >= 0) stack_capture_at(kind, 1);
}
static void writer_trace_prepare(void) {
    if (!stack_game_base) return;
    const unsigned char expected[] = {0x1f,0xf5,0x00,0x14,0x1c,0xf5,0x00,0x14};
    const unsigned char *entry = (const unsigned char *)stack_game_base + 0xe9bb78c;
    for (Size i = 0; i < sizeof(expected); ++i) if (entry[i] != expected[i]) {
        record("URL writer string accessors mismatch; observation disabled"); return;
    }
    writer_string_length = (void *)entry;
    writer_string_chars = (void *)(entry + 4);
    record("URL writer observation prepared; original setter and arguments preserved");
}
