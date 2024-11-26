target_include_directories(app PRIVATE .)

target_sources(app PRIVATE
	       ${CMAKE_CURRENT_SOURCE_DIR}/m_event_queue.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/m_event_def.c
)

zephyr_linker_sources(RODATA m_event_queue.ld)
