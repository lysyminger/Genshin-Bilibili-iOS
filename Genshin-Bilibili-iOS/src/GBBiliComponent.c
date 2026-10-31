/* Real SDK adapter experiment. No game login response or combo token is fabricated. */
static Obj bili_sdk, bili_delegate, bili_delegate_class;
static long bili_generation_offset;
static int bili_finishing;
static void *(*bili_dlopen)(const char *, int);
static Obj (*bili_allocate_class)(Obj, const char *, Size);
static unsigned char (*bili_add_ivar)(Obj, const char *, Size, unsigned char, const char *);
static unsigned char (*bili_add_method)(Obj, Sel, Imp, const char *);
static void (*bili_register_class)(Obj);
static void *(*bili_find_ivar)(Obj, const char *);
static long (*bili_ivar_offset)(void *);
static unsigned long long (*bili_dispatch_time)(unsigned long long, long long);
static void (*bili_dispatch_after)(unsigned long long, void *, void *, void (*)(void *));
static void bili_timeout(void *generation);
#ifdef GB_ENABLE_COMBO_LOGIN
static void combo_begin(Obj, Obj, Obj);
#endif

static Obj bili_path(const char *relative) {
    Obj bundle = send0(get_class("NSBundle"), "mainBundle");
    return send1(send0(bundle, "bundlePath"), "stringByAppendingPathComponent:", string(relative));
}
static int bili_available(void) {
    if (!bili_dlopen || !bili_allocate_class || !bili_add_ivar || !bili_add_method ||
        !bili_register_class || !bili_find_ivar || !bili_ivar_offset ||
        !bili_dispatch_time || !bili_dispatch_after) return 0;
    Obj files = send0(get_class("NSFileManager"), "defaultManager");
    return ((unsigned char (*)(Obj, Sel, Obj))message)(files, selector("fileExistsAtPath:"),
        bili_path("Frameworks/GBBiliSDK.framework/GBBiliSDK"));
}
static int bili_current(Obj delegate) {
    return delegate == bili_delegate && !bili_finishing && bili_generation_offset > 0 &&
        *(unsigned long *)((char *)delegate + bili_generation_offset) == bridge.generation &&
        (bridge.phase == GB_SDK_LOGIN || bridge.phase == GB_VERIFYING);
}
static void bili_finish_on_main(void *result) {
    __UINTPTR_TYPE__ kind = (__UINTPTR_TYPE__)result;
    if (bili_sdk && responds(bili_sdk, "logout")) send0(bili_sdk, "logout");
    gb_cancel(&bridge, bridge.generation);
    unsigned long generation;
    if (gb_begin(&bridge, &generation) != GB_OK) return;
    Obj app = send0(get_class("UIApplication"), "sharedApplication");
    Obj controller = send0(send0(app, "keyWindow"), "rootViewController");
    for (int i = 0; controller && i < 16; ++i) {
        Obj next = send0(controller, "presentedViewController");
        if (!next) break;
        controller = next;
    }
    if (!controller) { bridge_resume_official(0, 0); return; }
    const char *text = kind == 1 ?
        "B站SDK已返回有效登录凭证。本次仅验证组件，尚未连接原神认证，不会进入世界树。" :
        "B站登录测试已结束或组件未能初始化。具体状态已记录，可返回官服登录。";
#ifdef GB_ENABLE_COMBO_LOGIN
    text = "B服登录未完成，原因和状态码已记录。可以返回官服登录。";
#endif
    const char *return_title = "返回官服登录";
#ifdef GB_BILI_REGION_ONLY
    text = "B服登录未完成，状态已记录。本包仅配置世界树，可返回登录界面后重试。";
    return_title = "返回登录界面";
#endif
    Obj alert = ((Obj (*)(Obj, Sel, Obj, Obj, long))message)(get_class("UIAlertController"),
        selector("alertControllerWithTitle:message:preferredStyle:"), string("登录组件测试结果"), string(text), 1);
    Obj action = ((Obj (*)(Obj, Sel, Obj, long, void *))message)(get_class("UIAlertAction"),
        selector("actionWithTitle:style:handler:"), string(return_title), 0, &official_action_block);
    send1(alert, "addAction:", action);
    ((void (*)(Obj, Sel, Obj, unsigned char, void *))message)(controller,
        selector("presentViewController:animated:completion:"), alert, 1, (void *)0);
}
static void bili_finish(int success) {
    if (bili_finishing) return;
    bili_finishing = 1;
    bridge_dispatch(bridge_main_queue, (void *)(__UINTPTR_TYPE__)success, bili_finish_on_main);
}
static void bili_success(Obj self, Sel cmd, Obj uid, Obj access_key) {
    (void)cmd;
    if (!bili_current(self)) return;
    int strings = ((unsigned char (*)(Obj, Sel, Obj))message)(uid, selector("isKindOfClass:"), get_class("NSString")) &&
                  ((unsigned char (*)(Obj, Sel, Obj))message)(access_key, selector("isKindOfClass:"), get_class("NSString"));
    if (!strings || gb_credentials(&bridge, bridge.generation, utf8(uid), utf8(access_key)) != GB_OK) {
        record("bili SDK callback rejected: credential shape invalid");
        bili_finish(0);
        return;
    }
#ifdef GB_ENABLE_COMBO_LOGIN
    record("bili SDK credential callback valid; starting game-server verification");
    combo_begin(self, uid, access_key);
#else
    record("bili SDK login callback: credential shape valid; combo verification not connected");
    /* Do not store credentials or report game-server authentication success. */
    bili_finish(1);
#endif
}
static void bili_failure(Obj self, Sel cmd, Obj error) {
    (void)cmd;
    if (!bili_current(self)) return;
    record("bili SDK login failed or cancelled");
    if (responds(error, "code")) {
        char line[80];
        Size n = append(line, 0, sizeof(line), "bili SDK error code=");
        number(line, n, sizeof(line), ((long (*)(Obj, Sel))message)(error, selector("code")));
        record(line);
    }
    bili_finish(0);
}
static void bili_exit(Obj self, Sel cmd) {
    (void)cmd;
    if (!bili_current(self)) return;
    record("bili SDK closed or logged out");
    bili_finish(0);
}
static int bili_prepare_delegate(void) {
    if (!bili_delegate_class) {
        bili_delegate_class = bili_allocate_class(get_class("NSObject"), "GBGenshinBiliDelegate", 0);
        if (!bili_delegate_class) return 0;
        if (!bili_add_ivar(bili_delegate_class, "generation", sizeof(unsigned long), 3, "Q") ||
            !bili_add_method(bili_delegate_class, selector("didLoginSuccessWithUid:accessKey:"), (Imp)bili_success, "v32@0:8@16@24") ||
            !bili_add_method(bili_delegate_class, selector("didLoginFailureWithError:"), (Imp)bili_failure, "v24@0:8@16")) return 0;
        for (int i = 0; i < 3; ++i) {
            const char *names[] = {"didLogout", "didGameExit", "didCloseAgreement"};
            if (!bili_add_method(bili_delegate_class, selector(names[i]), (Imp)bili_exit, "v16@0:8")) return 0;
        }
        bili_register_class(bili_delegate_class);
        void *ivar = bili_find_ivar(bili_delegate_class, "generation");
        if (!ivar) return 0;
        bili_generation_offset = bili_ivar_offset(ivar);
    }
    if (bili_generation_offset <= 0) return 0;
    Obj delegate = send0(bili_delegate_class, "new");
    if (!delegate) return 0;
    *(unsigned long *)((char *)delegate + bili_generation_offset) = bridge.generation;
    if (bili_delegate) bridge_release(bili_delegate);
    bili_delegate = delegate;
    return 1;
}
static void bili_start_on_main(void *context) {
    (void)context;
    record("bili component experiment: loading isolated candidate");
    void *handle = bili_dlopen(utf8(bili_path("Frameworks/GBBiliSDK.framework/GBBiliSDK")), 6);
    if (!handle) { record("bili SDK candidate dlopen failed"); bili_finish(0); return; }
    Obj klass = get_class("GSSDK");
    if (!responds(klass, "sharedGSInterface") || !bili_prepare_delegate()) {
        record("bili SDK entrypoint or delegate unavailable"); bili_finish(0); return;
    }
    bili_sdk = send0(klass, "sharedGSInterface");
    if (!responds(bili_sdk, "initWithGameId:merchantId:serverId:appKey:sandboxKey:delegate:") ||
        !responds(bili_sdk, "showLogin")) {
        record("bili SDK initialization selector unavailable"); bili_finish(0); return;
    }
    Obj config = send1(get_class("NSDictionary"), "dictionaryWithContentsOfFile:", bili_path("GBBiliExperiment.plist"));
    const char *keys[] = {"gameId", "merchantId", "serverId", "appKey", "sandboxKey"};
    Obj values[5];
    for (int i = 0; i < 5; ++i) {
        values[i] = send1(config, "objectForKey:", string(keys[i]));
        if (!((unsigned char (*)(Obj, Sel, Obj))message)(values[i], selector("isKindOfClass:"), get_class("NSString")) ||
            (i < 4 && !((Size (*)(Obj, Sel))message)(values[i], selector("length")))) {
            record("bili SDK experiment configuration missing"); bili_finish(0); return;
        }
    }
    record("bili SDK parameters source=Android APK; iOS acceptance unverified; platform unchanged");
    unsigned char accepted = ((unsigned char (*)(Obj, Sel, Obj, Obj, Obj, Obj, Obj, Obj))message)(bili_sdk,
        selector("initWithGameId:merchantId:serverId:appKey:sandboxKey:delegate:"),
        values[0], values[1], values[2], values[3], values[4], bili_delegate);
    if (!accepted) { record("bili SDK initialization returned false"); bili_finish(0); return; }
    if (bili_finishing) return;
    record("bili SDK initialization returned true; opening login UI");
    bili_dispatch_after(bili_dispatch_time(0, 180000000000LL), bridge_main_queue,
                        (void *)(__UINTPTR_TYPE__)bridge.generation, bili_timeout);
    send0(bili_sdk, "showLogin");
}

