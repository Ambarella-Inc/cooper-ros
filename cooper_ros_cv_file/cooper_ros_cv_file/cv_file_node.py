# Copyright (c) 2025 Ambarella International LP
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node
# Try to import qos_profile_default, if it fails create it manually
try:
    from rclpy.qos import qos_profile_default
except ImportError:
    from rclpy.qos import QoSProfile
    from rclpy.impl.implementation_singleton import rclpy_implementation as _rclpy
    qos_profile_default = QoSProfile(**_rclpy.rmw_qos_profile_t.predefined(
    'qos_profile_default').to_dict())

from std_msgs.msg import String
import sensor_msgs.msg
import vision_msgs.msg
import cooper_ros_msgs.msg
import os
import cv2 as cv
import numpy as np
from cv_bridge import CvBridge

COLOR_MAP = {
    "aqua": (0, 255, 255),       # Aqua
    "black": (0, 0, 0),          # Black
    "blue": (0, 0, 255),         # Blue
    "fuchsia": (255, 0, 255),    # Fuchsia
    "gray": (128, 128, 128),     # Gray
    "green": (0, 128, 0),        # Green
    "lime": (0, 255, 0),         # Lime
    "maroon": (128, 0, 0),       # Maroon
    "navy": (0, 0, 128),         # Navy
    "olive": (128, 128, 0),      # Olive
    "purple": (128, 0, 128),     # Purple
    "red": (255, 0, 0),          # Red
    "silver": (192, 192, 192),   # Silver
    "teal": (0, 128, 128),       # Teal
    "white": (255, 255, 255),    # White
    "yellow": (255, 255, 0),     # Yellow
}

COLOR_LIST = tuple(COLOR_MAP.values())

PUB_TOPIC_NAME_IMAGE          = "cv_file/image"
SUB_TOPIC_NAME_DETECTION      = "detections"
SUB_TOPIC_NAME_CLASSIFICATION = "classification"
SUB_TOPIC_NAME_SEGMENTATION   = "segmentation"
SUB_TOPIC_NAME_LANDMARK       = "landmarks"

PARAM_NAME_INPUT_PATH = "input_path"
PARAM_NAME_ENCODING   = "encoding"
PARAM_NAME_WIDTH      = "width"
PARAM_NAME_HEIGHT     = "height"
PARAM_NAME_OUTPUT_DIR = "output_dir"
PARAM_NAME_FPS        = "fps"
PARAM_NAME_LOOP_COUNT = "loop_count"


def string_to_index_unsigned(s, n):
    hash_val = 0
    for ch in s:
        hash_val = (hash_val * 131 + ord(ch))
    return hash_val % n


