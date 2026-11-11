add_library(rippl_hal INTERFACE)
target_include_directories(rippl_hal INTERFACE include)
target_link_libraries(rippl_hal INTERFACE rippl_core rippl_dsp)

if(CMAKE_CROSSCOMPILING)
    add_library(rippl_hal_plain INTERFACE)
    target_include_directories(rippl_hal_plain INTERFACE include)
    target_link_libraries(rippl_hal_plain INTERFACE rippl_core_plain rippl_dsp_plain)
endif()
