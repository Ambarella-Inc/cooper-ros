from setuptools import setup

package_name = 'cooper_ros_llm_proxy'

setup(
    name=package_name,
    version='0.0.1',
    packages=[package_name],
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        ('share/' + package_name + '/launch', ['launch/llm_proxy.launch.py']),
        ('share/' + package_name + '/config', ['config/bailian.yaml']),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='Cooper ROS Maintainers',
    maintainer_email='cooper_robot@ambarella.com',
    description='ROS 2 package for LLM services integration using topic communication',
    license='Apache-2.0',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'llm_proxy_service = cooper_ros_llm_proxy.llm_proxy_service:main',
            'llm_proxy_terminal = cooper_ros_llm_proxy.llm_proxy_terminal:main',
        ],
    },
)
