set(RIPPL_LIBMAD_DIR ${PROJECT_SOURCE_DIR}/third_party/libmad)
set(FPM_64BIT ON)
set(SIZEOF_INT 4)
configure_file(${RIPPL_LIBMAD_DIR}/mad.h.in ${CMAKE_CURRENT_BINARY_DIR}/libmad/mad.h @ONLY)
add_library(rippl_libmad STATIC
    ${RIPPL_LIBMAD_DIR}/bit.c
    ${RIPPL_LIBMAD_DIR}/fixed.c
    ${RIPPL_LIBMAD_DIR}/frame.c
    ${RIPPL_LIBMAD_DIR}/huffman.c
    ${RIPPL_LIBMAD_DIR}/layer12.c
    ${RIPPL_LIBMAD_DIR}/layer3.c
    ${RIPPL_LIBMAD_DIR}/stream.c
    ${RIPPL_LIBMAD_DIR}/synth.c
    ${RIPPL_LIBMAD_DIR}/timer.c
    ${RIPPL_LIBMAD_DIR}/version.c
)
target_include_directories(rippl_libmad SYSTEM
    PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/libmad
    PRIVATE ${RIPPL_LIBMAD_DIR}
)
target_compile_definitions(rippl_libmad PRIVATE OPT_ACCURACY NDEBUG)
target_compile_options(rippl_libmad PRIVATE -w)
if(CMAKE_CROSSCOMPILING)
    target_include_directories(rippl_libmad SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/libmad_libc)
endif()

set(RIPPL_FAAD2_DIR ${PROJECT_SOURCE_DIR}/third_party/faad2)
file(GLOB RIPPL_FAAD2_SOURCES ${RIPPL_FAAD2_DIR}/libfaad/*.c)
add_library(rippl_faad2 STATIC ${RIPPL_FAAD2_SOURCES})
target_include_directories(rippl_faad2 SYSTEM
    PUBLIC ${RIPPL_FAAD2_DIR}/include
    PRIVATE ${RIPPL_FAAD2_DIR}/libfaad
)
target_compile_definitions(rippl_faad2 PRIVATE
    FIXED_POINT LC_ONLY_DECODER DISABLE_SBR NDEBUG PACKAGE_VERSION="2.11.4"
)
if(NOT CMAKE_CROSSCOMPILING)
    target_compile_definitions(rippl_faad2 PRIVATE HAVE_INTTYPES_H=1)
endif()
target_compile_options(rippl_faad2 PRIVATE -w)
set_target_properties(rippl_faad2 PROPERTIES C_EXTENSIONS ON)
if(CMAKE_CROSSCOMPILING)
    target_compile_definitions(rippl_faad2 PRIVATE abs=__builtin_abs)
    target_compile_options(rippl_faad2 PRIVATE -O2)
    target_include_directories(rippl_faad2 SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/libmad_libc)
else()
    target_compile_definitions(rippl_faad2 PRIVATE HAVE_STDINT_H=1)
endif()

set(RIPPL_OPUS_DIR ${PROJECT_SOURCE_DIR}/third_party/opus)
file(GLOB RIPPL_OPUS_SOURCES ${RIPPL_OPUS_DIR}/celt/*.c ${RIPPL_OPUS_DIR}/silk/*.c)
add_library(rippl_opus STATIC
    ${RIPPL_OPUS_SOURCES}
    ${RIPPL_OPUS_DIR}/src/opus.c
    ${RIPPL_OPUS_DIR}/src/opus_decoder.c
    ${RIPPL_OPUS_DIR}/src/extensions.c
)
target_include_directories(rippl_opus SYSTEM
    PUBLIC ${RIPPL_OPUS_DIR}/include
    PRIVATE ${RIPPL_OPUS_DIR} ${RIPPL_OPUS_DIR}/celt ${RIPPL_OPUS_DIR}/silk ${RIPPL_OPUS_DIR}/silk/fixed
        ${RIPPL_OPUS_DIR}/src
)
target_compile_definitions(rippl_opus PRIVATE
    FIXED_POINT DISABLE_FLOAT_API OPUS_BUILD USE_ALLOCA NDEBUG PACKAGE_VERSION="1.6.1"
)
target_compile_options(rippl_opus PRIVATE -w)
set_target_properties(rippl_opus PROPERTIES C_EXTENSIONS ON)
if(CMAKE_CROSSCOMPILING)
    target_compile_definitions(rippl_opus PRIVATE alloca=__builtin_alloca abs=__builtin_abs)
    target_compile_options(rippl_opus PRIVATE -O2)
    target_include_directories(rippl_opus SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/libmad_libc)
endif()

set(RIPPL_DEVICE_LOGIC_SOURCES
    audio_engine.c
    audio_math.c
    backlight.c
    battery_gauge.c
    watchdog.c
    bt_module.c
    card_write.c
    core1_aac.c
    core1_alac.c
    core1_control.c
    core1_mp3.c
    core1_opus.c
    core1_qoa.c
    core1_stream.c
    dac_sequence.c
    flac_source.c
    dma_loop.c
    emmc_layout.c
    exfat.c
    exfat_create.c
    fat.c
    fat_create.c
    fat_layout.c
    fat_names.c
    fat_reserve.c
    flac_accel.c
    image_crc.c
    image_date.c
    jack_sense.c
    key_ladder.c
    key_scanner.c
    mp3_decoder.c
    mp3_probe.c
    msc.c
    np_faces_gate.c
    panel.c
    panic_text.c
    pcf8563.c
    pll.c
    start_record.c
    sd_card.c
    sleep_meter.c
    stack_mark.c
    stall_watch.c
    stream_drift.c
    tick_clock.c
    updater.c
    usb_audio.c
    usb_audio_sink.c
    usb_device.c
    usb_fallback.c
    vbus_debounce.c
    wav_source.c
)
add_library(rippl_device_logic STATIC ${RIPPL_DEVICE_LOGIC_SOURCES})
if(CMAKE_CROSSCOMPILING)
    set_source_files_properties(
        audio_math.c
        battery_gauge.c
        bt_module.c
        core1_control.c
        dac_sequence.c
        emmc_layout.c
        exfat.c
        exfat_create.c
        fat_create.c
        fat_layout.c
        fat_names.c
        flac_source.c
        fat_reserve.c
        image_crc.c
        image_date.c
        mp3_probe.c
        msc.c
        np_faces_gate.c
        panel.c
        panic_text.c
        pcf8563.c
        pll.c
        sd_card.c
        stack_mark.c
        start_record.c
        updater.c
        usb_audio.c
        usb_device.c
        PROPERTIES COMPILE_OPTIONS -Os
    )
endif()
target_include_directories(rippl_device_logic PUBLIC include)
target_link_libraries(rippl_device_logic
    PUBLIC rippl_core rippl_hal rippl_codec rippl_libmad rippl_faad2 rippl_opus
    PRIVATE rippl_options rippl_lto
)

if(CMAKE_CROSSCOMPILING)
    target_compile_definitions(rippl_device_logic PRIVATE RIPPL_RESIDENT_TABLES)
    add_library(rippl_device_logic_plain STATIC ${RIPPL_DEVICE_LOGIC_SOURCES})
    target_include_directories(rippl_device_logic_plain PUBLIC include)
    target_link_libraries(rippl_device_logic_plain
        PUBLIC rippl_core_plain rippl_hal_plain rippl_codec_plain rippl_libmad rippl_faad2 rippl_opus
        PRIVATE rippl_options
    )
endif()
