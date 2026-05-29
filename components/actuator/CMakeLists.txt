idf_component_register(
    SRCS "actuator.c"
    INCLUDE_DIRS "."
    REQUIRES driver hw_config sensors
)
