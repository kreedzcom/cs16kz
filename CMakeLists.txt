# -------------------------------------------------------------------
# -------------------------------------------------------------------
cmake_minimum_required(VERSION 3.10)
project(kz_global_api)

set(CMAKE_SIZEOF_VOID_P 4)
set(CMAKE_LIBRARY_ARCHITECTURE "i386-linux-gnu")

set(CMAKE_PREFIX_PATH /usr/lib/i386-linux-gnu /usr/lib32 ${CMAKE_PREFIX_PATH})
set(CMAKE_LIBRARY_PATH /usr/lib/i386-linux-gnu /usr/lib32 ${CMAKE_LIBRARY_PATH})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_definitions(-DHAVE_STDINT_H -DJIT -DASM32 -Dg_rehlds_available=RehldsApi)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Debug' as none was specified.")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

set(COMMON_FLAGS "-m32 -pipe -shared -lm -ldl")
set(COMMON_CXX_FLAGS "-flto=auto -funroll-loops -fpermissive -fno-strict-aliasing -fno-rtti --param=destructive-interference-size=64")

set(CMAKE_C_FLAGS_DEBUG "-O2 -ggdb3 -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_DEBUG "-O2 -ggdb3 -fno-omit-frame-pointer")

set(CMAKE_C_FLAGS_RELEASE "-O2 -s -fomit-frame-pointer -fvisibility=hidden")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -s -fomit-frame-pointer -fvisibility=hidden -fvisibility-inlines-hidden")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS} ${COMMON_CXX_FLAGS}")

add_subdirectory(deps/parson)

set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_ZLIB ON CACHE BOOL "" FORCE)
add_subdirectory(deps/ixwebsocket)
target_compile_options(ixwebsocket PUBLIC -fexceptions)

add_subdirectory(deps/SQLiteCpp)
target_compile_options(SQLiteCpp PUBLIC -fexceptions)

include_directories(
    ./deps/sdk/metamod
    ./deps/sdk/amxmodx/public
    ./deps/sdk/amxmodx/public/resdk
    ./deps
    ./src/include
)

set(SOURCES
    src/amxxmodule.cpp
    src/mod_rehlds_api.cpp
    src/kz_util.cpp
    src/kz_cvars.cpp
    src/kz_ws.cpp
    src/kz_ws_msgs.cpp
    src/kz_replay.cpp
    src/kz_storage.cpp
    src/kz_natives.cpp
    src/main.cpp
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_link_libraries(${PROJECT_NAME}
    SQLiteCpp
    sqlite3
    parson
    ixwebsocket
    ssl
    crypto
    z
    zstd
    pthread
    dl
    -m32
    -static-libgcc
    -static-libstdc++
)

set_target_properties(${PROJECT_NAME} PROPERTIES 
    PREFIX "" 
    OUTPUT_NAME "kz_global_api_amxx_i386" 
    SUFFIX ".so"
)


if(MODULE_DEST_DIR)
    message(STATUS "Post-build copy enabled to: ${MODULE_DEST_DIR}")
    
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:${PROJECT_NAME}>
            "${MODULE_DEST_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        COMMENT "Copying binary to ${MODULE_DEST_DIR}"
    )
endif()
