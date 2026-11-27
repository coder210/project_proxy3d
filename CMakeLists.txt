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
            if(ANDROID_ABI STREQUAL "arm64-v8a")
                set(JOY_CLR_PLATFORM "android-arm64")
            elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
                set(JOY_CLR_PLATFORM "android-armv7")
            elseif(ANDROID_ABI STREQUAL "x86_64")
                set(JOY_CLR_PLATFORM "android-x64")
            else()
                set(JOY_CLR_PLATFORM "android-${ANDROID_ABI}")
            endif()
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
        elseif(EXISTS "${_clr_plat}/libmonosgen-2.0.so")
            # Android 的 MonoVM 原生库叫 libmonosgen-2.0.so，不是 libcoreclr.so。
            set(MONO_LIBRARIES "${_clr_plat}/libmonosgen-2.0.so")
        elseif(EXISTS "${_clr_plat}/libcoreclr.so")
            set(MONO_LIBRARIES "${_clr_plat}/libcoreclr.so")
        elseif(EXISTS "${_clr_plat}/libcoreclr.dylib")
            set(MONO_LIBRARIES "${_clr_plat}/libcoreclr.dylib")
        elseif(EXISTS "${_clr_plat}/libmonosgen-2.0.dylib")
            set(MONO_LIBRARIES "${_clr_plat}/libmonosgen-2.0.dylib")
        endif()
    endif()
    # Android：MonoVM 运行时从 runtimes/nupkg 解包暂存，与桌面用同一套来源。
    # 这里强制覆盖 MONO_LIBRARIES（含旧的缓存值），因为 runtimes/<platform>/
    # 散装目录已删除，沿用旧路径只会指向不存在的文件。
    if(ANDROID)
        include("${JOY_REPO_DIR}/cmake/JoyMonoVm.cmake")
        joy_monovm_stage(_joy_monovm_stage)
        set(MONO_LIBRARIES "${_joy_monovm_stage}/libmonosgen-2.0.so")
    endif()
    # CoreCLR 桌面宿主需要 hostfxr（Android 走 libmonosgen 的 coreclr_*，不需要）。
    # 优先用系统 dotnet 的 hostfxr（与 JoyGame.runtimeconfig.json 的 framework 引用配套）；
    # 找不到时回退到 JOY_CLR_PLATFORM 目录里随仓部署的那份。
    if(WIN32 AND NOT ANDROID)
        if(NOT JOY_HOSTFXR_DLL)
            file(GLOB _hostfxr_candidates
                "C:/Program Files/dotnet/host/fxr/*/hostfxr.dll"
                "$ENV{DOTNET_ROOT}/host/fxr/*/hostfxr.dll"
                "${_clr_plat}/hostfxr.dll")
            if(_hostfxr_candidates)
                list(GET _hostfxr_candidates -1 JOY_HOSTFXR_DLL)
            endif()
        endif()
        # dotnet root = hostfxr.dll 所在目录往上三级（<root>/host/fxr/<ver>/hostfxr.dll）。
        # hostfxr 被部署到 exe 目录后无法反推，需显式 DOTNET_ROOT 告诉它 framework 在哪。
        if(JOY_HOSTFXR_DLL)
            get_filename_component(_fxr_dir "${JOY_HOSTFXR_DLL}" DIRECTORY)      # .../host/fxr/<ver>
            get_filename_component(_fxr_host "${_fxr_dir}" DIRECTORY)            # .../host/fxr
            get_filename_component(_fxr_host2 "${_fxr_host}" DIRECTORY)          # .../host
            get_filename_component(JOY_DOTNET_ROOT "${_fxr_host2}" DIRECTORY)    # <root>
        endif()
    elseif(APPLE AND NOT ANDROID)
        if(NOT JOY_HOSTFXR_DLL)
            file(GLOB _hostfxr_candidates
                "/usr/local/share/dotnet/host/fxr/*/libhostfxr.dylib"
                "/usr/share/dotnet/host/fxr/*/libhostfxr.dylib"
                "$ENV{DOTNET_ROOT}/host/fxr/*/libhostfxr.dylib"
                "${_clr_plat}/libhostfxr.dylib")
            if(_hostfxr_candidates)
                list(GET _hostfxr_candidates -1 JOY_HOSTFXR_DLL)
            endif()
        endif()
    elseif(UNIX AND NOT ANDROID)
        if(NOT JOY_HOSTFXR_DLL)
            file(GLOB _hostfxr_candidates
                "/usr/share/dotnet/host/fxr/*/libhostfxr.so"
                "/usr/lib/dotnet/host/fxr/*/libhostfxr.so"
                "$ENV{DOTNET_ROOT}/host/fxr/*/libhostfxr.so"
                "${_clr_plat}/libhostfxr.so")
            if(_hostfxr_candidates)
                list(GET _hostfxr_candidates -1 JOY_HOSTFXR_DLL)
            endif()
        endif()
    endif()

    # 桌面不再要求 MONO_LIBRARIES 哨兵：jcsx.c 用 LoadLibrary/dlopen 动态加载 hostfxr，
    # 运行时与 BCL 由 runtimes/nupkg 自包含部署（见 cmake/JoySelfContained.cmake）。
    # Android 仍必须有 libmonosgen 主库（要打进 APK 的 lib/<abi>）。
    if(MONO_INCLUDE_DIR AND EXISTS "${MONO_INCLUDE_DIR}" AND (NOT ANDROID OR MONO_LIBRARIES))
        set(JOY_MODULE_CS ON CACHE BOOL "C# 脚本模块" FORCE)
        set_property(GLOBAL PROPERTY JOY_REQUESTED_MODULES cs)
        message(STATUS "project_proxy3d: 启用 joy C# 模块 (${JOY_CLR_PLATFORM})")
        if(NOT ANDROID)
            if(JOY_HOSTFXR_DLL)
                message(STATUS "project_proxy3d: hostfxr = ${JOY_HOSTFXR_DLL}")
            else()
                message(WARNING "project_proxy3d: 未找到 hostfxr，桌面 CoreCLR 宿主将无法启动")
            endif()
        endif()
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

