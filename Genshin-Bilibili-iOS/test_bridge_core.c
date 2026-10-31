#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include "src/GBLoginBridgeCore.c"

int main(void) {
    GBLoginBridge b = {0};
    unsigned long first, second;
    assert(gb_begin(&b, &first) == GB_OK);
    assert(gb_begin(&b, &second) == GB_BUSY);
    assert(gb_select(&b, GB_BILIBILI, 0) == GB_NOT_READY);
    assert(b.phase == GB_CHOOSING && b.channel != GB_BILIBILI);
    assert(gb_select(&b, GB_OFFICIAL, 0) == GB_OK);
    assert(b.phase == GB_IDLE && b.channel == GB_OFFICIAL);

    assert(gb_begin(&b, &first) == GB_OK);
    assert(gb_select(&b, GB_BILIBILI, 1) == GB_OK);
    assert(gb_verified(&b, first, 0, "test-open-id", "test-combo-token") == GB_INVALID);
    assert(gb_credentials(&b, first, "bad-uid", "test-key") == GB_INVALID);
    assert(gb_credentials(&b, first, "123", "") == GB_INVALID);
    assert(gb_credentials(&b, first, "123", "test-key") == GB_OK);
    assert(b.phase == GB_VERIFYING);
    assert(gb_credentials(&b, first, "123", "test-key") == GB_INVALID);
    assert(gb_verified(&b, first, 0, "test-open-id", "") == GB_INVALID);
    assert(gb_verified(&b, first, -1, "", "") == GB_REJECTED);
    assert(b.phase == GB_IDLE);
    assert(gb_verified(&b, first, 0, "test-open-id", "test-combo-token") == GB_STALE);

    assert(gb_begin(&b, &second) == GB_OK);
    assert(gb_select(&b, GB_BILIBILI, 1) == GB_OK);
    assert(gb_credentials(&b, second, "123", "test-key") == GB_OK);
    assert(gb_verified(&b, second, 0, "test-open-id", "test-combo-token") == GB_OK);
    assert(b.phase == GB_AUTHENTICATED);
    assert(gb_verified(&b, second, 0, "test-open-id", "test-combo-token") == GB_INVALID);
    gb_logout(&b);
    assert(b.phase == GB_IDLE && b.channel == GB_OFFICIAL);
    assert(gb_credentials(&b, second, "123", "test-key") == GB_STALE);
    assert(gb_begin(&b, &first) == GB_OK);
    assert(gb_cancel(&b, first) == GB_OK);
    assert(gb_cancel(&b, first) == GB_STALE);
    return 0;
}
