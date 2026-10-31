/* Passive SDK tracing. Build for arm64 iOS; no Apple SDK headers required. */
typedef void *Obj;
typedef void *Sel;
typedef void (*Imp)(void);
typedef unsigned long Size;
extern void *dlsym(void *, const char *);
/* Taken through data so the only import is a regular bind, not a lazy-only table. */
static void *(*volatile resolve)(void *, const char *) = dlsym;
#ifndef GB_DIAGNOSTIC_VERSION
#define GB_DIAGNOSTIC_VERSION "0.6"
#endif
#ifdef GB_ENABLE_BRIDGE
static void bridge_install(void);
#endif

static Obj (*get_class)(const char *);
static Sel (*selector)(const char *);
static void *(*get_method)(Obj, Sel);
static const char *(*method_types)(void *);
static Imp (*replace_method)(void *, Imp);
static Imp message;
static int (*file_open)(const char *, int, ...);
static long (*file_write)(int, const void *, Size);
static int (*file_close)(int);
static char log_path[1024];

static Obj send0(Obj obj, const char *name) {
    return ((Obj (*)(Obj, Sel))message)(obj, selector(name));
}
static Obj send1(Obj obj, const char *name, Obj arg) {
    return ((Obj (*)(Obj, Sel, Obj))message)(obj, selector(name), arg);
}
static int responds(Obj obj, const char *name) {
    return ((unsigned char (*)(Obj, Sel, Sel))message)(obj, selector("respondsToSelector:"), selector(name));
}
static Obj string(const char *text) {
    return send1(get_class("NSString"), "stringWithUTF8String:", (Obj)text);
}
static const char *utf8(Obj value) {
    return (const char *)send0(value, "UTF8String");
}
static int equal(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static Size append(char *out, Size pos, Size capacity, const char *text) {
    if (text) while (*text && pos + 1 < capacity) out[pos++] = *text++;
    out[pos] = 0;
    return pos;
}
static Size number(char *out, Size pos, Size capacity, long value) {
    char digits[24];
    Size count = 0;
    if (value < 0) { pos = append(out, pos, capacity, "-"); value = -value; }
    do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value && count < sizeof(digits));
    while (count && pos + 1 < capacity) out[pos++] = digits[--count];
    out[pos] = 0;
    return pos;
}
static void record(const char *text) {
    if (!log_path[0]) return;
    char line[512];
    Size size = append(line, 0, sizeof(line), text);
    size = append(line, size, sizeof(line), "\n");
    /* Darwin O_WRONLY | O_APPEND | O_CREAT; one write per complete record. */
    int fd = file_open(log_path, 0x209, 0600);
    if (fd >= 0) { file_write(fd, line, size); file_close(fd); }
}
static void record_name(const char *prefix, Obj name) {
    char line[256];
    Size pos = append(line, 0, sizeof(line), prefix);
    unsigned char is_string = ((unsigned char (*)(Obj, Sel, Obj))message)(name, selector("isKindOfClass:"), get_class("NSString"));
    const char *text = is_string ? utf8(name) : "[nonstring]";
    if (text) {
        for (Size i = 0; text[i] && i < 128 && pos + 1 < sizeof(line); ++i) {
            char c = text[i];
            line[pos++] = (c >= 32 && c <= 126) ? c : '?';
        }
    }
    line[pos] = 0;
    record(line);
}

static void (*original_invoke)(Obj, Sel, Obj, Obj, int);
static Obj (*original_return)(Obj, Sel, Obj, Obj);
static void (*original_callback)(Obj, Sel, Obj, Obj, int);
static void (*original_callback_short)(Obj, Sel, Obj, Obj);
static void (*original_notify)(Obj, Sel, Obj, Obj);
static void (*original_environment)(Obj, Sel, long, Obj);