class CvFileNode(Node):

    def __init__(self):
        super().__init__('CvFileNode')
        self.input_path = self.declare_parameter(PARAM_NAME_INPUT_PATH, '').value
        assert self.input_path != '', 'input_path is not set'
        self.get_logger().info('input_path: %s' % self.input_path)
        self.encoding = self.declare_parameter(PARAM_NAME_ENCODING, 'rgb8').value
        assert self.encoding in ['rgb8', 'bgr8', 'mono8'], 'encoding must be rgb8 or bgr8 or mono8'
        self.get_logger().info('encoding: %s' % self.encoding)
        self.width = self.declare_parameter(PARAM_NAME_WIDTH, 0).value
        self.get_logger().info('width: %s' % self.width)
        self.height = self.declare_parameter(PARAM_NAME_HEIGHT, 0).value
        self.get_logger().info('height: %s' % self.height)
        self.output_dir = self.declare_parameter(PARAM_NAME_OUTPUT_DIR, './').value
        assert os.path.exists(self.output_dir), 'output_dir does not exist'
        assert os.path.isdir(self.output_dir), 'output_dir is not a directory'
        self.get_logger().info('output_dir: %s' % self.output_dir)
        self.fps = self.declare_parameter(PARAM_NAME_FPS, 10.0).value
        self.get_logger().info('fps: %s' % self.fps)
        self.loop_count = self.declare_parameter(PARAM_NAME_LOOP_COUNT, 1).value
        self.get_logger().info('loop_count: %s' % self.loop_count)

        self.image_file_list = []
        if os.path.isdir(self.input_path):
            self.image_file_list = [os.path.join(self.input_path, f) for f in sorted(os.listdir(self.input_path)) if f.endswith('.jpg') or f.endswith('.png')]
        else:
            self.image_file_list = [self.input_path]
        self.cur_image_file_index = 0
        self.get_logger().info('image_file_list: %s' % self.image_file_list)

        self.landmark_list = []
        self.detection_list = []
        self.classification_list = []
        self.segmentation_list = []
        self.published_image_count = 0

        self.timer = self.create_timer(1.0/self.fps, self.timer_callback)
        self.publish_done_time = None

        self.detection_subscription = self.create_subscription(
            vision_msgs.msg.Detection2DArray,
            SUB_TOPIC_NAME_DETECTION,
            self.detection_callback,
            qos_profile_default
        )
        self.landmark_subscription = self.create_subscription(
            cooper_ros_msgs.msg.LandmarkDetection2DArray,
            SUB_TOPIC_NAME_LANDMARK,
            self.landmark_callback,
            qos_profile_default
        )
        self.classification_subscription = self.create_subscription(
            vision_msgs.msg.Classification,
            SUB_TOPIC_NAME_CLASSIFICATION,
            self.classification_callback,
            qos_profile_default
        )
        self.segmentation_subscription = self.create_subscription(
            sensor_msgs.msg.Image,
            SUB_TOPIC_NAME_SEGMENTATION,
            self.segmentation_callback,
            qos_profile_default
        )
        self.init_256_color_table()
        self.image_publisher = self.create_publisher(
            sensor_msgs.msg.Image,
            PUB_TOPIC_NAME_IMAGE,
            qos_profile_default
        )

        self.get_logger().info('CV_FileNode initialized')

    def timer_callback(self):
        result_count = max(len(self.detection_list), len(self.classification_list), len(self.segmentation_list))
        if self.loop_count > 0 and self.image_publisher.get_subscription_count() > 0:
            self.publish_image(self.image_file_list[self.cur_image_file_index])
            self.cur_image_file_index = (self.cur_image_file_index + 1) % len(self.image_file_list)
            self.published_image_count += 1
            self.get_logger().info(f'published {self.published_image_count} images')
            if self.cur_image_file_index == 0:
                self.loop_count -= 1
                self.get_logger().info('loop_count: %s' % self.loop_count)
                if self.loop_count == 0:
                    self.publish_done_time = self.get_clock().now()

        if self.published_image_count > 0 and \
            (len(self.detection_list) == self.published_image_count or \
                len(self.classification_list) == self.published_image_count or \
                len(self.segmentation_list) == self.published_image_count):
            self.draw_result()
            self.get_logger().info(f'published {self.published_image_count} images, ' \
                f'received {result_count} results, done, exit')
            rclpy.shutdown()
        elif self.publish_done_time is not None \
            and self.get_clock().now() - self.publish_done_time > rclpy.duration.Duration(seconds=3):
            self.draw_result()
            self.get_logger().info(f'published {self.published_image_count} images, ' \
                f'received {result_count} results, timeout, exit')
            rclpy.shutdown()

    def detection_callback(self, msg):
        self.detection_list.append(msg)

    def landmark_callback(self, msg):
        self.landmark_list.append(msg)

    def classification_callback(self, msg):
        self.classification_list.append(msg)

    def segmentation_callback(self, msg):
        self.segmentation_list.append(msg)

    def has_result(self):
        return len(self.detection_list) > 0 or len(self.classification_list) > 0 or len(self.segmentation_list) > 0

    def draw_result(self):
        if not self.has_result():
            return
        if len(self.landmark_list) > 0: # landmark is attached to detection for face detection
            assert len(self.detection_list) == len(self.landmark_list), 'detection_list and landmark_list should have the same length'

        for i, path in enumerate(self.image_file_list):
            image = cv.imread(path)
            if len(self.detection_list) > 0:
                for k, detection in enumerate(self.detection_list[i].detections):
                    bbox = detection.bbox
                    x = int(image.shape[1] * (bbox.center.position.x - bbox.size_x / 2))
                    y = int(image.shape[0] * (bbox.center.position.y - bbox.size_y / 2))
                    w = int(image.shape[1] * bbox.size_x)
                    h = int(image.shape[0] * bbox.size_y)
                    color = COLOR_LIST[string_to_index_unsigned(detection.results[0].hypothesis.class_id, len(COLOR_LIST))]
                    cv.rectangle(image, (x, y), (x + w, y + h), color, 2)
                    text = f"{detection.results[0].hypothesis.class_id} {detection.results[0].hypothesis.score:.3f}"
                    cv.putText(image, text, (x, y), cv.FONT_HERSHEY_SIMPLEX, 0.5, COLOR_MAP['white'], 2)
                    if len(self.landmark_list) > 0:
                        for j, point in enumerate(self.landmark_list[i].detections[k].points):
                            x = int(image.shape[1] * point.x)
                            y = int(image.shape[0] * point.y)
                            cv.circle(image, (x, y), 5, COLOR_LIST[j % len(COLOR_LIST)], -1)
            if len(self.classification_list) > 0:
                for k, result in enumerate(self.classification_list[i].results):
                    text = f"{result.score:.3f} {result.class_id}"
                    cv.putText(image, text, (10, 30 + 30 * k), cv.FONT_HERSHEY_SIMPLEX, 1.0, COLOR_MAP['white'], 2)
            if len(self.segmentation_list) > 0:
                mono_img = CvBridge().imgmsg_to_cv2(self.segmentation_list[i], 'mono8')
                mono_img = cv.resize(mono_img, (image.shape[1], image.shape[0]), interpolation=cv.INTER_NEAREST)
                colored_mono = np.zeros((image.shape[0], image.shape[1], 4), dtype=np.uint8)
                colored_mono[:, :, :4] = self.color_table[mono_img, :4]
                blended = image.copy()
                for c in range(3):
                    blended[:, :, c] = (image[:, :, c] * (1.0 - (colored_mono[:, :, 3] / 255.0)) + colored_mono[:, :, c] * (colored_mono[:, :, 3] / 255.0))
                image = blended
            cv.imwrite(os.path.join(self.output_dir, os.path.basename(path)), image)

    def init_256_color_table(self):
        self.color_table = np.zeros((256, 4), dtype=np.uint8)
        ind = np.zeros(256, dtype=np.uint8)
        for i in range(256):
            ind[i] = i
        for shift in range(7, -1, -1):
            for i in range(256):
                self.color_table[i, 0] |= ((ind[i] >> 0) & 1) << shift
                self.color_table[i, 1] |= ((ind[i] >> 1) & 1) << shift
                self.color_table[i, 2] |= ((ind[i] >> 2) & 1) << shift
                self.color_table[i, 3] = 200
            for i in range(256):
                ind[i] >>= 3
        #convert from RGBA to BGRA
        self.color_table[:, [0, 2]] = self.color_table[:, [2, 0]]

    def publish_image(self, image_file_path):
        # Read image using opencv, then convert to ROS image
        if self.encoding == 'mono8':
            image = cv.imread(image_file_path, cv.IMREAD_GRAYSCALE)
        else:
            image = cv.imread(image_file_path, cv.IMREAD_COLOR)
        if self.encoding == 'rgb8':
            image = cv.cvtColor(image, cv.COLOR_BGR2RGB)
        if self.width > 0 and self.height > 0:
            image = cv.resize(image, (self.width, self.height))
        image = CvBridge().cv2_to_imgmsg(image, encoding=self.encoding)
        image.header.frame_id = 'cv_file'
        image.header.stamp = self.get_clock().now().to_msg()
        self.image_publisher.publish(image)
