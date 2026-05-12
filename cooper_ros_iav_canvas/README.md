# Cooper ROS™ IAV Canvas

The Cooper Robot Operating System (ROS) image, audio, and video (IAV) Canvas is a ROS 2 package that provides access to
Ambarella's IAV canvas system for camera image capture and publishing.
This package interfaces with the Cooper platform's video pipeline to capture
and distribute camera images in multiple formats (NV12, RGB8, BGR8, and MONO8) through ROS 2 topics.
Note that ROS is a trademark of Open Robotics.

## Run

<a id="1-set-up-the-camera"></a>

1. Set up the camera.

    ```bash
    eazyai_video.sh --stream_A 1080p --reallocate_mem overlay,0x01200000 --vin os08a10_mipi_brg
    ```

2. Set up the demo environment variables.

    ```bash
    # Lychee OS
    WORKDIR=$HOME/cooper_ros_workdir
    cd $WORKDIR
    source install/setup.bash

    # Yocto Build Firmware
    source /opt/ros/<ROS 2 distribution>/setup.bash
    ```

3. Run the IAV canvas node.

    - Publish NV12 images

        ```bash
        ros2 run cooper_ros_iav_canvas iav_canvas --ros-args -p enable_nv12:=true
        ```

    - Publish RGB8 images (360x360)

        ```bash
        ros2 run cooper_ros_iav_canvas iav_canvas --ros-args -p enable_rgb8:=true -p width_rgb8:=360 -p height_rgb8:=360
        ```

    - Publish BGR8 images (480x480)

        ```bash
        ros2 run cooper_ros_iav_canvas iav_canvas --ros-args -p enable_bgr8:=true -p width_bgr8:=480 -p height_bgr8:=480
        ```

    - Publish MONO8 images (720x720)

        ```bash
        ros2 run cooper_ros_iav_canvas iav_canvas --ros-args -p enable_mono8:=true -p width_mono8:=720 -p height_mono8:=720
        ```

    - Publish RGB8 and BGR8 images simultaneously

        ```bash
        ros2 run cooper_ros_iav_canvas iav_canvas --ros-args -p enable_rgb8:=true -p enable_bgr8:=true
        ```

4. Monitor the published images in other terminals.

    - Set up the ROS 2 environment

        ```bash
        # Lychee OS
        WORKDIR=$HOME/cooper_ros_workdir
        source $WORKDIR/install/setup.bash

        # Yocto Build Firmware
        source /opt/ros/<ROS 2 distribution>/setup.bash
        ```

    - Monitor images

        - Monitor RGB8 images (if enabled)

            ```bash
            ros2 topic echo /iav_canvas/rgb8 --no-arr
            ```

        - Monitor NV12 images (if enabled)

            ```bash
            ros2 topic echo /iav_canvas/nv12 --no-arr
            ```

        - Monitor BGR8 images (if enabled)

            ```bash
            ros2 topic echo /iav_canvas/bgr8 --no-arr
            ```

        - Monitor MONO8 images (if enabled)

            ```bash
            ros2 topic echo /iav_canvas/mono8 --no-arr
            ```

## ROS 2 Interfaces

### cooper_ros::iav_canvas::IavCanvasNode

- Publishing topics

    - NV12 image

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | iav_canvas/nv12 | sensor_msgs::msg::Image | NV12 format image data from the canvas |

    - RGB8 image

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | iav_canvas/rgb8 | sensor_msgs::msg::Image | RGB8 format image data converted from the canvas |

    - BGR8 image

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | iav_canvas/bgr8 | sensor_msgs::msg::Image | BGR8 format image data converted from the canvas |

    - MONO8 image

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | iav_canvas/mono8 | sensor_msgs::msg::Image | MONO8 format image data converted from the canvas |

- Parameters

    | Name | Default | Description | Valid Range |
    | ---- | ------- | ----------- | ----------- |
    | canvas_id | 1 | Canvas ID to query camera images from the IAV system | Any canvas enabled in [1. Set up the camera.](#1-set-up-the-camera) |
    | fps | 30.0 | Frames per second for publishing images | > 0 |
    | enable_nv12 | false | Enable publishing YUV NV12 image data | true or false |
    | enable_rgb8 | false | Enable publishing RGB8 image data | true or false |
    | enable_bgr8 | false | Enable publishing BGR8 image data | true or false |
    | enable_mono8 | false | Enable publishing MONO8 image data | true or false |
    | width_rgb8 | 0 | Width of the RGB8 image data. | 0 ~ 4096. 0 means same as the canvas width. |
    | height_rgb8 | 0 | Height of the RGB8 image data. | 0 ~ 4096. 0 means same as the canvas height. |
    | width_bgr8 | 0 | Width of the BGR8 image data. | 0 ~ 4096. 0 means same as the canvas width. |
    | height_bgr8 | 0 | Height of the BGR8 image data. | 0 ~ 4096. 0 means same as the canvas height. |
    | width_mono8 | 0 | Width of the MONO8 image data. | 0 ~ 4096. 0 means same as the canvas width. |
    | height_mono8 | 0 | Height of the MONO8 image data. | 0 ~ 4096. 0 means same as the canvas height. |

    * The RGB8, BGR8, and MONO8 images are scaled from the canvas NV12 image. The scaling ratio (both upscaling and downscaling) must not exceed 256x.

## Configuration

The IAV Canvas node can be configured through either parameter files or launch arguments.
The node supports dynamic parameter updates during runtime.

### Example Parameter File

    ```yaml
    cooper_ros:
        iav_canvas:
        ros__parameters:
            canvas_id: 1
            fps: 30.0
            enable_nv12: false
            enable_rgb8: true
            enable_bgr8: false
            enable_mono8: false
            width_rgb8: 0
            height_rgb8: 0
            width_bgr8: 0
            height_bgr8: 0
            width_mono8: 0
            height_mono8: 0
    ```
