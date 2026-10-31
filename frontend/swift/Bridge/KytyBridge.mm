// SPDX-License-Identifier: GPL-2.0-or-later
#import "KytyBridge.h"
#import <objc/runtime.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <cstdio>



#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <mutex>
#include <string>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <csignal>
#include <setjmp.h>
#include <sys/ucontext.h>
#include <thread>
#include <unistd.h>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <execinfo.h>
#endif

#if __has_include("common/assert.h")
#include "common/assert.h"
#include "common/hostException.h"
#endif

#if __has_include("emulator.h")
#include "emulator.h"
#include "libs/controller.h"
#include "common/trophies.h"
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL_main.h>
#define HAS_KYTY_CORE 1
#else
#define HAS_KYTY_CORE 0
#endif
#import <GameController/GameController.h>
#include <mach/mach_time.h>
#include <mach/mach.h>
#if TARGET_OS_IPHONE
#if __has_include(<os/proc.h>)
#import <os/proc.h>
#endif
#endif

extern "C" {
bool KytyAcquireJitPool(void) noexcept;
bool KytyIsJitPoolReady(void) noexcept;
const char* KytyGetJitPoolStatus(void) noexcept;
void KytySetWindowCallbacks(void (*created_cb)(void*), void (*destroyed_cb)());
}

NSString * const KytyLogUpdatedNotification = @"KytyLogUpdatedNotification";
NSString * const KytyEmulationCrashedNotification = @"KytyEmulationCrashedNotification";
NSString * const KytyCrashErrorKey = @"error";
NSString * const KytyCrashDetailKey = @"detail";
NSString * const KytyWindowDestroyedNotification = @"KytyWindowDestroyedNotification";
NSString * const KytyMemoryPressureNotification = @"KytyMemoryPressureNotification";
NSString * const KytyMemoryPressureLevelKey = @"level";
NSString * const KytyMemoryPressureReasonKey = @"reason";
NSString * const KytyMemoryUsedMBKey = @"usedMB";
NSString * const KytyMemoryAvailMBKey = @"availMB";
NSString * const KytyMemoryLimitMBKey = @"limitMB";

static void ConfigureMetalLayerHud(CALayer *layer, BOOL enabled) {
    if (!layer) return;
    if ([layer isKindOfClass:NSClassFromString(@"CAMetalLayer")]) {
        SEL setDevHud = NSSelectorFromString(@"setDeveloperHUDProperties:");
        if ([layer respondsToSelector:setDevHud]) {
            NSDictionary *props = enabled ? @{ @"mode" : @"default" } : nil;
            #pragma clang diagnostic push
            #pragma clang diagnostic ignored "-Warc-performSelector-leaks"
            [layer performSelector:setDevHud withObject:props];
            #pragma clang diagnostic pop
            std::printf("[KytyBridge] Metal Performance HUD configured on CAMetalLayer %p (enabled=%d)\n", layer, enabled);
            std::fflush(stdout);
        }
    }
    for (CALayer *sub in layer.sublayers) {
        ConfigureMetalLayerHud(sub, enabled);
    }
}

static void *g_host_view = nullptr;

extern "C" void *KytyGetHostView(void) {
    return g_host_view;
}

extern "C" void KytySetHostView(void *view) {
    g_host_view = view;
}

// CAMetalLayer hosted by the SwiftUI view. When set, the core renders into it directly
// (VK_EXT_metal_surface)
static CAMetalLayer *g_host_layer = nil;
static std::atomic<int> g_host_pixel_w{0};
static std::atomic<int> g_host_pixel_h{0};

static CAMetalLayer *FindMetalLayer(CALayer *root) {
    if ([root isKindOfClass:[CAMetalLayer class]]) return (CAMetalLayer *)root;
    for (CALayer *sub in root.sublayers) {
        CAMetalLayer *found = FindMetalLayer(sub);
        if (found) return found;
    }
    return nil;
}

extern "C" void *KytyGetHostMetalLayer(void) {
    return (__bridge void *)g_host_layer;
}

static void PushHostPixelSize(int w, int h) {
#if HAS_KYTY_CORE
    if (w <= 0 || h <= 0) return;
    SDL_Event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    ev.window.data1 = w;
    ev.window.data2 = h;
    SDL_PushEvent(&ev);
#endif
}

// Called by the core once its presentation surface exists. The core renders into the host
// view's CAMetalLayer, so there is nothing to show or hide here, we only sync
// the renderer with the view's current size and apply the Metal HUD.
extern "C" void KytyNotifyWindowCreated(void *window) {
    std::printf("[KytyBridge] Presentation surface created (host layer: %p)\n", (__bridge void *)g_host_layer);
    std::fflush(stdout);
    (void)window;
    if (g_host_layer == nil) return;
    PushHostPixelSize(g_host_pixel_w.load(), g_host_pixel_h.load());
    if ([KytyBridge isMetalHudEnabled]) {
        CAMetalLayer *layer = g_host_layer;
        dispatch_async(dispatch_get_main_queue(), ^{
            ConfigureMetalLayerHud(layer, YES);
        });
    }
}

extern "C" void KytyNotifyWindowDestroyed() {
    std::printf("[KytyBridge] Presentation surface destroyed\n");
    std::fflush(stdout);
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSNotificationCenter defaultCenter] postNotificationName:KytyWindowDestroyedNotification
                                                            object:nil];
    });
}

@interface KytyBridge ()
+ (void)addLogMessageInternal:(NSString *)message writeToFile:(BOOL)writeToFile;
@end

