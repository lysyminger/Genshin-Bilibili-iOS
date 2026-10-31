/* Use the host's signer, network client and callback ABI; never fabricate auth. */
static int combo_channel_active, combo_ready;
static Obj combo_bili_uid;
static Obj (*combo_next_return)(Obj, Sel, Obj, Obj);
static void *combo_stack_block_class;
static void *(*combo_allocate)(Size);
static void (*combo_free)(void *);
static int combo_dispatch_route_active(void) {
    return combo_channel_active && bridge.channel == GB_BILIBILI && bridge.phase == GB_AUTHENTICATED;
}

static int combo_is(Obj value, const char *name) {
    return ((unsigned char (*)(Obj, Sel, Obj))message)(value, selector("isKindOfClass:"), get_class(name));
}
static int combo_text(Obj value) {
    return combo_is(value, "NSString") && ((Size (*)(Obj, Sel))message)(value, selector("length")) > 0;
}
static Obj combo_get(Obj dict, const char *key) { return send1(dict, "objectForKey:", string(key)); }
static void combo_set(Obj dict, const char *key, Obj value) {
    if (value) ((void (*)(Obj, Sel, Obj, Obj))message)(dict, selector("setObject:forKey:"), value, string(key));
}
static Obj combo_num(long long value) {
    return ((Obj (*)(Obj, Sel, long long))message)(get_class("NSNumber"), selector("numberWithLongLong:"), value);
}
static Obj combo_dict(void) { return send0(get_class("NSMutableDictionary"), "dictionary"); }
static Obj combo_config(void) { return send0(get_class("MHYConfig"), "instance"); }
static int combo_channel(int value) {
    Obj config = combo_config();
    if (!responds(config, "setChannelID:") || !responds(config, "channelID")) return 0;
    ((void (*)(Obj, Sel, int))message)(config, selector("setChannelID:"), value);
    if (((int (*)(Obj, Sel))message)(config, selector("channelID")) != value) return 0;
    Obj manager = get_class("MiHoYoNetworkManager");
    if (responds(manager, "setHttpAdditionalHeaderForKey:value:"))
        ((void (*)(Obj, Sel, Obj, Obj))message)(manager, selector("setHttpAdditionalHeaderForKey:value:"),
            string("x-rpc-channel_id"), string(value == 14 ? "14" : "1"));
    combo_channel_active = value == 14;
    return 1;
}
static void combo_restore_official(void) {
    if (!combo_channel_active) return;
    if (combo_bili_uid) bridge_release(combo_bili_uid);
    combo_bili_uid = (Obj)0;
    send1(combo_config(), "setCurrentComboDic:", (Obj)0);
    combo_channel(1);
    record("combo channel restored to official; iOS client type unchanged");
}
static Obj combo_return(Obj self, Sel cmd, Obj function, Obj args) {
    Obj original = combo_next_return(self, cmd, function, args);
    if (!combo_channel_active || !combo_is(function, "NSString")) return original;
    const char *name = utf8(function);
    if (equal(name, "info_get_channel_id")) return string("14");
    if (equal(name, "info_get_sub_channel_id")) return string("0");
    if (equal(name, "login_get_asterisk_name")) return string("B站账号");
    return original;
}
static Obj combo_response_wire(int status, Obj data) {
    Obj response = combo_dict();
    combo_set(response, "ret", combo_num(status));
    combo_set(response, "msg", string(status == 0 ? "OK" : "B服登录未完成"));
    if (data) combo_set(response, "data", data);
    Obj wire = send1(get_class("MiHoYoSDKTool"), "dictionaryToJsonData:", response);
    return combo_text(wire) ? wire : (Obj)0;
}
static void combo_deliver(Obj function, int index, Obj wire) {
    Obj callback = send0(get_class("MiHoYoComboCallback"), "instance");
    ((void (*)(Obj, Sel, Obj, Obj, int))message)(callback, selector("callBack:response:index:"),
                                               function, wire, index);
}
static void combo_callback(Obj function, int index, int status, Obj data) {
    Obj wire = combo_response_wire(status, data);
    if (!wire) { record("combo callback JSON serialization failed"); return; }
    combo_deliver(function, index, wire);
}
static void combo_busy(Obj function, int index) {
    record("combo overlapping login request rejected; active attempt retained");
    combo_callback(function, index, -103, (Obj)0);
}
static int combo_prefix(const char *text, const char *prefix) {
    while (*prefix) if (*text++ != *prefix++) return 0;
    return 1;
}
static int combo_handle_invoke(Obj self, Sel cmd, Obj function, Obj args, int index) {
    (void)self; (void)cmd; (void)args;
    if (!combo_channel_active || !combo_is(function, "NSString")) return 0;
    const char *name = utf8(function);
    if (combo_prefix(name, "pay_") || combo_prefix(name, "cashier_") || combo_prefix(name, "login_pay")) {
        record("combo Bili payment unavailable; no purchase initiated");
        combo_callback(function, index, -1, (Obj)0);
        return 1;
    }
    if (equal(name, "login_logout") || equal(name, "login_switch_role")) {
        bili_finishing = 1;
        if (bili_sdk && responds(bili_sdk, "logout")) send0(bili_sdk, "logout");
        combo_restore_official();
        gb_logout(&bridge);
        combo_callback(function, index, 0, combo_dict());
        record("combo Bili session cleared for logout or role switch");
        return 1;
    }
    if (equal(name, "login_login") && bridge.phase == GB_AUTHENTICATED) {
        /* A fresh game login must re-authenticate; don't replay a cached token. */
        combo_restore_official();
        gb_logout(&bridge);
    }
    return 0;
}
static void combo_after_invoke(Obj function) {
    if (combo_channel_active && combo_is(function, "NSString") &&
        (equal(utf8(function), "all_set_env") || equal(utf8(function), "all_set_env_and_gamebiz")))
        combo_channel(14);
}

