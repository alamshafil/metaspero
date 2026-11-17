// SPDX-License-Identifier: GPL-2.0-or-later
#if defined(__ANDROID__)

#include "kyty_jni.h"
#include "emulator.h"
#include "common/hostException.h"
#include "common/trophies.h"
#include "libs/controller.h"
#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
#include "loader/fex/fexGuestEngine.h"
#endif

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>
#include <adrenotools/driver.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

#define LOG_TAG "Kyty-JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static std::atomic<ANativeWindow*> g_host_native_window{nullptr};
static std::thread g_emulator_thread;
static std::atomic<bool> g_is_running{false};
static std::atomic<bool> g_is_stopping{false};
static std::timed_mutex g_session_mutex;

static std::string g_custom_vulkan_path;
static void* g_custom_driver_handle = nullptr;
static void* g_adrenotools_handle = nullptr;
static PFN_vkGetInstanceProcAddr g_custom_vkGetInstanceProcAddr = nullptr;

extern "C" float KytyGetCurrentFps();

extern "C" void* KytyGetHostNativeWindow(void) {
    return g_host_native_window.load(std::memory_order_acquire);
}

extern "C" void KytySetHostNativeWindow(void* window) {
    g_host_native_window.store(static_cast<ANativeWindow*>(window), std::memory_order_release);
}

extern "C" void* KytyGetVkGetInstanceProcAddr(void) {
    if (g_custom_vkGetInstanceProcAddr != nullptr) {
        LOGI("KytyGetVkGetInstanceProcAddr returning custom driver proc_addr=%p", g_custom_vkGetInstanceProcAddr);
        return reinterpret_cast<void*>(g_custom_vkGetInstanceProcAddr);
    }
    return nullptr;
}

extern "C" const char* KytyGetCustomVulkanPath(void) {
    if (!g_custom_vulkan_path.empty()) {
        LOGI("KytyGetCustomVulkanPath returning custom driver path=%s", g_custom_vulkan_path.c_str());
        return g_custom_vulkan_path.c_str();
    }
    return nullptr;
}


static std::string g_files_dir;
static std::string g_cache_dir;

