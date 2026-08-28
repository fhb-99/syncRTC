include(FindPackageHandleStandardArgs)

set(_SYNCRTC_FFMPEG_DEFAULT_ROOTS "")
if(WIN32 AND EXISTS "E:/ffmpge-5.1.6/ffmpeg-n5.1.6-9-gdcdfd7fb62-win64-gpl-shared-5.1")
    list(APPEND _SYNCRTC_FFMPEG_DEFAULT_ROOTS
        "E:/ffmpge-5.1.6/ffmpeg-n5.1.6-9-gdcdfd7fb62-win64-gpl-shared-5.1")
endif()
if(WIN32 AND EXISTS "E:/ffmpeg-5.1.6/ffmpeg-n5.1.6-9-gdcdfd7fb62-win64-gpl-shared-5.1")
    list(APPEND _SYNCRTC_FFMPEG_DEFAULT_ROOTS
        "E:/ffmpeg-5.1.6/ffmpeg-n5.1.6-9-gdcdfd7fb62-win64-gpl-shared-5.1")
endif()

set(FFMPEG_ROOT "" CACHE PATH "FFmpeg development package root")

find_path(FFMPEG_INCLUDE_DIR
    NAMES libavcodec/avcodec.h
    HINTS
        "${FFMPEG_ROOT}"
        "$ENV{FFMPEG_ROOT}"
        ${_SYNCRTC_FFMPEG_DEFAULT_ROOTS}
    PATH_SUFFIXES include
)

set(_FFMPEG_REQUIRED_VARS FFMPEG_INCLUDE_DIR)
set(_FFMPEG_COMPONENT_TARGETS "")

foreach(_component IN LISTS FFmpeg_FIND_COMPONENTS)
    string(TOUPPER "${_component}" _component_upper)
    find_library(FFMPEG_${_component_upper}_LIBRARY
        NAMES lib${_component}.dll.a ${_component} lib${_component}
        HINTS
            "${FFMPEG_ROOT}"
            "$ENV{FFMPEG_ROOT}"
            ${_SYNCRTC_FFMPEG_DEFAULT_ROOTS}
        PATH_SUFFIXES lib lib64 bin
    )
    if(FFMPEG_${_component_upper}_LIBRARY)
        set(FFmpeg_${_component}_FOUND TRUE)
    endif()
    list(APPEND _FFMPEG_REQUIRED_VARS FFMPEG_${_component_upper}_LIBRARY)
endforeach()

find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS ${_FFMPEG_REQUIRED_VARS}
    HANDLE_COMPONENTS
)

if(FFmpeg_FOUND)
    foreach(_component IN LISTS FFmpeg_FIND_COMPONENTS)
        string(TOUPPER "${_component}" _component_upper)
        set(_target "FFmpeg::${_component}")
        if(NOT TARGET ${_target})
            add_library(${_target} UNKNOWN IMPORTED)
            set_target_properties(${_target} PROPERTIES
                IMPORTED_LOCATION "${FFMPEG_${_component_upper}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
            )
        endif()
        list(APPEND _FFMPEG_COMPONENT_TARGETS ${_target})
    endforeach()

    if(NOT TARGET FFmpeg::FFmpeg)
        add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
        set_target_properties(FFmpeg::FFmpeg PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_FFMPEG_COMPONENT_TARGETS}"
            INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
        )
    endif()
endif()
