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
