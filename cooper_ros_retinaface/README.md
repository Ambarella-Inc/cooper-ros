# Cooper ROS™ RetinaFace

The Cooper Robot Operating System (ROS) RetinaFace is a practical single-stage state-of-the-art (SOTA) face detector,
which was initially introduced in an arXiv technical report then accepted by the IEEE / CVF Conference on Computer Vision and Pattern Recognition (CVPR) 2020.<br>
This model has been converted from open neural network exchange (ONNX) to run with Ambarella's CVflow® platform.<br>
The sections below describe how to run the demo with the pre-converted models.  Note that ROS is a trademark of Open Robotics.

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

    RETINAFACE_IMAGE_PATH=<the path of image file/folder>
    RETINAFACE_OUTPUT_DIR=<the path to save output files>
    RETINAFACE_MODEL=<the path of the retinaface model> # For example, <Cooper Model Garden>/RetinaFace/n1-655_retinaface_resnet50.bin
    ```

3. The parameters in `params/retinaface_params.yaml` will be used and can be manually changed before running the demo.

    ```bash
    vi `ros2 pkg prefix cooper_ros_retinaface`/share/cooper_ros_retinaface/params/retinaface_params.yaml
    ```

4. Run the demo in terminal A.

    - Live mode

        ```bash
        export ROS_DOMAIN_ID=1
        ros2 launch cooper_ros_retinaface retinaface_live.launch.py model_path:=$RETINAFACE_MODEL canvas_id:=1 osd:=stream:0 has_landmark:=true width_rgb8:=640 height_rgb8:=640 fps:=30.0
        ```

        The result is displayed via <b>rtsp://\<IP Address\>/stream1</b>. Users can view the stream using the VideoLAN Client (VLC) player.

    - File mode

        ```bash
        export ROS_DOMAIN_ID=1
        ros2 launch cooper_ros_retinaface retinaface_file.launch.py model_path:=$RETINAFACE_MODEL fps:=10.0 loop_count:=1 width:=640 height:=640 encoding:="rgb8" input_path:=$RETINAFACE_IMAGE_PATH output_dir:=$RETINAFACE_OUTPUT_DIR
        ```

        The results are saved to `$RETINAFACE_OUTPUT_DIR`.

5. View the performance in terminal B.

    ```bash
    export ROS_DOMAIN_ID=1
    source /opt/ros/<ROS 2 distribution>/setup.bash
    ros2 topic echo /cooper_ros/retinaface/performance
    ```

## ROS 2 Interfaces

### cooper_ros::retinaface::RetinafaceNode

- Publishing topics

    - Bounding box (BBox)

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | retinaface/detections | vision_msgs::msg::Detection2DArray | Detection results of BBoxes with scores and labels |

    - Landmarks for retinaface

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | retinaface/landmarks | cooper_ros_msgs::msg::LandmarkDetection2DArray | Detection results of face landmarks |

    - Performance

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | retinaface/performance | std_msgs::msg::String | Performance data in JSON format {"image_delay_us": xx, "preprocess_us": xx, "inference_us": xxx, "post_process_us": xxx, "cvflow_us": xxx} |

- Subscribing topics
    - Image

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | image | sensor_msgs::msg::Image | The input image for the model with encoding in "rgb8", "bgr8", or "mono8" |

- Parameters

    | Name | Default | Description |
    | ---- | ---- | ----------- |
    | model_path | N/A | The path to the CVflow model file |
    | conf_threshold | 0.8 | The confidence threshold |
    | nms_threshold | 0.4 | The NMS threshold |
    | max_det_num | 300 | Maximum detections for buffer pre-allocation |
