set(OPENH264_ROOT "${CMAKE_CURRENT_LIST_DIR}/third_party/openh264/codec")
file(GLOB OPENH264_SOURCES
    "${OPENH264_ROOT}/common/src/*.cpp"
    "${OPENH264_ROOT}/decoder/core/src/*.cpp"
    "${OPENH264_ROOT}/decoder/plus/src/*.cpp")
set(VIDEO_PRIVATE_INCLUDES
    "${OPENH264_ROOT}/api/wels"
    "${OPENH264_ROOT}/common/inc"
    "${OPENH264_ROOT}/decoder/core/inc"
    "${OPENH264_ROOT}/decoder/plus/inc")

if(COMMAND idf_component_register)
    idf_component_register(SRCS "video_decoder.cpp" ${OPENH264_SOURCES}
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS ${VIDEO_PRIVATE_INCLUDES}
        REQUIRES pthread
        PRIV_REQUIRES heap)
    set(VIDEO_TARGET ${COMPONENT_LIB})
else()
    add_library(video_decoder STATIC video_decoder.cpp ${OPENH264_SOURCES})
    target_include_directories(video_decoder PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include"
        PRIVATE ${VIDEO_PRIVATE_INCLUDES})
    find_package(Threads REQUIRED)
    target_link_libraries(video_decoder PUBLIC Threads::Threads)
    set(VIDEO_TARGET video_decoder)
endif()

target_compile_features(${VIDEO_TARGET} PRIVATE cxx_std_17)
target_compile_options(${VIDEO_TARGET} PRIVATE -fno-strict-aliasing -fno-rtti)
# The vendored C++ predates current compilers; keep diagnostics in our wrapper.
set_source_files_properties(${OPENH264_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
# No X86_ASM/NEON/RVV macros: this build intentionally uses portable scalar code.
