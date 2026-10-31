/* Client-directory experiments; server authentication/signatures remain original. */
#include "GBRegionDirectoryCore.c"
static Obj (*region_get_association)(Obj, const void *);
static void (*region_set_association)(Obj, const void *, Obj, Size);
static void (*region_next_data)(Obj, Sel, Obj, Obj);
static void (*region_next_finish)(Obj, Sel, Obj);
static void (*region_next_fail)(Obj, Sel, Obj, Obj);
static char region_buffer_key;
static Obj region_bypass;

static int region_directory_request(Obj connection) {
    if (!responds(connection, "currentRequest")) return 0;
    Obj url = send0(send0(connection, "currentRequest"), "URL");
    return equal(utf8(send0(url, "scheme")), "https") &&
        equal(utf8(send0(url, "host")), "dispatchcnglobal.yuanshen.com") &&
        equal(utf8(send0(url, "path")), "/query_region_list");
}
static Obj region_directory_wire(Obj wire) {
    Obj decoded = ((Obj (*)(Obj, Sel, Obj, Size))message)(send0(get_class("NSData"), "alloc"),
        selector("initWithBase64EncodedData:options:"), wire, 0);
    if (!decoded) return wire;
    decoded = send0(decoded, "autorelease");
    Obj edited = send0(send0(decoded, "mutableCopy"), "autorelease");
    Size size = ((Size (*)(Obj, Sel))message)(edited, selector("length"));
#ifdef GB_DUAL_LOGIN
    if (size > 65536) return wire;
    const unsigned char *original = ((const unsigned char *(*)(Obj, Sel))message)(decoded, selector("bytes"));
    Size capacity = size + 2 + sizeof(region_qd_record);
    ((void (*)(Obj, Sel, Size))message)(edited, selector("setLength:"), capacity);
    unsigned char *bytes = ((unsigned char *(*)(Obj, Sel))message)(edited, selector("mutableBytes"));
    if (!region_append_qd_record(original, size, bytes, capacity)) {
#else
    unsigned char *bytes = ((unsigned char *(*)(Obj, Sel))message)(edited, selector("mutableBytes"));
    if (!bytes || !region_replace_record(bytes, size)) {
#endif
        record("directory profile not recognized; original directory retained");
        return wire;
    }
    Obj encoded = ((Obj (*)(Obj, Sel, Size))message)(edited, selector("base64EncodedStringWithOptions:"), 0);
    Obj result = ((Obj (*)(Obj, Sel, Size))message)(encoded, selector("dataUsingEncoding:"), 4);
    Size old_size = ((Size (*)(Obj, Sel))message)(wire, selector("length"));
#ifdef GB_DUAL_LOGIN
    old_size = ((capacity + 2) / 3) * 4;
#endif
    if (!result || ((Size (*)(Obj, Sel))message)(result, selector("length")) != old_size) {
        record("directory encoding size changed; original directory retained");
        return wire;
    }
#ifdef GB_DUAL_LOGIN
    record("directory official entry retained and World Tree appended; native iOS configuration preserved");
#else
    record("directory client profile set to official World Tree entry; iOS configuration preserved");
#endif
    return result;
}
static void region_data(Obj self, Sel cmd, Obj connection, Obj data) {
    Obj buffer = region_get_association(self, &region_buffer_key);
    if (buffer == region_bypass || (!buffer && !region_directory_request(connection))) {
        region_next_data(self, cmd, connection, data); return;
    }
    if (!buffer) {
        buffer = send0(get_class("NSMutableData"), "data");
        if (!buffer) {
            region_set_association(self, &region_buffer_key, region_bypass, 1);
            region_next_data(self, cmd, connection, data); return;
        }
        region_set_association(self, &region_buffer_key, buffer, 1);
    }
    Size buffered = ((Size (*)(Obj, Sel))message)(buffer, selector("length"));
    Size incoming = ((Size (*)(Obj, Sel))message)(data, selector("length"));
    if (incoming > 65536 || buffered > 65536 - incoming) {
        if (buffered) region_next_data(self, cmd, connection, buffer);
        region_next_data(self, cmd, connection, data);
        region_set_association(self, &region_buffer_key, region_bypass, 1);
        record("directory response exceeds experiment limit; original stream retained");
        return;
    }
    ((void (*)(Obj, Sel, Obj))message)(buffer, selector("appendData:"), data);
}
static void region_finish(Obj self, Sel cmd, Obj connection) {
    Obj buffer = region_get_association(self, &region_buffer_key);
    if (buffer && buffer != region_bypass && ((Size (*)(Obj, Sel))message)(buffer, selector("length")))
        region_next_data(self, selector("connection:didReceiveData:"), connection, region_directory_wire(buffer));
    region_set_association(self, &region_buffer_key, (Obj)0, 1);
    region_next_finish(self, cmd, connection);
}
static void region_fail(Obj self, Sel cmd, Obj connection, Obj error) {
    region_set_association(self, &region_buffer_key, (Obj)0, 1);
    region_next_fail(self, cmd, connection, error);
}
static void region_select_saved(void) {
    Obj defaults = send0(get_class("NSUserDefaults"), "standardUserDefaults");
    Obj text = send1(defaults, "objectForKey:", string("GENERAL_DATA"));
    if (!text || !responds(text, "dataUsingEncoding:")) {
        record("directory saved selection absent; single World Tree entry will be the default");
        return;
    }
    Obj data = ((Obj (*)(Obj, Sel, Size))message)(text, selector("dataUsingEncoding:"), 4);
    Obj json = ((Obj (*)(Obj, Sel, Obj, Size, Obj *))message)(get_class("NSJSONSerialization"),
        selector("JSONObjectWithData:options:error:"), data, 1, (Obj *)0);
    if (!json || !responds(json, "setObject:forKey:")) { record("directory saved selection format unrecognized"); return; }
    ((void (*)(Obj, Sel, Obj, Obj))message)(json, selector("setObject:forKey:"), string("cn_qd01"), string("selectedServerName"));
    data = ((Obj (*)(Obj, Sel, Obj, Size, Obj *))message)(get_class("NSJSONSerialization"),
        selector("dataWithJSONObject:options:error:"), json, 0, (Obj *)0);
    if (!data) return;
    Obj updated = ((Obj (*)(Obj, Sel, Obj, Size))message)(send0(get_class("NSString"), "alloc"),
        selector("initWithData:encoding:"), data, 4);
    if (!updated) return;
    updated = send0(updated, "autorelease");
    ((void (*)(Obj, Sel, Obj, Obj))message)(defaults, selector("setObject:forKey:"), updated, string("GENERAL_DATA"));
    ((unsigned char (*)(Obj, Sel))message)(defaults, selector("synchronize"));
    record("directory saved selection set to cn_qd01; other preferences preserved");
}
static void region_directory_install(void) {
    void *all = (void *)-2;
    region_get_association = resolve(all, "objc_getAssociatedObject");
    region_set_association = resolve(all, "objc_setAssociatedObject");
    region_bypass = send0(get_class("NSNull"), "null");
    const char *selectors[] = {"connection:didReceiveData:", "connectionDidFinishLoading:", "connection:didFailWithError:"};
    const char *types[] = {"v32@0:8@16@24", "v24@0:8@16", "v32@0:8@16@24"};
    if (!region_get_association || !region_set_association || !region_bypass) return;
    for (int i = 0; i < 3; ++i) {
        void *method = get_method(get_class("UnityWWWConnectionDelegate"), selector(selectors[i]));
        if (!method || !equal(method_types(method), types[i])) {
            record("directory experiment unavailable; original network callbacks retained"); return;
        }
    }
    region_next_data = (void *)install("UnityWWWConnectionDelegate", selectors[0], types[0], (Imp)region_data);
    region_next_finish = (void *)install("UnityWWWConnectionDelegate", selectors[1], types[1], (Imp)region_finish);
    region_next_fail = (void *)install("UnityWWWConnectionDelegate", selectors[2], types[2], (Imp)region_fail);
#ifdef GB_DUAL_LOGIN
    record("dual directory installed; saved server selection left unchanged");
#else
    region_select_saved();
#endif
    record("World Tree client-directory experiment installed; native iOS request generation unchanged");
}