namespace {

std::atomic<bool> g_is_running{false};
std::atomic<bool> g_crash_reported{false};
std::mutex g_session_mutex;
NSString *g_last_crash_error = nil;
std::unique_ptr<std::thread> g_emulator_thread;

// Thread-safe log storage
std::mutex g_log_mutex;
NSMutableArray<NSString *> *g_log_entries = nil;
constexpr NSUInteger kMaxLogEntries = 3500;
int g_pipe[2] = {-1, -1};
int g_orig_stdout = -1;
int g_orig_stderr = -1;
FILE *g_persistent_log_file = nullptr;
NSString *g_persistent_log_path = nil;

bool CheckProcessDebugged() {
    // 1. Check P_TRACED via sysctl (actively attached debugger like Xcode LLDB)
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    struct kinfo_proc info;
    size_t size = sizeof(info);
    info.kp_proc.p_flag = 0;
    if (sysctl(mib, 4, &info, &size, nullptr, 0) == 0) {
        if ((info.kp_proc.p_flag & P_TRACED) != 0) {
            return true;
        }
    }
    
    // 2. Check CS_DEBUGGED via syscall(SYS_csops) or csops (StikDebug or persistent debugged flag)
    uint32_t flags = 0;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if (syscall(169 /* SYS_csops */, getpid(), 0 /* CS_OPS_STATUS */, &flags, sizeof(flags)) == 0) {
        if ((flags & 0x10000000 /* CS_DEBUGGED */) != 0) {
            return true;
        }
    }
#pragma clang diagnostic pop

    typedef int (*csops_t)(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);
    static csops_t p_csops = (csops_t)dlsym(RTLD_DEFAULT, "csops");
    if (p_csops && p_csops(getpid(), 0, &flags, sizeof(flags)) == 0) {
        if ((flags & 0x10000000) != 0) {
            return true;
        }
    }
    return false;
}

std::atomic<bool> g_log_file_writing_enabled{false};
std::atomic<size_t> g_persistent_log_bytes_written{0};
constexpr size_t kMaxPersistentLogBytes = 2 * 1024 * 1024; // 2 MB limit to prevent excessive disk writes
uint64_t g_last_log_flush_time = 0;

void SetupLogRedirection() {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        g_log_entries = [NSMutableArray arrayWithCapacity:kMaxLogEntries];

        // Check user defaults for enableLogFileWriting. Default is NO to protect storage and reduce I/O.
        NSNumber *savedPref = [[NSUserDefaults standardUserDefaults] objectForKey:@"enableLogFileWriting"];
        BOOL shouldWriteLog = savedPref ? [savedPref boolValue] : NO;
        g_log_file_writing_enabled.store(shouldWriteLog);

        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        if (paths.count > 0) {
            g_persistent_log_path = [paths[0] stringByAppendingPathComponent:@"kyty_last_run.log"];
            if (shouldWriteLog) {
                g_persistent_log_file = fopen([g_persistent_log_path UTF8String], "w");
                if (g_persistent_log_file) {
                    std::fprintf(g_persistent_log_file, "--- KytyPS5 Session Log [%s] ---\n",
                                 [[NSDate date] description].UTF8String);
                    std::fflush(g_persistent_log_file);
                }
            }
        }

#if TARGET_OS_IPHONE
        // Listen for kernel memory pressure events directly from Darwin/Jetsam
        static dispatch_source_t memoryPressureSource = nil;
        dispatch_queue_t mQueue = dispatch_get_main_queue();
        memoryPressureSource = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_MEMORYPRESSURE,
            0,
            DISPATCH_MEMORYPRESSURE_WARN | DISPATCH_MEMORYPRESSURE_CRITICAL,
            mQueue
        );
        if (memoryPressureSource) {
            dispatch_source_set_event_handler(memoryPressureSource, ^{
                unsigned long flags = dispatch_source_get_data(memoryPressureSource);
                if (flags & DISPATCH_MEMORYPRESSURE_CRITICAL) {
                    [KytyBridge reportMemoryWarningWithLevel:@"Critical"
                                                     reason:@"Critical Jetsam memory pressure! System may kill app imminently"];
                } else if (flags & DISPATCH_MEMORYPRESSURE_WARN) {
                    [KytyBridge reportMemoryWarningWithLevel:@"Warning"
                                                     reason:@"High memory pressure approaching Jetsam limit"];
                }
            });
            dispatch_resume(memoryPressureSource);
        }

        // Listen for UIKit memory warnings to protect system resources
        [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidReceiveMemoryWarningNotification
                                                          object:nil
                                                           queue:[NSOperationQueue mainQueue]
                                                      usingBlock:^(NSNotification * _Nonnull note) {
            [KytyBridge reportMemoryWarningWithLevel:@"Warning"
                                             reason:@"System low-memory warning received from iOS"];
        }];
#endif

        // Save original descriptors so Xcode console and system logs still work
        g_orig_stdout = dup(STDOUT_FILENO);
        g_orig_stderr = dup(STDERR_FILENO);

        if (pipe(g_pipe) == 0) {
            dup2(g_pipe[1], STDOUT_FILENO);
            dup2(g_pipe[1], STDERR_FILENO);
            close(g_pipe[1]);

            dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
                FILE *fp = fdopen(g_pipe[0], "r");
                if (!fp) return;

                char lineBuffer[4096];
                while (fgets(lineBuffer, sizeof(lineBuffer), fp)) {
                    if (g_orig_stdout >= 0) {
                        write(g_orig_stdout, lineBuffer, strlen(lineBuffer));
                    }

                    if (g_log_file_writing_enabled.load() && g_persistent_log_file) {
                        if (g_persistent_log_bytes_written.load() < kMaxPersistentLogBytes) {
                            size_t len = strlen(lineBuffer);
                            std::fputs(lineBuffer, g_persistent_log_file);
                            g_persistent_log_bytes_written.fetch_add(len);
                            uint64_t now = mach_absolute_time();
                            if (now - g_last_log_flush_time > 3000000000ULL) { // Flush periodically (~3s)
                                g_last_log_flush_time = now;
                                std::fflush(g_persistent_log_file);
                            }
                        }
                    }

                    NSString *line = [[NSString stringWithUTF8String:lineBuffer]
                                      stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
                    if (line.length > 0) {
                        [KytyBridge addLogMessageInternal:line writeToFile:NO];
                    }
                }
                fclose(fp);
            });
        }
    });
}

