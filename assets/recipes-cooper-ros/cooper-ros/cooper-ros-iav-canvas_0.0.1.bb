# Copyright (C) 2025 Ambarella International LP

inherit ros_distro_humble
inherit ros_superflore_generated

SUMMARY = "This project is part of the Cooper ROS initiative, which offers source code for running DNNs, VLMs, and robotic algorithms using Cooper Foundry on ROS2."
DESCRIPTION = "This project is part of the Cooper ROS initiative, which offers source code for running DNNs, VLMs, and robotic algorithms using Cooper Foundry on ROS2."
AUTHOR = "Cooper ROS Maintainers <cooper_robot@ambarella.com>"
ROS_AUTHOR = "Cooper ROS Maintainers <cooper_robot@ambarella.com>"
SECTION = "devel"
# Original license in package.xml, joined with "&" when multiple license tags were used:
#         "Apache License 2.0 & Apache License 2.0"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://package.xml;beginline=8;endline=8;md5=82f0323c08605e5b6f343b05213cf7cc"

ROS_CN = "cooper_ros"
ROS_BPN = "cooper_ros_iav_canvas"

AMBA_ESRC = "${ENV_TOP_DIR}/app/cooper_ros/cooper_ros_iav_canvas"
AMBA_ESRC_BUILD = "${ENV_TOP_DIR}/app/cooper_ros/cooper_ros_iav_canvas"
SRC_URI = ""

DEPENDS = "opencv generic-header ambvideo-header board libeazyai libeazyai-utils libeazyai-postprocess"
RDEPENDS:${PN} = "opencv libeazyai libeazyai-utils libeazyai-postprocess"
EXTRADEPS = "AMBA_SOC!=s6lm"
EXTRA_OECMAKE += "-DCMAKE_INC_PREFIX=${STAGING_INCDIR}"

inherit ambaenv
inherit ambasrc
inherit ambadebug
inherit weakdep

ROS_BUILD_DEPENDS = " \
    rclcpp \
    rclcpp-components \
    std-msgs \
    sensor-msgs \
"

ROS_EXEC_DEPENDS = " \
    rclcpp \
    rclcpp-components \
    std-msgs \
    sensor-msgs \
"

ROS_BUILDTOOL_DEPENDS = " \
    ament-cmake-native \
"

ROS_EXPORT_DEPENDS = ""

ROS_BUILDTOOL_EXPORT_DEPENDS = ""

# Currently informational only -- see http://www.ros.org/reps/rep-0149.html#dependency-tags.
ROS_TEST_DEPENDS = ""

DEPENDS += "${ROS_BUILD_DEPENDS} ${ROS_BUILDTOOL_DEPENDS}"
# Bitbake doesn't support the "export" concept, so build them as if we needed them to build this package (even though we actually
# don't) so that they're guaranteed to have been staged should this package appear in another's DEPENDS.
DEPENDS += "${ROS_EXPORT_DEPENDS} ${ROS_BUILDTOOL_EXPORT_DEPENDS}"

RDEPENDS:${PN} += "${ROS_EXEC_DEPENDS}"

ROS_BUILD_TYPE = "ament_cmake"

inherit ros_${ROS_BUILD_TYPE}
