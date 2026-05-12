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
LLM Proxy Service Node using topic communication
"""

import rclpy
from rclpy.node import Node
from std_msgs.msg import String
from openai import OpenAI
import json


PUB_TOPIC_NAME_RESPONSE     = "llm_proxy/response"
SUB_TOPIC_NAME_USER_CONTENT = "user_content"

PARAM_NAME_API_KEY     = "api_key"
PARAM_NAME_BASE_URL    = "base_url"
PARAM_NAME_MODEL       = "model"
PARAM_NAME_MAX_RETRIES = "max_retries"
PARAM_NAME_TIMEOUT     = "timeout"


class LLMProxyService(Node):
    """
    ROS 2 node for LLM Proxy integration using topic communication
    """

    def __init__(self):
        super().__init__('llm_proxy_service')

        # Declare parameters
        self.declare_parameter(PARAM_NAME_API_KEY, '')
        self.declare_parameter(PARAM_NAME_BASE_URL, 'https://dashscope.aliyuncs.com/compatible-mode/v1')
        self.declare_parameter(PARAM_NAME_MODEL, 'qwen3.5-flash')
        self.declare_parameter(PARAM_NAME_MAX_RETRIES, 3)
        self.declare_parameter(PARAM_NAME_TIMEOUT, 30.0)

        # Get parameter values
        self.api_key = self.get_parameter(PARAM_NAME_API_KEY).value
        self.base_url = self.get_parameter(PARAM_NAME_BASE_URL).value
        self.default_model = self.get_parameter(PARAM_NAME_MODEL).value
        self.max_retries = self.get_parameter(PARAM_NAME_MAX_RETRIES).value
        self.timeout = self.get_parameter(PARAM_NAME_TIMEOUT).value

        # Fixed topic names
        self.user_content_topic = SUB_TOPIC_NAME_USER_CONTENT
        self.ai_response_topic = PUB_TOPIC_NAME_RESPONSE

        # Initialize OpenAI client
        self.client = OpenAI(
            base_url=self.base_url,
            api_key=self.api_key,
        )

        # Create subscriber and publisher
        self.subscription = self.create_subscription(
            String,
            self.user_content_topic,
            self.user_content_callback,
            10
        )

        self.publisher = self.create_publisher(
            String,
            self.ai_response_topic,
            10
        )

        self.get_logger().info(f'LLM Proxy Service started')
        self.get_logger().info(f'Subscribing to: {self.user_content_topic}')
        self.get_logger().info(f'Publishing to: {self.ai_response_topic}')
        self.get_logger().info(f'Default model: {self.default_model}')

    def user_content_callback(self, msg):
        """
        Handle user content messages
        """
        try:
            # Parse JSON data
            user_data = json.loads(msg.data)

            self.get_logger().info(f'Received request for model: {user_data.get("model", self.default_model)}')

            # Extract data
            model = user_data.get('model', self.default_model)
            input_text = user_data.get('input_text', '')

            # Call AI inference
            response_text, success, error_message = self.api_infer(input_text, model)

            # Create response message
            response_data = {
                'success': success,
                'response_text': response_text,
                'error_message': error_message
            }

            # Publish response
            response_msg = String()
            response_msg.data = json.dumps(response_data, ensure_ascii=False)
            self.publisher.publish(response_msg)

            if success:
                self.get_logger().info(f'Response sent successfully')
            else:
                self.get_logger().error(f'Error in response: {error_message}')

        except json.JSONDecodeError as e:
            self.get_logger().error(f'Failed to parse JSON: {e}')
        except Exception as e:
            self.get_logger().error(f'Error processing request: {e}')

    def api_infer(self, input_text, model=None):
        """
        Call Volcengine AI Gateway for inference

        Args:
            input_text (str): Input text
            model (str): Model name

        Returns:
            tuple: (response_text, success, error_message)
        """
        if model is None:
            model = self.default_model

        try:
            completion = self.client.chat.completions.create(
                model=model,
                messages=[
                    {"role": "system", "content": "You are a helpful assistant."},
                    {"role": "user", "content": input_text}
                ],
                timeout=self.timeout
            )

            response_text = completion.choices[0].message.content
            self.get_logger().info(f"Successfully got response from model {model}")
            return response_text, True, ""

        except Exception as e:
            error_msg = f"Error during inference: {str(e)}"
            self.get_logger().error(error_msg)
            return "", False, error_msg

def main(args=None):
    """
    Main function
    """
    rclpy.init(args=args)

    try:
        service_node = LLMProxyService()
        rclpy.spin(service_node)
    except KeyboardInterrupt:
        pass
    except Exception as e:
        print(f"Error starting LLM Proxy service: {e}")
    finally:
        if rclpy.ok():
            rclpy.shutdown()

if __name__ == '__main__':
    main()