static void KytySignalHandler(int sig, siginfo_t *info, void *ucontext) {
    char detail[512];
    uintptr_t fault_addr = (uintptr_t)(info ? info->si_addr : nullptr);
    uintptr_t pc = 0;
    uintptr_t sp = 0;
#if defined(__arm64__) || defined(__aarch64__)
    if (ucontext) {
        auto *uc = (ucontext_t *)ucontext;
        pc = (uintptr_t)arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
        sp = (uintptr_t)arm_thread_state64_get_sp(uc->uc_mcontext->__ss);
    }
#endif
    std::snprintf(detail, sizeof(detail),
                  "Signal %d (%s, code=%d) at addr=0x%016llx, PC=0x%016llx, SP=0x%016llx",
                  sig, strsignal(sig), info ? info->si_code : 0,
                  (unsigned long long)fault_addr,
                  (unsigned long long)pc,
                  (unsigned long long)sp);

#if defined(__APPLE__)
    void *callstack[32];
    int frames = backtrace(callstack, 32);
    char **strs = backtrace_symbols(callstack, frames);
    if (strs) {
        std::printf("--- Host Backtrace (sig=%d) ---\n", sig);
        for (int i = 0; i < frames; ++i) {
            std::printf("  [%d] %s\n", i, strs[i]);
        }
        std::fflush(stdout);
        free(strs);
    }
#endif

    [KytyBridge handleCrashWithReason:[NSString stringWithFormat:@"Fatal Signal (%s)", strsignal(sig)]
                               detail:[NSString stringWithUTF8String:detail]];
}

static void InstallCrashSignalHandlers() {
    static std::once_flag once;
    std::call_once(once, []() {
        stack_t ss;
        ss.ss_sp = malloc(SIGSTKSZ * 4);
        ss.ss_size = SIGSTKSZ * 4;
        ss.ss_flags = 0;
        sigaltstack(&ss, nullptr);

        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = KytySignalHandler;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);

        sigaction(SIGABRT, &sa, nullptr);
        sigaction(SIGTRAP, &sa, nullptr);
        sigaction(SIGFPE, &sa, nullptr);
    });
}

#if HAS_KYTY_CORE
static void KytyFatalExitCallback(int status, const char *message) {
    NSString *detail = message ? [NSString stringWithUTF8String:message] : [NSString stringWithFormat:@"Status code %d", status];
    [KytyBridge handleCrashWithReason:@"Fatal Core Assertion" detail:detail];
}
#endif

} // namespace

@implementation KytyBridge

+ (void)load {
    SetupLogRedirection();
    InstallCrashSignalHandlers();
#if HAS_KYTY_CORE
    SDL_SetMainReady();
    Common::SetFatalHandler(KytyFatalExitCallback);
    KytySetWindowCallbacks(KytyNotifyWindowCreated, KytyNotifyWindowDestroyed);
#endif
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"enableMetalHud"]) {
        setenv("MTL_HUD_ENABLED", "1", 1);
    }
#if TARGET_OS_OSX
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    if (paths.count > 0) {
        NSString *appSupportDir = [paths[0] stringByAppendingPathComponent:@"KytyPS5"];
        [[NSFileManager defaultManager] createDirectoryAtPath:appSupportDir
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        chdir([appSupportDir UTF8String]);
        std::printf("[KytyBridge] Working directory set to Application Support: %s\n", [appSupportDir UTF8String]);
    }
#else
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    if (paths.count > 0) {
        NSString *docsDir = paths[0];
        chdir([docsDir UTF8String]);
        std::printf("[KytyBridge] Working directory set to Documents: %s\n", [docsDir UTF8String]);
    }
#endif
    std::printf("[KytyBridge] Subsystems initialized. Process ID: %d, Debugger: %s\n",
                getpid(), CheckProcessDebugged() ? "ACTIVE" : "INACTIVE");
    std::fflush(stdout);
}

+ (pid_t)processId {
    return getpid();
}

+ (BOOL)isProcessDebugged {
    return CheckProcessDebugged();
}

+ (BOOL)isJitReady {
    return KytyIsJitPoolReady();
}

+ (BOOL)acquireJitPool {
    return KytyAcquireJitPool();
}

+ (BOOL)isJitAvailable {
    return KytyIsJitPoolReady() || CheckProcessDebugged();
}

+ (BOOL)refreshJitStatus {
    return KytyAcquireJitPool();
}

+ (void)openStikDebug {
    NSString *bundleID = [[NSBundle mainBundle] bundleIdentifier];
    if (!bundleID) bundleID = @"com.alamshafil.ps5emu";
    const pid_t pid = getpid();

    NSURLComponents *comp = [[NSURLComponents alloc] init];
    comp.scheme = @"stikdebug";
    comp.host = @"enable-jit";
    comp.queryItems = @[
        [NSURLQueryItem queryItemWithName:@"bundle-id" value:bundleID],
        [NSURLQueryItem queryItemWithName:@"pid" value:[NSString stringWithFormat:@"%d", pid]],
        [NSURLQueryItem queryItemWithName:@"script-name" value:@"universal.js"]
    ];

    NSURL *url = comp.URL;
#if TARGET_OS_OSX
    if (url) {
        [[NSWorkspace sharedWorkspace] openURL:url];
        return;
    }
    comp.scheme = @"stikjit";
    if (comp.URL) {
        [[NSWorkspace sharedWorkspace] openURL:comp.URL];
    }
#else
    if (url && [[UIApplication sharedApplication] canOpenURL:url]) {
        [[UIApplication sharedApplication] openURL:url options:@{} completionHandler:nil];
        return;
    }

    comp.scheme = @"stikjit";
    if (comp.URL) {
        [[UIApplication sharedApplication] openURL:comp.URL options:@{} completionHandler:nil];
    }
#endif
}

+ (NSString *)jitStatusDescription {
    if (KytyIsJitPoolReady()) {
        const char* status = KytyGetJitPoolStatus();
        return status ? [NSString stringWithUTF8String:status] : @"JIT Pool Ready";
    }

    const pid_t pid = getpid();
    if (CheckProcessDebugged()) {
        return [NSString stringWithFormat:@"Debugger Attached (PID: %d) - Tap 'Refresh' to prepare JIT pool", pid];
    }
    return [NSString stringWithFormat:@"JIT Inactive (PID: %d) - Open StikDebug or launch via Xcode", pid];
}

