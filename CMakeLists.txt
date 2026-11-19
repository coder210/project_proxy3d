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
# 新版 joy 的 joy_add.cmake 在仓库根；旧版在 joy/ 子目录。两种布局都兼容。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/joy/joy_add.cmake")
    set(JOY_ADD_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/joy/joy_add.cmake")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/joy/joy/joy_add.cmake")
    set(JOY_ADD_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/joy/joy/joy_add.cmake")
else()
    set(JOY_ADD_CMAKE "${CMAKE_SOURCE_DIR}/joy/joy_add.cmake")
endif()

# ---------- 启用 joy 的 C# 脚本模块（与 examples/csharp_host 同构） ----------
# 必须在 include(JoyTargets.cmake) 之前设置全局属性：joy_add_sources() 内部
# include JoyTargets，模块在那一步读 JOY_REQUESTED_MODULES 决定编真胶水还是空桩。
# 本项目不走 joy 主 CMake（那段的 CLR 探测只在 JOY_BUILD_EXAMPLES=ON 时执行），
# 故在这里独立完成 CLR 探测。
if(NOT EMSCRIPTEN)
    set(JOY_CLR_ROOT "${JOY_REPO_DIR}/runtimes")
    if(NOT JOY_CLR_PLATFORM)
        if(WIN32)
            set(JOY_CLR_PLATFORM "windows-x64")
        elseif(ANDROID)
            set(JOY_CLR_PLATFORM "android-${ANDROID_ABI}")
        elseif(APPLE)
            set(JOY_CLR_PLATFORM "macos-arm64")
        else()
            set(JOY_CLR_PLATFORM "linux-x64")
        endif()
    endif()
    set(_clr_plat "${JOY_CLR_ROOT}/${JOY_CLR_PLATFORM}")
    if(NOT MONO_INCLUDE_DIR)
        if(EXISTS "${JOY_CLR_ROOT}/include/mono-2.0")
            set(MONO_INCLUDE_DIR "${JOY_CLR_ROOT}/include/mono-2.0")
        elseif(EXISTS "${_clr_plat}/include/mono-2.0")
            set(MONO_INCLUDE_DIR "${_clr_plat}/include/mono-2.0")
        endif()
    endif()
    if(NOT MONO_LIBRARIES)
        if(WIN32 AND EXISTS "${_clr_plat}/coreclr.import.lib")
            set(MONO_LIBRARIES "${_clr_plat}/coreclr.import.lib")
        elseif(EXISTS "${_clr_plat}/libcoreclr.so")
            set(MONO_LIBRARIES "${_clr_plat}/libcoreclr.so")
        elseif(EXISTS "${_clr_plat}/libcoreclr.dylib")
            set(MONO_LIBRARIES "${_clr_plat}/libcoreclr.dylib")
        endif()
    endif()
    if(MONO_INCLUDE_DIR AND MONO_LIBRARIES)
        set(JOY_MODULE_CS ON CACHE BOOL "C# 脚本模块" FORCE)
        set_property(GLOBAL PROPERTY JOY_REQUESTED_MODULES cs)
        message(STATUS "project_proxy3d: 启用 joy C# 模块 (${JOY_CLR_PLATFORM})")
    else()
        message(WARNING
            "project_proxy3d: 未找到 CLR 开发包，C# 模块跳过（只编空桩）。"
            " 请设 -DMONO_INCLUDE_DIR 与 -DMONO_LIBRARIES")
    endif()
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
    # 资产已从 assets/project_proxy3d/ 上移到 assets/ 根目录（sounds/、textures/ 等），
    # 不再带与可执行文件同名的命名空间前缀，故 Linux 无后缀 exe 与资产目录撞名的问题
    # 已根治，无需再把 exe 塞进同名子目录。
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

# 旧版 joy API → 新版兼容头：强制注入到所有源文件，零改动业务源码适配新版引擎
set(JOY_COMPAT_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/compat/joy_compat.h")
if(MSVC)
    target_compile_options(${EXECUTABLE_NAME} PRIVATE "/FI${JOY_COMPAT_HEADER}")
else()
    target_compile_options(${EXECUTABLE_NAME} PRIVATE -include "${JOY_COMPAT_HEADER}")
endif()
target_sources(${EXECUTABLE_NAME} PRIVATE "${JOY_COMPAT_HEADER}")

# Web 平台输出 HTML
if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html" CACHE INTERNAL "")
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

# ------------------- C# 脚本模块：构建与运行时部署（同 examples/csharp_host） -------------------
# 仅当 C# 模块真正启用（CLR 探测成功、joy::all 含真胶水而非空桩）时才构建/部署。
if(JOY_MODULE_CS AND TARGET joy_module_cs AND NOT EMSCRIPTEN)
    # IDE 里按 joy 真实目录树分组注入的引擎源码（source_group 只在当前目录生效）
    joy_ide_source_groups()

    set(_cs_proj "${CMAKE_CURRENT_SOURCE_DIR}/game_cs/JoyGame.csproj")
    set(_cs_out  "${CMAKE_BINARY_DIR}/csharp")

    # 干净 checkout 没有 obj/：命令行 MSBuild 不隐式 restore，显式还原并串进依赖链。
    add_custom_target(project_proxy3d_cs_restore
        COMMAND dotnet restore "${_cs_proj}"
        COMMENT "dotnet restore game_cs (+ Joy.Bindings)"
        VERBATIM)
    add_custom_target(project_proxy3d_cs_managed
        COMMAND dotnet build "${_cs_proj}" -c $<IF:$<CONFIG:Debug>,Debug,Release> -o "${_cs_out}"
        BYPRODUCTS
            "${_cs_out}/JoyGame.dll"
            "${_cs_out}/Joy.Bindings.dll"
        COMMENT "dotnet build game_cs -> JoyGame.dll"
        VERBATIM)
    add_dependencies(project_proxy3d_cs_managed project_proxy3d_cs_restore)
    add_dependencies(${EXECUTABLE_NAME} project_proxy3d_cs_managed)

    # VS 解决方案挂入两个 C# 工程（否则 IDE 里看不到 .cs，且 NuGet restore 缺引用项目信息
    # 会报 NU1105）。三个构建点串成链，避免 VS 与 dotnet build 同时写 obj/。
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        include_external_msproject(JoyBindings "${JOY_BINDINGS_CSPROJ}" PLATFORM "Any CPU")
        include_external_msproject(JoyGame "${_cs_proj}" PLATFORM "Any CPU")
        add_dependencies(JoyBindings project_proxy3d_cs_restore)
        add_dependencies(JoyGame JoyBindings)
        add_dependencies(project_proxy3d_cs_managed JoyGame)
    endif()

    # 运行期部署：JoyGame.dll 放 exe 旁；BCL + Joy.Bindings.dll 放 lib/（CLR 宿主只从
    # MONO_LIB_DIR 指向的 lib 解析依赖）；coreclr.dll 由系统加载器从 exe 旁查找。
    get_filename_component(_clr_bin "${MONO_LIBRARIES}" DIRECTORY)
    set(_clr_libs "${_clr_bin}/libs")
    if(EXISTS "${_clr_libs}/System.Private.CoreLib.dll")
        set(_out_dir "$<TARGET_FILE_DIR:${EXECUTABLE_NAME}>")
        add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_cs_out}/JoyGame.dll" "${_out_dir}/JoyGame.dll"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${_out_dir}/lib"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${_clr_libs}" "${_out_dir}/lib"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_cs_out}/Joy.Bindings.dll" "${_out_dir}/lib/Joy.Bindings.dll"
            COMMENT "Deploying C# runtime (JoyGame.dll + lib/)")
        if(EXISTS "${_clr_bin}/coreclr.dll")
            add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_clr_bin}/coreclr.dll" "${_out_dir}/coreclr.dll"
                COMMENT "Deploying coreclr.dll for ${EXECUTABLE_NAME}")
        endif()
    else()
        message(WARNING "project_proxy3d: 缺平台 BCL (${_clr_libs}/System.Private.CoreLib.dll)，跳过 C# 运行时部署")
    endif()
endif()
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