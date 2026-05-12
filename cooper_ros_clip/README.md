# Cooper ROS™ CLIP

## Overview

The Cooper Robot Operating System (ROS) CLIP provides a ROS 2 interface for CLIP-based image and text processing.  Note that ROS is a trademark of Open Robotics.
Below are the three main search modes:
- **Text-to-Image Search**: Find images that match text descriptions
- **Image-to-Text Search**: Find text descriptions that match images
- **Image-to-Image Search**: Find similar images using image queries

## Run

1. Launch the CLIP model in terminal A.

    ```bash
    # Lychee OS
    WORKDIR=$HOME/cooper_ros_workdir
    cd $WORKDIR
    source install/setup.bash

    # Yocto Build Firmware
    source /opt/ros/<ROS 2 distribution>/setup.bash

    CLIP_PARAMS_DIR=`ros2 pkg prefix cooper_ros_clip`/share/cooper_ros_clip/params
    CLIP_IMAGE_ENCODER_PATH=<the path of image encoder model> # For example, <Cooper Model Garden>/LongCLIP/n1-655_longclip_base_patch16_image_encoder.bin
    CLIP_TEXT_ENCODER_PATH=<the path of image encoder model> # For example, <Cooper Model Garden>/LongCLIP/n1-655_longclip_base_patch16_text_encoder.bin
    CLIP_TEXT_EMBEDDING_PATH=<the path of the text embedding binary file> # For example, <Cooper Model Garden>/LongCLIP/longclip_vitb16_token_embedding_weight.bin
    CLIP_VOCAB_PATH=<the path of the tokenizers file> # For example, <Cooper Model Garden>/LongCLIP/tokenizer.json

    export ROS_DOMAIN_ID=1
    ros2 launch cooper_ros_clip clip.launch.py params_file:=$CLIP_PARAMS_DIR/clip_params.yaml image_encoder_path:=$CLIP_IMAGE_ENCODER_PATH text_encoder_path:=$CLIP_TEXT_ENCODER_PATH text_embedding_path:=$CLIP_TEXT_EMBEDDING_PATH vocab_path:=$CLIP_VOCAB_PATH
    ```

2. Run the CLIP terminal user interface (UI) in terminal B.

    ```bash
    # Lychee OS
    WORKDIR=$HOME/cooper_ros_workdir
    cd $WORKDIR
    source install/setup.bash

    # Yocto Build Firmware
    source /opt/ros/<ROS 2 distribution>/setup.bash

    export ROS_DOMAIN_ID=1
    ros2 run cooper_ros_clip_terminal clip_terminal --ros-args -r __ns:=/cooper_ros -r image_path:=clip/image_path -r text:=clip/text
    ```

    An example of the menu is provided below.

    ```bash

    CLIP Terminal:
    1. Text-to-image search
    2. Image-to-text match
    3. Image-to-image search
    Enter 1 to 3 or "q" to quit: q

    # Select 1
    Text-to-Image Search:
    <image dir>: /home/lychee/model/dra_img/
    <text>: a photo of cat

    # Select 2
    Image-to-Text Match:
    <text file>: /home/lychee/model/text_list.txt
    <image file>: /home/lychee/model/dra_img/output_0001_000001.jpg

    # Select 3
    Image-to-Image Search:
    <image dir>: /home/lychee/model/dra_img/
    <image file>: /home/lychee/model/dra_img/output_0001_000001.jpg
    ```

3. View the performance in terminal C.

    ```bash
    export ROS_DOMAIN_ID=1
    source /opt/ros/<ROS 2 distribution>/setup.bash
    ros2 topic echo /cooper_ros/clip/performance
    ```

## ROS 2 Interfaces

### cooper_ros::clip::ClipNode

- Publishing topics

    - Matched image path

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | clip/image_path | std_msgs::msg::String | The matched image path |

    - Matched text

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | clip/text | std_msgs::msg::String | The matched text |

    - Performance

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | clip/performance | std_msgs::msg::String | Performance data in JSON format {"cvflow_us":xxx,"image_delay_us":xxx,"inference_us":xxx,"post_process_us":xxx,"preprocess_us":xxx} |

- Subscribing topics
    - Request

        | Name | Type | Description |
        | ---- | ---- | ----------- |
        | request | std_msgs::msg::String | The request in JSON format.<br>{"search_type": "text_to_image", "image_dir": "<path>", "text": "<text>"}<br>{"search_type": "image_to_text", "text_file": "<path>", "image_file": "<path>"}<br>{"search_type": "image_to_image", "image_dir": "<path>", "image_file": "<path>"} |

- Parameters

    | Name | Default | Description |
    | ---- | ------- | ----------- |
    | image_encoder_path | N/A | The path to the CLIP image encoder CVflow® model file |
    | text_encoder_path | N/A | The path to the CLIP text encoder CVflow model file |
    | text_embedding_path | N/A | The path to the text embedding binary file |
    | vocab_path | N/A | The path to the tokenizers file |
    | top_k | 5 | Number of top searched results |
