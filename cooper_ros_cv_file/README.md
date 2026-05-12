# Cooper ROS™ CV File

The Cooper Robot Operating System (ROS) computer vision (CV) File package is used to run CV model packages in file mode,
such as cooper_ros_retinaface, cooper_ros_yolox, and more.  Note that ROS is a trademark of Open Robotics.

## ROS 2 Interfaces

### cooper_ros_cv_file.cv_file_node.CvFileNode

- Publishing topics

    - Image

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | cv_file/image | sensor_msgs::msg::Image | The image data read or converted from files |

- Subscribing topics

    - Bounding box (BBox)

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | detections | vision_msgs::msg::Detection2DArray | Detection results of BBoxes with labels |

    - Classification

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | classification | vision_msgs::msg::Classification | Detection results of classification |

    - Segmentation

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | segmentation | sensor_msgs::msg::Image | Detection results of segmentation |

    - Landmark

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | landmarks | cooper_ros_msgs::msg::LandmarkDetection2DArray | Detection results of face landmarks |

- Parameters

    | Name | Default | Description |
    | ---- | ---- | ----------- |
    | input_path | N/A | The path to the image file / folder to be published |
    | encoding | "rgb8" | The image encoding format to be published, in "rgb8," "bgr8," or "mono8" |
    | width | The width of the image read from files |  The width of the image to be published |
    | height | The height of the image read from files | The height of the image to be published |
    | output_dir | "./" | The directory to save the output files |
    | fps | 10.0 | The frequency to publish images |
    | loop_count | 1 | The loop count to publish all the images |
