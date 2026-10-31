#define GB_PRESERVE_NATIVE_DIRECTORY 1
#define GB_ENABLE_CATALOG_PROFILE 1
#define main unused_legacy_profile_fixture
#include "test_dispatch_profile.c"
#undef main

int main(void) {
    get_class = klass; selector = sel; message = (Imp)msg;
    file_open = open_log; file_write = write_log; file_close = close_log;
    strcpy(log_path, "test-only");
    reset();
    setup("https://dispatchcnglobal.yuanshen.com/query_region_list?version=CNRELiOS7.1.0&platform=1&channel_id=1&extra=A%2Bb");
    assert(dispatch_profile_request(&request) == &request && copies == 0);
    assert(strstr(captured, "native iOS directory request retained"));
    dispatch_next_unity_send = original_send;
    assert(dispatch_unity_send(0, 0, 0, &request, 0) == &request && copies == 0 && forwarded == 1);

    reset(); route_active = 1;
    setup("https://dispatchcnglobal.yuanshen.com/query_region_list?version=CNRELiOS7.1.0&platform=1&channel_id=1");
    assert(dispatch_profile_request(&request) == &request && copies == 0);

    reset(); route_active = 0;
    strcpy(host_buffer, "cngfdispatch.yuanshen.com"); strcpy(path_buffer, "/query_cur_region");
    setup("https://cngfdispatch.yuanshen.com/query_cur_region?version=CNRELiOS7.1.0&platform=1&channel_id=1&account_type=1&dispatchSeed=0000000000000000&aid=PRIVATE");
    assert(dispatch_profile_request(&request) == &request && copies == 0);
    assert(dispatch_unity_send(0, 0, 0, &request, 0) == &request && copies == 0);
    assert(!strstr(captured, "verified version/seed pairing applied") && !strstr(captured, "PRIVATE"));

    reset(); route_active = 1;
    strcpy(host_buffer, "cnqddispatch.yuanshen.com"); strcpy(path_buffer, "/query_cur_region");
    setup("https://cnqddispatch.yuanshen.com/query_cur_region?version=CNRELiOS7.1.0&platform=1&channel_id=14&account_type=14&dispatchSeed=0000000000000000&extra=A%2Bb&aid=PRIVATE");
    assert(dispatch_profile_request(&request) == &edited && copies == 1);
    assert(strstr(destination_text, catalog_query_version) && strstr(destination_text, catalog_query_selector));
    assert(strstr(destination_text, "platform=1&channel_id=14&account_type=14"));
    assert(strstr(destination_text, "extra=A%2Bb&aid=PRIVATE"));
    assert(!strstr(captured, "PRIVATE"));

    reset(); route_active = 0;
    strcpy(host_buffer, "cnqddispatch.yuanshen.com"); strcpy(path_buffer, "/query_cur_region");
    setup("https://cnqddispatch.yuanshen.com/query_cur_region?version=CNRELiOS7.1.0&platform=1&channel_id=14&account_type=14&dispatchSeed=0000000000000000");
    assert(dispatch_profile_request(&request) == &request && copies == 0);

    reset(); route_active = 1;
    strcpy(host_buffer, "cnqddispatch.yuanshen.com"); strcpy(path_buffer, "/query_cur_region");
    setup("https://cnqddispatch.yuanshen.com/query_cur_region?version=CNRELiOS7.1.0&platform=1&channel_id=14&account_type=14&dispatchSeed=bad");
    assert(dispatch_profile_request(&request) == &request && copies == 0);

    reset(); route_active = 1;
    strcpy(host_buffer, "cnqddispatch.yuanshen.com"); strcpy(path_buffer, "/query_cur_region");
    setup("https://cnqddispatch.yuanshen.com/query_cur_region?version=CNRELiOS7.1.0&platform=1&channel_id=14&account_type=14&dispatchSeed=0000000000000000");
    assert(dispatch_unity_send(0, 0, 0, &request, (Obj)101) == &request && copies == 0);
    return 0;
}
