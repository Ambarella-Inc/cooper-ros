# Cooper ROS IAV OSD

This package is used to display BBOX, EFM and segmentation map on a stream.

## ROS2 Interfaces

### cooper_ros::iav_osd::IavOsdNode

- subscribing topics

    - BBOX

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | detections | vision_msgs::msg::Detection2DArray | Detection result of bounding boxes with labels. |

    - Classification

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | classification | vision_msgs::msg::Classification | Detection result of classification. |

    - Segmentation

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | segmentation | sensor_msgs::msg::Image | Detection result of segmentation. |

    - Landmark

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | landmarks | cooper_ros_msgs::msg::LandmarkDetection2DArray | Detection result of face landmarks. |

    - EFM Image

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | efm_image | sensor_msgs::msg::Image | The image for encoding the stream. |

- parameters

    | Name | Default | Description |
    | ---- | ---- | ----------- |
    | media | "stream:0" | The media to show OSD, in "hdmi" / "stream:x".<br>The start index of stream in IAV is 0. The start index of stream in RTSP address is 1. |
    | overlay_buffer_offset | 0 | The offset in the overlay buffer which is shared by streams. <br>The used overlay buffer size is shown in the print log. <br>If OSD is shown on multi streams, <br>this offset can be used to split the sharing overlay buffer.  |
    | enable_bbox_textbox | true | Whether to enable the feature to draw BBOX and textbox. |
    | enable_segmentation | false | Whether to enable the feature to draw segmentation images with a color lookup table. |
    | enable_efm | false | Whether to enable the feature of EFM coding. <br>If enbaled, the subscribing EFM images will be shown on the stream. |
    | enable_crop_to_square| false | Whether to crop the subscribing EFM images to square. |
    | has_landmark | false | Whether to subscribe the landmark topic. <br>If true, the BBOX messages and the landmark messages will be matched with a time-stamp synchronizer. |