static void trace_invoke(Obj self, Sel cmd, Obj function, Obj args, int index) {
    record_name("invoke ", function);
    original_invoke(self, cmd, function, args, index);
}
static Obj trace_return(Obj self, Sel cmd, Obj function, Obj args) {
    record_name("invokeReturn ", function);
    return original_return(self, cmd, function, args);
}
static void record_status(Obj response) {
    unsigned char is_dict = ((unsigned char (*)(Obj, Sel, Obj))message)(response, selector("isKindOfClass:"), get_class("NSDictionary"));
    if (is_dict) {
        Obj ret = send1(response, "objectForKey:", string("ret"));
        if (!ret) ret = send1(response, "objectForKey:", string("retcode"));
        unsigned char numeric = ((unsigned char (*)(Obj, Sel, Sel))message)(ret, selector("respondsToSelector:"), selector("intValue"));
        if (numeric) {
            char line[64];
            Size pos = append(line, 0, sizeof(line), "callback status=");
            number(line, pos, sizeof(line), ((int (*)(Obj, Sel))message)(ret, selector("intValue")));
            record(line);
        }
    }
}
#ifdef GB_ENABLE_LOGIN_SHAPE
static int shape_is(Obj value, const char *name) {
    return value && ((unsigned char (*)(Obj, Sel, Obj))message)(value,
        selector("isKindOfClass:"), get_class(name));
}
static Obj shape_payload(Obj value) {
    for (int depth = 0; depth < 3; ++depth) {
        if (shape_is(value, "NSString")) {
            Size length = ((Size (*)(Obj, Sel))message)(value, selector("length"));
            Obj tool = get_class("MiHoYoSDKTool");
            if (length > 65536 || !responds(tool, "stringToDictionary:")) return (Obj)0;
            value = send1(tool, "stringToDictionary:", value);
        }
        if (!shape_is(value, "NSDictionary")) return (Obj)0;
        if (send1(value, "objectForKey:", string("open_id")) ||
            send1(value, "objectForKey:", string("combo_token"))) return value;
        Obj data = send1(value, "objectForKey:", string("data"));
        if (!data) return value;
        value = data;
    }
    return (Obj)0;
}
static void record_login_shape(const char *stage, Obj response) {
    Obj data = shape_payload(response);
    if (!data) { record("login shape unavailable: unrecognized payload"); return; }
    const char *keys[] = {"open_id", "combo_token", "device_id", "channel_token", "account_type",
        "channel_id", "app_id", "guest", "is_new_register", "login_method", "open_token",
        "name", "asterisk_name"};
    for (int i = 0; i < 13; ++i) {
        Obj value = send1(data, "objectForKey:", string(keys[i]));
        const char *kind = !value ? "missing" : shape_is(value, "NSString") ? "string" :
            shape_is(value, "NSNumber") ? "number" : shape_is(value, "NSDictionary") ? "dictionary" : "other";
        char line[192];
        Size pos = append(line, 0, sizeof(line), "login shape stage=");
        pos = append(line, pos, sizeof(line), stage);
        pos = append(line, pos, sizeof(line), " field=");
        pos = append(line, pos, sizeof(line), keys[i]);
        pos = append(line, pos, sizeof(line), " type=");
        pos = append(line, pos, sizeof(line), kind);
        if (equal(kind, "string")) {
            Size length = ((Size (*)(Obj, Sel))message)(value, selector("length"));
            pos = append(line, pos, sizeof(line), length ? " nonempty=1" : " nonempty=0");
        } else if (equal(kind, "number") && i >= 4 && i <= 6) {
            long long number_value = ((long long (*)(Obj, Sel))message)(value, selector("longLongValue"));
            if (number_value >= 0 && number_value <= 64) {
                pos = append(line, pos, sizeof(line), " value=");
                number(line, pos, sizeof(line), (long)number_value);
            }
        }
        record(line);
    }
}
static void trace_login_shape(Obj function, Obj response) {
    if (!shape_is(function, "NSString")) return;
    const char *name = utf8(function);
    if (equal(name, "login_login") || equal(name, "MiHoYoChannelModulelogin"))
        record_login_shape("host_callback", response);
}
#endif
static void trace_callback(Obj self, Sel cmd, Obj function, Obj response, int index) {
    record_name("callback indexed ", function);
    record_status(response);
#ifdef GB_ENABLE_LOGIN_SHAPE
    trace_login_shape(function, response);
#endif
    original_callback(self, cmd, function, response, index);
}
static void trace_callback_short(Obj self, Sel cmd, Obj function, Obj response) {
    record_name("callback direct ", function);
    record_status(response);
#ifdef GB_ENABLE_LOGIN_SHAPE
    trace_login_shape(function, response);
#endif
    original_callback_short(self, cmd, function, response);
}
static void trace_notify(Obj self, Sel cmd, Obj event, Obj params) {
    record_name("notify ", event);
    original_notify(self, cmd, event, params);
}
static void trace_environment(Obj self, Sel cmd, long env, Obj game_biz) {
    record("setEnv:gameBiz: begin");
    original_environment(self, cmd, env, game_biz);
    Obj config_class = get_class("MHYConfig");
    Obj config = responds(config_class, "instance") ? send0(config_class, "instance") : (Obj)0;
    if (!responds(config, "channelID") || !responds(config, "clientType")) {
        record("configuration unavailable");
        return;
    }
    char line[96];
    Size pos = append(line, 0, sizeof(line), "configuration env=");
    pos = number(line, pos, sizeof(line), env);
    pos = append(line, pos, sizeof(line), " channel=");
    pos = number(line, pos, sizeof(line), ((int (*)(Obj, Sel))message)(config, selector("channelID")));
    pos = append(line, pos, sizeof(line), " client_type=");
    number(line, pos, sizeof(line), ((long (*)(Obj, Sel))message)(config, selector("clientType")));
    record(line);
}