#ifdef GB_ENABLE_COMBO_LOGIN
#include "GBComboLogin.c"
#endif
static void bili_begin(void *block, Obj action) {
    (void)block;
    (void)action;
    if (gb_select(&bridge, GB_BILIBILI, bili_available()) != GB_OK) return;
    bili_finishing = 0;
    bridge_dispatch(bridge_main_queue, (void *)0, bili_start_on_main);
}
static void bili_timeout(void *generation) {
    if ((__UINTPTR_TYPE__)generation != bridge.generation || !bili_current(bili_delegate)) return;
    record("bili SDK login experiment timed out");
    bili_finish(0);
}
static void bili_install(void) {
    void *all = (void *)-2;
    bili_dlopen = resolve(all, "dlopen");
    bili_allocate_class = resolve(all, "objc_allocateClassPair");
    bili_add_ivar = resolve(all, "class_addIvar");
    bili_add_method = resolve(all, "class_addMethod");
    bili_register_class = resolve(all, "objc_registerClassPair");
    bili_find_ivar = resolve(all, "class_getInstanceVariable");
    bili_ivar_offset = resolve(all, "ivar_getOffset");
    bili_dispatch_time = resolve(all, "dispatch_time");
    bili_dispatch_after = resolve(all, "dispatch_after_f");
    record("bili component adapter installed; not loaded until explicitly selected");
}
