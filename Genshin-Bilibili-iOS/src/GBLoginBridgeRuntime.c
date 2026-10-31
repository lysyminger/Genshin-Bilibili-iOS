/* Included by GBDiagnostic.c. All SDK calls remain on the original path. */
#include "GBLoginBridgeCore.c"

static GBLoginBridge bridge = {GB_IDLE, GB_OFFICIAL, 0};
static Obj pending_self, pending_function, pending_args;
static Sel pending_selector;
static int pending_index;
static void (*bridge_next_invoke)(Obj, Sel, Obj, Obj, int);
static Obj (*bridge_retain)(Obj);
static void (*bridge_release)(Obj);
static void (*bridge_dispatch)(void *, void *, void (*)(void *));
static void *bridge_main_queue;
#ifdef GB_ENABLE_COMBO_LOGIN
static void combo_install(void);
static void combo_restore_official(void);
static int combo_handle_invoke(Obj, Sel, Obj, Obj, int);
static void combo_after_invoke(Obj);
static void combo_busy(Obj, int);
#ifdef GB_BILI_REGION_ONLY
static void combo_callback(Obj, int, int, Obj);
#endif
#endif

struct GBActionBlock {
    void *isa;
    int flags, reserved;
    void (*invoke)(void *, Obj);
    const void *descriptor;
};
static const struct {
    unsigned long reserved, size;
    const char *signature;
} action_descriptor = {0, sizeof(struct GBActionBlock), "v16@?0@8"};
static struct GBActionBlock official_action_block;
#ifdef GB_ENABLE_BILI_COMPONENT
static struct GBActionBlock bili_action_block;
static int bili_available(void);
static void bili_begin(void *, Obj);
static void bili_install(void);
#endif

static void bridge_resume_official(void *block, Obj action) {
    (void)block;
    (void)action;
#ifdef GB_BILI_REGION_ONLY
    if (gb_cancel(&bridge, bridge.generation) != GB_OK) return;
    Obj self = pending_self, function = pending_function, args = pending_args;
    int index = pending_index;
    pending_self = pending_function = pending_args = (Obj)0;
    if (function) combo_callback(function, index, -1, (Obj)0);
    bridge_release(self); bridge_release(function); bridge_release(args);
    record("B-only directory experiment cancelled; no official login forwarded");
#else
    if (gb_select(&bridge, GB_OFFICIAL, 0) != GB_OK) return;
#ifdef GB_ENABLE_COMBO_LOGIN
    combo_restore_official();
#endif
    Obj self = pending_self, function = pending_function, args = pending_args;
    Sel cmd = pending_selector;
    int index = pending_index;
    pending_self = pending_function = pending_args = (Obj)0;
    record("bridge selected official; forwarding original login");
    bridge_next_invoke(self, cmd, function, args, index);
    bridge_release(self);
    bridge_release(function);
    bridge_release(args);
#endif
}