static Imp install(const char *class_name, const char *method, const char *types, Imp replacement) {
    void *entry = get_method(get_class(class_name), selector(method));
    if (!entry || !equal(method_types(entry), types)) {
        char line[192];
        Size pos = append(line, 0, sizeof(line), "hook unavailable: ");
        pos = append(line, pos, sizeof(line), class_name);
        pos = append(line, pos, sizeof(line), " ");
        append(line, pos, sizeof(line), method);
        record(line);
        return (Imp)0;
    }
    Imp original = replace_method(entry, replacement);
    char line[192];
    Size pos = append(line, 0, sizeof(line), "hook installed: ");
    pos = append(line, pos, sizeof(line), class_name);
    pos = append(line, pos, sizeof(line), " ");
    append(line, pos, sizeof(line), method);
    record(line);
    return original;
}

#ifdef GB_ENABLE_DISPATCH_TRACE
#include "GBDispatchTrace.c"
#endif
#ifdef GB_ENABLE_DISPATCH_STACK
#include "GBDispatchStack.c"
#endif
#ifdef GB_ENABLE_URL_WRITER
#include "GBURLWriterTrace.c"
#endif
#ifdef GB_ENABLE_REGION_DIRECTORY
#include "GBRegionDirectory.c"
#endif

__attribute__((constructor)) static void initialize(void) {
    void *all = (void *)-2; /* RTLD_DEFAULT on Darwin. */
    get_class = resolve(all, "objc_getClass");
    selector = resolve(all, "sel_registerName");
    get_method = resolve(all, "class_getInstanceMethod");
    method_types = resolve(all, "method_getTypeEncoding");
    replace_method = resolve(all, "method_setImplementation");
    message = (Imp)resolve(all, "objc_msgSend");
    file_open = resolve(all, "open");
    file_write = resolve(all, "write");
    file_close = resolve(all, "close");
    Obj (*home_directory)(void) = resolve(all, "NSHomeDirectory");
    if (!get_class || !selector || !get_method || !method_types || !replace_method || !message || !file_open || !file_write || !file_close || !home_directory) return;
    const char *home = utf8(home_directory());
    Size pos = append(log_path, 0, sizeof(log_path), home);
    if (!home || pos > sizeof(log_path) - 48) { log_path[0] = 0; return; }
    append(log_path, pos, sizeof(log_path), "/Documents/GBDiagnostic.log");
    record("--- GBDiagnostic " GB_DIAGNOSTIC_VERSION " | target 7.1.0 build 1241 | tracing ---");
    record("No account/password/token/response payload logging is implemented.");
    Obj device = send0(get_class("UIDevice"), "currentDevice");
    record_name("iOS ", send0(device, "systemVersion"));
    original_invoke = (void *)install("MiHoYoComboManager", "invoke:args:index:", "v36@0:8@16@24i32", (Imp)trace_invoke);
    original_return = (void *)install("MiHoYoComboManager", "invokeReturn:args:", "@32@0:8@16@24", (Imp)trace_return);
    original_callback = (void *)install("MiHoYoComboCallback", "callBack:response:index:", "v36@0:8@16@24i32", (Imp)trace_callback);
    original_callback_short = (void *)install("MiHoYoComboCallback", "callBack:response:", "v32@0:8@16@24", (Imp)trace_callback_short);
    original_notify = (void *)install("MiHoYoComboCallback", "notifyWithEvent:param:", "v32@0:8@16@24", (Imp)trace_notify);
    original_environment = (void *)install("MiHoYoComboConfig", "setEnv:gameBiz:", "v32@0:8q16@24", (Imp)trace_environment);
    record("initialization complete");
#ifdef GB_ENABLE_DISPATCH_TRACE
    dispatch_trace_install();
#endif
#ifdef GB_ENABLE_DISPATCH_STACK
    dispatch_stack_install();
#endif
#ifdef GB_ENABLE_URL_WRITER
    writer_trace_prepare();
#endif
#ifdef GB_ENABLE_REGION_DIRECTORY
    region_directory_install();
#endif
#ifdef GB_ENABLE_BRIDGE
    bridge_install();
#endif
}

#ifdef GB_ENABLE_BRIDGE
#include "GBLoginBridgeRuntime.c"
#endif
