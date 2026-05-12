# Cooper ROS™ LLM Proxy

The Cooper Robot Operating System (ROS) large language model (LLM) Proxy is a ROS 2 package that provides integration with LLM services
for LLM inference.<br>
This package uses topic-based communication to enable seamless interaction
between ROS 2 nodes and cloud-based LLM services.<br>
The sections below describe how to build the Cooper ROS LLM Proxy package
and run the demo with different LLM models.  Note that ROS is a trademark of Open Robotics.

## Run

1. Set up the application programming interface (API) key environment variables in terminal A.

    In this demo, the API key from [Bailian](https://bailian.console.aliyun.com/) is used.

    ```bash
    export LLM_API_KEY="your_api_key_here"
    ```

2. Set up the demo environment variables in terminal A.

    ```bash
    # Lychee OS
    WORKDIR=$HOME/cooper_ros_workdir
    cd $WORKDIR
    source install/setup.bash

    # Yocto Build Firmware
    source /opt/ros/<ROS 2 distribution>/setup.bash

    LLM_PROXY_CONFIG_DIR=`ros2 pkg prefix cooper_ros_llm_proxy`/share/cooper_ros_llm_proxy/config
    ```

3. Launch the LLM proxy in terminal A.

    ```bash
    export ROS_DOMAIN_ID=1
    ros2 launch cooper_ros_llm_proxy llm_proxy.launch.py config_file:=$LLM_PROXY_CONFIG_DIR/bailian.yaml api_key:=$LLM_API_KEY
    ```

4. Run the LLM terminal user interface (UI) in terminal B.

    ```bash
    # Lychee OS
    WORKDIR=$HOME/cooper_ros_workdir
    cd $WORKDIR
    source install/setup.bash

    # Yocto Build Firmware
    source /opt/ros/<ROS 2 distribution>/setup.bash

    export ROS_DOMAIN_ID=1
    ros2 run cooper_ros_llm_proxy llm_proxy_terminal --ros-args -r __ns:=/cooper_ros -r response:=llm_proxy/response
    ```

## Usage

### Terminal Commands

When running the interactive terminal, users can use the commands below:

- `/quit`: Exits the terminal
- `/model <model_name>`: Changes the model (for example, `/model qwen3.5-flash`)
- `/status`: Shows the current status
- `/help`: Shows help information

### Example Session

```
User: Hello, how are you?
AI: Hello! I'm doing well, thank you for asking. How can I help you today?

User: /model qwen-14b
Model changed to: qwen-14b

User: What's the weather like?
AI: I'm sorry, but I don't have access to real-time weather data...

User: /quit
Goodbye!
```

## ROS 2 Interfaces

### cooper_ros_llm_proxy.llm_proxy_service.LLMProxyTerminal

- Publishing topics

    - User content

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | llm_proxy/user_content | std_msgs::msg::String | User input messages containing the model name and input text in JSON format |

- Subscribing topics

    - Model response

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | response | std_msgs::msg::String | The terminal node subscribes to model response messages |

- Parameters

    | Name | Default | Description |
    | ---- | ---- | ----------- |
    | model | "qwen3.5-flash" | The default model name |

### cooper_ros_llm_proxy.llm_proxy_service.LLMProxyService

- Publishing topics

    - Model response

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | llm_proxy/response | std_msgs::msg::String | Model response messages containing success status, response text, and error messages in JSON format |

- Subscribing topics

    - User content

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | user_content | std_msgs::msg::String | The service node subscribes to user input messages |

- Parameters

    | Name | Default | Description |
    | ---- | ---- | ----------- |
    | api_key     | ""              | The LLM service API key        |
    | base_url    | "https://dashscope.aliyuncs.com/compatible-mode/v1"    | Base URL of the LLM service       |
    | model       | "qwen3.5-flash" | The default model name               |
    | max_retries | 3               | Maximum number of retry attempts for API calls |
    | timeout     | 30.0            | Request timeout in seconds           |

### UserContent Message

```json
{
  "model": "qwen3.5-flash",
  "input_text": "User input text"
}
```

### AIResponse Message

```json
{
  "success": true,
  "response_text": "Model response text",
  "error_message": "Error message (if any)"
}
```
