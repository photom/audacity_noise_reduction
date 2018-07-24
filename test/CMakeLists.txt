enable_testing()

include_directories("../src/audacity")

set(TEST_SOURCE catch.hpp catchrunner.cpp test_noisered.cpp)

find_library(CRYPTO_LIBRARY crypto REQUIRE)

add_executable(catch_test ${TEST_SOURCE})

target_link_libraries(catch_test audacity-noisered sndfile soxr crypto)

add_test(
        NAME catch_test
        COMMAND catch_test
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
