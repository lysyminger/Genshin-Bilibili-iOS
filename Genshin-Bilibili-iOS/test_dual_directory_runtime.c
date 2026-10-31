#define GB_DUAL_LOGIN 1
#define main unused_single_directory_fixture
#include "test_region_directory_runtime.c"
#undef main

int main(void) {
    decoded_gf[0] = 0x12; decoded_gf[1] = sizeof(region_gf_record);
    memcpy(decoded_gf + 2, region_gf_record, sizeof(region_gf_record));
    decoded_size = 2 + sizeof(region_gf_record);
    reset();
    receive(data(fixture_gf_wire, 17));
    receive(data(fixture_gf_wire + 17, (Size)strlen(fixture_gf_wire) - 17));
    assert(!received); finish();
    assert(received == 1 && finished == 1 && !association);
    assert(delivered_size == strlen(fixture_dual_wire));
    assert(!memcmp(delivered, fixture_dual_wire, delivered_size));

    reset(); encoded_size_mismatch = 1;
    receive(data(fixture_gf_wire, (Size)strlen(fixture_gf_wire))); finish();
    assert(delivered_size == strlen(fixture_gf_wire));
    assert(!memcmp(delivered, fixture_gf_wire, delivered_size));

    reset(); host.text = "unrelated.example";
    receive(data("PRIVATE", 7)); finish();
    assert(received == 1 && delivered_size == 7 && !association);

    reset(); receive(data("not-base64", 10)); finish();
    assert(delivered_size == 10 && !memcmp(delivered, "not-base64", 10));

    reset(); receive(data("partial", 7));
    region_fail(&delegate, "connection:didFailWithError:", &connection, (Obj)99);
    assert(!received && failed == 1 && !association);
    return 0;
}
