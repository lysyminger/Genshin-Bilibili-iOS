#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned long Size;
#define GB_ENABLE_CATALOG_PROFILE 1
#include "src/GBDispatchProfileCore.c"

static void unchanged(const char *query, int list) {
    char output[1024];
    assert(!profile_rewrite(query, list, output, sizeof(output)));
}
int main(void) {
    char output[1024], expected[1024];
    const char *query = "version=CNRELiOS7.1.0&platform=1&channel_id=14&account_type=14&dispatchSeed=0000000000000000&aid=123&key_id=4&extra=A%2Bb";
    assert(profile_rewrite(query, 0, output, sizeof(output)));
    snprintf(expected, sizeof(expected), "%s&platform=1&channel_id=14&account_type=14&%s&aid=123&key_id=4&extra=A%%2Bb",
             catalog_query_version, catalog_query_selector);
    assert(!strcmp(output, expected));
    assert(!profile_rewrite(output, 0, expected, sizeof(expected)));
    assert(profile_rewrite("version=CNRELiOS7.1.0&platform=1&channel_id=1&lang=2&sub_channel_id=0", 1, output, sizeof(output)));
    assert(!strcmp(output, "version=CNRELAndroid7.1.0&platform=1&channel_id=14&lang=2&sub_channel_id=0"));
    unchanged("version=CNRELiOS7.1.0&platform=1&channel_id=14&account_type=14", 0);
    unchanged("version=CNRELiOS7.1.0&platform=1&channel_id=14&account_type=14&dispatchSeed=bad", 0);
    unchanged("version=CNRELiOS7.1.0&platform=1&channel_id=14&account_type=14&dispatchSeed=000000000000000G", 0);
    unchanged("version=CNRELiOS7.1.0&platform=2&channel_id=14&account_type=14&dispatchSeed=0000000000000000", 0);
    unchanged("version=CNRELiOS7.1.0&platform=1&channel_id=1&account_type=14&dispatchSeed=0000000000000000", 0);
    unchanged("version=CNRELiOS7.1.0&platform=1&channel_id=14&account_type=1&dispatchSeed=0000000000000000", 0);
    unchanged("version=CNRELiOS7.1.0&version=CNRELiOS7.1.0&platform=1&channel_id=14&account_type=14&dispatchSeed=0000000000000000", 0);
    unchanged("version=CNRELiOS7.1.0&platform=1&channel_id=14&account_type=14&dispatchSeed=0000000000000000&dispatchSeed=1111111111111111", 0);
    unchanged("version=CNRELiOS7.1.0&platform=1&channel_id=1&dispatchSeed=0000000000000000", 1);
    unchanged("version=CNRELiOS7.2.0&platform=1&channel_id=14&account_type=14&dispatchSeed=0000000000000000", 0);
    char small[16];
    assert(!profile_rewrite(query, 0, small, sizeof(small)));
    return 0;
}
