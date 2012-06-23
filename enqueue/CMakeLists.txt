bison_target(PARSER parse.y ${CMAKE_CURRENT_BINARY_DIR}/parse.c)

add_executable(graphite-enqueue
	enqueue.c
	${BISON_PARSER_OUTPUTS}
	$<TARGET_OBJECTS:common>
)

target_link_libraries(graphite-enqueue
	${RABBITMQ_LIBRARIES}
	${EVENT_LIBRARIES}
)

set(ENQUEUE_MANS graphite-enqueue.8 graphite-eneueue.conf.5)

foreach(man ${ENQUEUE_MANS})
	add_custom_command(OUTPUT ${man}.gz
		COMMAND ${GZIP_TOOL} -c ${man} >${man}.gz
		DEPENDS ${man}
		COMMENT "Building ${man}.gz")
endforeach()

install(TARGETS graphite-enqueue
	DESTINATION "sbin"
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

install(FILES graphite-enqueue.conf
	DESTINATION "/etc"
	PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ)

install(FILES graphite-enqueue.8.gz
	DESTINATION "share/man/man8"
	PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
install(FILES graphite-enqueue.conf.5.gz
	DESTINATION "share/man/man5"
	PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)