# Cooper ROS™ YOLOX

The Cooper Robot Operating System (ROS) YOLOX is a high-performance, real-time object detection model
that improves upon the You Only Look Once (YOLO) series with an anchor-free design,
achieving excellent speed-accuracy trade-offs for detecting multiple objects in images.<br>
This model has been converted from open neural network exchange (ONNX) to run with Ambarella's CVflow® platform.<br>
The sections below describe how to run the demo with the pre-converted models.  Note that ROS is a trademark of Open Robotics.

## Run

1. Set up the camera.

    ```bash
    eazyai_video.sh --stream_A 1080p --h264 --reallocate_mem overlay,0x01200000 --vin os08a10_mipi_brg
    ```

2. Set up the demo variables in terminal A.

    ```bash
    #Lychee OS
    WORKDIR=$HOME/cooper_ros_workdir
    cd $WORKDIR
    source install/setup.bash

    #Yocto Build Firmware
    source /opt/ros/<ROS 2 distribution>/setup.bash

    YOLOX_LABEL_PATH=`ros2 pkg prefix cooper_ros_yolox`/share/cooper_ros_yolox/label/label_coco_80.txt
    YOLOX_MODEL_PATH=<the path of the yolox model> # For example, <Cooper Model Garden>/YOLOX/n1-655_yolox_s.bin
    YOLOX_IMAGE_PATH=<the path of image file/folder>
    YOLOX_OUTPUT_DIR=<the path to save output files>
    ```

3. The parameters in `params/yolox_params.yaml` will be used and can be manually changed before running the demo.

    ```bash
    vi `ros2 pkg prefix cooper_ros_yolox`/share/cooper_ros_yolox/params/yolox_params.yaml
    ```

4. Run the demo in terminal A.

    - Live mode

      ```bash
      export ROS_DOMAIN_ID=1
      ros2 launch cooper_ros_yolox yolox_live.launch.py model_path:=$YOLOX_MODEL_PATH label_path:=$YOLOX_LABEL_PATH canvas_id:=1 media:=stream:0 enable_rgb8:=true fps:=10.0 class_agnostic:=true
      ```

      The result is displayed via <b>rtsp://\<IP Address\>/stream1</b>.
      Users can view the stream using the VideoLAN Client (VLC) player. The real-time detection results can be viewed on the stream.

    - File mode

      ```bash
      export ROS_DOMAIN_ID=1
      ros2 launch cooper_ros_yolox yolox_file.launch.py model_path:=$YOLOX_MODEL_PATH label_path:=$YOLOX_LABEL_PATH input_path:=$YOLOX_IMAGE_PATH output_dir:=$YOLOX_OUTPUT_DIR loop_count:=1 fps:=10.0 class_agnostic:=true
      ```

      The detection results can be viewed in the output files located in `$YOLOX_OUTPUT_DIR`.

5. View the performance in terminal B.

    ```bash
    export ROS_DOMAIN_ID=1
    source /opt/ros/<ROS 2 distribution>/setup.bash
    ros2 topic echo /cooper_ros/yolox/performance
    ```

## ROS 2 Interfaces

### cooper_ros::yolox::YoloxNode

- Publishing topics

    - Bounding box (BBox)

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | yolox/detections | vision_msgs::msg::Detection2DArray | Detection results of BBoxes with scores and labels |

    - Performance

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | yolox/performance | std_msgs::msg::String | Performance data in JSON format {"cvflow_us":xxx,"fps":"xxx","image_delay_us":xxx,"inference_us":xxx,"post_process_us":xxx,"preprocess_us":xxx} |

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
    | conf_threshold | 0.45 | The confidence threshold |
    | nms_threshold | 0.3 | The NMS threshold. Ignored when post-processing is included in the model |
    | max_det_num | 300 | Maximum detections for buffer pre-allocation |
    | class_agnostic | true | Use multi-class or best class in post-processing. false = multi-class, true = best class |
