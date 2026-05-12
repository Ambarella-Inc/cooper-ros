# Cooper ROS™ ResNet50

The Cooper Robot Operating System (ROS) Residual Network with 50 layers (ResNet50) is a groundbreaking ​​deep convolutional neural network (CNN)​​
introduced by Microsoft Research in 2015.
It is renowned for its depth and efficiency in image classification tasks.<br>
This model has been converted to run with Ambarella's CVflow® platform.<br>
The sections below describe how to run the demo with the pre-converted models.  Note that ROS is a trademark of Open Robotics.

## Run

1. Set up the camera.

    ```bash
    eazyai_video.sh --stream_A 1080p --h264 --reallocate_mem overlay,0x01200000 --vin os08a10_mipi_brg
    ```

2. Set up the demo variables in terminal A.

    ```bash
    # Lychee OS
    WORKDIR=$HOME/cooper_ros_workdir
    cd $WORKDIR
    source install/setup.bash

    # Yocto Build Firmware
    source /opt/ros/<ROS 2 distribution>/setup.bash

    RESNET50_LABEL=`ros2 pkg prefix cooper_ros_resnet50`/share/cooper_ros_resnet50/label/imagenet_1000.txt
    RESNET50_IMAGE_PATH=<the path of image file/folder>
    RESNET50_OUTPUT_DIR=<the path to save output files>
    RESNET50_MODEL=<the path of the resnet50 model> # For example, <Cooper Model Garden>/ResNet/n1-655_resnet_v1.5_50.bin
    ```

3. Run the demo in terminal A.

    - Live mode

        ```bash
        export ROS_DOMAIN_ID=1
        ros2 launch cooper_ros_resnet50 resnet50_live.launch.py model_path:=$RESNET50_MODEL label_path:=$RESNET50_LABEL canvas_id:=1 media:=stream:0 enable_bgr8:=true width_bgr8:=224 height_bgr8:=224 fps:=1.0 top_k:=5
        ```

        The result is displayed via <b>rtsp://\<IP Address\>/stream1</b>. Users can view the stream using the VideoLAN Client (VLC) player.

    - File mode

        ```bash
        export ROS_DOMAIN_ID=1
        ros2 launch cooper_ros_resnet50 resnet50_file.launch.py model_path:=$RESNET50_MODEL label_path:=$RESNET50_LABEL fps:=10.0 loop_count:=1 width:=224 height:=224 encoding:="bgr8" top_k:=5 input_path:=$RESNET50_IMAGE_PATH output_dir:=$RESNET50_OUTPUT_DIR
        ```

        The results are saved to `$RESNET50_OUTPUT_DIR`.

4. View the performance in terminal B.

    ```bash
    export ROS_DOMAIN_ID=1
    source /opt/ros/<ROS 2 distribution>/setup.bash
    ros2 topic echo /cooper_ros/resnet50/performance
    ```

## ROS 2 Interfaces

### cooper_ros::retinaface::ResNet50Node

- Publishing topics

    - Classification

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | resnet50/classification | vision_msgs::msg::Classification | Detection results of classification |

    - Performance

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | resnet50/performance | std_msgs::msg::String | Performance data in JSON format {"image_delay_us": xx, "preprocess_us": xx, "inference_us": xxx, "post_process_us": xxx, "cvflow_us": xxx}. |

- Subscribing topics
    - Image

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | image | sensor_msgs::msg::Image | The input image for the model with encoding in "rgb8", "bgr8", or "mono8" |

- Parameters

    | Name | Default | Description |
    | ---- | ---- | ----------- |
    | model_path | N/A | The path to the CVflow model file |
    | label_path | N/A | The path to the label file |
    | top_k | 5 | Number of top results to output |
