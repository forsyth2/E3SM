if (NOT DEBUG)
  string(APPEND CFLAGS " -O2")
  string(APPEND FFLAGS " -O2")
  string(APPEND CUDA_FLAGS " -O3 -arch sm_70 --use_fast_math")
endif()
if (DEBUG)
  string(APPEND CUDA_FLAGS " -O0 -g -arch sm_70")
endif()
if (COMP_NAME STREQUAL gptl)
  string(APPEND CPPDEFS " -DHAVE_SLASHPROC")
endif()
string(APPEND SLIBS " -L$ENV{ESSL_PATH}/lib64 -lessl")
set(MPICXX "mpiCC")
set(PIO_FILESYSTEM_HINTS "gpfs")
set(USE_CUDA "TRUE")