+ (NSString *)moltenVkVersion {
    // 1. Check embedded Frameworks/MoltenVK.framework
    NSString *bundleFrameworkPath = [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:@"Frameworks/MoltenVK.framework"];
    NSBundle *mvkBundle = [NSBundle bundleWithPath:bundleFrameworkPath];
    if (mvkBundle) {
        NSString *ver = [mvkBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
        if (ver && ver.length > 0) {
            return [NSString stringWithFormat:@"MoltenVK %@ (Metal)", ver];
        }
    }
    
    // 2. Check loaded bundle identifier
    NSBundle *loadedBundle = [NSBundle bundleWithIdentifier:@"com.moltenvk.framework"];
    if (loadedBundle) {
        NSString *ver = [loadedBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
        if (ver && ver.length > 0) {
            return [NSString stringWithFormat:@"MoltenVK %@ (Metal)", ver];
        }
    }
    
    return @"MoltenVK 1.4.3 (Metal)";
}

+ (void)setMetalHudEnabled:(BOOL)enabled {
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:@"enableMetalHud"];
    [[NSUserDefaults standardUserDefaults] synchronize];
    if (enabled) {
        setenv("MTL_HUD_ENABLED", "1", 1);
    } else {
        unsetenv("MTL_HUD_ENABLED");
    }
    std::printf("[KytyBridge] Metal Performance HUD: %s\n", enabled ? "ENABLED" : "DISABLED");
    std::fflush(stdout);
}

+ (BOOL)isMetalHudEnabled {
    return [[NSUserDefaults standardUserDefaults] boolForKey:@"enableMetalHud"];
}

+ (void)hostViewDidResizeToPixelWidth:(int)width height:(int)height {
    g_host_pixel_w.store(width);
    g_host_pixel_h.store(height);
    if (g_is_running.load() && g_host_layer != nil) {
        PushHostPixelSize(width, height);
    }
}

+ (BOOL)isEmulationRunning {
    return g_is_running.load();
}

+ (void)startEmulationWithGamePath:(NSString *)gamePath
                            inView:(PlatformView *)renderView
                        completion:(void (^)(BOOL, NSString * _Nullable))completion {
    if (g_is_running.load()) {
        if (completion) {
            completion(NO, @"Emulation session is already active.");
        }
        return;
    }

    const std::string path = [gamePath UTF8String];
    CAMetalLayer *targetLayer = FindMetalLayer(renderView.layer);
    void *targetView = (__bridge void *)renderView;
    g_crash_reported.store(false);
    g_last_crash_error = nil;
    g_is_running.store(true);
    g_host_view = targetView;
    g_host_layer = targetLayer;

    std::printf("[KytyBridge] ---\n");
    std::printf("[KytyBridge] Launching Game: %s\n", path.c_str());
    std::printf("[KytyBridge] Debugger: %s, PID: %d\n", CheckProcessDebugged() ? "ATTACHED" : "NONE", getpid());
    std::printf("[KytyBridge] ---\n");
    std::fflush(stdout);

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
        std::lock_guard<std::mutex> session_lock(g_session_mutex);
        g_is_running.store(true);
        g_host_view = targetView;
        g_host_layer = targetLayer;
#if HAS_KYTY_CORE
        try {
            Common::HostException::SetEmulationShuttingDown(false);
            Emulator::RunOptions options;
            std::filesystem::path p(path);
            
            if (std::filesystem::is_directory(p)) {
                options.app0_dir = p;
                options.elf      = "/app0/eboot.bin";
            } else {
                options.app0_dir = p.parent_path();
                if (options.app0_dir.empty()) {
                    options.app0_dir = ".";
                }
                options.elf = std::filesystem::path("/app0") / p.filename();
            }

            options.config.printf_direction = Config::LogDirection::Console;
            
            NSString *res = [[NSUserDefaults standardUserDefaults] stringForKey:@"resolutionScale"];
            if ([res isEqualToString:@"480p"]) {
                options.config.screen_width = 854;
                options.config.screen_height = 480;
            } else if ([res isEqualToString:@"1080p"]) {
                options.config.screen_width = 1920;
                options.config.screen_height = 1080;
            } else {
                options.config.screen_width = 1280;
                options.config.screen_height = 720;
            }

            NSString *present = [[NSUserDefaults standardUserDefaults] stringForKey:@"presentMode"];
            if ([present isEqualToString:@"Fifo"]) {
                options.config.present_mode = Config::PresentMode::Fifo;
            } else if ([present isEqualToString:@"Immediate"]) {
                options.config.present_mode = Config::PresentMode::Immediate;
            } else {
                options.config.present_mode = Config::PresentMode::Mailbox;
            }

            if ([KytyBridge isMetalHudEnabled]) {
                setenv("MTL_HUD_ENABLED", "1", 1);
            } else {
                unsetenv("MTL_HUD_ENABLED");
            }
            
            std::printf("[KytyBridge] Configured app0_dir: %s, elf: %s\n",
                        options.app0_dir.string().c_str(), options.elf.string().c_str());
            std::printf("[KytyBridge] Graphics: %ux%u, Present Mode: %s, Metal HUD: %s\n",
                        options.config.screen_width, options.config.screen_height,
                        present ? [present UTF8String] : "Mailbox",
                        [KytyBridge isMetalHudEnabled] ? "ENABLED" : "DISABLED");
            std::fflush(stdout);

            if (completion) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    completion(YES, nil);
                });
            }

            // Launch Kyty Emulator core
            Emulator::Run(options);
        } catch (const std::exception& ex) {
            std::printf("[KytyBridge] Exception during emulation: %s\n", ex.what());
            std::fflush(stdout);
            [KytyBridge handleCrashWithReason:@"C++ Exception"
                                       detail:[NSString stringWithUTF8String:ex.what()]];
        } catch (...) {
            std::printf("[KytyBridge] Unknown C++ Exception during emulation\n");
            std::fflush(stdout);
            [KytyBridge handleCrashWithReason:@"Unknown C++ Exception"
                                       detail:@"An unhandled C++ exception occurred during emulation."];
        }
#else
        std::printf("[KytyBridge] Kyty core worker thread running for: %s\n", path.c_str());
        std::fflush(stdout);
        if (completion) {
            dispatch_async(dispatch_get_main_queue(), ^{
                completion(YES, nil);
            });
        }
#endif
        g_is_running.store(false);
        g_host_layer = nil;
        std::printf("[KytyBridge] Emulation thread ended.\n");
        std::fflush(stdout);
    });
}

