# Cooper ROS™

This project is part of the Cooper Robot Operating System (ROS) initiative, which offers source code for running deep neural networks (DNNs),
vision-language models (VLMs), and robotic algorithms using Cooper Foundry on ROS 2.  Note that ROS is a trademark of Open Robotics.
The following models are currently supported:
- LongCLIP
- LLaVA-OneVision
- ResNet
- RetinaFace
- TopFormer
- YOLOX

It also provides proxies for Cooper Foundry on ROS 2 to access public generative artificial intelligence (AI) services.
The following model is currently supported:
- Large language model (LLM)

Refer to the `README.md` in each package for instructions on running the demo and using the ROS interfaces.

- Nodes

    <p align="center">
        <img src="assets/image/snapshot_nodes.png" alt="snapshot" width="640" height="326" />
    </p>

- Demos

    <p align="center">
        <img src="assets/image/snapshot_demos.png" alt="snapshot" width="640" height="659" />
    </p>

## Pre-conditions

- Hardware

    | Item | Tested Configuration |
    |------|----------------------|
    | Board | Ambarella DevKit |
    | Camera | os08a10_mipi_brg connected to VIN0-A |

- Software development kit (SDK) version

    | Tag | Cooper Foundry |
    | --- | --------------- |
    | v0.0.2 | 2.3 (Lychee OS Cooper SDK 2.5.4) |

