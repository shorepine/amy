add_library(usermod_amy INTERFACE)

file(GLOB_RECURSE AMY_SOURCES ${CMAKE_CURRENT_LIST_DIR}/*.c)

target_sources(usermod_amy INTERFACE
    ${AMY_SOURCES}
)

target_include_directories(usermod_amy INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(usermod_amy INTERFACE
    MODULE_AMY_ENABLED=1
)

target_link_libraries(usermod INTERFACE usermod_amy)