+ (void)stopEmulation {
    if (!g_is_running.load()) {
        return;
    }
    std::printf("[KytyBridge] Stopping emulation session...\n");
    std::fflush(stdout);
    g_host_view = nullptr;
    g_is_running.store(false);

#if HAS_KYTY_CORE
    Common::HostException::SetEmulationShuttingDown(true);
    SDL_Event quit_ev;
    std::memset(&quit_ev, 0, sizeof(quit_ev));
    quit_ev.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit_ev);
#endif

    if (g_emulator_thread && g_emulator_thread->joinable()) {
        std::thread([t = std::move(g_emulator_thread)]() mutable {
            if (t && t->joinable()) {
                t->join();
            }
        }).detach();
    }
}

+ (void)setButton:(KytyButton)button pressed:(BOOL)pressed {
#if HAS_KYTY_CORE
    uint32_t pad_btn = 0;
    switch (button) {
        case KytyButtonCross:    pad_btn = Libs::Controller::PAD_BUTTON_CROSS; break;
        case KytyButtonCircle:   pad_btn = Libs::Controller::PAD_BUTTON_CIRCLE; break;
        case KytyButtonSquare:   pad_btn = Libs::Controller::PAD_BUTTON_SQUARE; break;
        case KytyButtonTriangle: pad_btn = Libs::Controller::PAD_BUTTON_TRIANGLE; break;
        case KytyButtonDpadUp:   pad_btn = Libs::Controller::PAD_BUTTON_UP; break;
        case KytyButtonDpadDown: pad_btn = Libs::Controller::PAD_BUTTON_DOWN; break;
        case KytyButtonDpadLeft: pad_btn = Libs::Controller::PAD_BUTTON_LEFT; break;
        case KytyButtonDpadRight:pad_btn = Libs::Controller::PAD_BUTTON_RIGHT; break;
        case KytyButtonL1:       pad_btn = Libs::Controller::PAD_BUTTON_L1; break;
        case KytyButtonR1:       pad_btn = Libs::Controller::PAD_BUTTON_R1; break;
        case KytyButtonL2:       pad_btn = Libs::Controller::PAD_BUTTON_L2; break;
        case KytyButtonR2:       pad_btn = Libs::Controller::PAD_BUTTON_R2; break;
        case KytyButtonL3:       pad_btn = Libs::Controller::PAD_BUTTON_L3; break;
        case KytyButtonR3:       pad_btn = Libs::Controller::PAD_BUTTON_R3; break;
        case KytyButtonOptions:  pad_btn = Libs::Controller::PAD_BUTTON_OPTIONS; break;
        case KytyButtonTouchpad: pad_btn = Libs::Controller::PAD_BUTTON_TOUCH_PAD; break;
        case KytyButtonShare:
        case KytyButtonPS:
            break;
    }
    if (pad_btn != 0) {
        std::printf("[KytyBridge] setButton:%d pressed:%d (pad_btn=0x%08x)\n", (int)button, (int)pressed, pad_btn);
        std::fflush(stdout);
        if (button == KytyButtonTouchpad) {
            Libs::Controller::SetTouchPad(Libs::Controller::HOST_INPUT_CONTROLLER_ID, 0, pressed, 0.5f, 0.5f);
        }
        Libs::Controller::SetButton(Libs::Controller::HOST_INPUT_CONTROLLER_ID, pad_btn, pressed);
    }
#endif
}

+ (void)setLeftStickX:(float)x y:(float)y {
#if HAS_KYTY_CORE
    int ix = std::clamp(static_cast<int>(128.0f + x * 127.0f), 0, 255);
    int iy = std::clamp(static_cast<int>(128.0f - y * 127.0f), 0, 255);
    Libs::Controller::SetAxis(Libs::Controller::HOST_INPUT_CONTROLLER_ID, Libs::Controller::Axis::LeftX, ix);
    Libs::Controller::SetAxis(Libs::Controller::HOST_INPUT_CONTROLLER_ID, Libs::Controller::Axis::LeftY, iy);
#endif
}

+ (void)setRightStickX:(float)x y:(float)y {
#if HAS_KYTY_CORE
    int ix = std::clamp(static_cast<int>(128.0f + x * 127.0f), 0, 255);
    int iy = std::clamp(static_cast<int>(128.0f - y * 127.0f), 0, 255);
    Libs::Controller::SetAxis(Libs::Controller::HOST_INPUT_CONTROLLER_ID, Libs::Controller::Axis::RightX, ix);
    Libs::Controller::SetAxis(Libs::Controller::HOST_INPUT_CONTROLLER_ID, Libs::Controller::Axis::RightY, iy);
#endif
}

+ (void)setTriggerL2:(float)l2 r2:(float)r2 {
#if HAS_KYTY_CORE
    int il2 = std::clamp(static_cast<int>(l2 * 255.0f), 0, 255);
    int ir2 = std::clamp(static_cast<int>(r2 * 255.0f), 0, 255);
    Libs::Controller::SetAxis(Libs::Controller::HOST_INPUT_CONTROLLER_ID, Libs::Controller::Axis::TriggerLeft, il2);
    Libs::Controller::SetAxis(Libs::Controller::HOST_INPUT_CONTROLLER_ID, Libs::Controller::Axis::TriggerRight, ir2);
#endif
}

+ (NSArray<NSString *> *)getRecentLogs {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_log_entries) {
        return @[];
    }
    return [g_log_entries copy];
}

+ (void)clearLogs {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    [g_log_entries removeAllObjects];
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSNotificationCenter defaultCenter] postNotificationName:KytyLogUpdatedNotification object:nil];
    });
}

