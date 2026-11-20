# cmake -DFILE=<path> -DEXPECTED=<sha256> -P check_sha256.cmake
# Fails, and removes the file, when its SHA-256 is not the expected one.
file(SHA256 ${FILE} actual)
if(NOT actual STREQUAL EXPECTED)
  file(REMOVE ${FILE})
  message(FATAL_ERROR "${FILE}: SHA-256 is ${actual}, expected ${EXPECTED}")
endif()
