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

"""
LLM Proxy Terminal Node using topic communication
"""

import rclpy
from rclpy.node import Node
from std_msgs.msg import String
import threading
import json
import time

PUB_TOPIC_NAME_USER_CONTENT = "llm_proxy/user_content"
SUB_TOPIC_NAME_RESPONSE     = "response"

PARAM_NAME_MODEL = "model"


class LLMProxyTerminal(Node):
    """
    ROS 2 terminal node for user interaction with LLM Proxy using topic communication
    """

    def __init__(self):
        super().__init__('llm_proxy_terminal')

        # Declare parameters
        self.declare_parameter(PARAM_NAME_MODEL, 'qwen3.5-flash')

        # Get parameter values
        self.default_model = self.get_parameter(PARAM_NAME_MODEL).value

        # Fixed topic names
        self.user_content_topic = PUB_TOPIC_NAME_USER_CONTENT
        self.ai_response_topic = SUB_TOPIC_NAME_RESPONSE

        # Create publisher and subscriber
        self.publisher = self.create_publisher(
            String,
            self.user_content_topic,
            10
        )

        self.subscription = self.create_subscription(
            String,
            self.ai_response_topic,
            self.ai_response_callback,
            10
        )

        # Current model being used
        self.current_model = self.default_model

        # Flag for waiting for response
        self.waiting_for_response = False
        self.latest_response = None

        # Thread lock
        self.lock = threading.Lock()

        self.get_logger().info('LLM Proxy Terminal ready!')
        self.get_logger().info(f'Publishing to: {self.user_content_topic}')
        self.get_logger().info(f'Subscribing to: {self.ai_response_topic}')

        # Start user input thread
        self.input_thread = threading.Thread(target=self.user_input_loop)
        self.input_thread.daemon = True
        self.input_thread.start()

    def ai_response_callback(self, msg):
        """
        Handle AI response messages
        """
        try:
            # Parse JSON data
            response_data = json.loads(msg.data)

            with self.lock:
                if self.waiting_for_response:
                    self.latest_response = response_data
                    self.waiting_for_response = False

        except json.JSONDecodeError as e:
            self.get_logger().error(f'Failed to parse JSON response: {e}')
        except Exception as e:
            self.get_logger().error(f'Error processing response: {e}')

    def send_request(self, input_text, model=None):
        """
        Send inference request
        """
        if model is None:
            model = self.current_model

        # Create request data
        request_data = {
            'model': model,
            'input_text': input_text
        }

        # Set waiting for response flag
        with self.lock:
            self.waiting_for_response = True
            self.latest_response = None

        # Publish request
        msg = String()
        msg.data = json.dumps(request_data, ensure_ascii=False)
        self.publisher.publish(msg)

        return True

    def user_input_loop(self):
        """
        User input loop
        """
        print(f"\n=== LLM Proxy Terminal (Topic Communication) ===")
        print(f"Current model: {self.current_model}")
        print(f"Type your message and press Enter. Use /quit to exit.")
        print(f"Commands: /quit, /model <model_name>, /help, /status")
        print("=" * 60)

        while rclpy.ok():
            try:
                user_input = input("\nUser: ").strip()

                if not user_input:
                    continue

                # Handle commands
                if user_input.startswith('/'):
                    if user_input == '/quit':
                        print("Goodbye!")
                        rclpy.shutdown()
                        break
                    elif user_input == '/help':
                        self.show_help()
                    elif user_input.startswith('/model'):
                        parts = user_input.split(' ', 1)
                        if len(parts) == 2:
                            new_model = parts[1].strip()
                            if new_model:
                                self.current_model = new_model
                                print(f"Model changed to: {self.current_model}")
                            else:
                                print("Please provide a model name")
                        else:
                            print(f"Current model: {self.current_model}")
                    elif user_input == '/status':
                        self.show_status()
                    else:
                        print("Unknown command. Use /help for available commands.")
                    continue

                # Send inference request
                print("AI: Thinking...")
                self.send_request(user_input)

                # Wait for response (with timeout)
                timeout = 30.0  # 30 seconds timeout
                start_time = time.time()

                while time.time() - start_time < timeout:
                    with self.lock:
                        if not self.waiting_for_response and self.latest_response is not None:
                            # Display response
                            if self.latest_response.get('success', False):
                                print(f"AI: {self.latest_response.get('response_text', 'No response')}")
                            else:
                                print(f"Error: {self.latest_response.get('error_message', 'Unknown error')}")
                            break
                    time.sleep(0.1)
                else:
                    # Timeout
                    with self.lock:
                        self.waiting_for_response = False
                    print("Error: Request timeout")

            except KeyboardInterrupt:
                print("\nGoodbye!")
                rclpy.shutdown()
                break
            except EOFError:
                print("\nGoodbye!")
                rclpy.shutdown()
                break
            except Exception as e:
                print(f"Error: {e}")

    def show_help(self):
        """
        Display help information
        """
        print("\n=== LLM Proxy Terminal Help ===")
        print("Commands:")
        print("  /quit                 - Exit the terminal")
        print("  /model <model_name>   - Change the model")
        print("  /model                - Show current model")
        print("  /status               - Show current status")
        print("  /help                 - Show help information")
        print(f"\nCurrent model: {self.current_model}")
        print("Just type your message to chat with AI!")
        print("=" * 40)

    def show_status(self):
        """
        Display current status
        """
        with self.lock:
            waiting_status = "Yes" if self.waiting_for_response else "No"

        print(f"\n=== Status ===")
        print(f"Current model: {self.current_model}")
        print(f"Waiting for response: {waiting_status}")
        print(f"Publishing to: {self.user_content_topic}")
        print(f"Subscribing to: {self.ai_response_topic}")
        print("=" * 20)

def main(args=None):
    """
    Main function
    """
    rclpy.init(args=args)

    try:
        terminal_node = LLMProxyTerminal()
        rclpy.spin(terminal_node)
    except KeyboardInterrupt:
        pass
    except Exception as e:
        print(f"Error starting LLM Proxy terminal: {e}")
    finally:
        if rclpy.ok():
            rclpy.shutdown()

if __name__ == '__main__':
    main()
