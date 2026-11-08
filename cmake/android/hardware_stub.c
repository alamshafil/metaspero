// SPDX-License-Identifier: Apache-2.0
// Minimal stub library providing libhardware.so symbols for custom Mesa Turnip drivers
// on Android versions where libhardware.so is restricted from app namespaces.

__attribute__((visibility("default")))
int hw_get_module(const char *id, void **module) {
    (void)id;
    if (module) *module = 0;
    return -1;
}

__attribute__((visibility("default")))
int hw_get_module_by_class(const char *class_id, const char *inst, void **module) {
    (void)class_id;
    (void)inst;
    if (module) *module = 0;
    return -1;
}