+ (void)handleCrashWithReason:(NSString *)reason detail:(NSString *)detail {
    if (g_crash_reported.exchange(true)) {
        return;
    }

    g_is_running.store(false);
#if HAS_KYTY_CORE
    Common::HostException::SetEmulationShuttingDown(true);
#endif

    NSString *summary = [NSString stringWithFormat:@"%@\n%@", reason, detail ?: @""];
    
    // Trim summary for dialog representation to avoid UI stalls with large log payloads
    const NSUInteger kMaxDialogLength = 800;
    NSString *dialogSummary = summary;
    if (dialogSummary.length > kMaxDialogLength) {
        dialogSummary = [NSString stringWithFormat:@"%@\n\n... (log trimmed, tap 'View Logs' to view full report)",
                         [dialogSummary substringToIndex:kMaxDialogLength]];
    }
    g_last_crash_error = dialogSummary;

    NSString *fullMsg = [NSString stringWithFormat:
        @"\n---\n"
        @"KYTY EMULATION CRASH DETECTED\n"
        @"Reason: %@\n"
        @"Detail: %@\n"
        @"PID: %d | Host Thread: %p\n"
        @"---\n",
        reason, detail ?: @"", getpid(), (void*)pthread_self()];

    if (g_orig_stderr >= 0) {
        write(g_orig_stderr, [fullMsg UTF8String], [fullMsg lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
    } else {
        std::fprintf(stderr, "%s", [fullMsg UTF8String]);
        std::fflush(stderr);
    }

    if (g_persistent_log_file) {
        std::fputs([fullMsg UTF8String], g_persistent_log_file);
        std::fflush(g_persistent_log_file);
    }

    [self addLogMessageInternal:fullMsg writeToFile:NO];

#if HAS_KYTY_CORE
    SDL_Event quitEvt;
    std::memset(&quitEvt, 0, sizeof(quitEvt));
    quitEvt.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quitEvt);
#endif

    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSNotificationCenter defaultCenter] postNotificationName:KytyEmulationCrashedNotification
                                                            object:nil
                                                          userInfo:@{
                                                              KytyCrashErrorKey: dialogSummary,
                                                              KytyCrashDetailKey: detail ?: @"",
                                                              @"fullLog": fullMsg
                                                          }];
    });

    if (![NSThread isMainThread]) {
        pthread_exit(nullptr);
    }
}

+ (NSString *)logFilePath {
    if (!g_persistent_log_path) {
#if TARGET_OS_OSX
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
        if (paths.count > 0) {
            NSString *appSupportDir = [paths[0] stringByAppendingPathComponent:@"KytyPS5"];
            [[NSFileManager defaultManager] createDirectoryAtPath:appSupportDir
                                      withIntermediateDirectories:YES
                                                       attributes:nil
                                                            error:nil];
            g_persistent_log_path = [appSupportDir stringByAppendingPathComponent:@"kyty_last_run.log"];
        }
#else
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        if (paths.count > 0) {
            g_persistent_log_path = [paths[0] stringByAppendingPathComponent:@"kyty_last_run.log"];
        }
#endif
    }
    return g_persistent_log_path;
}

+ (NSURL *)logFileURL {
    NSString *path = [self logFilePath];
    return path ? [NSURL fileURLWithPath:path] : nil;
}

+ (NSString *)loadSavedLog {
    NSString *path = [self logFilePath];
    if (path && [[NSFileManager defaultManager] fileExistsAtPath:path]) {
        NSError *error = nil;
        NSString *content = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&error];
        if (content) {
            return content;
        }
    }
    return @"";
}

+ (nullable NSString *)lastCrashError {
    return g_last_crash_error;
}

+ (void)addLogMessage:(NSString *)message {
    [self addLogMessageInternal:message writeToFile:YES];
}

+ (void)addLogMessageInternal:(NSString *)message writeToFile:(BOOL)writeToFile {
    if (!message || message.length == 0) return;

    @autoreleasepool {
        if (writeToFile && g_log_file_writing_enabled.load() && g_persistent_log_file) {
            if (g_persistent_log_bytes_written.load() < kMaxPersistentLogBytes) {
                std::fputs([message UTF8String], g_persistent_log_file);
                std::fputc('\n', g_persistent_log_file);
                g_persistent_log_bytes_written.fetch_add([message lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1);
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_log_mutex);
            if (!g_log_entries) {
                g_log_entries = [[NSMutableArray alloc] initWithCapacity:kMaxLogEntries];
            }
            if (g_log_entries.count >= kMaxLogEntries) {
                [g_log_entries removeObjectAtIndex:0];
            }
            [g_log_entries addObject:message];
        }

        static std::atomic<bool> s_notification_pending{false};
        if (!s_notification_pending.exchange(true)) {
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(250 * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
                s_notification_pending.store(false);
                [[NSNotificationCenter defaultCenter] postNotificationName:KytyLogUpdatedNotification object:nil];
            });
        }
    }
}

+ (BOOL)isSDLGamepadSubsystemActive {
#if HAS_KYTY_CORE
    return SDL_WasInit(SDL_INIT_GAMEPAD) != 0;
#else
    return NO;
#endif
}

+ (NSArray<NSDictionary<NSString *, id> *> *)getConnectedControllers {
    NSMutableArray *list = [NSMutableArray array];
#if HAS_KYTY_CORE
    int count = 0;
    SDL_JoystickID *gamepads = SDL_GetGamepads(&count);
    if (gamepads) {
        for (int i = 0; i < count; i++) {
            SDL_JoystickID jid = gamepads[i];
            const char *name = SDL_GetGamepadNameForID(jid);
            SDL_GamepadType type = SDL_GetGamepadTypeForID(jid);
            NSString *typeStr = @"Generic Gamepad";
            switch (type) {
                case SDL_GAMEPAD_TYPE_PS5: typeStr = @"PlayStation 5 (DualSense)"; break;
                case SDL_GAMEPAD_TYPE_PS4: typeStr = @"PlayStation 4 (DualShock 4)"; break;
                case SDL_GAMEPAD_TYPE_PS3: typeStr = @"PlayStation 3 (DualShock 3)"; break;
                case SDL_GAMEPAD_TYPE_XBOX360:
                case SDL_GAMEPAD_TYPE_XBOXONE: typeStr = @"Xbox Controller"; break;
                case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
                case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: typeStr = @"Nintendo Switch Pro"; break;
                default: break;
            }
            [list addObject:@{
                @"id": @(jid),
                @"name": name ? [NSString stringWithUTF8String:name] : @"Gamepad",
                @"type": typeStr,
                @"source": @"SDL3 Gamepad"
            }];
        }
        SDL_free(gamepads);
    }
#endif
    for (GCController *gc in [GCController controllers]) {
        NSString *name = gc.vendorName ?: @"Wireless Gamepad";
        BOOL alreadyInList = NO;
        for (NSDictionary *d in list) {
            if ([d[@"name"] isEqualToString:name]) {
                alreadyInList = YES;
                break;
            }
        }
        if (!alreadyInList) {
            NSString *type = @"Apple MFi Controller";
            if ([name containsString:@"DualSense"] || [name containsString:@"PS5"]) {
                type = @"PlayStation 5 (DualSense)";
            } else if ([name containsString:@"DUALSHOCK"] || [name containsString:@"PS4"]) {
                type = @"PlayStation 4 (DualShock 4)";
            } else if ([name containsString:@"Xbox"]) {
                type = @"Xbox Wireless Controller";
            }
            [list addObject:@{
                @"id": @((NSInteger)gc.hash),
                @"name": name,
                @"type": type,
                @"source": @"Apple GameController"
            }];
        }
    }
    return list;
}

