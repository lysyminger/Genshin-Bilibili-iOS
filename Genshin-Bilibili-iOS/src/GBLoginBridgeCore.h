#ifndef GB_LOGIN_BRIDGE_CORE_H
#define GB_LOGIN_BRIDGE_CORE_H

enum GBChannel { GB_OFFICIAL = 1, GB_BILIBILI = 14 };
enum GBPhase { GB_IDLE, GB_CHOOSING, GB_SDK_LOGIN, GB_VERIFYING, GB_AUTHENTICATED };
enum GBResult { GB_OK, GB_BUSY, GB_NOT_READY, GB_STALE, GB_INVALID, GB_REJECTED };

typedef struct {
    enum GBPhase phase;
    enum GBChannel channel;
    unsigned long generation;
} GBLoginBridge;

int gb_begin(GBLoginBridge *bridge, unsigned long *generation);
int gb_select(GBLoginBridge *bridge, enum GBChannel channel, int provider_ready);
int gb_credentials(GBLoginBridge *bridge, unsigned long generation,
                   const char *uid, const char *access_key);
int gb_verified(GBLoginBridge *bridge, unsigned long generation, int server_code,
                const char *open_id, const char *combo_token);
int gb_cancel(GBLoginBridge *bridge, unsigned long generation);
void gb_logout(GBLoginBridge *bridge);

#endif
