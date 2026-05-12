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

import os
import sys
import json
import time
import threading
from pathlib import Path

import rclpy
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
from sensor_msgs.msg import Image
import cv2
from cv_bridge import CvBridge

PUB_TOPIC_NAME_IMAGE_CAPTURED = "vlm_terminal/image_captured"
PUB_TOPIC_NAME_USER_CONTENT   = "vlm_terminal/user_content"
SUB_TOPIC_NAME_IMAGE          = "image"
SUB_TOPIC_NAME_RESPONSE       = "response"
SUB_TOPIC_NAME_PERFORMANCE    = "performance"

# Timeout configuration
RESPONSE_TIMEOUT_SECONDS = 30.0


class VLMTerminalNode(Node):
    def __init__(self):
        super().__init__('vlm_terminal_node')
        self.get_logger().info('Cooper ROS VLM Terminal starting...')



        # Publishers
        self.image_captured_publisher = self.create_publisher(Image, PUB_TOPIC_NAME_IMAGE_CAPTURED, qos_profile_default)
        self.user_content_publisher = self.create_publisher(String, PUB_TOPIC_NAME_USER_CONTENT, qos_profile_default)

        # Subscribers
        self.image_subscriber = self.create_subscription(
            Image, SUB_TOPIC_NAME_IMAGE, self.image_callback, qos_profile_default)
        self.response_subscriber = self.create_subscription(
            String, SUB_TOPIC_NAME_RESPONSE, self.response_callback, qos_profile_default)
        self.performance_subscriber = self.create_subscription(
            String, SUB_TOPIC_NAME_PERFORMANCE, self.performance_callback, qos_profile_default)

        # State
        self.current_image = None
        self.image_published = False  # Track if image has been published to LLaVA
        self.latest_response = None
        self.latest_performance = None
        self.waiting_for_response = False
        self.chat_mode = False
        self.streaming_response = False
        self.stream_buffer = ""
        self.response_start_time = None  # Track when response waiting started

        # OpenCV bridge
        self.bridge = CvBridge()

        # Start terminal interface
        self.setup_terminal()

    def setup_terminal(self):
        """Set up terminal interface"""

        # Start input thread
        self.input_thread = threading.Thread(target=self.menu_loop, daemon=True)
        self.input_thread.start()

    def image_callback(self, msg):
        """Handle image from iav_canvas/rgb8 topic"""
        try:
            self.current_image = self.bridge.imgmsg_to_cv2(msg, 'bgr8')
            # Image is received from camera stream, can be used for processing
        except Exception as e:
            self.get_logger().error(f'Error processing image: {e}')

    def response_callback(self, msg):
        """Handle LLaVA streaming response"""
        token = msg.data
        if self.waiting_for_response:
            # Reset timeout timer when we receive any response
            self.response_start_time = time.time()

            if token == '<EOS>':
                # End of streaming - add newline and reset
                print('\n')
                self.streaming_response = False
                self.stream_buffer = ''
                self.waiting_for_response = False
                self.response_start_time = None

                if not self.chat_mode:
                    self.show_menu()
            else:
                # Regular token
                if not self.streaming_response:
                    # First token - start streaming
                    self.streaming_response = True
                    self.stream_buffer = ''
                    print('LLaVA: ', end='', flush=True)

                # Print token immediately
                print(token, end='', flush=True)
                self.stream_buffer += token
        else:
            # Not waiting for response - could be error message
            if not self.streaming_response:
                print(f'LLaVA: {msg.data}\n')

    def performance_callback(self, msg):
        """Handle performance metrics"""
        self.latest_performance = msg.data
        try:
            perf_data = json.loads(msg.data)
            processing_time = perf_data.get('processing_time_ms', 0)
            # if processing_time > 0:
            #     self.get_logger().info(f'Processing time: {processing_time}ms')
        except json.JSONDecodeError:
            pass

    def capture_camera(self):
        """Capture image from camera"""
        print('\nCapturing camera image...')

        if self.current_image is None:
            print('Error: No camera image available from iav_canvas/rgb8')
            return False

        try:
            # Convert OpenCV image to ROS message
            img_msg = self.bridge.cv2_to_imgmsg(self.current_image, 'bgr8')
            img_msg.header.stamp = self.get_clock().now().to_msg()

            # Publish captured image
            self.image_captured_publisher.publish(img_msg)
            self.image_published = True  # Mark image as published

            print('Camera image captured and sent to LLaVA')
            return True

        except Exception as e:
            print(f'Error capturing camera: {e}')
            return False

    def read_image_file(self):
        """Read image from file"""
        print('\nRead Image File:')

        try:
            image_path = input('<image file>: ').strip()
            if not image_path:
                print('Error: Image file path cannot be empty')
                return False

            file_path = Path(image_path)
            if not file_path.exists() or not file_path.is_file():
                print(f'Error: Image file does not exist: {image_path}')
                return False

            # Read image using OpenCV
            image = cv2.imread(str(file_path))
            if image is None:
                print(f'Error: Cannot read image file: {image_path}')
                return False

            # Convert to ROS message
            img_msg = self.bridge.cv2_to_imgmsg(image, 'bgr8')
            img_msg.header.stamp = self.get_clock().now().to_msg()

            # Publish image
            self.image_captured_publisher.publish(img_msg)
            self.image_published = True  # Mark image as published

            print(f'Image file read: {image_path}')
            return True

        except KeyboardInterrupt:
            print('\nCancelled')
            return False
        except Exception as e:
            print(f'Error reading image file: {e}')
            return False

    def start_chat(self):
        """Start chat mode"""
        print('\nChat Mode:')
        print('Ask questions about the current image. Type \'q\' to quit.')

        self.chat_mode = True

        while self.chat_mode:
            try:
                user_input = input('<text>(question): ').strip()

                if user_input.lower() == 'q':
                    self.chat_mode = False
                    print('Exiting chat mode')
                    break

                if not user_input:
                    continue

                # Send user question to LLaVA
                msg = String()
                msg.data = user_input
                self.user_content_publisher.publish(msg)

                # print(f'You: {user_input}')
                print('Waiting for response...')
                self.waiting_for_response = True
                self.response_start_time = time.time()

                # Wait for response before showing next prompt
                while self.waiting_for_response and self.chat_mode:
                    # Check for timeout
                    if self.response_start_time and (time.time() - self.response_start_time) > RESPONSE_TIMEOUT_SECONDS:
                        print('\nTimeout: No response received within 30 seconds')
                        self.waiting_for_response = False
                        self.streaming_response = False
                        self.stream_buffer = ""
                        self.response_start_time = None
                        break
                    time.sleep(0.1)

            except (EOFError, KeyboardInterrupt):
                self.chat_mode = False
                print('\nExiting chat mode')
                break
            except Exception as e:
                self.get_logger().error(f'Chat error: {e}')

    def show_menu(self):
        """Display the main menu"""
        print('\nVLM Terminal:')
        print('1. Capture camera')
        print('2. Read image file')
        print('3. Chat')
        print('Enter 1-3 or \'q\' to quit: ', end='', flush=True)

    def menu_loop(self):
        """Main menu loop"""
        self.show_menu()

        while rclpy.ok():
            try:
                if self.waiting_for_response:
                    # Check for timeout
                    if self.response_start_time and (time.time() - self.response_start_time) > RESPONSE_TIMEOUT_SECONDS:
                        print('\nTimeout: No response received within 30 seconds')
                        self.waiting_for_response = False
                        self.streaming_response = False
                        self.stream_buffer = ""
                        self.response_start_time = None
                        self.show_menu()
                        continue
                    time.sleep(0.1)
                    continue

                if self.chat_mode:
                    time.sleep(0.1)
                    continue

                user_input = input().strip().lower()

                if user_input in ['q', 'quit', 'exit']:
                    print('Goodbye!')
                    os._exit(0)

                elif user_input == '1':
                    success = self.capture_camera()
                    if success:
                        print('Image captured. Use option 3 to chat about it.')
                    self.show_menu()

                elif user_input == '2':
                    success = self.read_image_file()
                    if success:
                        print('Image loaded. Use option 3 to chat about it.')
                    self.show_menu()

                elif user_input == '3':
                    # Check if image has been published to LLaVA
                    if not self.image_published:
                        print('Error: No image has been published to LLaVA. Please select option 1 (Capture camera) or 2 (Read image file) first.')
                    else:
                        self.start_chat()
                    self.show_menu()

                elif user_input in ['menu', 'help']:
                    self.show_menu()

                elif user_input == '':
                    continue

                else:
                    print(f'Invalid option: \'{user_input}\'. Enter 1-3 or \'q\' to quit.')

            except (EOFError, KeyboardInterrupt):
                print('\nGoodbye!')
                os._exit(0)
            except Exception as e:
                self.get_logger().error(f'Menu loop error: {e}')


def main(args=None):
    try:
        rclpy.init(args=args)
        node = VLMTerminalNode()

        # Use MultiThreadedExecutor for concurrent callback processing
        from rclpy.executors import MultiThreadedExecutor
        executor = MultiThreadedExecutor()

        try:
            executor.add_node(node)
            executor.spin()
        except KeyboardInterrupt:
            print('\nShutting down VLM Terminal...')
        finally:
            node.destroy_node()
            executor.shutdown()

    except KeyboardInterrupt:
        print('\nGoodbye!')
    finally:
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
