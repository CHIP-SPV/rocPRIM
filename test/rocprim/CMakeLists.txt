# MIT License
#
# Copyright (c) 2017-2023 Advanced Micro Devices, Inc. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

set(AMDGPU_TEST_TARGETS "" CACHE STRING "List of specific device types to test for") # Leave empty for default system device

# Gets a test target name based on the first source file.
function(get_rocprim_test_target TEST_SOURCES TEST_TARGET)
  list(GET TEST_SOURCES 0 TEST_MAIN_SOURCE)
  get_filename_component(TEST_TARGET ${TEST_MAIN_SOURCE} NAME_WE)
  set(TEST_TARGET ${TEST_TARGET} PARENT_SCOPE)
endfunction()

function(add_rocprim_test TEST_NAME TEST_SOURCES)
  get_rocprim_test_target(${TEST_SOURCES} TEST_TARGET)
  add_rocprim_test_internal(${TEST_NAME} "${TEST_SOURCES}" ${TEST_TARGET})
endfunction()

function(add_rocprim_test_internal TEST_NAME TEST_SOURCES TEST_TARGET)
  add_executable(${TEST_TARGET} ${TEST_SOURCES})
  rocm_install(TARGETS ${TEST_TARGET} COMPONENT tests)

  target_include_directories(${TEST_TARGET} SYSTEM BEFORE
    PUBLIC
      $<BUILD_INTERFACE:${COMMON_TEST_HEADER_DIRECTORY}>
  )

  target_link_libraries(${TEST_TARGET}
    PRIVATE
      GTest::GTest
      GTest::Main
  )
  if(NOT USE_HIP_CPU)
    target_link_libraries(${TEST_TARGET}
      PRIVATE
        rocprim_hip
    )
  else()
    target_link_libraries(${TEST_TARGET}
      PRIVATE
        rocprim
        Threads::Threads
        hip_cpu_rt::hip_cpu_rt
    )
    if(STL_DEPENDS_ON_TBB)
      target_link_libraries(${TEST_TARGET}
        PRIVATE
          TBB::tbb
      )
    endif()
  endif()

  target_compile_options(${TEST_TARGET}
    PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:
        /bigobj # number of sections exceeded object file format limit: compile with /bigobj
      >
  )

  set_target_properties(${TEST_TARGET}
    PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test/rocprim"
  )
  if(AMDGPU_TEST_TARGETS)
    foreach(AMDGPU_TARGET IN LISTS AMDGPU_TEST_TARGETS)
      add_relative_test("${AMDGPU_TARGET}-${TEST_NAME}" ${TEST_TARGET})
      set_tests_properties("${AMDGPU_TARGET}-${TEST_NAME}"
          PROPERTIES
              RESOURCE_GROUPS "1,${AMDGPU_TARGET}:1"
              LABELS "hip;${AMDGPU_TARGET}"
      )
    endforeach()
  else()
      add_relative_test(${TEST_NAME} ${TEST_TARGET})
      set_tests_properties(${TEST_NAME}
          PROPERTIES
              LABELS "hip"
      )
  endif()

  if (WIN32 AND NOT DEFINED DLLS_COPIED_2)
    set(DLLS_COPIED_2 "YES")
    set(DLLS_COPIED_2 ${DLLS_COPIED_2} PARENT_SCOPE)
    # for now adding in all .dll as dependency chain is not cmake based on win32
    file( GLOB third_party_dlls
    LIST_DIRECTORIES ON
    CONFIGURE_DEPENDS
    ${HIP_DIR}/bin/*.dll
    ${CMAKE_SOURCE_DIR}/rtest.*
    )
    foreach( file_i ${third_party_dlls})
      add_custom_command( TARGET ${TEST_TARGET} POST_BUILD COMMAND ${CMAKE_COMMAND} ARGS -E copy_if_different ${file_i} ${PROJECT_BINARY_DIR}/test/rocprim )
    endforeach( file_i )
  endif()
endfunction()

# Returns a list of values that match the pattern
# "if ${WORD} == <value>" with optional whitespace between the tokens.
function(get_match_list FILE_STRING WORD MATCH_LIST)
  # collect all substrings that match the pattern
  string(REGEX MATCHALL "${WORD}[ ]*==[ ]*[0-9]*" LINE_MATCHES "${${FILE_STRING}}")
  set(${MATCH_LIST} "")
  # iterate over the substrings, record the values using the same regex
  foreach(LINE IN LISTS LINE_MATCHES)
    string(REGEX MATCH "${WORD}[ ]*==[ ]*([0-9]*)" TMP "${LINE}")
    list(APPEND ${MATCH_LIST} "${CMAKE_MATCH_1}")
  endforeach()
  set(${MATCH_LIST} ${${MATCH_LIST}} PARENT_SCOPE)
endfunction()

# Replacement for add_rocprim_test that splits up test cases allowing them to be compiled in parallel.
# A single .cpp.in file is provided containing valid C++ code with the addition of slice definitions.
# The slice definitions ROCPRIM_TEST_SLICE, ROCPRIM_TEST_SUITE_SLICE, and ROCPRIM_TEST_TYPE_SLICE demarkate
# slices of non-typed tests, typed test suites, and test types respectively. The slice cases must be
# marked with an "(el)if" statement (no "else") that has a unique value to ensure that the correct slice
# gets enabled. This function will generate a separate .cpp file for all non-typed test slices and the
# product of the typed test suites and test types.
#
# This example will generate five files:
#
# #cmakedefine ROCPRIM_TEST_SUITE_SLICE @ROCPRIM_TEST_SUITE_SLICE@
# #cmakedefine ROCPRIM_TEST_TYPE_SLICE  @ROCPRIM_TEST_TYPE_SLICE@
# #cmakedefine ROCPRIM_TEST_SLICE       @ROCPRIM_TEST_SLICE@
#
# #if   ROCPRIM_TEST_SLICE == 0
#   DEFINE_NAMED_TEST(TestSuiteName, NonTypedTest)
# #endif
# #if   ROCPRIM_TEST_SUITE_SLICE == 0
#   REGISTER_TYPED_TEST(TestSuiteName, TestZero, test_zero)
#   REGISTER_TYPED_TEST_SUITE(TestSuiteName, TestZero)
# #elif ROCPRIM_TEST_SUITE_SLICE == 1
#   REGISTER_TYPED_TEST(TestSuiteName, TestOne, test_one)
#   REGISTER_TYPED_TEST(TestSuiteName, TestTwo, test_two)
#   REGISTER_TYPED_TEST_SUITE(TestSuiteName, TestOne, TestTwo)
# #endif
# #if   ROCPRIM_TEST_TYPE_SLICE == 0
#   INSTANTIATE_TYPED_TEST(TestSuiteName, double)
# #elif ROCPRIM_TEST_TYPE_SLICE == 1
#   INSTANTIATE_TYPED_TEST(TestSuiteName, float)
#   INSTANTIATE_TYPED_TEST(TestSuiteName, int)
# #endif
function(add_rocprim_test_parallel TEST_NAME TEST_SOURCE)
  get_rocprim_test_target(${TEST_SOURCE} TEST_TARGET)

  file(READ ${TEST_SOURCE} FILE_CONTENTS)

  set(SOURCES "")

  # first, handle all non-typed tests
  # disable typed test, generate one file for each non-typed test
  set(ROCPRIM_TEST_SUITE_SLICE -1)
  set(ROCPRIM_TEST_TYPE_SLICE -1)
  get_match_list(FILE_CONTENTS "ROCPRIM_TEST_SLICE" TEST_SLICE_LIST)
  list(LENGTH TEST_SLICE_LIST TEST_SLICE_COUNT)
  if(TEST_SLICE_COUNT EQUAL 0)
    message(VERBOSE "found no non-typed tests for test target ${TEST_TARGET}")
  else()
    message(VERBOSE "found ${TEST_SLICE_COUNT} non-typed test slice(s) for test target ${TEST_TARGET}")
    foreach(ROCPRIM_TEST_SLICE IN LISTS TEST_SLICE_LIST)
      set(FILENAME "${TEST_TARGET}.parallel/${TEST_TARGET}_${ROCPRIM_TEST_SLICE}.cpp")
      configure_file(${TEST_SOURCE} ${FILENAME} @ONLY)
      list(APPEND SOURCES "${CMAKE_CURRENT_BINARY_DIR}/${FILENAME}")
    endforeach()
  endif()

  # second, handle all typed tests
  # disable non-typed test, generate one file for each test suite and test type pair
  set(ROCPRIM_TEST_SLICE -1)
  get_match_list(FILE_CONTENTS "ROCPRIM_TEST_SUITE_SLICE" TEST_SUITE_SLICE_LIST)
  list(LENGTH TEST_SUITE_SLICE_LIST TEST_SUITE_SLICE_COUNT)
  get_match_list(FILE_CONTENTS "ROCPRIM_TEST_TYPE_SLICE" TEST_TYPE_SLICE_LIST)
  list(LENGTH TEST_TYPE_SLICE_LIST TEST_TYPE_SLICE_COUNT)
  if(TEST_SUITE_SLICE_COUNT EQUAL 0 OR TEST_TYPE_SLICE_COUNT EQUAL 0)
    message(VERBOSE "found no typed tests for test target ${TEST_TARGET}")
  else()
    message(VERBOSE "found ${TEST_SUITE_SLICE_COUNT} test suite slice(s) and \
${TEST_TYPE_SLICE_COUNT} test type slice(s) for test target ${TEST_TARGET}")
    foreach(ROCPRIM_TEST_SUITE_SLICE IN LISTS TEST_SUITE_SLICE_LIST)
      foreach(ROCPRIM_TEST_TYPE_SLICE IN LISTS TEST_TYPE_SLICE_LIST)
        set(FILENAME "${TEST_TARGET}.parallel/${TEST_TARGET}_typed_${ROCPRIM_TEST_SUITE_SLICE}_${ROCPRIM_TEST_TYPE_SLICE}.cpp")
        configure_file(${TEST_SOURCE} ${FILENAME} @ONLY)
        list(APPEND SOURCES "${CMAKE_CURRENT_BINARY_DIR}/${FILENAME}")
      endforeach()
    endforeach()
  endif()

  # if no files are generated, nothing is built for the target
  list(LENGTH SOURCES SOURCES_COUNT)
  if(${SOURCES_COUNT} EQUAL 0)
    message(FATAL_ERROR "no .cpp files generated for target ${TEST_TARGET}")
  endif()

  set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${TEST_TARGET}.parallel")
  add_rocprim_test_internal(${TEST_NAME} "${SOURCES}" ${TEST_TARGET})
  target_include_directories("${TEST_TARGET}" PRIVATE "../../test/rocprim")
endfunction()

# ****************************************************************************
# Tests
# ****************************************************************************

# HIP basic test, which also checks if there are no linkage problems when there are multiple sources
add_rocprim_test("rocprim.basic_test" "test_basic.cpp;detail/get_rocprim_version.cpp")

# GPU: Validation errors.
# CPU: Unsupported SPIR-V module (capability 65, OpGroupNonUniformShuffle)
add_rocprim_test("rocprim.arg_index_iterator" test_arg_index_iterator.cpp)
add_rocprim_test("rocprim.temporary_storage_partitioning" test_temporary_storage_partitioning.cpp)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: 42 failed out of 343 (all half precision tests).
add_rocprim_test_parallel("rocprim.block_adjacent_difference" test_block_adjacent_difference.cpp.in)

# GPU: warning: kernel _Z27flag_heads_and_tails_kernelIfcN7rocprim12not_equal_toIfEELj64ELj32EEvPT_PxS5_  compiled SIMD8 allocated 128 regs and spilled around 22 (CL_OUT_OF_RESOURCES)
# CPU: 21 failed out of 147 (all half precision tests).
add_rocprim_test_parallel("rocprim.block_discontinuity" test_block_discontinuity.cpp.in)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: 84 failed out of 252, BlockedToWarpStriped/WarpStripedToBlocked
add_rocprim_test("rocprim.block_exchange" test_block_exchange.cpp)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: SPIRV module requires unsupported capability 65
# Crashes at build time:
# opt: /home/pjaaskel/src/llvm-project/llvm/include/llvm/Support/Casting.h:578: decltype(auto) llvm::cast(From*) [with To = llvm::Constant; From = llvm::Value]: Assertion `isa<To>(Val) && "cast<Ty>() argument of incompatible type!"' failed.
# add_rocprim_test("rocprim.block_histogram" test_block_histogram.cpp)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: 6 failed out of 294 (all half precision tests).
add_rocprim_test("rocprim.block_load_store" test_block_load_store.cpp)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: SPIRV module requires unsupported capability 65
add_rocprim_test("rocprim.block_sort_merge" test_block_sort_merge.cpp)
add_rocprim_test("rocprim.block_sort_merge_stable" test_block_sort_merge_stable.cpp)
add_rocprim_test_parallel("rocprim.block_radix_rank" test_block_radix_rank.cpp.in)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: SPIRV module requires unsupported capability 65
add_rocprim_test_parallel("rocprim.block_radix_sort" test_block_radix_sort.cpp.in)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: 3 / 193 pass (and takes forever to run)
# CPU: SPIRV module requires unsupported capability 65
add_rocprim_test("rocprim.block_reduce" test_block_reduce.cpp)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: took too long, killed.
add_rocprim_test_parallel("rocprim.block_scan" test_block_scan.cpp.in)

# GPU: CL_OUT_OF_RESOURCES at build time then verif. error (inifite loop in the main program too maybe?)
# CPU: OK
add_rocprim_test("rocprim.block_shuffle" test_block_shuffle.cpp)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: took too long, killed.
add_rocprim_test("rocprim.block_sort_bitonic" test_block_sort_bitonic.cpp)

# GPU: !HostMatchesDevice
# CPU: !HostMatchesDevice
add_rocprim_test("rocprim.config_dispatch" test_config_dispatch.cpp)

# GPU: OK
# CPU: (CL_INVALID_ARG_SIZE clSetKernelArg failed)
add_rocprim_test("rocprim.constant_iterator" test_constant_iterator.cpp)
add_rocprim_test("rocprim.counting_iterator" test_counting_iterator.cpp)

# GPU: hipErrorInvalidHandle (passed in nullptr) in src/chip-spv/src/CHIPBindings.cc:2628:hipMemcpy
# CPU: same
add_rocprim_test("rocprim.device_binary_search" test_device_binary_search.cpp)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: hipErrorInvalidHandle (passed in nullptr) in /home/pjaaskel/src/chip-spv/src/CHIPBindings.cc:2628:hipMemcpy
add_rocprim_test("rocprim.device_adjacent_difference" test_device_adjacent_difference.cpp)

# GPU: 2/35 pass.
# CPU: hipErrorTbd (CL_INVALID_ARG_SIZE clSetKernelArg failed)
add_rocprim_test("rocprim.device_histogram" test_device_histogram.cpp)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: hipErrorInvalidHandle (passed in nullptr) in /home/pjaaskel/src/chip-spv/src/CHIPBindings.cc:2628:hipMemcpy
add_rocprim_test("rocprim.device_merge" test_device_merge.cpp)

# GPU: CL_OUT_OF_RESOURCES at build time and hipErrorInvalidHandle (passed in nullptr) in /home/pjaaskel/src/chip-spv/src/CHIPBindings.cc:2628:hipMemcpy
# CPU: took too long, killed.
add_rocprim_test("rocprim.device_merge_sort" test_device_merge_sort.cpp)

# GPU: CL_OUT_OF_RESOURCES at build time and hipErrorInvalidHandle (passed in nullptr) in /home/pjaaskel/src/chip-spv/src/CHIPBindings.cc:2628:hipMemcpy
# CPU: took too long, killed.
add_rocprim_test("rocprim.device_partition" test_device_partition.cpp)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: segfault
# Crashes in llvm-spirv's switch..case validation during rocPRIM build.
add_rocprim_test_parallel("rocprim.device_radix_sort" test_device_radix_sort.cpp.in)

# GPU: warning: kernel _ZN7rocprim6detail13reduce_by_key6kernelINS1_14default_configILj0EiiEEPiS5_S5_S5_PjNS_8equal_toIiEENS_4plusIiEENS0_19lookback_scan_stateINS_5tupleIJjiEEELb0ELb0EEEEEvT0_T1_T2_T3_T4_T6_T5_T7_NS0_16ordered_block_idIjEEmm  compiled SIMD8 allocated 128 regs and spilled around 49 (CL_OUT_OF_RESOURCES)
# CPU: hipErrorOutOfMemory (Allocation size exceeds limits for a single allocation) in /home/pjaaskel/src/chip-spv/src/CHIPBackend.cc:1110:allocate
add_rocprim_test("rocprim.device_reduce_by_key" test_device_reduce_by_key.cpp)

# GPU: 28 / 64 pass (rest do not validate)
# CPU: 28 / 64 pass (rest do not validate)
add_rocprim_test("rocprim.device_reduce" test_device_reduce.cpp)

# GPU: warning: kernel _ZN7rocprim6detail13reduce_by_key6kernelINS1_14default_configILj0EijEEPiNS_17constant_iteratorIjlEEN10test_utils17identity_iteratorIiEESA_SA_NS_8equal_toIiEENS_4plusIjEENS0_19lookback_scan_stateINS_5tupleIJjjEEELb0ELb0EEEEEvT0_T1_T2_T3_T4_T6_T5_T7_NS0_16ordered_block_idIjEEmm  compiled SIMD8 allocated 128 regs and spilled around 52 (CL_OUT_OF_RESOURCES)
# CPU: took too long, killed.
add_rocprim_test("rocprim.device_run_length_encode" test_device_run_length_encode.cpp)

# Crashes in llvm-spirv's switch..case validation during rocPRIM build.
# add_rocprim_test("rocprim.device_scan" test_device_scan.cpp)

# GPU: took too long, killed
# CPU: took too long, killed
add_rocprim_test_parallel("rocprim.device_segmented_radix_sort" test_device_segmented_radix_sort.cpp.in)

# GPU: all fail validation and hipErrorInvalidHandle (passed in nullptr) in src/chip-spv/src/CHIPBindings.cc:2628:hipMemcpy
# CPU: took too long, killed
add_rocprim_test("rocprim.device_segmented_reduce" test_device_segmented_reduce.cpp)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: took too long, killed
# Crashes in llvm-spirv's switch..case validation during rocPRIM build.
#add_rocprim_test("rocprim.device_segmented_scan" test_device_segmented_scan.cpp)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: took too long, killed
add_rocprim_test("rocprim.device_select" test_device_select.cpp)

# GPU: hipErrorInvalidHandle (passed in nullptr)
# CPU: hipErrorInvalidHandle (passed in nullptr)
add_rocprim_test("rocprim.device_transform" test_device_transform.cpp)

# GPU: OK
# CPU: hipErrorOutOfMemory (allocation size exceeds limits for a single allocation)
add_rocprim_test("rocprim.discard_iterator" test_discard_iterator.cpp)

# GPU: CL_OUT_OF_RESOURCES at build time.
# CPU: segfault
add_rocprim_test("rocprim.reverse_iterator" test_reverse_iterator.cpp)
#Crashes at build time, thus disabled.
#opt: ... HipTextureLowering.cpp:459: bool (anonymous namespace)::lowerTextureFunctions(llvm::Module &): Assertion `false && "Unsupported texture function use."' failed
# add_rocprim_test("rocprim.texture_cache_iterator" test_texture_cache_iterator.cpp)

# GPU: OK
# CPU: OK
add_rocprim_test("rocprim.thread" test_thread.cpp)

# GPU: OK
# CPU: OK
add_rocprim_test("rocprim.thread_algos" test_thread_algos.cpp)

# GPU: all fail validation
# CPU: hipErrorTbd (CL_INVALID_ARG_SIZE clSetKernelArg failed)
add_rocprim_test("rocprim.transform_iterator" test_transform_iterator.cpp)
add_rocprim_test("rocprim.no_half_operators" test_no_half_operators.cpp)

add_rocprim_test("rocprim.intrinsics" test_intrinsics.cpp)

# CPU: Likely fails most warp cases in one way or another due to
# the lack of required subgroup size + flat id mapping support.
# GPU: passes
add_rocprim_test("rocprim.warp_exchange" test_warp_exchange.cpp)
add_rocprim_test("rocprim.warp_load" test_warp_load.cpp)
add_rocprim_test("rocprim.warp_reduce" test_warp_reduce.cpp)
add_rocprim_test("rocprim.warp_scan" test_warp_scan.cpp)
add_rocprim_test("rocprim.warp_sort" test_warp_sort.cpp)
add_rocprim_test("rocprim.warp_store" test_warp_store.cpp)

# GPU: 3/4 pass, TransformReduce fail.
# CPU: likely stuck
add_rocprim_test("rocprim.zip_iterator" test_zip_iterator.cpp)

set_tests_properties(
  rocprim.arg_index_iterator
  rocprim.basic_test
  rocprim.intrinsics
  rocprim.warp_exchange
  rocprim.warp_load
  rocprim.warp_reduce
  rocprim.warp_scan
  rocprim.warp_sort
  rocprim.warp_store
  rocprim.constant_iterator
  rocprim.counting_iterator
  rocprim.discard_iterator
  rocprim.thread
  rocprim.thread_algos
  PROPERTIES
  LABELS "chip-spv-gpu"
)

set_tests_properties(
  rocprim.block_shuffle
  rocprim.counting_iterator
  rocprim.intrinsics
  rocprim.thread
  rocprim.thread_algos
  rocprim.warp_load
  PROPERTIES
  LABELS "chip-spv-cpu"
)

# This define is needed for CHIP-SPV because of atomicAdd for unsigned long,
# which is not documented in HIP programmer's manual:
add_compile_definitions(CHIP_ENABLE_NON_COMPLIANT_DEVICELIB_CODE=1)
