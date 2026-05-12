# Cooper ROS™ LLaVA

LLaVA-OneVision-7B is a large multimodal model (LMM) that extends large language and vision assistant (LLaVA) to video, three-dimensional (3D),
and single image scenarios with enhanced visual understanding capabilities.<br>
This model has been optimized to run with Ambarella's EazyAI framework on the Cooper platform.<br>
The sections below describe how to build the Cooper Robot Operating System (ROS) LLaVA package,
and how to run the demo with the pre-converted models.  Note that ROS is a trademark of Open Robotics.

## Run

1. Set up the camera and the EazyAI environment.

    ```bash
    eazyai_video.sh --stream_A 1080p --h264 --stream_B 1080p --h264 --efm --reallocate_mem usr,0x4000000 --vin os08a10_mipi_brg
    ```

2. Set up the demo variables in terminal A.

    ```bash
    # Lychee OS
    WORKDIR=$HOME/cooper_ros_workdir
    cd $WORKDIR
    source install/setup.bash

    # Yocto Build Firmware
    source /opt/ros/<ROS 2 distribution>/setup.bash

    LLAVA_PARAMS_DIR=`ros2 pkg prefix cooper_ros_llava`/share/cooper_ros_llava/params
    LLAVA_MODEL_BASE_PATH=<the path to LLaVA-OneVision-7B model directory> #For example, <Cooper Model Garden>/LLaVA-OneVision/n1_llava_onevision_7B_6NVP
    LLAVA_MODEL_VIT_PATH=<the path of the LLaVA-OneVision-7B ViT model> #For example, <Cooper Model Garden>/LLaVA-OneVision/n1_llava_onevision_7B_6NVP/multi_image_self_contained_fp16_n1_cavalry.bin
    ```

3. Launch the model in terminal A.

    ```bash
    export ROS_DOMAIN_ID=1
    ros2 launch cooper_ros_llava llava.launch.py \
        params_file:=$LLAVA_PARAMS_DIR/llava_params.yaml \
        vit_path:=$LLAVA_MODEL_VIT_PATH \
        base_path:=$LLAVA_MODEL_BASE_PATH
    ```

4. View the image to be analyzed.

    The video stream can be played using the VideoLAN Client (VLC) player or ffplay:

    - Live video

        rtsp://\<IP Address\>/stream1

    - Captured image

        rtsp://\<IP Address\>/stream2

5. Run the terminal user interface (UI) in terminal B.

    ```bash
    # Lychee OS
    WORKDIR=$HOME/cooper_ros_workdir
    cd $WORKDIR
    source install/setup.bash

    # Yocto Build Firmware
    source /opt/ros/<ROS 2 distribution>/setup.bash

    export ROS_DOMAIN_ID=1
    ros2 run cooper_ros_vlm_terminal vlm_terminal \
        --ros-args \
        -r __ns:=/cooper_ros \
        -r __node:=vlm_terminal \
        -r image:=iav_canvas/rgb8 \
        -r response:=llava/response \
        -r performance:=llava/performance
    ```

    The following is the workflow of the terminal UI.

    1. Select <b>1. Capture camera</b> to get a live camera frame from `iav_canvas/rgb8`,
    or <b>2. Read image file</b> to input a file path.
    2. Select <b>3. Chat</b> to enter chat mode.
    3. Ask questions about the image.
    4. The system will stream responses in real-time.

    The resize function supports images up to 4096×4096 pixels. Larger images exceed the library's limit and will fail.

6. View the performance in terminal C.

    ```bash
    source /opt/ros/<ROS 2 distribution>/setup.bash
    export ROS_DOMAIN_ID=1
    ros2 topic echo /cooper_ros/llava/performance
    ```

## ROS 2 Interfaces

### cooper_ros::llava::LlavaNode

- Publishing topics

    - LLaVA Response

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | llava/response | std_msgs::msg::String | Streaming responses from LLaVA model |

    - Performance

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | llava/performance | std_msgs::msg::String | Performance data in JSON format: {"image": "xx s", "input": "xx s", "output": "xx s", "prefill": "xx tokens/s", "generate": "xx tokens/s"} |

- Subscribing topics

    - Image for processing

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | image_captured | sensor_msgs::msg::Image | Images to be processed by LLaVA |

    - User questions

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | user_content | std_msgs::msg::String | User questions about the image |

- Parameters

    | Name | Default | Description |
    | ---- | ------- | ----------- |
    | vit_path | N/A | Path to Vision Transformer (ViT) model file |
    | vit_video_path | N/A | Path to Video ViT (ViViT) model file |
    | vit_single_path | N/A | Path to Single ViT model file |
    | base_path | N/A | Path to model weight base folder |
    | llm_path | N/A | Path to large language model (LLM) file |
    | device | `2` | Device ID for model inference |
    | llm_mode | `1` | LLM mode (0: LLAVA, 1: LLAVA_OV) |

## Troubleshooting

```bash
# Check model paths exist
ls -la $LLAVA_MODEL_PATH/

# Monitor topics
export ROS_DOMAIN_ID=1
ros2 topic echo /cooper_ros/llava/response
ros2 topic echo /cooper_ros/llava/performance
ros2 topic echo /cooper_ros/iav_canvas/rgb8

# Check topics with namespace
ros2 topic list | grep llava
```
