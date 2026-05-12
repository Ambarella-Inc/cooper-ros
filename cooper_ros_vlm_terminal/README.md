# Cooper ROS™ VLM Terminal

LLaVA-OneVision-7B is a large multimodal model (LMM) that extends large language and vision assistant (LLaVA) to video, three-dimensional (3D),
and single image scenarios with enhanced visual understanding capabilities.<br>
This model has been optimized to run with Ambarella's EazyAI framework on the Cooper platform.
For information on how to convert and optimize the model, refer to Ambarella's
[Cooper Model Garden](https://github.com/Ambarella-Inc/cooper-model-garden).<br>
The sections below describe how to build the Cooper ROS LLaVA package
and how to run the demo with the pre-converted models.  Note that ROS is a trademark of Open Robotics.

## Run

Refer to the `cooper_ros_llava/README.md` for information on how to run the demo.

## ROS 2 Interfaces

### cooper_ros_vlm_terminal.vlm_terminal_node.VLMTerminalNode

- Publishing topics

    - Image capture

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | vlm_terminal/image_captured | sensor_msgs::msg::Image | Images captured and sent to LLaVA for processing |

    - User input

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | vlm_terminal/user_content | std_msgs::msg::String | User questions and text inputs |

- Subscribing topics

    - Camera input

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | image | sensor_msgs::msg::Image | Camera input from the EazyAI video stream |

    - LLaVA response

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | response | std_msgs::msg::String | Streaming responses from the LLaVA model |
