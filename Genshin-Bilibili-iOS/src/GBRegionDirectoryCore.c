/* Adapt only a recognized public directory record; all other fields stay intact. */
#include "GBRegionProfiles.h"
_Static_assert(sizeof(region_gf_record) == sizeof(region_qd_record), "Directory record size changed");

static int region_bytes_equal(const unsigned char *a, const unsigned char *b, unsigned long size) {
    for (unsigned long i = 0; i < size; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
static int region_varint(const unsigned char *data, unsigned long size, unsigned long *pos,
                         unsigned long long *value) {
    *value = 0;
    for (unsigned shift = 0; *pos < size && shift < 64; shift += 7) {
        unsigned char byte = data[(*pos)++];
        if (shift == 63 && byte > 1) return 0;
        *value |= (unsigned long long)(byte & 127) << shift;
        if (!(byte & 128)) return 1;
    }
    return 0;
}
static int region_find_gf_record(const unsigned char *data, unsigned long size, unsigned long *found) {
    unsigned long pos = 0, record_pos = 0;
    int regions = 0, recognized = 0;
    while (pos < size) {
        unsigned long long tag, value;
        if (!region_varint(data, size, &pos, &tag) || !(tag >> 3)) return 0;
        unsigned wire = (unsigned)(tag & 7);
        if (wire == 0) {
            if (!region_varint(data, size, &pos, &value) || ((tag >> 3) == 1 && value != 0)) return 0;
        } else if (wire == 2) {
            if (!region_varint(data, size, &pos, &value) || value > size - pos) return 0;
            if ((tag >> 3) == 2) {
                ++regions;
                if (value == sizeof(region_gf_record) &&
                    region_bytes_equal(data + pos, region_gf_record, sizeof(region_gf_record))) {
                    recognized = 1; record_pos = pos;
                }
            }
            pos += (unsigned long)value;
        } else if (wire == 1 || wire == 5) {
            unsigned long count = wire == 1 ? 8 : 4;
            if (count > size - pos) return 0;
            pos += count;
        } else return 0;
    }
    if (regions != 1 || !recognized) return 0;
    *found = record_pos;
    return 1;
}
static int region_replace_record(unsigned char *data, unsigned long size) {
    unsigned long record_pos;
    if (!region_find_gf_record(data, size, &record_pos)) return 0;
    for (unsigned long i = 0; i < sizeof(region_qd_record); ++i) data[record_pos + i] = region_qd_record[i];
    return 1;
}
static unsigned long region_append_qd_record(const unsigned char *data, unsigned long size,
                                            unsigned char *out, unsigned long capacity) {
    _Static_assert(sizeof(region_qd_record) < 128, "Directory record requires a longer varint");
    const unsigned long extra = 2 + sizeof(region_qd_record);
    unsigned long record_pos;
    if (!data || !out || capacity < extra || size > capacity - extra ||
        !region_find_gf_record(data, size, &record_pos)) return 0;
    for (unsigned long i = 0; i < size; ++i) out[i] = data[i];
    out[size] = 0x12;
    out[size + 1] = sizeof(region_qd_record);
    for (unsigned long i = 0; i < sizeof(region_qd_record); ++i) out[size + 2 + i] = region_qd_record[i];
    return size + extra;
}
