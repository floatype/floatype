# cmake -DPATCH=<patch> -DDIR=<source directory> -P apply_patch.cmake
# Applies a patch to a fetched source tree once: a tree that has it already is
# left alone, so configuring again does not fail.
execute_process(
  COMMAND git apply --reverse --check ${PATCH}
  WORKING_DIRECTORY ${DIR}
  RESULT_VARIABLE applied
  OUTPUT_QUIET ERROR_QUIET)
if(NOT applied EQUAL 0)
  execute_process(
    COMMAND git apply ${PATCH}
    WORKING_DIRECTORY ${DIR}
    RESULT_VARIABLE failed)
  if(NOT failed EQUAL 0)
    message(FATAL_ERROR "Cannot apply ${PATCH} in ${DIR}")
  endif()
endif()
