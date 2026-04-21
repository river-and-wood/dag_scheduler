cmake_minimum_required(VERSION 3.20)
project(my_dag_scheduler VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
  message(FATAL_ERROR "ENABLE_ASAN and ENABLE_TSAN are mutually exclusive")
endif()

set(SAN_FLAGS "")
if(ENABLE_ASAN)
  set(SAN_FLAGS -fsanitize=address -fno-omit-frame-pointer)
elseif(ENABLE_TSAN)
  set(SAN_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
endif()

add_library(dag_core
  src/types.cpp
  src/workflow.cpp
  src/dag_builder.cpp
  src/executor.cpp
  src/state_store.cpp
  src/observer.cpp
  src/replay.cpp
  src/scheduler.cpp
)

target_include_directories(dag_core PUBLIC include)

target_compile_options(dag_core PRIVATE -Wall -Wextra -Wpedantic)
if(SAN_FLAGS)
  target_compile_options(dag_core PRIVATE ${SAN_FLAGS})
  target_link_options(dag_core PRIVATE ${SAN_FLAGS})
endif()

add_executable(dag_scheduler src/main.cpp)
target_link_libraries(dag_scheduler PRIVATE dag_core)
if(SAN_FLAGS)
  target_compile_options(dag_scheduler PRIVATE ${SAN_FLAGS})
  target_link_options(dag_scheduler PRIVATE ${SAN_FLAGS})
endif()

enable_testing()
add_executable(dag_tests tests/test_main.cpp)
target_link_libraries(dag_tests PRIVATE dag_core)
target_compile_options(dag_tests PRIVATE -Wall -Wextra -Wpedantic)
if(SAN_FLAGS)
  target_compile_options(dag_tests PRIVATE ${SAN_FLAGS})
  target_link_options(dag_tests PRIVATE ${SAN_FLAGS})
endif()
add_test(NAME dag_tests COMMAND dag_tests)
