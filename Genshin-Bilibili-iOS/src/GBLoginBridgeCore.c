#include "GBLoginBridgeCore.h"

int gb_begin(GBLoginBridge *bridge, unsigned long *generation) {
    if (bridge->phase != GB_IDLE) return GB_BUSY;
    bridge->phase = GB_CHOOSING;
    *generation = ++bridge->generation;
    return GB_OK;
}

int gb_select(GBLoginBridge *bridge, enum GBChannel channel, int provider_ready) {
    if (bridge->phase != GB_CHOOSING) return GB_INVALID;
    if (channel != GB_OFFICIAL && channel != GB_BILIBILI) return GB_INVALID;
    if (channel == GB_BILIBILI && !provider_ready) return GB_NOT_READY;
    bridge->channel = channel;
    bridge->phase = channel == GB_OFFICIAL ? GB_IDLE : GB_SDK_LOGIN;
    return GB_OK;
}

int gb_credentials(GBLoginBridge *bridge, unsigned long generation,
                   const char *uid, const char *access_key) {
    if (generation != bridge->generation) return GB_STALE;
    if (bridge->phase != GB_SDK_LOGIN || !uid || !*uid || !access_key || !*access_key)
        return GB_INVALID;
    /* Bili UID is numeric. Credential contents remain with the SDK adapter. */
    for (const char *p = uid; *p; ++p)
        if (*p < '0' || *p > '9') return GB_INVALID;
    bridge->phase = GB_VERIFYING;
    return GB_OK;
}

int gb_verified(GBLoginBridge *bridge, unsigned long generation, int server_code,
                const char *open_id, const char *combo_token) {
    if (generation != bridge->generation) return GB_STALE;
    if (bridge->phase != GB_VERIFYING) return GB_INVALID;
    if (server_code != 0) {
        bridge->phase = GB_IDLE;
        ++bridge->generation;
        return GB_REJECTED;
    }
    if (!open_id || !*open_id || !combo_token || !*combo_token) return GB_INVALID;
    bridge->phase = GB_AUTHENTICATED;
    return GB_OK;
}

int gb_cancel(GBLoginBridge *bridge, unsigned long generation) {
    if (generation != bridge->generation) return GB_STALE;
    if (bridge->phase != GB_CHOOSING && bridge->phase != GB_SDK_LOGIN &&
        bridge->phase != GB_VERIFYING) return GB_INVALID;
    bridge->phase = GB_IDLE;
    ++bridge->generation;
    return GB_OK;
}

void gb_logout(GBLoginBridge *bridge) {
    bridge->phase = GB_IDLE;
    bridge->channel = GB_OFFICIAL;
    ++bridge->generation;
}
