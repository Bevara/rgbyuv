cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(RGBYUV_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/cvt_rgbyuv.c
)

# No third-party archive: this is arithmetic.
SET(RGBYUV_LIBS)

SET(RGBYUV_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(rgbyuv
        "${RGBYUV_SRC}"
        "${RGBYUV_LIBS}"
        []
        ""
        "${RGBYUV_INC}"
        ""
        "1")