struct GBComboBlock {
    void *isa;
    int flags, reserved;
    void (*invoke)(struct GBComboBlock *, Obj, Obj, Obj);
    const void *descriptor;
    unsigned long generation;
};
static const struct { unsigned long reserved, size; const char *signature; }
    combo_block_descriptor = {0, sizeof(struct GBComboBlock), "v32@?0@8@16@24"};
struct GBComboEvent { unsigned long generation; Obj response; };

static void combo_release_pending(void) {
    bridge_release(pending_self); bridge_release(pending_function); bridge_release(pending_args);
    pending_self = pending_function = pending_args = (Obj)0;
}
static void combo_result_on_main(void *context) {
    struct GBComboEvent event = *(struct GBComboEvent *)context;
    combo_free(context);
    Obj response = event.response;
    if (event.generation != bridge.generation || bridge.phase != GB_VERIFYING || bili_finishing) {
        bridge_release(response);
        record("combo stale response ignored");
        return;
    }
    int failure = 1;
    Obj outer_status = combo_is(response, "NSDictionary") ? combo_get(response, "ret") : 0;
    Obj server = outer_status && combo_is(outer_status, "NSNumber") &&
        ((int (*)(Obj, Sel))message)(outer_status, selector("intValue")) == 0 ? combo_get(response, "data") : 0;
    Obj server_status = combo_is(server, "NSDictionary") ? combo_get(server, "retcode") : 0;
    Obj verified = 0;
    if (combo_is(server_status, "NSNumber")) {
        int status = ((int (*)(Obj, Sel))message)(server_status, selector("intValue"));
        char line[80]; Size n = append(line, 0, sizeof(line), "combo server retcode=");
        number(line, n, sizeof(line), status); record(line);
        if (status == 0) verified = combo_get(server, "data");
        else gb_verified(&bridge, event.generation, status, "", "");
    } else record("combo network failure or malformed server response");
    Obj open_id = combo_is(verified, "NSDictionary") ? combo_get(verified, "open_id") : 0;
    Obj token = combo_is(verified, "NSDictionary") ? combo_get(verified, "combo_token") : 0;
    Obj account_type = combo_is(verified, "NSDictionary") ? combo_get(verified, "account_type") : 0;
    if (combo_text(open_id) && combo_text(token) && combo_is(account_type, "NSNumber")) {
#ifdef GB_ENABLE_LOGIN_SHAPE
        record_login_shape("bili_verified", verified);
#endif
        Obj data = send0(verified, "mutableCopy");
        Obj fatigue = combo_get(data, "fatigue_remind");
        send1(combo_config(), "setFatigueRemindConfig:", combo_is(fatigue, "NSDictionary") ? fatigue : (Obj)0);
        Obj extra = combo_get(data, "data");
        if (combo_text(extra)) {
            Obj parsed = send1(get_class("MiHoYoSDKTool"), "stringToDictionary:", extra);
            if (combo_is(parsed, "NSDictionary")) send1(data, "addEntriesFromDictionary:", parsed);
        }
        send1(data, "removeObjectForKey:", string("data"));
        send1(data, "removeObjectForKey:", string("fatigue_remind"));
        send1(data, "removeObjectForKey:", string("combo_id"));
        /* Preserve server identity even if extension data has similarly named fields. */
        combo_set(data, "open_id", open_id); combo_set(data, "combo_token", token);
        combo_set(data, "account_type", account_type);
        combo_set(data, "device_id", send0(get_class("MiHoYoSDKTool"), "getDeviceID"));
        combo_set(data, "channel_id", combo_num(14)); combo_set(data, "app_id", combo_num(4));
        /* Android's verified channel adapter uses the Bili UID as channelToken. */
        combo_set(data, "channel_token", combo_bili_uid ? combo_bili_uid : string(""));
        /* Match the normal SDK's string-valued UI fields without inventing identity. */
        const char *profile_keys[] = {"name", "email", "mobile", "realname", "identity_card",
                                      "safe_mobile", "asterisk_name", "open_token"};
        for (int i = 0; i < 8; ++i)
            if (!combo_get(data, profile_keys[i])) combo_set(data, profile_keys[i], string(""));
        if (!combo_get(data, "is_email_verify")) combo_set(data, "is_email_verify", string("0"));
        /* These defaults match the Android Bzhan adapter's non-guest response. */
        if (!combo_get(data, "guest")) combo_set(data, "guest",
            ((Obj (*)(Obj, Sel, unsigned char))message)(get_class("NSNumber"), selector("numberWithBool:"), 0));
        if (!combo_get(data, "is_new_register")) combo_set(data, "is_new_register",
            ((Obj (*)(Obj, Sel, unsigned char))message)(get_class("NSNumber"), selector("numberWithBool:"), 0));
#ifdef GB_ENABLE_LOGIN_SHAPE
        record_login_shape("bili_normalized", data);
#endif
#ifdef GB_COMBO_VERIFY_ONLY
        bridge_release(data);
        bridge_release(response);
        record("combo verify-only sample complete; Bili game identity not delivered; return to official login");
        bili_finish(1);
        return;
#endif
        Obj game_response = combo_response_wire(0, data);
        Obj envelope = combo_dict(); combo_set(envelope, "ret", combo_num(0)); combo_set(envelope, "data", data);
        Obj login = send0(get_class("MiHoYoComboLogin"), "instance");
        if (game_response && responds(login, "loginSuccess:response:") &&
            gb_verified(&bridge, event.generation, 0, utf8(open_id), utf8(token)) == GB_OK) {
            bili_finishing = 1;
            ((void (*)(Obj, Sel, Obj, Obj))message)(login, selector("loginSuccess:response:"), (Obj)0, envelope);
            if (bili_sdk && responds(bili_sdk, "heartbeatStart")) send0(bili_sdk, "heartbeatStart");
            combo_deliver(string("login_login"), pending_index, game_response);
            combo_release_pending();
            record("combo verified by server; game login result delivered on channel 14");
            failure = 0;
        }
        bridge_release(data);
    }
    bridge_release(response);
    if (failure) {
        record("combo authentication not completed; no game success delivered");
        bili_finish(0);
    }
}
static void combo_network_callback(struct GBComboBlock *block, Obj data, Obj http_response, Obj error) {
    Obj normalized = ((Obj (*)(Obj, Sel, Obj, Obj, Obj))message)(get_class("MiHoYoNetworkManager"),
        selector("handle:response:error:"), data, http_response, error);
    struct GBComboEvent *event = combo_allocate(sizeof(*event));
    if (!event) { record("combo response allocation failed"); bili_finish(0); return; }
    event->generation = block->generation;
    event->response = bridge_retain(normalized);
    bridge_dispatch(bridge_main_queue, event, combo_result_on_main);
}
static void combo_begin(Obj delegate, Obj uid, Obj access_key) {
    if (!combo_ready || !bili_current(delegate)) { record("combo host API unavailable"); bili_finish(0); return; }
    Obj config = combo_config();
    Obj app = send0(config, "comboAppid"), secret = send0(config, "comboAppkey");
    Obj url = send0(send0(get_class("MiHoYoURLConfig"), "instance"), "comboLogin");
    Obj device = send0(get_class("MiHoYoSDKTool"), "getDeviceID");
    if (!combo_text(url) || !equal(utf8(url), "https://hk4e-sdk.mihoyo.com/hk4e_cn/combo/granter/login/v2/login") ||
        !responds(app, "intValue") || ((int (*)(Obj, Sel))message)(app, selector("intValue")) != 4 ||
        !combo_text(secret) || !combo_text(device) || !combo_channel(14)) {
        record("combo endpoint, app configuration or channel mismatch"); bili_finish(0); return;
    }
    Obj credentials = combo_dict();
    long long numeric_uid = ((long long (*)(Obj, Sel))message)(uid, selector("longLongValue"));
    if (numeric_uid <= 0) { record("combo invalid Bili numeric UID"); bili_finish(0); return; }
    combo_set(credentials, "uid", combo_num(numeric_uid)); combo_set(credentials, "access_key", access_key);
    Obj serialized = send1(get_class("MiHoYoSDKTool"), "dictionaryToJsonData:", credentials);
    if (!combo_text(serialized)) { record("combo credential serialization failed"); bili_finish(0); return; }
    Obj body = combo_dict();
    combo_set(body, "app_id", combo_num(4)); combo_set(body, "channel_id", combo_num(14));
    combo_set(body, "device", device); combo_set(body, "data", serialized);
    Obj sign = ((Obj (*)(Obj, Sel, Obj, Obj))message)(get_class("MiHoYoHmacSHA256Encryptor"),
        selector("sign:secret:"), body, secret);
    if (!combo_text(sign)) { record("combo request signing failed"); bili_finish(0); return; }
    combo_set(body, "sign", sign);
    /* The native client's body argument is JSON text, not an NSDictionary. */
    Obj wire_body = send1(get_class("MiHoYoSDKTool"), "dictionaryToJsonData:", body);
    if (!combo_text(wire_body)) { record("combo outer request serialization failed"); bili_finish(0); return; }
    if (combo_bili_uid) bridge_release(combo_bili_uid);
    combo_bili_uid = bridge_retain(uid);
    struct GBComboBlock block = {combo_stack_block_class, 1 << 30, 0,
        combo_network_callback, &combo_block_descriptor, bridge.generation};
    Obj manager = send0(get_class("MiHoYoNetworkManager"), "instance");
    record("combo submitting signed credential exchange to official endpoint; iOS platform unchanged");
    ((void (*)(Obj, Sel, Obj, Obj, double, void *))message)(manager,
        selector("postRequest:body:timeout:callback:"), url, wire_body, 30.0, &block);
}
static int combo_method(const char *name, const char *sel, const char *type) {
    void *method = get_method(get_class(name), selector(sel));
    return method && equal(method_types(method), type);
}
static void combo_install(void) {
    void *all = (void *)-2;
    combo_stack_block_class = resolve(all, "_NSConcreteStackBlock");
    combo_allocate = resolve(all, "malloc"); combo_free = resolve(all, "free");
    combo_ready = combo_stack_block_class && combo_allocate && combo_free &&
        combo_method("MiHoYoNetworkManager", "postRequest:body:timeout:callback:", "v48@0:8@16@24d32@?40") &&
        combo_method("MiHoYoComboCallback", "callBack:response:index:", "v36@0:8@16@24i32");
    combo_next_return = (void *)install("MiHoYoComboManager", "invokeReturn:args:",
        "@32@0:8@16@24", (Imp)combo_return);
    if (!combo_next_return) combo_ready = 0;
    record(combo_ready ? "combo adapter installed; server acceptance unverified" : "combo adapter unavailable");
}
