set(DEQUEUE_MANS graphite-dequeue.8 graphite-dequeue.conf.5)
set(DEQUEUE_MANS_COMPRESSED graphite-dequeue.8.gz graphite-dequeue.conf.5.gz)

foreach(man ${DEQUEUE_MANS})
	add_custom_command(OUTPUT ${man}.gz
		COMMAND ${GZIP_TOOL} -c ${man} >${man}.gz
		DEPENDS ${man}
		COMMENT "Building ${man}.gz")
endforeach()

bison_target(PARSER parse.y ${CMAKE_CURRENT_BINARY_DIR}/parse.c)

add_executable(graphite-dequeue
	dequeue.c
	${BISON_PARSER_OUTPUTS}
	$<TARGET_OBJECTS:common>
	${DEQUEUE_MANS_COMPRESSED}
)

target_link_libraries(graphite-dequeue
	${RABBITMQ_LIBRARIES}
)

install(TARGETS graphite-dequeue
	DESTINATION "sbin"
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

install(FILES graphite-dequeue.conf
	DESTINATION "/etc"
	PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ)

install(FILES graphite-dequeue.8.gz
	DESTINATION "share/man/man8"
	PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
install(FILES graphite-dequeue.conf.5.gz
	DESTINATION "share/man/man5"
	PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
