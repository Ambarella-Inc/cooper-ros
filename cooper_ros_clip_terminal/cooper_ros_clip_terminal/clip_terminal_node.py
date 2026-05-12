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

#!/usr/bin/env python3

import os
import sys
import json
import time
import threading
from pathlib import Path

# Import readline for better input handling
try:
    import readline
    # Enable tab completion and history
    readline.parse_and_bind('tab: complete')
    readline.set_completer_delims(' \t\n;')
    HAS_READLINE = True
except ImportError:
    HAS_READLINE = False
    print("Warning: readline not available, limited input editing capabilities")

import rclpy
from rclpy.node import Node
from std_msgs.msg import String

# Try to import qos_profile_default, if it fails create it manually
try:
    from rclpy.qos import qos_profile_default
except ImportError:
    from rclpy.qos import QoSProfile
    from rclpy.impl.implementation_singleton import rclpy_implementation as _rclpy
    qos_profile_default = QoSProfile(**_rclpy.rmw_qos_profile_t.predefined(
    'qos_profile_default').to_dict())

PUB_TOPIC_NAME_REQUEST    = "clip_terminal/request"
SUB_TOPIC_NAME_IMAGE_PATH = "image_path"
SUB_TOPIC_NAME_TEXT       = "text"

class ClipTerminalNode(Node):
    def __init__(self):
        super().__init__('clip_terminal_node')
        self.get_logger().info('Cooper ROS CLIP Terminal starting...')

        # Publishers
        self.request_publisher = self.create_publisher(String, PUB_TOPIC_NAME_REQUEST, qos_profile_default)

        # Subscribers
        self.image_path_subscriber = self.create_subscription(
            String, SUB_TOPIC_NAME_IMAGE_PATH, self.image_path_callback, qos_profile_default)
        self.text_subscriber = self.create_subscription(
            String, SUB_TOPIC_NAME_TEXT, self.text_callback, qos_profile_default)

        # State
        self.latest_image_path = None
        self.latest_text = None
        self.waiting_for_response = False

        # Initialize logging
        self.get_logger().info('Subscribed to topics: image_path, text, performance')
        self.get_logger().info('Publishing to topic: request')

        # Log readline status
        if HAS_READLINE:
            self.get_logger().info('Readline support enabled - full input editing available')
        else:
            self.get_logger().info('Readline support disabled - limited input editing')

        # Start terminal interface
        self.setup_terminal()

    def safe_input(self, prompt):
        """Safe input function with readline support"""
        try:
            if HAS_READLINE:
                # Use readline for better input handling
                return input(prompt)
            else:
                # Fallback to standard input
                print(prompt, end='', flush=True)
                return input()
        except (EOFError, KeyboardInterrupt):
            raise
        except Exception as e:
            self.get_logger().error(f"Input error: {e}")
            return ""

    def setup_terminal(self):
        """Set up terminal interface"""
        # Start input thread
        self.input_thread = threading.Thread(target=self.menu_loop, daemon=True)
        self.input_thread.start()

    def image_path_callback(self, msg):
        """Handle image path response from Long-CLIP"""
        self.latest_image_path = msg.data
        if self.waiting_for_response:
            print(f"\nResult:\n{msg.data}")
        self.waiting_for_response = False

    def text_callback(self, msg):
        """Handle text response from Long-CLIP"""
        self.latest_text = msg.data
        if self.waiting_for_response:
            print(f"\nResult:\n{msg.data}")
        self.waiting_for_response = False

    def performance_callback(self, msg):
        """Handle performance metrics from Long-CLIP"""
        try:
            # Parse performance JSON
            performance_data = json.loads(msg.data)

            # Extract metrics
            operation = performance_data.get('operation', 'unknown')
            model = performance_data.get('model', 'unknown')

            # Extract detailed timing information (in microseconds)
            image_delay_us = performance_data.get('image_delay_us', 0)
            preprocess_us = performance_data.get('preprocess_us', 0)
            inference_us = performance_data.get('inference_us', 0)
            post_process_us = performance_data.get('post_process_us', 0)
            cvflow_us = performance_data.get('cvflow_us', 0)

            # Convert to milliseconds for display
            image_delay_ms = image_delay_us / 1000.0
            preprocess_ms = preprocess_us / 1000.0
            inference_ms = inference_us / 1000.0
            post_process_ms = post_process_us / 1000.0
            cvflow_ms = cvflow_us / 1000.0

            # Format and print performance metrics
            print(f"\n Performance Metrics:")
            print(f"   Operation: {operation}")
            print(f"   Model: {model}")
            print(f"   ├─ Image Delay: {image_delay_ms:.2f} ms")
            print(f"   ├─ Preprocess: {preprocess_ms:.2f} ms")
            print(f"   ├─ Inference: {inference_ms:.2f} ms")
            print(f"   ├─ Post Process: {post_process_ms:.2f} ms")
            print(f"   └─ CV Flow: {cvflow_ms:.2f} ms")
            print(f"   {'─' * 40}")

        except json.JSONDecodeError:
            print(f"\n Performance data parsing error: {msg.data}")
        except Exception as e:
            print(f"\n Performance callback error: {e}")

    def send_request(self, request_data):
        """Send JSON request to Long-CLIP node"""
        # Add timestamp_us to request for timing calculations
        request_data["timestamp_us"] = int(time.time() * 1_000_000)

        msg = String()
        msg.data = json.dumps(request_data)
        self.request_publisher.publish(msg)

        self.waiting_for_response = True
        print("Processing...")

    def wait_for_response(self, timeout=30.0):
        """Wait for response with timeout"""
        start_time = time.time()
        while self.waiting_for_response and rclpy.ok():
            if time.time() - start_time > timeout:
                print("Timeout waiting for response")
                self.waiting_for_response = False
                return False
            time.sleep(0.1)

        # If response was received, wait for user to press Enter
        if not self.waiting_for_response:
            time.sleep(0.2)
            print("\nReturning to menu...")

        return True

    def text_to_image_search(self):
        """Handle text-to-image search mode (Option 1)"""
        print("\nText-to-Image Search:")

        try:
            # Get image directory
            image_dir = self.safe_input("<image dir>: ").strip()
            if not image_dir:
                print("Error: Image directory cannot be empty")
                return

            # Get text query
            text_query = self.safe_input("<text>: ").strip()
            if not text_query:
                print("Error: Text query cannot be empty")
                return

            # Create request
            request = {
                "search_type": "text_to_image",
                "image_dir": str(Path(image_dir).absolute()),
                "text": text_query
            }

            print(f"Searching for: '{text_query}' in {image_dir}")
            self.send_request(request)

            # Wait for response
            self.wait_for_response()

        except KeyboardInterrupt:
            print("\nCancelled")
            self.waiting_for_response = False
        except Exception as e:
            print(f"Error: {e}")
            self.waiting_for_response = False

    def image_to_text_search(self):
        """Handle image-to-text matching mode (Option 2)"""
        print("\nImage-to-Text Match:")

        try:
            # Get text file
            text_file = self.safe_input("<text file>: ").strip()
            if not text_file:
                print("Error: Text file path cannot be empty")
                return

            # Get image file
            image_file = self.safe_input("<image file>: ").strip()
            if not image_file:
                print("Error: Image file path cannot be empty")
                return

            # Create request
            request = {
                "search_type": "image_to_text",
                "text_file": str(Path(text_file).absolute()),
                "image_file": str(Path(image_file).absolute())
            }

            print(f"Finding text match for: {image_file} using {text_file}")
            self.send_request(request)

            # Wait for response
            self.wait_for_response()

        except KeyboardInterrupt:
            print("\nCancelled")
            self.waiting_for_response = False
        except Exception as e:
            print(f"Error: {e}")
            self.waiting_for_response = False

    def image_to_image_search(self):
        """Handle image-to-image search mode (Option 3)"""
        print("\nImage-to-Image Search:")

        try:
            # Get target image directory
            image_dir = self.safe_input("<image dir>: ").strip()
            if not image_dir:
                print("Error: Image directory cannot be empty")
                return

            # Get query image
            query_image = self.safe_input("<image file>: ").strip()
            if not query_image:
                print("Error: Query image path cannot be empty")
                return

            # Create request
            request = {
                "search_type": "image_to_image",
                "image_dir": str(Path(image_dir).absolute()),
                "image_file": str(Path(query_image).absolute())
            }

            print(f"Finding similar images to: {query_image} in {image_dir}")
            self.send_request(request)

            # Wait for response
            self.wait_for_response()

        except KeyboardInterrupt:
            print("\nCancelled")
            self.waiting_for_response = False
        except Exception as e:
            print(f"Error: {e}")
            self.waiting_for_response = False

    def show_menu(self):
        """Display the main menu"""
        print("\nCLIP Terminal:")
        print("1. Text-to-image search")
        print("2. Image-to-text match")
        print("3. Image-to-image search")
        if HAS_READLINE:
            print("Navigation: Use ← → arrows to edit input, ↑ ↓ for history")
        print("Enter 1-3 or 'q' to quit: ", end="", flush=True)

    def menu_loop(self):
        """Main menu loop"""
        self.show_menu()

        while rclpy.ok():
            try:
                if self.waiting_for_response:
                    time.sleep(0.1)
                    continue

                user_input = self.safe_input("").strip().lower()

                if user_input in ['q', 'quit', 'exit']:
                    print("Goodbye!")
                    os._exit(0)

                elif user_input == '1':
                    self.text_to_image_search()
                    self.show_menu()

                elif user_input == '2':
                    self.image_to_text_search()
                    self.show_menu()

                elif user_input == '3':
                    self.image_to_image_search()
                    self.show_menu()

                elif user_input in ['menu', 'help']:
                    self.show_menu()

                elif user_input == '':
                    continue

                else:
                    print(f"Invalid option: '{user_input}'. Enter 1-3 or 'q' to quit.")

            except (EOFError, KeyboardInterrupt):
                print("\nGoodbye!")
                os._exit(0)
            except Exception as e:
                self.get_logger().error(f"Menu loop error: {e}")


def main(args=None):
    try:
        rclpy.init(args=args)
        node = ClipTerminalNode()

        # Use MultiThreadedExecutor for concurrent callback processing
        from rclpy.executors import MultiThreadedExecutor
        executor = MultiThreadedExecutor()

        try:
            executor.add_node(node)
            executor.spin()
        except KeyboardInterrupt:
            print("\nShutting down CLIP Terminal...")
        finally:
            node.destroy_node()
            executor.shutdown()

    except KeyboardInterrupt:
        print("\nGoodbye!")
    finally:
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
