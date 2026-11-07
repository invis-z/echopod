add_library(rippl_options INTERFACE)

target_compile_options(rippl_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wcast-qual
    -Wpointer-arith
    -Wundef
    -Wvla
    -Wdouble-promotion
    -Wimplicit-fallthrough
    -Wformat=2
)

add_library(rippl_compact INTERFACE)
if(CMAKE_CROSSCOMPILING)
    target_compile_options(rippl_compact INTERFACE -Os)
endif()

if(RIPPL_SANITIZE)
    target_compile_options(rippl_options INTERFACE
        -fsanitize=undefined
        -fno-sanitize-recover=all
        -fno-omit-frame-pointer
    )
    target_link_options(rippl_options INTERFACE -fsanitize=undefined)
endif()

if(RIPPL_ADDRESS_SANITIZER)
    target_compile_options(rippl_options INTERFACE -fsanitize=address)
    target_link_options(rippl_options INTERFACE -fsanitize=address)
endif()

add_library(rippl_lto INTERFACE)
if(CMAKE_CROSSCOMPILING)
    target_compile_options(rippl_lto INTERFACE -flto -ffat-lto-objects -g)
endif()
