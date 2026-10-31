/* This pairing was observed with no explicit account credentials; it is version-specific. */
#include "GBReleaseCatalogProfile.h"
static int catalog_named(const char *segment, Size length, const char *name) {
    Size size = profile_span(name);
    if (length < size + 1 || segment[size] != '=') return 0;
    for (Size i = 0; i < size; ++i) if (segment[i] != name[i]) return 0;
    return 1;
}
static int catalog_seed(const char *segment, Size length) {
    if (!catalog_named(segment, length, "dispatchSeed") || length != 13 + 16) return 0;
    for (Size i = 13; i < length; ++i) {
        char c = segment[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
    }
    return 1;
}
static int catalog_query_valid(const char *query, int list) {
    int versions = 0, seeds = 0, channels = 0, platforms = 0, accounts = 0;
    for (const char *cursor = query; ; ) {
        const char *end = cursor;
        while (*end && *end != '&') ++end;
        Size length = (Size)(end - cursor);
        if (catalog_named(cursor, length, "version")) {
            if (!profile_segment(cursor, length, "version", catalog_native_version)) return 0;
            ++versions;
        } else if (catalog_named(cursor, length, "dispatchSeed")) {
            if (!catalog_seed(cursor, length)) return 0;
            ++seeds;
        } else if (catalog_named(cursor, length, "channel_id")) {
            if (!profile_segment(cursor, length, "channel_id", "14") &&
                !(list && profile_segment(cursor, length, "channel_id", "1"))) return 0;
            ++channels;
        } else if (catalog_named(cursor, length, "platform")) {
            if (!profile_segment(cursor, length, "platform", "1")) return 0;
            ++platforms;
        } else if (catalog_named(cursor, length, "account_type")) {
            if (!profile_segment(cursor, length, "account_type", "14")) return 0;
            ++accounts;
        }
        if (!*end) break;
        cursor = end + 1;
    }
    return versions == 1 && channels == 1 && platforms == 1 &&
        (list ? seeds == 0 : seeds == 1 && accounts == 1);
}
