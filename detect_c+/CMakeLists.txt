cmake_minimum_required(VERSION 3.20)

project(document_detector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

add_library(document_detector
    src/document_detector.cpp
    src/document_enhance.cpp
    src/document_geometry.cpp
    src/document_transform.cpp
)

target_include_directories(document_detector
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(document_detector
    PUBLIC
        ${OpenCV_LIBS}
)

add_library(document_detector_ffi SHARED
    src/document_detector_ffi.cpp
)

target_compile_definitions(document_detector_ffi
    PRIVATE
        DOCDET_FFI_BUILD
)

target_link_libraries(document_detector_ffi
    PRIVATE
        document_detector
)

set_target_properties(document_detector_ffi
    PROPERTIES
        OUTPUT_NAME document_detector
)

add_executable(detect_doc
    src/main.cpp
)

target_link_libraries(detect_doc
    PRIVATE
        document_detector
)
