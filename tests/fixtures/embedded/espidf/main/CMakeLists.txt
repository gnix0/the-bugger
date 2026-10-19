idf_component_register(SRCS "main.c" "../../workload.c" INCLUDE_DIRS "../..")
target_compile_options(${COMPONENT_LIB} PRIVATE -Og -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls)