+ (void)setLogFileWritingEnabled:(BOOL)enabled {
    g_log_file_writing_enabled.store(enabled);
    [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:@"enableLogFileWriting"];
    if (!enabled && g_persistent_log_file) {
        std::fflush(g_persistent_log_file);
        fclose(g_persistent_log_file);
        g_persistent_log_file = nullptr;
    } else if (enabled && !g_persistent_log_file) {
        NSString *path = [self logFilePath];
        if (path) {
            g_persistent_log_file = fopen([path UTF8String], "a");
        }
    }
}

+ (BOOL)isLogFileWritingEnabled {
    return g_log_file_writing_enabled.load();
}

+ (BOOL)hasTrophiesForGamePath:(NSString *)gamePath {
    if (!gamePath || gamePath.length == 0) return NO;
    NSFileManager *fm = [NSFileManager defaultManager];
    NSArray<NSString *> *candidates = @[
        [gamePath stringByAppendingPathComponent:@"sce_sys/trophy2"],
        [gamePath stringByAppendingPathComponent:@"app0/sce_sys/trophy2"]
    ];
    for (NSString *dir in candidates) {
        BOOL isDir = NO;
        if ([fm fileExistsAtPath:dir isDirectory:&isDir] && isDir) {
            NSArray<NSString *> *contents = [fm contentsOfDirectoryAtPath:dir error:nil];
            for (NSString *file in contents) {
                if ([file hasPrefix:@"trophy"] && [file hasSuffix:@".ucp"]) {
                    return YES;
                }
            }
        }
    }
    return NO;
}

