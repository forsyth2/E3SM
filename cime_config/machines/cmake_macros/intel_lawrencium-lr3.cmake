if (COMP_NAME STREQUAL gptl)
  string(APPEND CPPDEFS " -DHAVE_VPRINTF -DHAVE_GETTIMEOFDAY")
endif()
