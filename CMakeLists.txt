cmake_minimum_required(VERSION 2.8.8)

project(graphite-amqp-tools C)

set(CMAKE_C_FLAGS "${CMAKE_CFLAGS} -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wshadow -Wpointer-arith -Wcast-qual -Wsign-compare")

include(FindBISON)
include(FindPkgConfig)
pkg_check_modules(RABBITMQ REQUIRED librabbitmq)
pkg_check_modules(EVENT REQUIRED libevent>=2)

include_directories(
	common
	${RABBITMQ_INCLUDE_DIRS}
	${EVENT_INCLUDE_DIRS}
)

link_directories(
	${RABBITMQ_LIBRARY_DIRS}
	${EVENT_LIBRARY_DIRS}
)

add_subdirectory(common)
add_subdirectory(enqueue)
add_subdirectory(dequeue)
