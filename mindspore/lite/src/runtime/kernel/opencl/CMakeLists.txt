file(GLOB_RECURSE OPENCL_KERNEL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.cc)
add_library(opencl_kernel_lib_ OBJECT ${KERNEL_SRC} ${OPENCL_KERNEL_SRC})
