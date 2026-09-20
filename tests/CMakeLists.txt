# A2DPWB unit/integration tests (doctest)
#
# Unit tests for app/src/codec_policy.h and the SSC encoder policy decisions.
# Hermetic on purpose: the test binary only compiles the pure decision helpers,
# so it does not link a2dpwb_core / wxWidgets / btstack.

include(CTest)

if(BUILD_TESTING)
    add_executable(a2dpwb_tests
        src/test_main.cpp
        unit/codec_policy_tests.cpp
        integration/negotiation_tests.cpp
    )

    target_include_directories(a2dpwb_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    )

    target_compile_features(a2dpwb_tests PRIVATE cxx_std_17)

    if(MSVC)
        target_compile_options(a2dpwb_tests PRIVATE /W3 /utf-8)
    else()
        target_compile_options(a2dpwb_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME a2dpwb_tests COMMAND a2dpwb_tests)
endif()