# 游戏逻辑/场景/渲染已全部迁移到 C#（CSharp/），本 target 只保留根目录的 C++ 瘦宿主：
# main.cpp / clr_host.cpp / clr_host.h（引擎源码由 joy_add_sources 引入）。
file(GLOB SRC_FILES CONFIGURE_DEPENDS "*.h" "*.c" "*.hpp" "*.cpp")

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

# Apple 平台额外图标：根目录 logo.png 已移入 assets/，
# 由下方 add_resource 打包进 bundle 的 Resources 根（文件名仍为 logo.png）。
if(APPLE)
    add_definitions(-DLUA_USE_IOS)
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

# 引入 joy 框架源码（纯文件引入，不再构建独立库 target）
include(${JOY_ADD_CMAKE})
joy_add_sources(${EXECUTABLE_NAME})

# 项目根目录（main.cpp / clr_host.cpp 使用）
target_include_directories(${EXECUTABLE_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# CoreCLR 桌面宿主：把系统 dotnet root 传给 jcsx.c，初始化前设 DOTNET_ROOT，
# 否则 hostfxr 从部署目录（exe 旁）找不到 Microsoft.NETCore.App framework。
if(JOY_MODULE_CS AND NOT ANDROID)
    if(JOY_DOTNET_ROOT)
        target_compile_definitions(${EXECUTABLE_NAME} PRIVATE
            "JOY_DOTNET_ROOT=\"${JOY_DOTNET_ROOT}\"")
    endif()
    if(JOY_HOSTFXR_DLL)
        # CMake 里是正斜杠，C 字符串可用；转成正斜杠避免转义问题。
        string(REPLACE "\\" "/" _hostfxr_cmake "${JOY_HOSTFXR_DLL}")
        target_compile_definitions(${EXECUTABLE_NAME} PRIVATE
            "JOY_HOSTFXR_PATH_MACRO=\"${_hostfxr_cmake}\"")
    endif()
endif()

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
# logo.png 已移入 assets/ 并由 add_resource 打包，storyboard 在根目录。
set(RESOURCE_FILES "")
if(IOS)
    set(RESOURCE_FILES "iosLaunchScreen.storyboard")
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

    set(_cs_proj "${CMAKE_CURRENT_SOURCE_DIR}/CSharp/JoyGame.csproj")
    set(_cs_out  "${CMAKE_BINARY_DIR}/CSharp")

    # 干净 checkout 没有 obj/：命令行 MSBuild 不隐式 restore，显式还原并串进依赖链。
    add_custom_target(project_proxy3d_cs_restore
        COMMAND dotnet restore "${_cs_proj}"
        COMMENT "dotnet restore CSharp (+ Joy.Bindings)"
        VERBATIM)
    add_custom_target(project_proxy3d_cs_managed
        COMMAND dotnet build "${_cs_proj}" -c $<IF:$<CONFIG:Debug>,Debug,Release> -o "${_cs_out}"
        BYPRODUCTS
            "${_cs_out}/JoyGame.dll"
            "${_cs_out}/Joy.Bindings.dll"
        COMMENT "dotnet build CSharp -> JoyGame.dll"
        VERBATIM)
    add_dependencies(project_proxy3d_cs_managed project_proxy3d_cs_restore)

    # ---- 桌面自包含运行时（Godot 4 导出版的做法）----
    # dotnet publish --self-contained + BCL 白名单裁剪，把 ~88MB 降到 ~48MB。
    # 共享模块与 joy example_csharp 用同一套，保证主工程/示例部署一致。
    # Android 不走这里（用 libmonosgen 的 coreclr_* API）。
    if(ANDROID)
        set(_cs_runtime_dir "${MOBILE_ASSETS_DIR}")
        # 必须每次构建都同步托管 DLL/PDB：POST_BUILD 只在 exe 重链时触发，
        # 只改 C# 时 build/<Config>/JoyGame.dll 会停留在旧版本，符号对不上。
        add_custom_target(project_proxy3d_cs_deploy
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_cs_runtime_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_cs_out}/JoyGame.dll" "${_cs_runtime_dir}/JoyGame.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_cs_out}/JoyGame.pdb" "${_cs_runtime_dir}/JoyGame.pdb"
            DEPENDS project_proxy3d_cs_managed
            COMMENT "Syncing JoyGame.dll/pdb to runtime directory")
    else()
        # macOS 是 .app bundle，exe 实际在 <cfg>/<name>.app/Contents/MacOS/。
        # 运行时必须部署到 exe 同目录：宿主会把 CWD 对齐到 exe 目录，jcsx.c 从
        # 那里探测 libhostfxr.dylib；放到 <cfg> 根会探测失败并退回
        # framework-dependent 模式，再尝试加载不存在的 hostfxr.dll。
        if(APPLE)
            set(_cs_runtime_dir "$<TARGET_FILE_DIR:${EXECUTABLE_NAME}>")
        else()
            set(_cs_runtime_dir "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
        endif()
        get_filename_component(_joy_cmake_dir "${JOY_ADD_CMAKE}" DIRECTORY)
        include(${_joy_cmake_dir}/cmake/JoySelfContained.cmake)
        joy_deploy_selfcontained(${EXECUTABLE_NAME} "${_cs_proj}" "${_cs_out}"
                                 "${_cs_runtime_dir}" "$<IF:$<CONFIG:Debug>,Debug,Release>")
        add_dependencies(${EXECUTABLE_NAME}_sc_deploy project_proxy3d_cs_managed)
        add_custom_target(project_proxy3d_cs_deploy)
        add_dependencies(project_proxy3d_cs_deploy ${EXECUTABLE_NAME}_sc_deploy)
    endif()
    add_dependencies(${EXECUTABLE_NAME} project_proxy3d_cs_deploy)

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
    # Android：程序集/BCL 进 APK assets 根（AAssetManager 可读），MonoVM 原生库
    # 与 libSystem.* shim 进 APK lib/<abi>（系统加载器只从那里 dlopen .so）。
    get_filename_component(_clr_bin "${MONO_LIBRARIES}" DIRECTORY)
    set(_clr_libs "${_clr_bin}/libs")
    # 只有 Android 需要仓库里的 MonoVM BCL（随 assets 解包）；桌面 BCL 由
    # joy_deploy_selfcontained 从 runtimes/nupkg 铺到 exe 目录。
    if(NOT ANDROID OR EXISTS "${_clr_libs}/System.Private.CoreLib.dll")
        if(ANDROID)
            set(_out_dir "${MOBILE_ASSETS_DIR}")
        else()
            set(_out_dir "$<TARGET_FILE_DIR:${EXECUTABLE_NAME}>")
        endif()
        # 逐个 copy_if_different：内容相同就跳过，不触碰被运行进程锁定的 DLL。
        # 注意：MonoVM 的 BCL（System.Private.CoreLib.dll 等）只在 Android 需要。
        # 桌面走系统真 CoreCLR + hostfxr，若把 MonoVM BCL 放 exe/lib 会让
        # CoreCLR 误加载 Mono 版 CoreLib（MonoStackFrame not found 崩溃）。
        file(GLOB _clr_lib_files LIST_DIRECTORIES false "${_clr_libs}/*")
        set(_copy_lib_cmds)
        if(ANDROID)
            foreach(_lib IN LISTS _clr_lib_files)
                list(APPEND _copy_lib_cmds
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${_lib}" "${_out_dir}/lib/")
            endforeach()
        endif()
        # 桌面：Joy.Bindings.dll 必须与 JoyGame.dll 同目录，保证 hostfxr 加载的
        # 绑定程序集与 JoyGame 依赖解析到同一个 AssemblyLoadContext 实例。
        if(ANDROID)
            set(_bindings_deploy "${_out_dir}/lib/Joy.Bindings.dll")
        else()
            set(_bindings_deploy "${_out_dir}/Joy.Bindings.dll")
        endif()
        add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_cs_out}/JoyGame.dll" "${_out_dir}/JoyGame.dll"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}/lib"
            ${_copy_lib_cmds}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_cs_out}/Joy.Bindings.dll" "${_bindings_deploy}"
            COMMENT "Deploying C# runtime (JoyGame.dll + Joy.Bindings.dll)")
        # 托管断点需要 PDB 与 DLL 同目录；Release/Debug 都会生成 portable PDB。
        if(EXISTS "${_cs_out}/JoyGame.pdb")
            add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_cs_out}/JoyGame.pdb" "${_out_dir}/JoyGame.pdb"
                COMMENT "Deploying JoyGame.pdb for managed breakpoints")
        endif()
        if(EXISTS "${_cs_out}/Joy.Bindings.pdb")
            if(ANDROID)
                set(_bindings_pdb_deploy "${_out_dir}/lib/Joy.Bindings.pdb")
            else()
                set(_bindings_pdb_deploy "${_out_dir}/Joy.Bindings.pdb")
            endif()
            add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_cs_out}/Joy.Bindings.pdb" "${_bindings_pdb_deploy}"
                COMMENT "Deploying Joy.Bindings.pdb")
        endif()
        # MonoVM coreclr.dll 只在 Android 需要（libmonosgen 是 .so，桌面不用它）。
        # 桌面用系统真 CoreCLR，部署 MonoVM coreclr.dll 会导致 CoreLib 混用崩溃。
        if(ANDROID AND EXISTS "${_clr_bin}/coreclr.dll")
            add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_clr_bin}/coreclr.dll" "${_out_dir}/coreclr.dll"
                COMMENT "Deploying coreclr.dll for ${EXECUTABLE_NAME}")
        endif()

        # 桌面自包含：hostfxr.dll / coreclr.dll / BCL 已由 sc_deploy 铺到 exe 目录，
        # jcsx.c 探测到 exe 目录的 hostfxr.dll 就走命令行模式（Godot 导出版做法），
        # 玩家无需装 .NET。这里只需补上自包含版 runtimeconfig.json（无 framework 节点）。
        if(NOT ANDROID)
            add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_CURRENT_SOURCE_DIR}/CSharp/JoyGame.runtimeconfig.json"
                        "${_out_dir}/JoyGame.runtimeconfig.json"
                COMMENT "Deploying JoyGame.runtimeconfig.json for CoreCLR host")
        endif()

        if(ANDROID)
            # APK 只把 lib/<abi>/*.so 解压为可加载库，assets/ 是压缩数据，
            # 系统加载器找不到。MonoVM 主库 + libSystem.* shim 必须进 APK 的
            # lib/<abi>；缺任一 shim，CoreLib 首次 JIT 的 P/Invoke 会闪退。
            # Gradle 的 jniLibs.srcDir 指向 app/libs，故同时落到 app/libs/<abi>；
            # 再拷一份到构建树 lib/<abi>（与 joy examples 的打包约定一致）。
            file(GLOB _clr_shims "${_clr_bin}/libSystem.*.so")
            set(_clr_so_files "${MONO_LIBRARIES}" ${_clr_shims})
            set(_apk_libs "${CMAKE_BINARY_DIR}/lib/${ANDROID_ABI}")
            if(MOBILE_MAIN_DIR)
                get_filename_component(_gradle_app_dir "${MOBILE_MAIN_DIR}/.." ABSOLUTE)
                list(APPEND _apk_libs "${_gradle_app_dir}/libs/${ANDROID_ABI}")
            endif()
            foreach(_apk_lib IN LISTS _apk_libs)
                add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${_apk_lib}"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            ${_clr_so_files} "${_apk_lib}/"
                    COMMENT "Deploying MonoVM + CoreLib shims into ${_apk_lib}")
            endforeach()
        elseif(UNIX)
            # Linux/macOS 的 CLR 运行时（真 CoreCLR + BCL）由 joy_deploy_selfcontained
            # 从 runtimes/nupkg 自包含部署到 exe 目录。
            # 这里**不能**再拷 runtimes/<platform>/ 下的 MonoVM 主库/libSystem shim：
            # 会把自包含的真 CoreCLR 覆盖成 MonoVM 版，hostfxr 加载后直接崩溃
            # （此前 Linux/macOS smoke segfault 的根因）。只保留 rpath 供 SDL3 解析。
            if(APPLE)
                set_target_properties(${EXECUTABLE_NAME} PROPERTIES
                    BUILD_RPATH "@loader_path"
                    INSTALL_RPATH "@loader_path")
            else()
                set_target_properties(${EXECUTABLE_NAME} PROPERTIES
                    BUILD_RPATH "$ORIGIN"
                    INSTALL_RPATH "$ORIGIN")
            endif()
        endif()

        # SwiftShader 软件 Vulkan：无独显环境让 SDL3 GPU 后端可用。
        # 三个文件登记在 third_party/vulkan/，部署到 exe 旁；运行时还需设置
        # VK_ICD_FILENAMES 指向 exe 目录的 vk_swiftshader_icd.json（见 clr_host.cpp）。
        set(_vk_src "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan")
        if(WIN32 AND EXISTS "${_vk_src}/vulkan-1.dll")
            foreach(_vk_file vulkan-1.dll vk_swiftshader.dll vk_swiftshader_icd.json)
                add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${_vk_src}/${_vk_file}" "${_out_dir}/${_vk_file}"
                    COMMENT "Deploying SwiftShader Vulkan (${_vk_file})")
            endforeach()
        endif()
    else()
        # 缺 CoreLib 时 mono_jit_init() 会直接 abort（assembly.c:2718 corlib not met），
        # 与其产出坏包在运行时崩，不如配置阶段就失败（同 joy examples/csharp_host）。
        message(FATAL_ERROR "project_proxy3d: 缺平台 BCL ${_clr_libs}/System.Private.CoreLib.dll —— 请确认 joy 子模块 runtimes/ 已正确 checkout")
    endif()
endif()
if(MSVC)
    set_target_properties(${EXECUTABLE_NAME} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>"
        # 原生 exe 内嵌 CoreCLR：F5 调试需开启诊断，混合调试引擎才能挂上。
        VS_DEBUGGER_ENVIRONMENT "DOTNET_EnableDiagnostics=1\nCOMPlus_EnableDiagnostics=1"
    )
    # 调试器类型必须写进 .vcxproj.user：VS 的“配置属性→调试”只读 user 文件，
    # 写在 props 里的值 MSBuild 能求值但 IDE 启动调试时不生效（C# 断点不绑定）。
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/project_proxy3d.vcxproj.user.in"
            "${CMAKE_BINARY_DIR}/project_proxy3d.vcxproj.user" @ONLY)
    endif()
    set_property(TARGET ${EXECUTABLE_NAME} PROPERTY
        VS_USER_PROPS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Debugger.props")
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