cmake_minimum_required(VERSION 3.8)
project(cooper_ros_iav_osd)

# Set policy CMP0148 to suppress warnings about FindPythonInterp
if(POLICY CMP0148)
  cmake_policy(SET CMP0148 OLD)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wno-variadic-macros -Wno-pedantic)
endif()

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(NOT CMAKE_C_STANDARD)
  set(CMAKE_C_STANDARD 99)
endif()

find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(vision_msgs REQUIRED)
find_package(message_filters REQUIRED)
find_package(cooper_ros_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
if(${cv_bridge_VERSION} VERSION_LESS_EQUAL "3.2.1")
  add_definitions(-DUSE_CV_BRIDGE_H)
endif()

find_package(OpenCV REQUIRED)

if ("$ENV{ENV_BUILD_MODE}" STREQUAL "yocto")
  set(EAZYAI_INCLUDE_DIRS ${CMAKE_INC_PREFIX}/libeazyai)
  set(EAZYAI_UTILS_INCLUDE_DIRS ${CMAKE_INC_PREFIX}/libeazyai-utils)
  set(EAZYAI_IO_INCLUDE_DIRS ${CMAKE_INC_PREFIX}/libeazyai-io)

  set(EAZYAI_LIBRARIES eazyai)
  set(EAZYAI_UTILS_LIBRARIES eazyai_utils)
  set(EAZYAI_IO_LIBRARIES eazyai_io)
else ()
  find_package(PkgConfig)
  pkg_search_module(EAZYAI REQUIRED libamba-eazyai-$ENV{AMBARELLA_ARCH})
  pkg_search_module(EAZYAI_UTILS REQUIRED libamba-eazyai-utils-$ENV{AMBARELLA_ARCH})
  pkg_search_module(EAZYAI_IO REQUIRED libamba-eazyai-io-$ENV{AMBARELLA_ARCH})
endif ()

set(TARGET_DEPS
  rclcpp
  rclcpp_components
  std_msgs
  sensor_msgs
  vision_msgs
  cv_bridge
  message_filters
  cooper_ros_msgs)

set(INCLUDE_DIRS
  ${EAZYAI_INCLUDE_DIRS}
  ${EAZYAI_UTILS_INCLUDE_DIRS}
  ${EAZYAI_IO_INCLUDE_DIRS})

set(LINK_LIBS
  ${EAZYAI_LIBRARIES}
  ${EAZYAI_UTILS_LIBRARIES}
  ${EAZYAI_IO_LIBRARIES}
  opencv_core opencv_imgcodecs)

include_directories(include)

add_executable(iav_osd src/iav_osd.cpp src/iav_osd_node.cpp)
target_include_directories(iav_osd PRIVATE ${INCLUDE_DIRS})
target_link_libraries(iav_osd ${LINK_LIBS})
ament_target_dependencies(iav_osd ${TARGET_DEPS})

add_library(iav_osd_node SHARED src/iav_osd_node.cpp)
target_include_directories(iav_osd_node PRIVATE ${INCLUDE_DIRS})
target_link_libraries(iav_osd_node ${LINK_LIBS})
ament_target_dependencies(iav_osd_node ${TARGET_DEPS})

rclcpp_components_register_nodes(iav_osd_node "cooper_ros::iav_osd::IAV_OSD_Node")
set(node_plugins "${node_plugins}cooper_ros::iav_osd::IAV_OSD_Node;$<TARGET_FILE:iav_osd_node>\n")
set_target_properties(iav_osd_node PROPERTIES
  BUILD_WITH_INSTALL_RPATH TRUE
  BUILD_RPATH_USE_ORIGIN TRUE
  INSTALL_RPATH_USE_LINK_PATH TRUE)

install(TARGETS iav_osd iav_osd_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY params DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # comment the line when a copyright and license is added to all source files
  set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # comment the line when this package is in a git repo and when
  # a copyright and license is added to all source files
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
