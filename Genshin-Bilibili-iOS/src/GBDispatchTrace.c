/* Trace dispatch metadata; optionally route verified Bili sessions to B dispatch. */
static void (*dispatch_next_resume)(Obj, Sel);
static void (*dispatch_next_unity_start)(Obj, Sel);
static Obj (*dispatch_next_unity_send)(Obj, Sel, Obj, Obj, Obj);
static int dispatch_resume_seen, dispatch_unity_start_seen, dispatch_unity_send_seen;
#ifdef GB_ENABLE_DISPATCH_ROUTE
static int combo_dispatch_route_active(void);
#endif

static int dispatch_parameter(const char *name) {
    return equal(name, "version") || equal(name, "platform") ||
        equal(name, "channel_id") || equal(name, "sub_channel_id") ||
        equal(name, "lang") || equal(name, "binary") || equal(name, "key_id") ||
        equal(name, "region") || equal(name, "account_type");
}
static int dispatch_host(const char *host) {
    return equal(host, "dispatchcnglobal.yuanshen.com") ||
        equal(host, "cngfdispatch.yuanshen.com") || equal(host, "cnqddispatch.yuanshen.com");
}
static Size dispatch_value(char *line, Size pos, Size capacity, const char *value) {
    /* Query values are untrusted: bound length and prevent forged log lines. */
    for (Size i = 0; value && value[i] && i < 120 && pos + 1 < capacity; ++i) {
        char c = value[i];
        line[pos++] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.') ? c : '?';
    }
    line[pos] = 0;
    return pos;
}
#ifdef GB_ENABLE_DISPATCH_PROFILE
#include "GBDispatchProfileCore.c"
static void dispatch_seed(Obj items, Size count) {
    for (Size i = 0; i < count; ++i) {
        Obj item = ((Obj (*)(Obj, Sel, Size))message)(items, selector("objectAtIndex:"), i);
        if (!responds(item, "name") || !responds(item, "value")) continue;
        if (!equal(utf8(send0(item, "name")), "dispatchSeed")) continue;
        char digits[9];
        char line[64];
        profile_hex(digits, profile_checksum(utf8(send0(item, "value"))));
        Size pos = append(line, 0, sizeof(line), "dispatch seed checksum=");
        append(line, pos, sizeof(line), digits);
        record(line);
        return;
    }
}
static Obj dispatch_profile_request(Obj request) {
    if (!request || !responds(request, "URL")) return request;
    Obj url = send0(request, "URL");
    if (!responds(url, "host") || !responds(url, "path") || !responds(url, "absoluteString") ||
        !equal(utf8(send0(url, "scheme")), "https")) return request;
    const char *host = utf8(send0(url, "host"));
    const char *path = utf8(send0(url, "path"));
    if (!dispatch_host(host)) return request;
    int list = equal(path, "/query_region_list");
    if (!list && !equal(path, "/query_cur_region")) return request;
#ifdef GB_PRESERVE_NATIVE_DIRECTORY
    if (list) {
        record("dispatch native iOS directory request retained; pairing applies only to current region");
        return request;
    }
#endif
    /* The region list precedes login; the current-region request only follows verified Bili login. */
    if (!list && !combo_dispatch_route_active()) return request;
#ifdef GB_ENABLE_CATALOG_PROFILE
    if (!list && !equal(host, "cnqddispatch.yuanshen.com")) return request;
#endif
    const char *absolute = utf8(send0(url, "absoluteString"));
    if (!absolute) return request;
    const char *query = 0;
    for (const char *cursor = absolute; *cursor; ++cursor) if (*cursor == '?') { query = cursor + 1; break; }
    if (!query || !*query) return request;
    char rewritten[2048];
    if (!profile_rewrite(query, list, rewritten, sizeof(rewritten))) return request;
    Obj parts = send1(get_class("NSURLComponents"), "componentsWithString:", string(absolute));
    if (!parts || !responds(parts, "setPercentEncodedQuery:") || !responds(parts, "percentEncodedQuery"))
        return request;
    ((void (*)(Obj, Sel, Obj))message)(parts, selector("setPercentEncodedQuery:"), string(rewritten));
    Obj destination = send0(parts, "URL");
    const char *applied = utf8(send0(parts, "percentEncodedQuery"));
    if (!destination || !applied || !equal(applied, rewritten)) {
        record("dispatch profile rewrite unavailable; original request retained");
        return request;
    }
    Obj edited = send0(request, "mutableCopy");
    if (!edited) return request;
    ((void (*)(Obj, Sel, Obj))message)(edited, selector("setURL:"), destination);
#ifdef GB_ENABLE_CATALOG_PROFILE
    record(list ? "dispatch region list carries the verified B version profile"
                : "dispatch verified version/seed pairing applied; iOS platform and other fields preserved");
#else
    record(list ? "dispatch region list request carries the B release profile; other bytes preserved"
                : "dispatch region request carries the B release profile; other bytes preserved");
#endif
    return send0(edited, "autorelease");
}
#endif
static void dispatch_request(Obj request) {
    if (!responds(request, "URL")) return;
    Obj url = send0(request, "URL");
    if (!responds(url, "host") || !responds(url, "path")) return;
    const char *host = utf8(send0(url, "host"));
    const char *path = utf8(send0(url, "path"));
    if (!dispatch_host(host) || (!equal(path, "/query_region_list") && !equal(path, "/query_cur_region"))) return;
    char line[480];
    Size pos = append(line, 0, sizeof(line), "dispatch request host=");
    pos = append(line, pos, sizeof(line), host);
    pos = append(line, pos, sizeof(line), " path=");
    pos = append(line, pos, sizeof(line), path);
    Obj components = send1(get_class("NSURLComponents"), "componentsWithString:", send0(url, "absoluteString"));
    Obj items = send0(components, "queryItems");
    Size count = ((Size (*)(Obj, Sel))message)(items, selector("count"));
    if (count > 128) count = 128;
    for (Size i = 0; i < count; ++i) {
        Obj item = ((Obj (*)(Obj, Sel, Size))message)(items, selector("objectAtIndex:"), i);
        const char *name = utf8(send0(item, "name"));
        if (!dispatch_parameter(name)) continue;
        pos = append(line, pos, sizeof(line), " ");
        pos = append(line, pos, sizeof(line), name);
        pos = append(line, pos, sizeof(line), "=");
        pos = dispatch_value(line, pos, sizeof(line), utf8(send0(item, "value")));
    }
    record(line);
#ifdef GB_ENABLE_DISPATCH_PROFILE
    if (equal(path, "/query_cur_region")) dispatch_seed(items, count);
#endif
}
static void dispatch_resume(Obj self, Sel cmd) {
    if (!__atomic_exchange_n(&dispatch_resume_seen, 1, __ATOMIC_RELAXED))
        record("dispatch NSURLSession resume entry observed");
    Obj request = responds(self, "currentRequest") ? send0(self, "currentRequest") : (Obj)0;
    if (!request && responds(self, "originalRequest")) request = send0(self, "originalRequest");
    dispatch_request(request);
    dispatch_next_resume(self, cmd);
}
static void dispatch_unity_start(Obj self, Sel cmd) {
    if (!__atomic_exchange_n(&dispatch_unity_start_seen, 1, __ATOMIC_RELAXED))
        record("dispatch UnityWWW startConnection entry observed");
    if (responds(self, "request")) dispatch_request(send0(self, "request"));
    dispatch_next_unity_start(self, cmd);
}
#ifdef GB_ENABLE_DISPATCH_ROUTE
static Obj dispatch_bili_request(Obj request) {
    if (!request || !combo_dispatch_route_active()) return request;
    Obj url = send0(request, "URL");
    if (!equal(utf8(send0(url, "scheme")), "https") ||
        !equal(utf8(send0(url, "host")), "cngfdispatch.yuanshen.com") ||
        !equal(utf8(send0(url, "path")), "/query_cur_region")) return request;
    Obj parts = send1(get_class("NSURLComponents"), "componentsWithString:", send0(url, "absoluteString"));
    Obj items = send0(parts, "queryItems");
    Size count = ((Size (*)(Obj, Sel))message)(items, selector("count"));
    if (count > 128) return request;
    int channels = 0, accounts = 0;
    for (Size i = 0; i < count; ++i) {
        Obj item = ((Obj (*)(Obj, Sel, Size))message)(items, selector("objectAtIndex:"), i);
        const char *name = utf8(send0(item, "name"));
        if (!equal(name, "channel_id") && !equal(name, "account_type")) continue;
        if (!equal(utf8(send0(item, "value")), "14")) return request;
        if (equal(name, "channel_id")) ++channels; else ++accounts;
    }
    if (channels != 1 || accounts != 1) return request;
    Obj query = send0(parts, "percentEncodedQuery");
    ((void (*)(Obj, Sel, Obj))message)(parts, selector("setHost:"), string("cnqddispatch.yuanshen.com"));
    Obj destination = send0(parts, "URL");
    if (!destination || !equal(utf8(send0(parts, "percentEncodedQuery")), utf8(query)) ||
        !equal(utf8(send0(destination, "host")), "cnqddispatch.yuanshen.com")) {
        record("dispatch Bili route unavailable; original request retained");
        return request;
    }
    Obj routed = send0(request, "mutableCopy");
    if (!routed) return request;
    ((void (*)(Obj, Sel, Obj))message)(routed, selector("setURL:"), destination);
    record("dispatch Bili route cngfdispatch -> cnqddispatch; encoded query preserved");
    return send0(routed, "autorelease");
}
#endif
static Obj dispatch_unity_send(Obj self, Sel cmd, Obj connection, Obj request, Obj response) {
    if (!__atomic_exchange_n(&dispatch_unity_send_seen, 1, __ATOMIC_RELAXED))
        record("dispatch UnityWWW willSendRequest entry observed");
    Obj result = dispatch_next_unity_send(self, cmd, connection, request, response);
#ifdef GB_ENABLE_DISPATCH_ROUTE
    /* Route the initial request once; preserve subsequent server redirects. */
    if (!response) result = dispatch_bili_request(result);
#endif
#ifdef GB_ENABLE_DISPATCH_PROFILE
    if (!response) result = dispatch_profile_request(result);
#endif
    dispatch_request(result);
    return result;
}
static void dispatch_trace_install(void) {
    /* Concrete Foundation tasks override the abstract NSURLSessionTask method. */
    const char *classes[] = {"__NSCFLocalSessionTask", "NSURLSessionTask"};
    for (Size i = 0; i < sizeof(classes) / sizeof(classes[0]); ++i) {
        void *entry = get_method(get_class(classes[i]), selector("resume"));
        if (!entry || !equal(method_types(entry), "v16@0:8")) continue;
        dispatch_next_resume = (void *)replace_method(entry, (Imp)dispatch_resume);
        char line[160];
        Size pos = append(line, 0, sizeof(line), "dispatch read-only trace installed: ");
        append(line, pos, sizeof(line), classes[i]);
        record(line);
        break;
    }
    if (!dispatch_next_resume) record("dispatch NSURLSession trace unavailable");
    dispatch_next_unity_start = (void *)install("UnityWWWConnectionDelegate", "startConnection",
        "v16@0:8", (Imp)dispatch_unity_start);
    dispatch_next_unity_send = (void *)install("UnityWWWConnectionDelegate",
        "connection:willSendRequest:redirectResponse:", "@40@0:8@16@24@32", (Imp)dispatch_unity_send);
}
