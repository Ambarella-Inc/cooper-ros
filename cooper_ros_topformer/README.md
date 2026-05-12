# Cooper ROS™ TopFormer

## Overview

The Cooper Robot Operating System (ROS) TopFormer is a ROS 2 package that provides semantic segmentation capabilities using the TopFormer model.
It integrates with the Cooper ROS ecosystem and leverages Ambarella's EazyAI framework for efficient inference.<br>
This model has been converted from open neural network exchange (ONNX) to run with Ambarella's CVflow® platform.  Note that ROS is a trademark of Open Robotics.

## Run

1. Set up the camera.

    ```bash
    eazyai_video.sh --stream_A 1080p --reallocate_mem overlay,0x01200000 --vin os08a10_mipi_brg
    ```

2. Set up the demo variables in terminal A.

    ```bash
    # Lychee OS
    WORKDIR=$HOME/cooper_ros_workdir
    cd $WORKDIR
    source install/setup.bash

    # Yocto Build Firmware
    source /opt/ros/<ROS 2 distribution>/setup.bash

    TOPFORMER_MODEL=<the path of the topformer model> # For example, <Cooper Model Garden>/TopFormer/n1-655_topformer_base.bin
    TOPFORMER_IMAGE_PATH=<the path of image file/folder>
    TOPFORMER_OUTPUT_DIR=<the path to save output files>
    ```

3. Run the demo in terminal A.

    - Live mode

        ```bash
        export ROS_DOMAIN_ID=1
        ros2 launch cooper_ros_topformer topformer_live.launch.py model_path:=$TOPFORMER_MODEL
        ```

        The result is displayed via <b>rtsp://\<IP Address\>/stream1</b>.
        Users can view the stream using the VideoLAN Client (VLC) player.

    - File mode

        ```bash
        export ROS_DOMAIN_ID=1
        ros2 launch cooper_ros_topformer topformer_file.launch.py model_path:=$TOPFORMER_MODEL input_path:=$TOPFORMER_IMAGE_PATH output_dir:=$TOPFORMER_OUTPUT_DIR
        ```

        The segmentation results can be viewed in the output files located in the `$TOPFORMER_OUTPUT_DIR`.

4. View the performance in terminal B.

    ```bash
    export ROS_DOMAIN_ID=1
    source /opt/ros/<ROS 2 distribution>/setup.bash
    ros2 topic echo /cooper_ros/topformer/performance
    ```

## ROS 2 Interfaces

### cooper_ros::topformer::TopformerNode

- Publishing topics

    - Segmentation

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | topformer/segmentation | sensor_msgs::msg::Image | Segmentation results of the image |

    - Performance

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | topformer/performance | std_msgs::msg::String | Performance data in JSON format {"image_delay_us": xx, "preprocess_us": xx, "inference_us": xxx, "post_process_us": xxx, "cvflow_us": xxx, "fps": xx} |

- Subscribing topics
    - Image

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | image | sensor_msgs::msg::Image | The input image for the model with encoding in "rgb8", "bgr8", or "mono8" |

- Parameters

    | Name | Default | Description |
    | ---- | ---- | ----------- |
    | model_path | N/A | The path to the CVflow model file |
