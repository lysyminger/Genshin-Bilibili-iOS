#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
#include "src/GBRegionDirectoryCore.c"

static unsigned long frame(unsigned char *data, const unsigned char *record) {
    data[0] = 0x12; data[1] = sizeof(region_gf_record);
    memcpy(data + 2, record, sizeof(region_gf_record));
    return 2 + sizeof(region_gf_record);
}
int main(void) {
    unsigned char data[512], before[512];
    unsigned long size = frame(data, region_gf_record);
    data[size++] = 0x2a; data[size++] = 3; data[size++] = 7; data[size++] = 8; data[size++] = 9;
    memcpy(before, data, size);
    assert(region_replace_record(data, size));
    assert(!memcmp(data + 2, region_qd_record, sizeof(region_qd_record)));
    assert(!memcmp(data + 2 + sizeof(region_qd_record), before + 2 + sizeof(region_qd_record), 5));
    assert(data[0] == before[0] && data[1] == before[1]);
    assert(!region_replace_record(data, size)); /* Already adapted; no second rewrite. */
    size = frame(data, region_gf_record); memcpy(before, data, size);
    assert(!region_replace_record(data, size - 1) && !memcmp(data, before, size));
    data[size++] = 8; data[size++] = 1; memcpy(before, data, size);
    assert(!region_replace_record(data, size) && !memcmp(data, before, size));
    size = frame(data, region_gf_record); size += frame(data + size, region_gf_record); memcpy(before, data, size);
    assert(!region_replace_record(data, size) && !memcmp(data, before, size));
    size = frame(data, region_gf_record); data[10] ^= 1; memcpy(before, data, size);
    assert(!region_replace_record(data, size) && !memcmp(data, before, size));
    size = frame(data, region_gf_record); data[0] = 0x2a; memcpy(before, data, size);
    assert(!region_replace_record(data, size) && !memcmp(data, before, size)); /* Pattern in another field. */
    memset(data, 0x80, 11); assert(!region_replace_record(data, 11));
    data[0] = 0; assert(!region_replace_record(data, 1));
    return 0;
}
