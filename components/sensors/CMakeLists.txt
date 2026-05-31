idf_component_register(SRCS "sensors.c" INCLUDE_DIRS "." REQUIRES driver hw_config rs485)

target_compile_definitions(${COMPONENT_LIB} PRIVATE DEMO_MODE)