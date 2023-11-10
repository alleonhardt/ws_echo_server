file (GLOB SOURCES *.cc)
idf_component_register(SRCS ${SOURCES}
                       EMBED_FILES root.html
                       INCLUDE_DIRS ".")
