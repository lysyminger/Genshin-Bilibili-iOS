extern void *objc_getClass(const char *name);
__attribute__((constructor)) static void initialize(void) {
    objc_getClass("MHYConfig");
}
