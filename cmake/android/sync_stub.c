// SPDX-License-Identifier: Apache-2.0
// Minimal stub library providing libsync.so symbols for custom Mesa Turnip drivers
// on Android versions where libsync.so is restricted from app namespaces.

#include <poll.h>
#include <unistd.h>

__attribute__((visibility("default")))
int sync_wait(int fd, int timeout) {
    if (fd < 0) return -1;
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int ret;
    do {
        ret = poll(&pfd, 1, timeout);
    } while (ret == -1 && (timeout < 0 || timeout > 0));
    return (ret > 0) ? 0 : -1;
}

__attribute__((visibility("default")))
int sync_merge(const char *name, int fd1, int fd2) {
    (void)name;
    (void)fd1;
    (void)fd2;
    return -1;
}
