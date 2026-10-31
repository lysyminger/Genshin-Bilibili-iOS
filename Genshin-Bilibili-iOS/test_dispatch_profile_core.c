#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
typedef unsigned long Size;
#include "src/GBDispatchProfileCore.c"

static void rewrite(const char *input, int list, const char *expected) {
    char output[512];
    assert(profile_rewrite(input, list, output, sizeof(output)) == 1);
    assert(!strcmp(output, expected));
}
static void unchanged(const char *input, int list) {
    char output[512];
    assert(profile_rewrite(input, list, output, sizeof(output)) == 0);
}
int main(void) {
    rewrite("version=CNRELiOS7.1.0&lang=2&platform=1&binary=1&channel_id=1&sub_channel_id=0", 1,
        "version=CNRELAndroid7.1.0&lang=2&platform=1&binary=1&channel_id=14&sub_channel_id=0");
    rewrite("version=CNRELiOS7.1.0&lang=2&platform=1&binary=1&channel_id=1&sub_channel_id=0", 0,
        "version=CNRELAndroid7.1.0&lang=2&platform=1&binary=1&channel_id=1&sub_channel_id=0");
    rewrite("version=CNRELiOS7.1.0&dispatchSeed=A%2Bb&aid=123", 0,
        "version=CNRELAndroid7.1.0&dispatchSeed=A%2Bb&aid=123");
    rewrite("channel_id=1&version=CNRELiOS7.1.0", 1, "channel_id=14&version=CNRELAndroid7.1.0");
    rewrite("version=CNRELiOS7.1.0&version=CNRELiOS7.1.0", 0,
        "version=CNRELAndroid7.1.0&version=CNRELAndroid7.1.0");
    rewrite("channel_id=1&version=CNRELiOS7.1.0&", 1, "channel_id=14&version=CNRELAndroid7.1.0&");
    unchanged("version=CNRELAndroid7.1.0&channel_id=14", 1);
    unchanged("version=CNRELWin7.1.0&channel_id=1", 1);
    unchanged("channel_id=1&platform=2", 0);
    unchanged("channel_id=1&platform=2", 1);
    unchanged("", 1);
    unchanged("lang=2", 1);
    rewrite("version=CNRELiOS7.1.0&channel=1", 1, "version=CNRELAndroid7.1.0&channel=1");
    unchanged("sub_channel_id=1&channel_id=10", 1);
    char small[16];
    assert(profile_rewrite("version=CNRELiOS7.1.0", 0, small, sizeof(small)) == 0);
    assert(profile_checksum("") == 2166136261u);
    assert(profile_checksum("abc") == profile_checksum("abc"));
    assert(profile_checksum("abc") != profile_checksum("abd"));
    char hex[9];
    profile_hex(hex, 0x00a1b2c3u);
    assert(!strcmp(hex, "00a1b2c3"));
    profile_hex(hex, 0xffffffffu);
    assert(!strcmp(hex, "ffffffff"));
    return 0;
}
