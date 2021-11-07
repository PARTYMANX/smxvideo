cmake_minimum_required(VERSION 3.10)

project(smxvideo)

set(SRC "${CMAKE_SOURCE_DIR}/src")

set(SOURCE_FILES
	"${SRC}/main.c"
	"${SRC}/smx-adapter/smx-adapter.cpp"
)

set(HEADER_FILES
	"${SRC}/smx-adapter/smx-adapter.h"
)

# add sdl2
find_package(SDL2 REQUIRED)
list(APPEND LIBS 
	SDL2::SDL2
	SDL2::SDL2main
)
list(APPEND INCLUDES ${SDL2_INCLUDE_DIRS})

# add ffmpeg
find_package(FFMPEG REQUIRED)
list(APPEND LIBS
	${FFMPEG_libavformat_LIBRARY}
	${FFMPEG_libavcodec_LIBRARY}
	${FFMPEG_libavutil_LIBRARY}
	${FFMPEG_libswscale_LIBRARY}
	${FFMPEG_libswresample_LIBRARY}
)
list(APPEND INCLUDES 
	${FFMPEG_libavformat_INCLUDE_DIRS}
	${FFMPEG_libavcodec_INCLUDE_DIRS}
	${FFMPEG_libavutil_INCLUDE_DIRS}
	${FFMPEG_libswscale_INCLUDE_DIRS}
	${FFMPEG_libswresample_INCLUDE_DIRS}
)

# add stepmaniax sdk
add_library(SMX "${PROJECT_SOURCE_DIR}/thirdparty/stepmaniax-sdk/SMX.lib")
set_target_properties(SMX PROPERTIES LINKER_LANGUAGE C)
list(APPEND LIBS SMX)
list(APPEND INCLUDES "${PROJECT_SOURCE_DIR}/thirdparty/stepmaniax-sdk")

add_executable(smxvideo)

target_sources(smxvideo PRIVATE
	${SOURCE_FILES}
	${HEADER_FILES}
)

target_link_libraries(smxvideo PUBLIC ${LIBS})

target_include_directories(smxvideo PUBLIC
	"${PROJECT_SOURCE_DIR}/src"
	${INCLUDES}
)
