# SwiftUI app for iOS and native macOS. Added from the top-level CMakeLists.txt when KYTY_BUILD_IOS_APP
# or KYTY_BUILD_SWIFTUI_APP are set. It links the kyty_core library target so all transitive dependencies
# (SDL3, SPIRV, FFmpeg, FEX, ...) come from CMake.

set(KYTY_APP_BUNDLE_ID "com.alamshafil.ps5emu" CACHE STRING "App bundle identifier")
set(KYTY_IOS_TEAM_ID "" CACHE STRING "Apple development team ID used for Xcode code signing (empty = unset)")
set(KYTY_APP_VERSION "0.1.0" CACHE STRING "SwiftUI app version")
set(KYTY_APP_BUILD "1" CACHE STRING "SwiftUI app build number")

enable_language(Swift OBJC OBJCXX)
set(CMAKE_Swift_LANGUAGE_VERSION 5)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SWIFT_SOURCES
    KytyApp.swift
    Services/CompatibilityService.swift
    Views/ContentView.swift
    Views/LibraryView.swift
    Views/GameDetailsView.swift
    Views/TrophiesView.swift
    Views/TrophyOverviewView.swift
    Views/InputMappingView.swift
    Views/AboutView.swift
    Views/EmulationView.swift
    Views/TouchControlsView.swift
    Views/SettingsView.swift
    Views/LogView.swift
    Views/MemoryWarningToastView.swift
    Bridge/KytyBridge.h
    Bridge/KytyBridge.mm
    Icon.icon
)

set_source_files_properties(Icon.icon PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources
)

if(IOS)
    list(APPEND SWIFT_SOURCES LaunchScreen.storyboard)
endif()

add_executable(Metaspero
    ${SWIFT_SOURCES}
)
set_target_properties(Metaspero PROPERTIES FOLDER "Application")

target_compile_options(Metaspero PRIVATE
    "$<$<COMPILE_LANGUAGE:Swift>:-import-objc-header;${CMAKE_CURRENT_SOURCE_DIR}/Bridge/KytyBridge.h>"
)

set(KYTY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

target_include_directories(Metaspero PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Bridge"
    "${KYTY_ROOT}/3rdparty/magic_enum/include/magic_enum"
    "${KYTY_ROOT}/3rdparty/magic_enum/include"
    "${KYTY_ROOT}/3rdparty/xxhash"
    "${KYTY_ROOT}/3rdparty/cpuinfo/include"
    "${KYTY_ROOT}/3rdparty/renderdoc"
    "${KYTY_ROOT}/3rdparty/stb"
)

target_compile_definitions(Metaspero PRIVATE
    HAS_KYTY_CORE=1
)

set(COMMON_FRAMEWORKS
    "-framework SwiftUI"
    "-framework Metal"
    "-framework MetalKit"
    "-framework GameController"
    "-framework Foundation"
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework AVFoundation"
    "-framework CoreBluetooth"
    "-framework CoreGraphics"
    "-framework QuartzCore"
    "-framework VideoToolbox"
    "-framework Security"
    "-framework CoreFoundation"
    "-framework CoreHaptics"
    "-liconv"
    "-lpthread"
    "-lm"
)

if(IOS)
    target_link_libraries(Metaspero PRIVATE
        kyty_core
        ${COMMON_FRAMEWORKS}
        "-framework UIKit"
        "-framework CoreMotion"
        "-framework OpenGLES"
    )
    set_target_properties(Metaspero PROPERTIES
        XCODE_GENERATE_SCHEME ON
        OUTPUT_NAME "Metaspero"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Metaspero"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${KYTY_APP_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${KYTY_APP_BUILD}"
        XCODE_ATTRIBUTE_MARKETING_VERSION "${KYTY_APP_VERSION}"
        XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${KYTY_APP_BUILD}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${KYTY_APP_BUNDLE_ID}"
        XCODE_ATTRIBUTE_PRODUCT_NAME "Metaspero"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "Icon"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/Bridge/KytyBridge.h"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${KYTY_IOS_MIN_VERSION}"
        XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/ios_jit.entitlements"
        XCODE_EMBED_FRAMEWORKS "${KYTY_MOLTENVK_FRAMEWORK}"
        XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY ON
        XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY ON
    )
    if(KYTY_IOS_TEAM_ID)
        set_target_properties(Metaspero PROPERTIES XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${KYTY_IOS_TEAM_ID}")
    endif()
else()
    include("${CMAKE_SOURCE_DIR}/cmake/MoltenVKMacOS.cmake")
    target_link_libraries(Metaspero PRIVATE
        kyty_core
        ${COMMON_FRAMEWORKS}
        "-framework AppKit"
    )
    set_target_properties(Metaspero PROPERTIES
        XCODE_GENERATE_SCHEME ON
        OUTPUT_NAME "Metaspero"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Metaspero"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${KYTY_APP_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${KYTY_APP_BUILD}"
        XCODE_ATTRIBUTE_MARKETING_VERSION "${KYTY_APP_VERSION}"
        XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${KYTY_APP_BUILD}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${KYTY_APP_BUNDLE_ID}"
        XCODE_ATTRIBUTE_PRODUCT_NAME "Metaspero"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "Icon"
        XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
        XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/Bridge/KytyBridge.h"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/macos_jit.entitlements"
        XCODE_ATTRIBUTE_COMBINE_HIDPI_IMAGES "YES"
        XCODE_EMBED_FRAMEWORKS "${KYTY_MOLTENVK_FRAMEWORK}"
        XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY ON
        XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY ON
    )
    if(KYTY_MOLTENVK_DYLIB)
        add_custom_command(TARGET Metaspero POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Metaspero>/../Frameworks"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${KYTY_MOLTENVK_DYLIB}"
                "$<TARGET_FILE_DIR:Metaspero>/../Frameworks/libMoltenVK.dylib"
            COMMAND codesign -s - --force "$<TARGET_FILE_DIR:Metaspero>/../Frameworks/libMoltenVK.dylib"
            COMMENT "Copy and codesign libMoltenVK.dylib into Metaspero.app/Contents/Frameworks"
        )
    endif()
endif()

if(NOT CMAKE_GENERATOR MATCHES "Xcode")
    find_program(ACTOOL_EXECUTABLE actool)
    if(ACTOOL_EXECUTABLE)
        if(IOS)
            set(ACTOOL_PLATFORM "iphoneos")
            set(ACTOOL_TARGET "iphone")
            set(ACTOOL_MIN_VER "${KYTY_IOS_MIN_VERSION}")
            set(ACTOOL_OUT_DIR "$<TARGET_FILE_DIR:Metaspero>")
        else()
            set(ACTOOL_PLATFORM "macosx")
            set(ACTOOL_TARGET "mac")
            set(ACTOOL_MIN_VER "15.0")
            set(ACTOOL_OUT_DIR "$<TARGET_FILE_DIR:Metaspero>/../Resources")
        endif()
        add_custom_command(TARGET Metaspero POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${ACTOOL_OUT_DIR}"
            COMMAND ${ACTOOL_EXECUTABLE}
                --output-format human-readable-text
                --compile "${ACTOOL_OUT_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/Icon.icon"
                --platform "${ACTOOL_PLATFORM}"
                --target-device "${ACTOOL_TARGET}"
                --minimum-deployment-target "${ACTOOL_MIN_VER}"
                --app-icon Icon
                --output-partial-info-plist "${ACTOOL_OUT_DIR}/icon-partial.plist"
            COMMENT "Compiling Icon.icon into app bundle resources"
        )
    endif()
endif()
