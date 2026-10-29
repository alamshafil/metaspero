#include "common/assert.h"

#include "common/logging/log.h"
#include "common/subsystems.h"
#include "kytyGitVersion.h"

#include <cstdio>
#include <cstdlib>
#include <fmt/format.h>
#include <string>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <TargetConditionals.h>
#include <execinfo.h>
#include <mach/mach.h>
#include <pthread.h>
#include <unistd.h>
#endif

namespace Common {

static FatalHandler g_fatal_handler = nullptr;
static std::string  g_last_fatal_message;

void SetFatalHandler(FatalHandler handler) {
	g_fatal_handler = handler;
}

void InvokeFatalHandler(int status, const char* message) {
	if (g_fatal_handler != nullptr) {
		g_fatal_handler(status, message);
	}
}

const char* GetLastFatalMessage() {
	return g_last_fatal_message.c_str();
}

static std::string BuildFatalReport(const char* title, std::string_view text, const char* file,
                                    int line) {
	std::string report = fmt::format("--- Build ---\n{}\n{}\n{} in {}:{}\n", KYTY_BUILD_LABEL, title, text, file,
	                   line);
#if defined(__APPLE__)
	void* callstack[64];
	int frames = backtrace(callstack, 64);
	char** strs = backtrace_symbols(callstack, frames);
	if (strs != nullptr) {
		report += "--- Host Call Stack ---\n";
		for (int i = 0; i < frames; ++i) {
			report += fmt::format("  [{}] {}\n", i, strs[i]);
		}
		free(strs);
	}
#endif
	return report;
}

static int DbgReport(const char* title, std::string_view text, const char* file, int line) {
	g_last_fatal_message = BuildFatalReport(title, text, file, line);
	Log::WriteFatal(g_last_fatal_message);
	Subsystems::EmergencyShutdownActive();
	InvokeFatalHandler(321, g_last_fatal_message.c_str());
	return 1;
}

int DbgExitIfHandler(const char* expr, const char* file, int line) {
	return DbgReport("--- Fatal Error ---", fmt::format("Error: condition ({}) is true", expr),
	                 file, line);
}

int DbgNotImplementedHandler(const char* expr, const char* file, int line) {
	return DbgReport("--- Fatal Error ---", fmt::format("Not implemented ({})", expr), file, line);
}

int DbgExitHandler(const char* file, int line, std::string_view text) {
	g_last_fatal_message = BuildFatalReport("--- Error ---", text, file, line);
	Log::WriteFatal(g_last_fatal_message);
	InvokeFatalHandler(321, g_last_fatal_message.c_str());
	return 1;
}

int DbgExitHandler(const char* file, int line, fmt::text_style style, std::string_view text) {
	g_last_fatal_message = BuildFatalReport("--- Error ---", text, file, line);
	Log::WriteFatal(style, g_last_fatal_message);
	InvokeFatalHandler(321, g_last_fatal_message.c_str());
	return 1;
}

void DbgExit(int status) {
	Subsystems::EmergencyShutdownActive();
	std::fflush(nullptr);
	InvokeFatalHandler(status, g_last_fatal_message.empty() ? "Fatal error: DbgExit called" : g_last_fatal_message.c_str());
#if defined(__APPLE__)
	if (pthread_main_np() == 0) {
		pthread_exit(nullptr);
	}
#if TARGET_OS_IPHONE
	while (true) {
		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, false);
	}
#else
	std::_Exit(status);
#endif
#else
	std::_Exit(status);
#endif
}

} // namespace Common