extern "C" {

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeInit(
    JNIEnv* env, jclass, jstring j_files_dir, jstring j_cache_dir) {
    if (j_files_dir != nullptr) {
        const char* fdir = env->GetStringUTFChars(j_files_dir, nullptr);
        if (fdir) {
            g_files_dir = fdir;
            setenv("HOME", fdir, 1);
            setenv("TMPDIR", fdir, 1);
            std::error_code ec;
            std::filesystem::create_directories(fdir, ec);
            chdir(fdir);
            env->ReleaseStringUTFChars(j_files_dir, fdir);
        }
    }
    if (j_cache_dir != nullptr) {
        const char* cdir = env->GetStringUTFChars(j_cache_dir, nullptr);
        if (cdir) {
            g_cache_dir = cdir;
            setenv("XDG_CACHE_HOME", cdir, 1);
            std::error_code ec;
            std::filesystem::create_directories(cdir, ec);
            env->ReleaseStringUTFChars(j_cache_dir, cdir);
        }
    }
    LOGI("NativeBridge initialized (files_dir: %s, cache_dir: %s)", g_files_dir.c_str(), g_cache_dir.c_str());
}

JNIEXPORT jboolean JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeSetCustomDriver(
    JNIEnv* env, jclass, jstring j_driver_dir, jstring j_driver_name, jstring j_hook_dir) {
    if (g_custom_driver_handle != nullptr) {
        dlclose(g_custom_driver_handle);
        g_custom_driver_handle = nullptr;
    }
    if (g_adrenotools_handle != nullptr) {
        dlclose(g_adrenotools_handle);
        g_adrenotools_handle = nullptr;
    }
    g_custom_vkGetInstanceProcAddr = nullptr;
    g_custom_vulkan_path.clear();

    if (j_driver_dir == nullptr || j_driver_name == nullptr) {
        LOGI("Custom GPU driver cleared: reverting to system default Vulkan");
        SDL_SetHint(SDL_HINT_VULKAN_LIBRARY, nullptr);
        return JNI_TRUE;
    }

    const char* driver_dir = env->GetStringUTFChars(j_driver_dir, nullptr);
    const char* driver_name = env->GetStringUTFChars(j_driver_name, nullptr);
    const char* hook_dir = j_hook_dir != nullptr ? env->GetStringUTFChars(j_hook_dir, nullptr) : nullptr;

    if (!driver_dir || !driver_name) {
        if (driver_dir) env->ReleaseStringUTFChars(j_driver_dir, driver_dir);
        if (driver_name) env->ReleaseStringUTFChars(j_driver_name, driver_name);
        if (hook_dir) env->ReleaseStringUTFChars(j_hook_dir, hook_dir);
        return JNI_FALSE;
    }

    std::string driver_dir_slash = driver_dir;
    if (!driver_dir_slash.empty() && driver_dir_slash.back() != '/') {
        driver_dir_slash += '/';
    }
    std::string full_path = driver_dir_slash + driver_name;
    LOGI("Configuring custom GPU driver: %s", full_path.c_str());

    // 1. Try adrenotools
    if (hook_dir != nullptr && driver_dir != nullptr && driver_name != nullptr) {
        LOGI("Attempting adrenotools_open_libvulkan with hook_dir=%s, driver_dir=%s, driver_name=%s",
             hook_dir, driver_dir_slash.c_str(), driver_name);
        void* hooked_vulkan = adrenotools_open_libvulkan(
            RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM,
            nullptr, hook_dir, driver_dir_slash.c_str(), driver_name, nullptr, nullptr);
        if (hooked_vulkan != nullptr) {
            g_custom_vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                dlsym(hooked_vulkan, "vkGetInstanceProcAddr"));
            if (g_custom_vkGetInstanceProcAddr != nullptr) {
                g_custom_driver_handle = hooked_vulkan;
                g_custom_vulkan_path = full_path;
                LOGI("Successfully loaded custom driver via adrenotools: proc_addr=%p",
                     g_custom_vkGetInstanceProcAddr);
            } else {
                LOGE("dlsym vkGetInstanceProcAddr failed on hooked_vulkan: %s", dlerror());
                dlclose(hooked_vulkan);
            }
        } else {
            LOGE("adrenotools_open_libvulkan failed for '%s'", full_path.c_str());
        }
    }

    if (g_custom_vkGetInstanceProcAddr != nullptr) {
        SDL_SetHint(SDL_HINT_VULKAN_LIBRARY, full_path.c_str());
    }

    env->ReleaseStringUTFChars(j_driver_dir, driver_dir);
    env->ReleaseStringUTFChars(j_driver_name, driver_name);
    if (hook_dir) env->ReleaseStringUTFChars(j_hook_dir, hook_dir);

    return g_custom_vkGetInstanceProcAddr != nullptr ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeSetSurface(
    JNIEnv* env, jclass, jobject surface) {
    if (surface == nullptr) {
        if (auto* old = g_host_native_window.exchange(nullptr)) {
            ANativeWindow_release(old);
        }
        return;
    }
    ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
    if (auto* old = g_host_native_window.exchange(win)) {
        ANativeWindow_release(old);
    }
    LOGI("Host surface set: %p", win);
}

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeDestroySurface(
    JNIEnv*, jclass) {
    if (auto* old = g_host_native_window.exchange(nullptr)) {
        ANativeWindow_release(old);
    }
    LOGI("Host surface destroyed");
}

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeSurfaceChanged(
    JNIEnv*, jclass, jint width, jint height) {
    if (width <= 0 || height <= 0) return;
    SDL_Event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    ev.window.data1 = width;
    ev.window.data2 = height;
    SDL_PushEvent(&ev);
}

JNIEXPORT jboolean JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeStartGame(
    JNIEnv* env, jclass, jstring j_game_path, jstring j_res, jstring j_present,
    jboolean j_skip_mesh_shaders, jboolean j_srt_arm64_jit) {
    if (g_is_running.load() && !g_is_stopping.load()) {
        return JNI_FALSE;
    }

    const char* path_chars = env->GetStringUTFChars(j_game_path, nullptr);
    std::string game_path = path_chars ? path_chars : "";
    if (path_chars) env->ReleaseStringUTFChars(j_game_path, path_chars);

    if (game_path.find("/tree/primary:") == 0) {
        game_path = "/storage/emulated/0/" + game_path.substr(strlen("/tree/primary:"));
    } else if (game_path.find("primary:") == 0) {
        game_path = "/storage/emulated/0/" + game_path.substr(strlen("primary:"));
    }

    std::string resolution = "720p";
    if (j_res != nullptr) {
        const char* res_chars = env->GetStringUTFChars(j_res, nullptr);
        if (res_chars) {
            resolution = res_chars;
            env->ReleaseStringUTFChars(j_res, res_chars);
        }
    }

    std::string present_mode = "Mailbox";
    if (j_present != nullptr) {
        const char* pres_chars = env->GetStringUTFChars(j_present, nullptr);
        if (pres_chars) {
            present_mode = pres_chars;
            env->ReleaseStringUTFChars(j_present, pres_chars);
        }
    }

    if (g_emulator_thread.joinable()) {
        g_emulator_thread.join();
    }

    if (j_skip_mesh_shaders == JNI_TRUE) {
        setenv("KYTY_SKIP_MESH_SHADERS", "1", 1);
        LOGI("Graphics Debug: Skip Mesh Shaders ENABLED");
    } else {
        unsetenv("KYTY_SKIP_MESH_SHADERS");
    }

    if (j_srt_arm64_jit == JNI_TRUE) {
        setenv("KYTY_SRT_ARM64_JIT", "1", 1);
        LOGI("Graphics Debug: ARM64 SRT Walker JIT ENABLED (experimental)");
    } else {
        unsetenv("KYTY_SRT_ARM64_JIT");
    }

    g_is_running.store(true);
    g_is_stopping.store(false);

    g_emulator_thread = std::thread([game_path, resolution, present_mode]() {
        std::unique_lock<std::timed_mutex> session_lock(g_session_mutex, std::chrono::seconds(10));
        if (!session_lock.owns_lock()) {
            LOGE("Failed to acquire session lock within 10s");
            g_is_running.store(false);
            g_is_stopping.store(false);
            return;
        }

        Common::HostException::SetEmulationShuttingDown(false);

        if (!g_files_dir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(g_files_dir, ec);
            chdir(g_files_dir.c_str());
        }

        Emulator::RunOptions options;
        std::filesystem::path p(game_path);

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

        options.config.printf_direction = Config::LogDirection::Silent;

        if (resolution == "480p") {
            options.config.screen_width  = 854;
            options.config.screen_height = 480;
        } else if (resolution == "1080p") {
            options.config.screen_width  = 1920;
            options.config.screen_height = 1080;
        } else {
            options.config.screen_width  = 1280;
            options.config.screen_height = 720;
        }

        if (present_mode == "Fifo") {
            options.config.present_mode = Config::PresentMode::Fifo;
        } else if (present_mode == "Immediate") {
            options.config.present_mode = Config::PresentMode::Immediate;
        } else {
            options.config.present_mode = Config::PresentMode::Mailbox;
        }

        options.config.user_name        = "Kyty";
        options.config.user_id          = Config::DEFAULT_USER_ID;
        options.config.console_language = Config::DEFAULT_CONSOLE_LANGUAGE;
        options.config.vblank_frequency = 60;

        LOGI("Starting Emulator::Run for %s", game_path.c_str());
        Emulator::Run(options);
        LOGI("Emulator::Run exited cleanly");

        g_is_running.store(false);
        g_is_stopping.store(false);
    });

    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeStopGame(
    JNIEnv*, jclass) {
    if (!g_is_running.load()) return;
    g_is_stopping.store(true);
    LOGI("Stopping emulation session...");

    Common::HostException::SetEmulationShuttingDown(true);
    SDL_Event quit_ev;
    std::memset(&quit_ev, 0, sizeof(quit_ev));
    quit_ev.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit_ev);

#if defined(KYTY_ENABLE_FEX_GUEST_CPU)
    Loader::Fex::FexGuestEngine::Instance().StopAllThreads();
#endif

    if (g_emulator_thread.joinable()) {
        g_emulator_thread.join();
    }
    g_is_running.store(false);
    g_is_stopping.store(false);
}

JNIEXPORT jboolean JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeIsRunning(
    JNIEnv*, jclass) {
    return g_is_running.load() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeGetVersion(
    JNIEnv* env, jclass) {
    return env->NewStringUTF(KYTY_VERSION);
}

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeSendButton(
    JNIEnv*, jclass, jint button, jboolean pressed) {
    uint32_t pad_btn = 0;
    switch (button) {
        case 0: pad_btn = Libs::Controller::PAD_BUTTON_CROSS; break;
        case 1: pad_btn = Libs::Controller::PAD_BUTTON_CIRCLE; break;
        case 2: pad_btn = Libs::Controller::PAD_BUTTON_SQUARE; break;
        case 3: pad_btn = Libs::Controller::PAD_BUTTON_TRIANGLE; break;
        case 4: pad_btn = Libs::Controller::PAD_BUTTON_UP; break;
        case 5: pad_btn = Libs::Controller::PAD_BUTTON_DOWN; break;
        case 6: pad_btn = Libs::Controller::PAD_BUTTON_LEFT; break;
        case 7: pad_btn = Libs::Controller::PAD_BUTTON_RIGHT; break;
        case 8: pad_btn = Libs::Controller::PAD_BUTTON_L1; break;
        case 9: pad_btn = Libs::Controller::PAD_BUTTON_R1; break;
        case 10: pad_btn = Libs::Controller::PAD_BUTTON_L2; break;
        case 11: pad_btn = Libs::Controller::PAD_BUTTON_R2; break;
        case 12: pad_btn = Libs::Controller::PAD_BUTTON_L3; break;
        case 13: pad_btn = Libs::Controller::PAD_BUTTON_R3; break;
        case 14: pad_btn = Libs::Controller::PAD_BUTTON_OPTIONS; break;
        case 15: pad_btn = Libs::Controller::PAD_BUTTON_TOUCH_PAD; break;
        default: break;
    }
    if (pad_btn != 0) {
        if (button == 15) {
            Libs::Controller::SetTouchPad(Libs::Controller::HOST_INPUT_CONTROLLER_ID, 0, pressed, 0.5f, 0.5f);
        }
        Libs::Controller::SetButton(Libs::Controller::HOST_INPUT_CONTROLLER_ID, pad_btn, pressed);
    }
}

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeSendStick(
    JNIEnv*, jclass, jint stick, jfloat x, jfloat y) {
    int ix = std::clamp(static_cast<int>(128.0f + x * 127.0f), 0, 255);
    int iy = std::clamp(static_cast<int>(128.0f - y * 127.0f), 0, 255);
    if (stick == 0) { // Left stick
        Libs::Controller::SetAxis(Libs::Controller::HOST_INPUT_CONTROLLER_ID, Libs::Controller::Axis::LeftX, ix);
        Libs::Controller::SetAxis(Libs::Controller::HOST_INPUT_CONTROLLER_ID, Libs::Controller::Axis::LeftY, iy);
    } else { // Right stick
        Libs::Controller::SetAxis(Libs::Controller::HOST_INPUT_CONTROLLER_ID, Libs::Controller::Axis::RightX, ix);
        Libs::Controller::SetAxis(Libs::Controller::HOST_INPUT_CONTROLLER_ID, Libs::Controller::Axis::RightY, iy);
    }
}

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeSendTrigger(
    JNIEnv*, jclass, jint trigger, jfloat value) {
    int iv = std::clamp(static_cast<int>(value * 255.0f), 0, 255);
    if (trigger == 0) { // L2
        Libs::Controller::SetAxis(Libs::Controller::HOST_INPUT_CONTROLLER_ID, Libs::Controller::Axis::TriggerLeft, iv);
    } else { // R2
        Libs::Controller::SetAxis(Libs::Controller::HOST_INPUT_CONTROLLER_ID, Libs::Controller::Axis::TriggerRight, iv);
    }
}

JNIEXPORT void JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeSendTouch(
    JNIEnv*, jclass, jint finger, jboolean down, jfloat x, jfloat y) {
    Libs::Controller::SetTouchPad(Libs::Controller::HOST_INPUT_CONTROLLER_ID, finger, down, x, y);
}

JNIEXPORT jfloat JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeGetFps(JNIEnv*, jclass) {
    return KytyGetCurrentFps();
}

JNIEXPORT jfloat JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeGetFrameTimeMs(JNIEnv*, jclass) {
    float fps = KytyGetCurrentFps();
    return fps > 0.01f ? (1000.0f / fps) : 0.0f;
}

static int ResolveConsoleLanguage(const std::string& tag) {
    if (tag.rfind("ja", 0) == 0) return 0;
    if (tag.rfind("en-GB", 0) == 0) return 18;
    if (tag.rfind("en", 0) == 0) return 1;
    if (tag.rfind("fr-CA", 0) == 0) return 22;
    if (tag.rfind("fr", 0) == 0) return 2;
    if (tag.rfind("es-419", 0) == 0) return 20;
    if (tag.rfind("es", 0) == 0) return 3;
    if (tag.rfind("de", 0) == 0) return 4;
    if (tag.rfind("it", 0) == 0) return 5;
    if (tag.rfind("nl", 0) == 0) return 6;
    if (tag.rfind("pt-BR", 0) == 0) return 17;
    if (tag.rfind("pt", 0) == 0) return 7;
    if (tag.rfind("ru", 0) == 0) return 8;
    if (tag.rfind("ko", 0) == 0) return 9;
    if (tag.rfind("zh-Hant", 0) == 0 || tag.rfind("zh-TW", 0) == 0 || tag.rfind("zh-HK", 0) == 0) return 10;
    if (tag.rfind("zh", 0) == 0) return 11;
    if (tag.rfind("fi", 0) == 0) return 12;
    if (tag.rfind("sv", 0) == 0) return 13;
    if (tag.rfind("da", 0) == 0) return 14;
    if (tag.rfind("no", 0) == 0 || tag.rfind("nb", 0) == 0) return 15;
    if (tag.rfind("pl", 0) == 0) return 16;
    if (tag.rfind("tr", 0) == 0) return 19;
    if (tag.rfind("ar", 0) == 0) return 21;
    if (tag.rfind("cs", 0) == 0) return 23;
    if (tag.rfind("hu", 0) == 0) return 24;
    if (tag.rfind("el", 0) == 0) return 25;
    if (tag.rfind("ro", 0) == 0) return 26;
    if (tag.rfind("th", 0) == 0) return 27;
    if (tag.rfind("vi", 0) == 0) return 28;
    if (tag.rfind("id", 0) == 0) return 29;
    return 1;
}

JNIEXPORT jboolean JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeHasTrophies(
    JNIEnv* env, jclass, jstring j_game_path) {
    if (!j_game_path) return JNI_FALSE;
    const char* path_str = env->GetStringUTFChars(j_game_path, nullptr);
    if (!path_str) return JNI_FALSE;
    std::string game_path(path_str);
    env->ReleaseStringUTFChars(j_game_path, path_str);
    if (game_path.empty()) return JNI_FALSE;

    std::error_code ec;
    std::filesystem::path root(game_path);
    if (std::filesystem::is_regular_file(root, ec)) {
        root = root.parent_path();
    }

    std::vector<std::filesystem::path> candidates = {
        root / "sce_sys" / "trophy2",
        root / "app0" / "sce_sys" / "trophy2",
        root.parent_path() / "sce_sys" / "trophy2"
    };

    for (const auto& dir : candidates) {
        if (std::filesystem::is_directory(dir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (entry.is_regular_file(ec)) {
                    std::string filename = entry.path().filename().string();
                    if (filename.rfind("trophy", 0) == 0 &&
                        filename.size() >= 4 &&
                        filename.compare(filename.size() - 4, 4, ".ucp") == 0) {
                        return JNI_TRUE;
                    }
                }
            }
        }
    }
    return JNI_FALSE;
}

JNIEXPORT jobjectArray JNICALL Java_com_alamshafil_ps5emu_NativeBridge_nativeGetTrophies(
    JNIEnv* env, jclass, jstring j_game_path, jstring j_lang_tag, jboolean j_include_icons) {
    if (!j_game_path) return nullptr;
    const char* path_str = env->GetStringUTFChars(j_game_path, nullptr);
    if (!path_str) return nullptr;
    std::string game_path(path_str);
    env->ReleaseStringUTFChars(j_game_path, path_str);
    if (game_path.empty()) return nullptr;

    bool include_icons = (j_include_icons != JNI_FALSE);

    std::string lang_tag = "en-US";
    if (j_lang_tag) {
        const char* lang_str = env->GetStringUTFChars(j_lang_tag, nullptr);
        if (lang_str) {
            lang_tag = lang_str;
            env->ReleaseStringUTFChars(j_lang_tag, lang_str);
        }
    }
    int consoleLanguage = ResolveConsoleLanguage(lang_tag);

    jclass trophyClass = env->FindClass("com/alamshafil/ps5emu/NativeTrophy");
    if (!trophyClass) {
        LOGE("Could not find class com/alamshafil/ps5emu/NativeTrophy");
        return nullptr;
    }
    jmethodID trophyCtor = env->GetMethodID(trophyClass, "<init>", "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z[B)V");
    if (!trophyCtor) {
        LOGE("Could not find constructor for com/alamshafil/ps5emu/NativeTrophy");
        return nullptr;
    }

    std::error_code ec;
    std::filesystem::path root(game_path);
    if (std::filesystem::is_regular_file(root, ec)) {
        root = root.parent_path();
    }

    std::vector<std::filesystem::path> candidates = {
        root / "sce_sys" / "trophy2",
        root / "app0" / "sce_sys" / "trophy2",
        root.parent_path() / "sce_sys" / "trophy2"
    };

    struct ParsedTrophy {
        int id;
        std::string name;
        std::string description;
        std::string grade;
        bool hidden;
        std::vector<std::byte> icon;
    };
    std::vector<ParsedTrophy> allTrophies;

    for (const auto& dir : candidates) {
        if (std::filesystem::is_directory(dir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (entry.is_regular_file(ec)) {
                    std::string filename = entry.path().filename().string();
                    if (filename.rfind("trophy", 0) == 0 &&
                        filename.size() >= 4 &&
                        filename.compare(filename.size() - 4, 4, ".ucp") == 0) {
                        auto pkg = Common::Trophies::LoadPackage(entry.path(), consoleLanguage);
                        for (auto& [tid, trophy] : pkg.trophies) {
                            std::string gradeStr = "Bronze";
                            switch (trophy.grade) {
                                case 1: gradeStr = "Platinum"; break;
                                case 2: gradeStr = "Gold"; break;
                                case 3: gradeStr = "Silver"; break;
                                case 4: gradeStr = "Bronze"; break;
                                default: break;
                            }
                            std::string name = trophy.name.empty()
                                ? ("Trophy " + std::to_string(trophy.id))
                                : std::move(trophy.name);
                            std::vector<std::byte> iconData;
                            if (include_icons && !trophy.icon_png.empty()) {
                                iconData = std::move(trophy.icon_png);
                            }
                            allTrophies.push_back({
                                trophy.id,
                                std::move(name),
                                std::move(trophy.description),
                                std::move(gradeStr),
                                trophy.hidden,
                                std::move(iconData)
                            });
                        }
                    }
                }
            }
        }
    }

    jobjectArray result = env->NewObjectArray(static_cast<jsize>(allTrophies.size()), trophyClass, nullptr);
    if (!result) return nullptr;

    for (jsize i = 0; i < static_cast<jsize>(allTrophies.size()); ++i) {
        const auto& t = allTrophies[i];
        jstring j_name = env->NewStringUTF(t.name.c_str());
        jstring j_desc = env->NewStringUTF(t.description.c_str());
        jstring j_grade = env->NewStringUTF(t.grade.c_str());
        jbyteArray j_icon = nullptr;
        if (!t.icon.empty()) {
            j_icon = env->NewByteArray(static_cast<jsize>(t.icon.size()));
            if (j_icon) {
                env->SetByteArrayRegion(j_icon, 0, static_cast<jsize>(t.icon.size()),
                                       reinterpret_cast<const jbyte*>(t.icon.data()));
            }
        }
        jobject obj = env->NewObject(trophyClass, trophyCtor,
                                     static_cast<jint>(t.id),
                                     j_name,
                                     j_desc,
                                     j_grade,
                                     static_cast<jboolean>(t.hidden ? JNI_TRUE : JNI_FALSE),
                                     j_icon);
        env->SetObjectArrayElement(result, i, obj);
        env->DeleteLocalRef(j_name);
        env->DeleteLocalRef(j_desc);
        env->DeleteLocalRef(j_grade);
        if (j_icon) env->DeleteLocalRef(j_icon);
        env->DeleteLocalRef(obj);
    }

    return result;
}

// Stubs for non-PIC FFmpeg NEON audio transforms in libavutil (tx_float_neon.S)
// that prevent tx_float_neon.o from being pulled into shared libraries on AArch64.
void ff_tx_fft16_float_neon() {}
void ff_tx_fft16_ns_float_neon() {}
void ff_tx_fft2_float_neon() {}
void ff_tx_fft32_float_neon() {}
void ff_tx_fft32_ns_float_neon() {}
void ff_tx_fft4_fwd_float_neon() {}
void ff_tx_fft4_inv_float_neon() {}
void ff_tx_fft8_float_neon() {}
void ff_tx_fft8_ns_float_neon() {}
void ff_tx_fft_sr_float_neon() {}
void ff_tx_fft_sr_ns_float_neon() {}

} // extern "C"

#endif // defined(__ANDROID__)
