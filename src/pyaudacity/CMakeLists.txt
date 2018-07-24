project(AudacityNoiseReduction)

include_directories(${CMAKE_SOURCE_DIR}/src/audacity)

include_directories(${PYTHON_INCLUDE_DIRS})

set(PY_SOURCE noiseredmodule.cpp)

add_library(noiseredmodule SHARED ${PY_SOURCE})

set_target_properties(noiseredmodule PROPERTIES LINKER_LANGUAGE CXX)
