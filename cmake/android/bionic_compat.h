// SPDX-License-Identifier: GPL-2.0-or-later
// glibc-isms that Bionic lacks. Force-included into every C++ translation unit of Android builds.
#pragma once
#if defined(__cplusplus) && defined(__ANDROID__)
#include <csignal>
#include <cstdint>
#include <errno.h>
#include <pthread.h>

#ifndef PTHREAD_CANCEL_ENABLE
#define PTHREAD_CANCEL_ENABLE       0
#define PTHREAD_CANCEL_DISABLE      1
#define PTHREAD_CANCEL_DEFERRED     0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#endif
#ifndef PTHREAD_CANCELED
#define PTHREAD_CANCELED            ((void*)(intptr_t)-1)
#endif
#ifndef PTHREAD_PRIO_PROTECT
#define PTHREAD_PRIO_PROTECT 2
#endif

namespace Kyty::Android::ThreadCancel {

inline thread_local int  t_cancel_state   = PTHREAD_CANCEL_ENABLE;
inline thread_local int  t_cancel_type    = PTHREAD_CANCEL_DEFERRED;
inline thread_local bool t_cancel_pending = false;

inline void CancelSignalHandler(int, siginfo_t*, void*) {
    if (t_cancel_state == PTHREAD_CANCEL_ENABLE) {
        if (t_cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS) {
            pthread_exit(PTHREAD_CANCELED);
        } else {
            t_cancel_pending = true;
        }
    }
}

inline void EnsureHandlerInstalled() {
    static const bool s_installed = []() {
        struct sigaction sa {};
        sa.sa_sigaction = CancelSignalHandler;
        sa.sa_flags     = SA_SIGINFO | SA_RESTART;
        sigemptyset(&sa.sa_mask);
        ::sigaction(SIGUSR2, &sa, nullptr);
        return true;
    }();
    (void)s_installed;
}

} // namespace Kyty::Android::ThreadCancel

static inline int pthread_setcancelstate(int state, int* old_state) {
    if (old_state != nullptr) *old_state = Kyty::Android::ThreadCancel::t_cancel_state;
    if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE) return EINVAL;
    Kyty::Android::ThreadCancel::t_cancel_state = state;
    return 0;
}

static inline int pthread_setcanceltype(int type, int* old_type) {
    if (old_type != nullptr) *old_type = Kyty::Android::ThreadCancel::t_cancel_type;
    if (type != PTHREAD_CANCEL_DEFERRED && type != PTHREAD_CANCEL_ASYNCHRONOUS) return EINVAL;
    Kyty::Android::ThreadCancel::t_cancel_type = type;
    return 0;
}

static inline void pthread_testcancel() {
    if (Kyty::Android::ThreadCancel::t_cancel_pending &&
        Kyty::Android::ThreadCancel::t_cancel_state == PTHREAD_CANCEL_ENABLE) {
        pthread_exit(PTHREAD_CANCELED);
    }
}

static inline int pthread_cancel(pthread_t thread) {
    Kyty::Android::ThreadCancel::EnsureHandlerInstalled();
    return pthread_kill(thread, SIGUSR2);
}
#endif
