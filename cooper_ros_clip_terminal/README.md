# Cooper ROS™ CLIP Terminal UI

## Overview

The Cooper Robot Operating System (ROS) CLIP Terminal provides a user interface (UI) for CLIP-based image and text processing.  Note that ROS is a trademark of Open Robotics.
Below are the three main search modes:
- **Text-to-Image Search**: Find images that match text descriptions
- **Image-to-Text Search**: Find text descriptions that match images
- **Image-to-Image Search**: Find similar images using image queries

## ROS 2 Interfaces

### cooper_ros_clip_terminal.clip_terminal_node.ClipTerminalNode

- Publishing topics

    - Request

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | clip_terminal/request | std_msgs::msg::String | The request in JSON format.<br>{"search_type": "image_retrieval", "image_dir": "<path>", "text": "<text>"}<br>{"search_type": "image_classification", "text_file": "<path>", "image_file": "<path>"}<br>{"search_type": "image_search", "image_dir": "<path>", "image_file": "<path>"} |

- Subscribing topics

    - Matched image path

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | image_path | std_msgs::msg::String | The path to the matched image |

    - Matched text

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | text | std_msgs::msg::String | The matched text |