- Converted model

    The pre-converted models can be downloaded from [model garden](https://huggingface.co/Ambarella/models).<br>

- Others

    Test image files.

## Build

- Lychee OS

    The command below can be used to check the Cooper SDK version.

    ```bash
    test_encode --show-driver
    ```

    Verify that ROS 2 Jazzy is installed.  Ensure that `/opt/ros/jazzy` is a symlink to `/usr/lib64/ros-jazzy`:

    ```bash
    ls -l /opt/ros/jazzy
    ```

    If the check fails, refer to the [Lychee OS ROS 2 installation guide](https://lychee.ambarella.com/compose/ssp/Web/questions/faqs/lch2026-ros2-installation-steps.html).

    Record the pre-installed numpy version.  If compatibility issues related to numpy occur, check whether the version has been changed.

    ```bash
    pip show numpy
    ```

    Follow the steps below to build Cooper ROS from the source on Lychee OS.

    1. Install dependency development packages.

        ```bash
        sudo dnf-3 config-manager --set-enabled lychee-updates
        sudo dnf install libtokenizers-cpp-devel

        # Installing `iperf3` is optional for network throughput testing.
        sudo dnf install iperf3
        ```

    2. Make directories for colcon build.

        ```bash
        mkdir -p $HOME/cooper_ros_workdir/src
        WORKDIR=$HOME/cooper_ros_workdir
        ```

    3. Download dependency ROS 2 packages.

        ```bash
        cd $WORKDIR/src
        git clone https://github.com/ros-perception/vision_msgs.git
        ```

    4. Download the Cooper ROS source code.

        ```bash
        cd $WORKDIR/src/
        git clone https://github.com/Ambarella-Inc/cooper-ros.git cooper_ros
        ```

    5. Build using colcon.

        ```bash
        eval $(rpm --eval %set_build_flags)
        source /opt/ros/<ROS 2 distribution>/setup.bash # Change <ROS 2 distribution> to humble, jazzy, and more.
        cd $WORKDIR
        export console_bridge_DIR=/usr/lib64/console_bridge/cmake # Workaround when CMake cannot find console_bridge
        colcon build --packages-skip vision_msgs_rviz_plugins
        ```

- Ubuntu ROS 2 with Cooper ROS Python packages

    Follow the steps below to build Cooper ROS Python packages from the source on Ubuntu ROS 2.

    1. Install dependencies.

        Install the <b>ros-\<ROS 2 distribution\>-desktop</b> package with the steps in the ROS 2 documentation.

    2. Make directories for colcon build.

        ```bash
        mkdir -p $HOME/cooper_ros_workdir/src
        WORKDIR=$HOME/cooper_ros_workdir
        ```

    3. Download dependency ROS 2 packages.

        ```bash
        cd $WORKDIR/src
        git clone https://github.com/ros-perception/vision_msgs.git
        ```

    4. Download the Cooper ROS source code.

        ```bash
        cd $WORKDIR/src/
        git clone https://github.com/Ambarella-Inc/cooper-ros.git cooper_ros
        ```

    5. Build using colcon.

        ```bash
        source /opt/ros/<ROS 2 distribution>/setup.bash # Change <ROS 2 distribution> to humble, jazzy, and more.
        cd $WORKDIR
        colcon build --packages-skip vision_msgs_rviz_plugins --packages-up-to cooper_ros_clip_terminal cooper_ros_cv_file cooper_ros_llm_proxy cooper_ros_vlm_terminal
        ```

    > **Note:** `colcon build` may print CMake deprecation warnings from `vision_msgs` and ROS `gtest_vendor` (`cmake_minimum_required`).  These warnings do not affect the build result.

## Install

- Lychee OS

    1. Connect to the network.

        ```bash
        # Run the GUI tool nm-connection-editor in Konsole on the desktop to configure the IP address, DNS, and more.
        nm-connection-editor
        ```

    2. Install the dependency Python packages.

        ```bash
        cd $WORKDIR
        pip install -r src/cooper_ros/requirements.txt
        ```

    3. Set up the packages.

        ```bash
        cd $WORKDIR
        source install/setup.bash
        ```

- Ubuntu ROS 2 with Cooper ROS Python packages

    1. Install the dependency Python packages.

        ```bash
        cd $WORKDIR
        pip install -r src/cooper_ros/requirements.txt
        ```

    2. Install the secure shell (SSH) server if required.

        ```bash
        sudo apt install openssh-server
        ```

    3. Set up the packages.

        ```bash
        cd $WORKDIR
        source install/setup.bash
        ```

## Run

1. Prepare the models.

    The pre-converted CVflow® models can be downloaded from the model garden on
    [Hugging Face](https://huggingface.co/Ambarella/models).<br>

    | Model | Ambarella DevKit | Download |
    | ----- | ---------- | -------- |
    | LongCLIP | ✓ | [LongCLIP](https://huggingface.co/Ambarella/LongCLIP) |
    | LLaVA-OneVision | ✓ | [LLaVA-OneVision](https://huggingface.co/Ambarella/LLaVA-OneVision) |
    | ResNet | ✓ | [ResNet](https://huggingface.co/Ambarella/ResNet) |
    | RetinaFace | ✓ | [RetinaFace](https://huggingface.co/Ambarella/RetinaFace) |
    | TopFormer | ✓ | [TopFormer](https://huggingface.co/Ambarella/TopFormer) |
    | YOLOX | ✓ | [YOLOX](https://huggingface.co/Ambarella/YOLOX) |

    The names of the pre-converted CVflow model files start with the system on chip (SoC) name.

    | Device | SoC |
    | -------| --------- |
    | Ambarella DevKit | N1-655 |

    For the compressed packages in `LLaVA-OneVision`, use the commands below to decompress them.

    ```bash
    cd LLaVA-OneVision
    mkdir <package name>
    tar -xvf <package name>.tar -C <package name>
    ```

    The following Python tool can be used to download the model files from the model garden automatically.
    The tested model versions are specified in `model_garden_info.yaml` with commit hash.

    ```bash
    WORKDIR=$HOME/cooper_ros_workdir
    MODEL_GARDEN_DOWNLOAD_DIR=$WORKDIR/src/cooper_ros/assets/model_garden_download
    pip install -r $MODEL_GARDEN_DOWNLOAD_DIR/requirements.txt

    python3 $MODEL_GARDEN_DOWNLOAD_DIR/model_garden_download.py $MODEL_GARDEN_DOWNLOAD_DIR/model_garden_info.yaml <output dir>
    ```

    Use the Hugging Face mirror site when necessary.

    ```bash
    WORKDIR=$HOME/cooper_ros_workdir
    MODEL_GARDEN_DOWNLOAD_DIR=$WORKDIR/src/cooper_ros/assets/model_garden_download
    pip install -r $MODEL_GARDEN_DOWNLOAD_DIR/requirements.txt

    export HF_ENDPOINT=https://hf-mirror.com
    python3 $MODEL_GARDEN_DOWNLOAD_DIR/model_garden_download.py $MODEL_GARDEN_DOWNLOAD_DIR/model_garden_info.yaml <output dir>
    ```

2. Run the demos.

    Refer to the `README.txt` in packages for information on how to run demos.<br>

    For each model with an image input, the image will be resized to match the model's input without padding.
    As a result, the input image's aspect ratio will be changed to the model's input aspect ratio.
    If the change in aspect ratio exceeds the model's detection tolerance,
    noticeable accuracy loss may occur.
    For best results, ensure that the input image's aspect ratio is close to the model's input aspect ratio.

    The network interfaces below are used in the test of this project.

    | Device | Network Interface |
    | -------| --------- |
    | Ambarella DevKit | end0 |

    By default, all the connected network interfaces are used in ROS 2 transport.
    If the blazenet@0x network interfaces are connected, they become discoverable by peer machines even without a direct connection.
    As a result, peer machines will send packets to their IP addresses, which wastes bandwidth and may lead to communication failures.

    Users can use one of the methods below to resolve the network interface issue.

    - Disconnect the unused network interfaces.

        This method is used when testing the demos.

    - Add an interface whitelist to an XML file.

        Refer to [Interface Whitelist](https://fast-dds.docs.eprosima.com/en/2.14.x/fastdds/transport/whitelist.html).

        In `fast_dds_whitelist.xml`, the `192.168.1.11` IP address is used as an example.

        ```xml
        <?xml version="1.0" encoding="UTF-8" ?>
        <profiles xmlns="http://www.eprosima.com">
            <transport_descriptors>
                <transport_descriptor>
                    <transport_id>CustomTcpTransportWhitelistAddress</transport_id>
                    <type>UDPv4</type>
                    <interfaceWhiteList>
                        <address>192.168.1.11</address>
                    </interfaceWhiteList>
                </transport_descriptor>
            </transport_descriptors>

            <participant profile_name="CustomTcpTransportWhitelistAddressParticipant" is_default_profile="true">
                <rtps>
                    <useBuiltinTransports>false</useBuiltinTransports>
                    <userTransports>
                        <transport_id>CustomTcpTransportWhitelistAddress</transport_id>
                    </userTransports>
                </rtps>
            </participant>
        </profiles>
        ```

        ```bash
        export FASTRTPS_DEFAULT_PROFILES_FILE=<the path to fast_dds_whitelist.xml>
        ```


    To ensure reliable image publishing, a network connection with a bandwidth of at least 1 gigabit per second (Gbps) is recommended.
    Utilizing a connection at 100 megabits per second (Mbps) or lower may result in failures due to insufficient data throughput.

    Users can use the commands below to test the network throughput.

    ```bash
    # server
    iperf3 -s -p 5201

    # client
    iperf3 -c <server IP> -p 5201 -u -b 1G -t 10 -i 1 --get-server-output -l <block size, 16 ~ 65507>
    ```

    If nodes cannot stably receive the image messages due to user datagram protocol (UDP) package loss, especially in cross-machine communication,
    try one of the following configuration steps before launching demos.
    Refer to the [very slow publishing of large messages](https://github.com/ros2/ros2/issues/1242)
    for more information about large messages issues,
    [FASTDDS_BUILTIN_TRANSPORTS](https://fast-dds.docs.eprosima.com/en/stable/fastdds/env_vars/env_vars.html), and
    [Large Data](https://fast-dds.docs.eprosima.com/en/stable/fastdds/use_cases/tcp/tcp_large_data_with_options.html)
    for some eProsima Fast Data Distribution Service (DDS) configurations.

    - With Fast Real-Time Publish-Subscribe (RTPS) profiles in XML file

        `large_message_profile.xml`

        ```xml
        <?xml version="1.0" encoding="UTF-8" ?>
        <dds>
            <profiles xmlns="http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles" >
                <transport_descriptors>
                    <transport_descriptor>
                        <transport_id>TransportId1</transport_id>
                        <type>UDPv4</type>
                        <maxMessageSize>1400</maxMessageSize>
                    </transport_descriptor>
                </transport_descriptors>

                <participant profile_name="participant_profile" is_default_profile="true">
                    <rtps>
                        <userTransports>
                            <transport_id>TransportId1</transport_id>
                        </userTransports>
                        <useBuiltinTransports>false</useBuiltinTransports>
                    </rtps>
                </participant>

                <publisher profile_name="default publisher profile" is_default_profile="true">
                    <qos>
                        <publishMode>
                            <kind>SYNCHRONOUS</kind>
                        </publishMode>
                    </qos>
                    <historyMemoryPolicy>PREALLOCATED_WITH_REALLOC</historyMemoryPolicy>
                </publisher>
                <subscriber profile_name="default subscriber profile" is_default_profile="true">
                    <historyMemoryPolicy>PREALLOCATED_WITH_REALLOC</historyMemoryPolicy>
                </subscriber>
            </profiles>
        </dds>
        ```

        ```bash
        export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
        export FASTRTPS_DEFAULT_PROFILES_FILE=<the path to large_message_profile.xml>
        export RMW_FASTRTPS_USE_QOS_FROM_XML=1
        ```

    - With limited UDP over IPv4 (UDPv4) message size

        ```bash
        export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
        export FASTDDS_BUILTIN_TRANSPORTS=DEFAULT?max_msg_size=1400B&sockets_size=1400B&non_blocking=false
        ```

    - Using transmission control protocol over IPv4 (TCPv4) for large data

        ```bash
        export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
        export FASTDDS_BUILTIN_TRANSPORTS=LARGE_DATA?max_msg_size=1MB&sockets_size=1MB&non_blocking=true&tcp_negotiation_timeout=50
        ```

## Preview

- LongCLIP

    <p align="center">
        <img src="assets/image/snapshot_clip.png" alt="snapshot" width="640" height="360" />
    </p>

- LLaVA-OneVision

    <p align="center">
        <img src="assets/image/snapshot_llava.png" alt="snapshot" width="640" height="360" />
    </p>

- ResNet

    <p align="center">
        <img src="assets/image/snapshot_resnet50.png" alt="snapshot" width="640" height="360" />
    </p>

- RetinaFace

    <p align="center">
        <img src="assets/image/snapshot_retinaface.png" alt="snapshot" width="640" height="360" />
    </p>

- TopFormer

    <p align="center">
        <img src="assets/image/snapshot_topformer.png" alt="snapshot" width="640" height="360" />
    </p>

- YOLOX

    <p align="center">
        <img src="assets/image/snapshot_yolox.png" alt="snapshot" width="640" height="360" />
    </p>
