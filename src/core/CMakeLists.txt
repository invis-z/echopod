set(RIPPL_CORE_SOURCES
    deadline.c
    format.c
    frame_stats.c
    hash.c
    i18n.c
    key_tracker.c
    low_battery.c
    permutation.c
    power_policy.c
    random.c
    string_builder.c
    utf8.c
    volume_path.c
    wall_clock.c
    daylight.c
)
add_library(rippl_core STATIC ${RIPPL_CORE_SOURCES})
target_include_directories(rippl_core PUBLIC include)
target_link_libraries(rippl_core PRIVATE rippl_options rippl_lto)
if(NOT CMAKE_CROSSCOMPILING)
    target_compile_definitions(rippl_core PRIVATE RIPPL_BUILTIN_ENGLISH)
endif()
set_source_files_properties(i18n.c string_builder.c format.c key_tracker.c low_battery.c permutation.c power_policy.c wall_clock.c daylight.c PROPERTIES COMPILE_OPTIONS "$<$<BOOL:${CMAKE_CROSSCOMPILING}>:-Os>")

if(CMAKE_CROSSCOMPILING)
    add_library(rippl_core_plain STATIC ${RIPPL_CORE_SOURCES})
    target_include_directories(rippl_core_plain PUBLIC include)
    target_link_libraries(rippl_core_plain PRIVATE rippl_options)
endif()
