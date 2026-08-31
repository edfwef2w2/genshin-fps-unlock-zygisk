cmake_minimum_required(VERSION 3.22)
project(unlockfps)

add_library(unlockfps SHARED
    main.cpp
    config.cpp
    ipc.cpp
    pattern.cpp
    unlocker.cpp
    utils.cpp
)

target_include_directories(unlockfps PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(unlockfps PRIVATE cxx_std_20)
target_compile_options(unlockfps PRIVATE
    -fno-exceptions
    -fno-rtti
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -ffunction-sections
    -fdata-sections
    -Os
    -Wall
    -Wextra
    -Wno-unused-parameter
)
target_link_options(unlockfps PRIVATE -Wl,--gc-sections -Wl,--exclude-libs,ALL)
target_link_libraries(unlockfps log)

set_target_properties(unlockfps PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

add_custom_command(TARGET unlockfps POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../magisk/zygisk"
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:unlockfps>
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../magisk/zygisk/arm64-v8a.so"
)
