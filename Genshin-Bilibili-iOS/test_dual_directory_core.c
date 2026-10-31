#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
#include "src/GBRegionDirectoryCore.c"

int main(void) {
    unsigned char original[512], out[1024], before[512];
    original[0] = 0x12; original[1] = sizeof(region_gf_record);
    memcpy(original + 2, region_gf_record, sizeof(region_gf_record));
    unsigned long size = 2 + sizeof(region_gf_record);
    original[size++] = 0x2a; original[size++] = 3;
    original[size++] = 7; original[size++] = 8; original[size++] = 9;
    memcpy(before, original, size);
    unsigned long extra = 2 + sizeof(region_qd_record);
    assert(region_append_qd_record(original, size, out, sizeof(out)) == size + extra);
    assert(!memcmp(original, before, size) && !memcmp(out, original, size));
    assert(out[size] == 0x12 && out[size + 1] == sizeof(region_qd_record));
    assert(!memcmp(out + size + 2, region_qd_record, sizeof(region_qd_record)));
    assert(!region_append_qd_record(out, size + extra, out, sizeof(out)));
    memset(out, 0x55, sizeof(out));
    assert(!region_append_qd_record(original, size, out, size + extra - 1) && out[0] == 0x55);
    assert(!region_append_qd_record(original, size - 1, out, sizeof(out)) && out[0] == 0x55);
    assert(!region_append_qd_record(0, size, out, sizeof(out)));
    assert(!region_append_qd_record(original, size, 0, sizeof(out)));
    assert(region_append_qd_record(original, size, original, sizeof(original)) == size + extra);
    assert(!memcmp(original, before, size));
    memcpy(original, before, size);
    original[size++] = 8; original[size++] = 1;
    assert(!region_append_qd_record(original, size, out, sizeof(out)));
    return 0;
}
