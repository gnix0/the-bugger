idf_component_register(SRCS "main.c" "../../workload.c" INCLUDE_DIRS "../..")
# Keep our fixture strict without changing warning policy for SDK components.
# Specific SDK -Wno-error=... options take precedence over a plain -Werror.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror
    -Werror=unused-function -Werror=unused-variable
    -Werror=unused-but-set-variable -Werror=deprecated-declarations -Werror=extra
    -Og -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls)
