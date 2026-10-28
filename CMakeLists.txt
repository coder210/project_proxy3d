# ============================================================
# 独立仓库模式：本文件作为仓库根 CMakeLists 时，自行构建第三方库。
# 被上层根 CMake add_subdirectory 时整段跳过。
# 引擎以 submodule 形式位于 third_party/joy（内含 joy/ SDL/ msgpack/ spine/）。
# ============================================================
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(project_proxy3d C CXX)

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE INTERNAL "")

    if ((APPLE AND NOT CMAKE_SYSTEM_NAME MATCHES "Darwin") OR EMSCRIPTEN)
        set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
        set(SDL_SHARED OFF)
    else()
        set(SDL_SHARED ON)
    endif()

    if(MSVC)
        if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
            add_definitions(/MP)
        endif()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    set(JOY_REPO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/joy")
    add_subdirectory("${JOY_REPO_DIR}/msgpack" "${CMAKE_BINARY_DIR}/msgpack")
    add_subdirectory("${JOY_REPO_DIR}/spine"   "${CMAKE_BINARY_DIR}/spine")
    add_subdirectory("${JOY_REPO_DIR}/SDL"     "${CMAKE_BINARY_DIR}/SDL")
endif()

# 引擎路径：独立仓库用 third_party/joy，单仓用 <根>/joy
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/joy/joy/joy_add.cmake")
    set(JOY_ADD_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/joy/joy/joy_add.cmake")
else()
    set(JOY_ADD_CMAKE "${CMAKE_SOURCE_DIR}/joy/joy_add.cmake")
endif()

# 可执行文件名
set(EXECUTABLE_NAME "project_proxy3d")
message(STATUS "EXECUTABLE_NAME = '${EXECUTABLE_NAME}'")

# 如果是 Android，必须命名为 main
if (ANDROID)
    set(MOBILE_ASSETS_DIR "${MOBILE_MAIN_DIR}/assets")
    message(STATUS "MOBILE_ASSETS_DIR = '${MOBILE_ASSETS_DIR}'")
    set(EXECUTABLE_NAME main)
    add_library(${EXECUTABLE_NAME} SHARED)
else()
    add_executable(${EXECUTABLE_NAME})
endif()

file(GLOB SRC_FILES CONFIGURE_DEPENDS "*.h"  "*.c" "*.hpp" "*.cpp" "scenes/*.h" "scenes/*.cpp" "layers/*.h" "layers/*.cpp" "systems/*.h" "systems/*.cpp" "passes/*.h" "passes/*.cpp" "components/*.h" "components/*.cpp" "network/*.h" "network/*.cpp" "*.h" "shared/*.h" "shared/components/*.h" "shared/systems/*.h")

# 合并所有源文件
set(PROJECT_FILES
    ${SRC_FILES}
)

# 添加源文件到目标
target_sources(${EXECUTABLE_NAME}
    PRIVATE
        ${PROJECT_FILES}
        iosLaunchScreen.storyboard
)

# Windows 平台嵌入 .exe 图标
if(WIN32)
    target_sources(${EXECUTABLE_NAME} PRIVATE app_icon.rc)
endif()

# Apple 平台额外图标
if(APPLE)
    add_definitions(-DLUA_USE_IOS)
    target_sources(${EXECUTABLE_NAME} PRIVATE logo.png)
endif()

# 设置语言标准
set_target_properties(${EXECUTABLE_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# MSVC UTF-8 编码支持（防止中文注释导致解析错误）
if(MSVC)
    target_compile_options(${EXECUTABLE_NAME} PRIVATE /utf-8)
endif()

# Web 平台输出 HTML
if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html" CACHE INTERNAL "")
endif()

# ---------- Headless 逻辑系统性能基准（独立 target，不进入主 GLOB） ----------
option(BUILD_BENCH_LOGIC "Build headless logic-system benchmark (bench_logic)" OFF)
if(BUILD_BENCH_LOGIC)
    add_executable(bench_logic bench/bench_logic.cpp)
    include(${JOY_ADD_CMAKE})
    joy_add_sources(bench_logic)
    target_include_directories(bench_logic PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/shared")
    if(MSVC)
        target_compile_options(bench_logic PRIVATE /utf-8)
    endif()
    # 共享系统头文件使用内联变量/结构化绑定等 C++17 特性
    target_compile_features(bench_logic PRIVATE cxx_std_17)
endif()

# 引入 joy 框架源码（纯文件引入，不再构建独立库 target）
include(${JOY_ADD_CMAKE})
joy_add_sources(${EXECUTABLE_NAME})

# 添加 shared 共享目录 和 project_proxy3d 自身目录
target_include_directories(${EXECUTABLE_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/shared")
target_include_directories(${EXECUTABLE_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# ------------------- 资产处理 -------------------
set(ASSETS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/assets")

# 递归搜索资产文件（排除版本控制目录）
file(GLOB_RECURSE ALL_ASSET_FILES "${ASSETS_ROOT}/*")
set(ASSET_FILES "")
foreach(file ${ALL_ASSET_FILES})
    if(NOT file MATCHES "\\.svn/" AND NOT file MATCHES "\\.git/")
        file(RELATIVE_PATH rel_path "${ASSETS_ROOT}" "${file}")
        list(APPEND ASSET_FILES "${rel_path}")
    endif()
endforeach()

# 根据不同平台处理资产
if (APPLE)
    macro(add_resource FILE)
        file(RELATIVE_PATH relpath "${ASSETS_ROOT}" "${FILE}")
        get_filename_component(dir "${relpath}" DIRECTORY)
        target_sources(${EXECUTABLE_NAME} PRIVATE "${FILE}")
        set_property(SOURCE "${FILE}" PROPERTY MACOSX_PACKAGE_LOCATION "Resources/${dir}")
    endmacro()
    foreach(asset_file ${ASSET_FILES})
        add_resource("${ASSETS_ROOT}/${asset_file}")
    endforeach()

elseif(EMSCRIPTEN)
    set(PRELOAD_FLAGS "")
    foreach(asset_file ${ASSET_FILES})
        list(APPEND PRELOAD_FLAGS "--preload-file \"${ASSETS_ROOT}/${asset_file}@${asset_file}\"")
    endforeach()
    message(STATUS "PRELOAD_FLAGS: ${PRELOAD_FLAGS}")
    target_link_libraries(${EXECUTABLE_NAME} PRIVATE
        ${PRELOAD_FLAGS}
    )
else()
    if(UNIX AND NOT APPLE)
        target_link_libraries(${EXECUTABLE_NAME} PRIVATE m)
    endif()

    macro(copy_helper filename)
        if (ANDROID)
            if (NOT MOBILE_ASSETS_DIR)
                message(FATAL_ERROR "Building for Android, but MOBILE_ASSETS_DIR is not set")
            endif()
            set(outname "${MOBILE_ASSETS_DIR}/${filename}")
        else()
            set(outname "${CMAKE_BINARY_DIR}/$<CONFIGURATION>/${filename}")
        endif()
        # 目标目录在部分 CMake 版本（如 3.22）下 copy_if_different 不会自动创建，
        # 全新构建时父目录尚不存在会导致 copy 失败；显式 mkdir 保证跨版本一致。
        get_filename_component(_asset_rel_dir "${filename}" DIRECTORY)
        if (ANDROID)
            set(_asset_outdir "${MOBILE_ASSETS_DIR}/${_asset_rel_dir}")
        else()
            set(_asset_outdir "${CMAKE_BINARY_DIR}/$<CONFIGURATION>/${_asset_rel_dir}")
        endif()
        add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_asset_outdir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ASSETS_ROOT}/${filename}" "${outname}"
        )
    endmacro()

    foreach(asset_file ${ASSET_FILES})
        message(STATUS "Copying asset: ${asset_file}")
        copy_helper("${asset_file}")
    endforeach()
endif()

# ------------------- 平台特定属性 -------------------
set(RESOURCE_FILES "logo.png")
if(IOS)
    set(RESOURCE_FILES "iosLaunchScreen.storyboard;logo.png")
endif()

set_target_properties(${EXECUTABLE_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    XCODE_GENERATE_SCHEME TRUE
    XCODE_ATTRIBUTE_BUNDLE_IDENTIFIER "com.game.${PROJECT_NAME}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.game.${PROJECT_NAME}"
    XCODE_ATTRIBUTE_CURRENTYEAR "${CURRENTYEAR}"
    RESOURCE "${RESOURCE_FILES}"
)
if(MSVC)
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>"
    )
endif()
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT "${EXECUTABLE_NAME}")

if(CMAKE_SYSTEM_NAME MATCHES "Darwin" AND NOT CMAKE_GENERATOR MATCHES "Xcode")
    install(TARGETS ${EXECUTABLE_NAME}
        BUNDLE DESTINATION ./install COMPONENT Runtime
        RUNTIME DESTINATION ./install/bin COMPONENT Runtime
    )
    set(DEP_DIR "${CMAKE_BINARY_DIR}")
    install(CODE "
        include(BundleUtilities)
        fixup_bundle(\"\$<TARGET_BUNDLE_DIR:${EXECUTABLE_NAME}>\" \"\" \"${DEP_DIR}\")
    ")
    set(CPACK_GENERATOR "DragNDrop")
    include(CPack)
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/.." FILES ${PROJECT_FILES})

message(STATUS "Project current dir: ${CMAKE_CURRENT_LIST_DIR}")
message(STATUS "Project source dir: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "Project binary dir: ${CMAKE_CURRENT_BINARY_DIR}")