/* Rewrite only the published dispatch profile fields; every other query byte is preserved. */
static Size profile_span(const char *text) {
    Size size = 0;
    if (text) while (text[size]) ++size;
    return size;
}
static int profile_segment(const char *segment, Size length, const char *name, const char *value) {
    Size name_length = profile_span(name);
    Size value_length = profile_span(value);
    if (length != name_length + 1 + value_length) return 0;
    for (Size i = 0; i < name_length; ++i) if (segment[i] != name[i]) return 0;
    if (segment[name_length] != '=') return 0;
    for (Size i = 0; i < value_length; ++i) if (segment[name_length + 1 + i] != value[i]) return 0;
    return 1;
}
#ifdef GB_ENABLE_CATALOG_PROFILE
#include "GBCatalogProfileCore.c"
#endif
static int profile_rewrite(const char *query, int region_list, char *out, Size capacity) {
#ifdef GB_ENABLE_CATALOG_PROFILE
    const char *version_to = catalog_query_version;
#else
    static const char version_to[] = "version=CNRELAndroid7.1.0";
#endif
    static const char channel_to[] = "channel_id=14";
    if (!query || !out || capacity < 2) return 0;
#ifdef GB_ENABLE_CATALOG_PROFILE
    if (!catalog_query_valid(query, region_list)) return 0;
#endif
    /* Refuse to half-apply: the channel switch is only valid together with a recognized iOS profile. */
    int ios_profile = 0;
    for (const char *cursor = query; ; ) {
        const char *end = cursor;
        while (*end && *end != '&') ++end;
        if (profile_segment(cursor, (Size)(end - cursor), "version", "CNRELiOS7.1.0")) { ios_profile = 1; break; }
        if (!*end) break;
        cursor = end + 1;
    }
    if (!ios_profile) return 0;
    Size pos = 0;
    int changed = 0;
    const char *cursor = query;
    for (;;) {
        const char *end = cursor;
        while (*end && *end != '&') ++end;
        Size length = (Size)(end - cursor);
        const char *source = cursor;
        if (profile_segment(cursor, length, "version", "CNRELiOS7.1.0")) {
            source = version_to;
            length = profile_span(source);
            changed = 1;
#ifdef GB_ENABLE_CATALOG_PROFILE
        } else if (!region_list && catalog_seed(cursor, length)) {
            source = catalog_query_selector;
            length = profile_span(source);
            changed = 1;
#endif
        } else if (region_list && profile_segment(cursor, length, "channel_id", "1")) {
            source = channel_to;
            length = sizeof(channel_to) - 1;
            changed = 1;
        }
        if (pos + length + 2 > capacity) return 0;
        for (Size i = 0; i < length; ++i) out[pos++] = source[i];
        out[pos] = 0;
        if (!*end) break;
        out[pos++] = '&';
        out[pos] = 0;
        cursor = end + 1;
    }
    return changed;
}
static unsigned profile_checksum(const char *text) {
    /* FNV-1a: identifies whether the private seed changed without persisting it. */
    unsigned value = 2166136261u;
    if (!text) return 0;
    for (const char *cursor = text; *cursor; ++cursor) {
        value ^= (unsigned)(unsigned char)*cursor;
        value *= 16777619u;
    }
    return value;
}
static void profile_hex(char *out, unsigned value) {
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 8; ++i) out[i] = digits[(value >> (28 - i * 4)) & 15];
    out[8] = 0;
}
