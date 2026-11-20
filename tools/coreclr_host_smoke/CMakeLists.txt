if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(coreclr_host_smoke LANGUAGES CXX)
    set(CORELR_SMOKE_STANDALONE TRUE)
endif()

find_program(CORELR_SMOKE_DOTNET_EXE dotnet REQUIRED)

# 独立解决方案里把 C# 项目也挂进去，VS 解决方案资源管理器中就能直接看到 ManagedSmoke。
if(CORELR_SMOKE_STANDALONE AND CMAKE_GENERATOR MATCHES "Visual Studio")
    include_external_msproject(ManagedSmoke
        "${CMAKE_CURRENT_SOURCE_DIR}/ManagedSmoke.csproj"
        PLATFORM "Any CPU")
endif()

file(GLOB _hostfxr_candidates
    "C:/Program Files/dotnet/host/fxr/*/hostfxr.dll"
    "$ENV{DOTNET_ROOT}/host/fxr/*/hostfxr.dll"
)
if(NOT _hostfxr_candidates)
    message(FATAL_ERROR "coreclr_host_smoke: hostfxr.dll not found; set DOTNET_ROOT")
endif()
list(GET _hostfxr_candidates -1 CORELR_SMOKE_HOSTFXR_DLL)

file(GLOB _native_inc_dirs
    "C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Host.win-x64/*/runtimes/win-x64/native"
    "$ENV{DOTNET_ROOT}/packs/Microsoft.NETCore.App.Host.win-x64/*/runtimes/win-x64/native"
)
if(NOT _native_inc_dirs)
    message(FATAL_ERROR "coreclr_host_smoke: hostfxr.h not found; set DOTNET_ROOT")
endif()
list(GET _native_inc_dirs -1 CORELR_SMOKE_NATIVE_INC_DIR)

set(_managed_src "${CMAKE_CURRENT_SOURCE_DIR}/ManagedSmoke.csproj")
set(_managed_out "${CMAKE_BINARY_DIR}/$<CONFIGURATION>/ManagedSmoke")

add_custom_target(coreclr_smoke_managed ALL
    COMMAND "${CORELR_SMOKE_DOTNET_EXE}" build "${_managed_src}"
            -c $<IF:$<CONFIG:Debug>,Debug,Release>
            -o "${_managed_out}"
    COMMENT "Building ManagedSmoke.dll for CoreCLR host smoke test"
    VERBATIM)

add_executable(coreclr_host_smoke host.cpp)
target_compile_features(coreclr_host_smoke PRIVATE cxx_std_17)
add_dependencies(coreclr_host_smoke coreclr_smoke_managed)

target_include_directories(coreclr_host_smoke PRIVATE "${CORELR_SMOKE_NATIVE_INC_DIR}")
target_compile_definitions(coreclr_host_smoke PRIVATE
    HOSTFXR_PATH="${CORELR_SMOKE_HOSTFXR_DLL}")

set_target_properties(coreclr_host_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${_managed_out}"
    VS_DEBUGGER_WORKING_DIRECTORY "${_managed_out}"
    VS_DEBUGGER_ENVIRONMENT "DOTNET_EnableDiagnostics=1\nCOMPlus_EnableDiagnostics=1")

add_custom_command(TARGET coreclr_host_smoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/ManagedSmoke.runtimeconfig.json"
            "$<TARGET_FILE_DIR:coreclr_host_smoke>/ManagedSmoke.runtimeconfig.json"
    COMMENT "Deploying ManagedSmoke.runtimeconfig.json")

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    if(CORELR_SMOKE_STANDALONE)
        set(_smoke_user_file "${CMAKE_BINARY_DIR}/coreclr_host_smoke.vcxproj.user")
    else()
        set(_smoke_user_file "${CMAKE_BINARY_DIR}/tools/coreclr_host_smoke/coreclr_host_smoke.vcxproj.user")
    endif()
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/coreclr_host_smoke.vcxproj.user.in"
        "${_smoke_user_file}" @ONLY)
endif()

if(MSVC)
    if(CORELR_SMOKE_STANDALONE)
        set(_debugger_props "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/Debugger.props")
    else()
        set(_debugger_props "${CMAKE_SOURCE_DIR}/cmake/Debugger.props")
    endif()
    set_property(TARGET coreclr_host_smoke PROPERTY VS_USER_PROPS "${_debugger_props}")
    if(CORELR_SMOKE_STANDALONE)
        set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY
            VS_STARTUP_PROJECT coreclr_host_smoke)
    endif()
endif()