+ (NSArray<NSDictionary<NSString *, id> *> *)getTrophiesForGamePath:(NSString *)gamePath {
    NSMutableArray *result = [NSMutableArray array];
    if (!gamePath || gamePath.length == 0) return result;
#if HAS_KYTY_CORE
    try {
        NSFileManager *fm = [NSFileManager defaultManager];
        NSArray<NSString *> *candidates = @[
            [gamePath stringByAppendingPathComponent:@"sce_sys/trophy2"],
            [gamePath stringByAppendingPathComponent:@"app0/sce_sys/trophy2"]
        ];
        
        // Resolve console language according to Common::Trophies::locales:
        // 0: ja-JP, 1: en-US, 2: fr-FR, 3: es-ES, 4: de-DE, 5: it-IT, 6: nl-NL, 7: pt-PT,
        // 8: ru-RU, 9: ko-KR, 10: zh-Hant, 11: zh-Hans, 12: fi-FI, 13: sv-SE, 14: da-DK, 15: no-NO,
        // 16: pl-PL, 17: pt-BR, 18: en-GB, 19: tr-TR, 20: es-419, 21: ar-AE, 22: fr-CA, 23: cs-CZ,
        // 24: hu-HU, 25: el-GR, 26: ro-RO, 27: th-TH, 28: vi-VN, 29: id-ID
        int consoleLanguage = 1; // Default to en-US (English)
        NSString *preferredLang = [[NSLocale preferredLanguages] firstObject];
        if (preferredLang != nil) {
            if ([preferredLang hasPrefix:@"ja"]) consoleLanguage = 0;
            else if ([preferredLang hasPrefix:@"en-GB"]) consoleLanguage = 18;
            else if ([preferredLang hasPrefix:@"en"]) consoleLanguage = 1;
            else if ([preferredLang hasPrefix:@"fr-CA"]) consoleLanguage = 22;
            else if ([preferredLang hasPrefix:@"fr"]) consoleLanguage = 2;
            else if ([preferredLang hasPrefix:@"es-419"]) consoleLanguage = 20;
            else if ([preferredLang hasPrefix:@"es"]) consoleLanguage = 3;
            else if ([preferredLang hasPrefix:@"de"]) consoleLanguage = 4;
            else if ([preferredLang hasPrefix:@"it"]) consoleLanguage = 5;
            else if ([preferredLang hasPrefix:@"nl"]) consoleLanguage = 6;
            else if ([preferredLang hasPrefix:@"pt-BR"]) consoleLanguage = 17;
            else if ([preferredLang hasPrefix:@"pt"]) consoleLanguage = 7;
            else if ([preferredLang hasPrefix:@"ru"]) consoleLanguage = 8;
            else if ([preferredLang hasPrefix:@"ko"]) consoleLanguage = 9;
            else if ([preferredLang hasPrefix:@"zh-Hant"]) consoleLanguage = 10;
            else if ([preferredLang hasPrefix:@"zh-Hans"] || [preferredLang hasPrefix:@"zh"]) consoleLanguage = 11;
            else if ([preferredLang hasPrefix:@"fi"]) consoleLanguage = 12;
            else if ([preferredLang hasPrefix:@"sv"]) consoleLanguage = 13;
            else if ([preferredLang hasPrefix:@"da"]) consoleLanguage = 14;
            else if ([preferredLang hasPrefix:@"no"] || [preferredLang hasPrefix:@"nb"]) consoleLanguage = 15;
            else if ([preferredLang hasPrefix:@"pl"]) consoleLanguage = 16;
            else if ([preferredLang hasPrefix:@"tr"]) consoleLanguage = 19;
            else if ([preferredLang hasPrefix:@"ar"]) consoleLanguage = 21;
            else if ([preferredLang hasPrefix:@"cs"]) consoleLanguage = 23;
            else if ([preferredLang hasPrefix:@"hu"]) consoleLanguage = 24;
            else if ([preferredLang hasPrefix:@"el"]) consoleLanguage = 25;
            else if ([preferredLang hasPrefix:@"ro"]) consoleLanguage = 26;
            else if ([preferredLang hasPrefix:@"th"]) consoleLanguage = 27;
            else if ([preferredLang hasPrefix:@"vi"]) consoleLanguage = 28;
            else if ([preferredLang hasPrefix:@"id"]) consoleLanguage = 29;
        }
        
        for (NSString *dir in candidates) {
            BOOL isDir = NO;
            if ([fm fileExistsAtPath:dir isDirectory:&isDir] && isDir) {
                NSArray<NSString *> *contents = [fm contentsOfDirectoryAtPath:dir error:nil];
                for (NSString *file in contents) {
                    if ([file hasPrefix:@"trophy"] && [file hasSuffix:@".ucp"]) {
                        NSString *ucpPath = [dir stringByAppendingPathComponent:file];
                        auto pkg = Common::Trophies::LoadPackage(std::filesystem::path([ucpPath UTF8String]), consoleLanguage);
                        
                        for (const auto& [tid, trophy] : pkg.trophies) {
                            NSString *gradeStr = @"Bronze";
                            switch (trophy.grade) {
                                case 1: gradeStr = @"Platinum"; break;
                                case 2: gradeStr = @"Gold"; break;
                                case 3: gradeStr = @"Silver"; break;
                                case 4: gradeStr = @"Bronze"; break;
                                default: break;
                            }
                            
                            NSData *iconData = nil;
                            if (!trophy.icon_png.empty()) {
                                iconData = [NSData dataWithBytes:trophy.icon_png.data() length:trophy.icon_png.size()];
                            }
                            
                            NSMutableDictionary *dict = [NSMutableDictionary dictionary];
                            dict[@"id"] = @(trophy.id);
                            dict[@"name"] = trophy.name.empty() ? [NSString stringWithFormat:@"Trophy %d", trophy.id] : [NSString stringWithUTF8String:trophy.name.c_str()];
                            dict[@"description"] = [NSString stringWithUTF8String:trophy.description.c_str()];
                            dict[@"grade"] = gradeStr;
                            dict[@"hidden"] = @(trophy.hidden);
                            if (iconData) {
                                dict[@"iconData"] = iconData;
                            }
                            [result addObject:dict];
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::printf("[KytyBridge] Error loading trophies: %s\n", e.what());
    } catch (...) {
        std::printf("[KytyBridge] Unknown error loading trophies\n");
    }
#endif
    return result;
}

static size_t GetProcessPhysFootprintBytes() {
    task_vm_info_data_t vmInfo;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    kern_return_t kr = task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vmInfo, &count);
    if (kr == KERN_SUCCESS) {
        return (size_t)vmInfo.phys_footprint;
    }
    return 0;
}

static size_t GetOsAvailableMemoryBytes() {
#if TARGET_OS_IPHONE
    if (&os_proc_available_memory != NULL) {
        return os_proc_available_memory();
    }
#endif
    return 0;
}

static uint64_t g_last_memory_warning_mach_time = 0;
static NSString *g_last_memory_warning_level = nil;

+ (NSDictionary<NSString *, id> *)currentMemoryStats {
    size_t usedBytes = GetProcessPhysFootprintBytes();
    size_t availBytes = GetOsAvailableMemoryBytes();
    size_t totalDeviceBytes = (size_t)[NSProcessInfo processInfo].physicalMemory;
    
    double usedMB = (double)usedBytes / (1024.0 * 1024.0);
    double availMB = (double)availBytes / (1024.0 * 1024.0);
    double totalDeviceMB = (double)totalDeviceBytes / (1024.0 * 1024.0);
    double limitMB = (availBytes > 0) ? (usedMB + availMB) : totalDeviceMB;
    
    return @{
        KytyMemoryUsedMBKey: @(usedMB),
        KytyMemoryAvailMBKey: @(availMB),
        KytyMemoryLimitMBKey: @(limitMB),
        @"deviceMB": @(totalDeviceMB)
    };
}

+ (void)reportMemoryWarningWithLevel:(NSString *)level reason:(NSString *)reason {
    uint64_t now = mach_absolute_time();
    // Throttle duplicate warnings of same severity within ~3.0s
    if (g_last_memory_warning_level && [g_last_memory_warning_level isEqualToString:level]) {
        if (now - g_last_memory_warning_mach_time < 3000000000ULL) {
            return;
        }
    }
    g_last_memory_warning_mach_time = now;
    g_last_memory_warning_level = level;
    
    // Trim in-memory log entries immediately to relieve pressure
    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        if (g_log_entries && g_log_entries.count > 150) {
            [g_log_entries removeObjectsInRange:NSMakeRange(0, g_log_entries.count - 150)];
        }
    }
    
    NSDictionary *stats = [self currentMemoryStats];
    double usedMB = [stats[KytyMemoryUsedMBKey] doubleValue];
    double availMB = [stats[KytyMemoryAvailMBKey] doubleValue];
    double limitMB = [stats[KytyMemoryLimitMBKey] doubleValue];
    
    NSString *logLine = [NSString stringWithFormat:
        @"[Jetsam Warning] Level: %@ | Reason: %@ | Footprint: %.1f MB / %.1f MB (%.1f MB remaining)",
        level, reason, usedMB, limitMB, availMB];
    
    std::printf("%s\n", [logLine UTF8String]);
    std::fflush(stdout);
    [self addLogMessageInternal:logLine writeToFile:NO];
    
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSNotificationCenter defaultCenter] postNotificationName:KytyMemoryPressureNotification
                                                            object:nil
                                                          userInfo:@{
            KytyMemoryPressureLevelKey: level,
            KytyMemoryPressureReasonKey: reason,
            KytyMemoryUsedMBKey: @(usedMB),
            KytyMemoryAvailMBKey: @(availMB),
            KytyMemoryLimitMBKey: @(limitMB),
            @"message": [NSString stringWithFormat:@"Memory Pressure (%@): %.0f MB / %.0f MB (%.0f MB remaining)",
                         level, usedMB, limitMB, availMB]
        }];
    });
}

@end