static void bridge_present(void *context) {
    (void)context;
    Obj application = send0(get_class("UIApplication"), "sharedApplication");
    Obj window = send0(application, "keyWindow");
    Obj controller = send0(window, "rootViewController");
    /* Follow presented controllers without dismissing the user's existing UI. */
    for (int i = 0; controller && i < 16; ++i) {
        Obj next = send0(controller, "presentedViewController");
        if (!next) break;
        controller = next;
    }
    Obj alert_class = get_class("UIAlertController");
    Obj action_class = get_class("UIAlertAction");
    if (!controller || !responds(alert_class, "alertControllerWithTitle:message:preferredStyle:") ||
        !responds(action_class, "actionWithTitle:style:handler:")) {
#ifdef GB_BILI_REGION_ONLY
        record("B-only directory menu unavailable; login request cancelled");
#else
        record("bridge menu unavailable; forwarding official login");
#endif
        bridge_resume_official(0, 0);
        return;
    }
    const char *explanation = "官服可以继续登录。B服登录组件尚未接入。";
    const char *bili_title = "B服登录（暂不可用）";
    void *bili_handler = 0;
    int bili_enabled = 0;
#ifdef GB_ENABLE_BILI_COMPONENT
    bili_enabled = bili_available();
    if (bili_enabled) {
#ifdef GB_ENABLE_COMBO_LOGIN
        explanation = "选择登录渠道。B服使用B站账号，游戏认证和区服连接仍需实测。本版不支持B服充值。";
        bili_title = "B服登录";
#else
        explanation = "B站入口仅测试登录组件，尚不能进入世界树。使用安卓包参数，iOS兼容性未确认。";
        bili_title = "B站登录测试（实验）";
#endif
        bili_handler = &bili_action_block;
    }
#endif
    const char *title = "选择登录渠道", *official_title = "官服登录";
    long official_style = 0;
#ifdef GB_DUAL_LOGIN
    title = "官服 / B服";
    explanation = "官服登录后请选择天空岛，B服登录后请选择世界树。B服开门异常仍在排查，本版不支持B服充值。";
#endif
#ifdef GB_COMBO_VERIFY_ONLY
    title = "认证结构对照";
    explanation = "本次先验证B服账号，再自动返回官服继续对照测试，无法进入世界树。不支持充值。";
    bili_title = "B服认证取样后回官服";
#endif
#ifdef GB_BILI_REGION_ONLY
    title = "世界树目录实验";
    explanation = "本实验包只配置世界树。请选择B服登录；区服连接仍需验证。本版不支持充值。";
    official_title = "取消";
    official_style = 1;
#endif
    Obj alert = ((Obj (*)(Obj, Sel, Obj, Obj, long))message)(alert_class,
        selector("alertControllerWithTitle:message:preferredStyle:"), string(title),
        string(explanation), 1);
    Obj official = ((Obj (*)(Obj, Sel, Obj, long, void *))message)(action_class,
        selector("actionWithTitle:style:handler:"), string(official_title), official_style, &official_action_block);
    Obj bili = ((Obj (*)(Obj, Sel, Obj, long, void *))message)(action_class,
        selector("actionWithTitle:style:handler:"), string(bili_title), 0, bili_handler);
    if (!alert || !official || !bili) {
        bridge_resume_official(0, 0);
        return;
    }
    ((void (*)(Obj, Sel, unsigned char))message)(bili, selector("setEnabled:"), bili_enabled);
    send1(alert, "addAction:", official);
    send1(alert, "addAction:", bili);
    ((void (*)(Obj, Sel, Obj, unsigned char, void *))message)(controller,
        selector("presentViewController:animated:completion:"), alert, 1, (void *)0);
    record(bili_enabled ? "bridge menu presented; Bili component test available" :
                          "bridge menu presented; Bili provider unavailable");
}

static void bridge_invoke(Obj self, Sel cmd, Obj function, Obj args, int index) {
#ifdef GB_ENABLE_COMBO_LOGIN
    if (combo_handle_invoke(self, cmd, function, args, index)) return;
#endif
    unsigned char is_string = ((unsigned char (*)(Obj, Sel, Obj))message)(function,
        selector("isKindOfClass:"), get_class("NSString"));
    if (!is_string || !equal(utf8(function), "login_login")) {
        bridge_next_invoke(self, cmd, function, args, index);
#ifdef GB_ENABLE_COMBO_LOGIN
        combo_after_invoke(function);
#endif
        return;
    }
    unsigned long generation;
    if (gb_begin(&bridge, &generation) != GB_OK) {
#ifdef GB_ENABLE_COMBO_LOGIN
        combo_busy(function, index);
#else
        /* Each invocation has its own callback index; never silently discard it. */
        record("bridge menu already open; forwarding additional official login");
        bridge_next_invoke(self, cmd, function, args, index);
#endif
        return;
    }
    pending_self = bridge_retain(self);
    pending_function = bridge_retain(function);
    pending_args = bridge_retain(args);
    pending_selector = cmd;
    pending_index = index;
    bridge_dispatch(bridge_main_queue, (void *)0, bridge_present);
}

static void bridge_install(void) {
    void *all = (void *)-2;
    bridge_retain = resolve(all, "objc_retain");
    bridge_release = resolve(all, "objc_release");
    bridge_dispatch = resolve(all, "dispatch_async_f");
    bridge_main_queue = resolve(all, "_dispatch_main_q");
    official_action_block.isa = resolve(all, "_NSConcreteGlobalBlock");
    if (!bridge_retain || !bridge_release || !bridge_dispatch || !bridge_main_queue ||
        !official_action_block.isa) {
        record("bridge unavailable; original SDK observers remain active");
        return;
    }
    official_action_block.flags = (1 << 28) | (1 << 30);
    official_action_block.invoke = bridge_resume_official;
    official_action_block.descriptor = &action_descriptor;
#ifdef GB_ENABLE_BILI_COMPONENT
    bili_action_block = official_action_block;
    bili_action_block.invoke = bili_begin;
    bili_install();
#endif
#ifdef GB_ENABLE_COMBO_LOGIN
    combo_install();
#endif
    bridge_next_invoke = (void *)install("MiHoYoComboManager", "invoke:args:index:",
        "v36@0:8@16@24i32", (Imp)bridge_invoke);
    record(bridge_next_invoke ? "bridge chooser installed; Bili provider not ready" :
                               "bridge chooser not installed");
}

#ifdef GB_ENABLE_BILI_COMPONENT
#include "GBBiliComponent.c"
#endif
