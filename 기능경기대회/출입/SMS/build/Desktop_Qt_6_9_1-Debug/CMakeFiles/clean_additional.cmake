# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles/SafetyManagementSystem_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/SafetyManagementSystem_autogen.dir/ParseCache.txt"
  "SafetyManagementSystem_autogen"
  )
endif()
