// SPDX-License-Identifier: GPL-2.0-or-later
#import <Foundation/Foundation.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
typedef NSView PlatformView;
#else
#import <UIKit/UIKit.h>
typedef UIView PlatformView;
#endif

NS_ASSUME_NONNULL_BEGIN

extern NSString * const KytyLogUpdatedNotification;
extern NSString * const KytyEmulationCrashedNotification;
extern NSString * const KytyCrashErrorKey;
extern NSString * const KytyCrashDetailKey;
extern NSString * const KytyWindowDestroyedNotification;
extern NSString * const KytyMemoryPressureNotification;
extern NSString * const KytyMemoryPressureLevelKey;
extern NSString * const KytyMemoryPressureReasonKey;
extern NSString * const KytyMemoryUsedMBKey;
extern NSString * const KytyMemoryAvailMBKey;
extern NSString * const KytyMemoryLimitMBKey;

#ifdef __cplusplus
extern "C" {
#endif
void KytyNotifyWindowCreated(void * _Nullable uiwindow);
void KytyNotifyWindowDestroyed(void);
void * _Nullable KytyGetHostView(void);
void KytySetHostView(void * _Nullable view);
void * _Nullable KytyGetHostMetalLayer(void);
#ifdef __cplusplus
}
#endif

typedef NS_ENUM(NSInteger, KytyButton) {
    KytyButtonCross = 0,
    KytyButtonCircle,
    KytyButtonSquare,
    KytyButtonTriangle,
    KytyButtonDpadUp,
    KytyButtonDpadDown,
    KytyButtonDpadLeft,
    KytyButtonDpadRight,
    KytyButtonL1,
    KytyButtonR1,
    KytyButtonL2,
    KytyButtonR2,
    KytyButtonL3,
    KytyButtonR3,
    KytyButtonOptions,
    KytyButtonShare,
    KytyButtonTouchpad,
    KytyButtonPS
};

@interface KytyBridge : NSObject

/// Returns the current host process ID (crucial for attaching StikDebug)
+ (pid_t)processId;

/// Checks if a debugger is attached to this process (P_TRACED flag via sysctl)
+ (BOOL)isProcessDebugged;

/// Checks if executable JIT memory pool is verified and ready for execution
+ (BOOL)isJitReady;

/// Attempts to acquire and preallocate the JIT memory pool (64 MB)
+ (BOOL)acquireJitPool;

/// Checks if executable JIT memory is currently obtainable or already ready
+ (BOOL)isJitAvailable;

/// Forces a fresh re-probe of JIT capabilities and pool acquisition (e.g. after attaching StikDebug)
+ (BOOL)refreshJitStatus;

/// Opens StikDebug with bundle ID, PID, and universal.js parameters
+ (void)openStikDebug;

/// Detailed diagnostic status string of JIT and debugger attachment
+ (NSString *)jitStatusDescription;

/// Returns the dynamically detected or bundled MoltenVK version
+ (NSString *)moltenVkVersion;

/// Applies graphics debug flags (e.g. skip mesh shaders) based on user preferences
+ (void)applyGraphicsDebugSettings;

/// Controls Apple's Metal Performance HUD (FPS, GPU/CPU frame times, memory)
+ (void)setMetalHudEnabled:(BOOL)enabled;
+ (BOOL)isMetalHudEnabled;

/// Starts emulator execution with the specified game ELF, ZAR, or directory path
+ (void)startEmulationWithGamePath:(NSString *)gamePath
                            inView:(PlatformView *)renderView
                        completion:(void (^)(BOOL success, NSString * _Nullable error))completion;

/// Requests the running emulator to gracefully stop
+ (void)stopEmulation;

/// Reports the host Metal view's size in pixels so the renderer can resize its swapchain
+ (void)hostViewDidResizeToPixelWidth:(int)width height:(int)height;

/// Returns whether an emulation session is currently active
+ (BOOL)isEmulationRunning;

/// Dispatches a digital button press or release
+ (void)setButton:(KytyButton)button pressed:(BOOL)pressed;

/// Dispatches analog thumbstick coordinates (-1.0 to 1.0)
+ (void)setLeftStickX:(float)x y:(float)y;
+ (void)setRightStickX:(float)x y:(float)y;

/// Dispatches analog trigger values (0.0 to 1.0)
+ (void)setTriggerL2:(float)l2 r2:(float)r2;

/// Retrieves the in-memory buffered log lines (captured from std::printf, LOGF, and stderr)
+ (NSArray<NSString *> *)getRecentLogs;

/// Clears the in-memory log buffer
+ (void)clearLogs;

/// Appends a custom log line to the buffer
+ (void)addLogMessage:(NSString *)message;

/// Controller & SDL subsystem status
+ (BOOL)isSDLGamepadSubsystemActive;
+ (NSArray<NSDictionary<NSString *, id> *> *)getConnectedControllers;

/// Controls writing log output to persistent disk file (prevents excessive disk writes and reduces I/O pressure)
+ (void)setLogFileWritingEnabled:(BOOL)enabled;
+ (BOOL)isLogFileWritingEnabled;

/// Trophy data inspection
+ (BOOL)hasTrophiesForGamePath:(NSString *)gamePath;
+ (NSArray<NSDictionary<NSString *, id> *> *)getTrophiesForGamePath:(NSString *)gamePath;

/// Absolute path to the persistent log file on disk (Documents/kyty_last_run.log)
+ (NSString *)logFilePath;

/// File URL to the persistent log file (convenient for sharing / exporting)
+ (nullable NSURL *)logFileURL;

/// Reads and returns the complete text of the persistent log file
+ (NSString *)loadSavedLog;

/// Current physical memory statistics (usedMB, availMB, limitMB, deviceMB)
+ (NSDictionary<NSString *, id> *)currentMemoryStats;

/// Triggers and logs a memory warning / pressure event
+ (void)reportMemoryWarningWithLevel:(NSString *)level reason:(NSString *)reason;

/// The most recent crash error message (if any)
+ (nullable NSString *)lastCrashError;

/// Called by the crash / signal handler when an unhandled fault occurs
+ (void)handleCrashWithReason:(NSString *)reason detail:(NSString *)detail;

@end

NS_ASSUME_NONNULL_END
