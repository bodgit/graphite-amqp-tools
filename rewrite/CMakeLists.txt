set(REWRITE_MANS graphite-rewrite.8 graphite-rewrite.conf.5)
set(REWRITE_MANS_COMPRESSED graphite-rewrite.8.gz graphite-rewrite.conf.5.gz)

foreach(man ${REWRITE_MANS})
	add_custom_command(OUTPUT ${man}.gz
		COMMAND ${GZIP_TOOL} -c ${CMAKE_CURRENT_SOURCE_DIR}/${man} >${man}.gz
		DEPENDS ${man}
		COMMENT "Building ${man}.gz")
endforeach()

bison_target(PARSER ${CMAKE_CURRENT_SOURCE_DIR}/parse.y ${CMAKE_CURRENT_BINARY_DIR}/parse.c)

add_executable(graphite-rewrite
	rewrite.c
	${BISON_PARSER_OUTPUTS}
	$<TARGET_OBJECTS:common>
	$<TARGET_OBJECTS:stomp>
	$<TARGET_OBJECTS:graphite>
	${REWRITE_MANS_COMPRESSED}
)

target_link_libraries(graphite-rewrite
	${EVENT_LIBRARIES}
	${OPENSSL_LIBRARIES}
	${PCRE_LIBRARIES}
)

install(TARGETS graphite-rewrite
	DESTINATION "sbin"
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/graphite-rewrite.conf
	DESTINATION "/etc"
	PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/graphite-rewrite.8.gz
	DESTINATION "share/man/man8"
	PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/graphite-rewrite.conf.5.gz
	DESTINATION "share/man/man5"
	PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
