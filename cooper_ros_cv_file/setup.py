from setuptools import find_packages, setup

package_name = 'cooper_ros_cv_file'

setup(
    name=package_name,
    version='0.0.1',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='Cooper ROS Maintainers',
    maintainer_email='cooper_robot@ambarella.com',
    description='Cooper ROS CV File module for file mode CV tasks',
    license='Apache-2.0',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'cv_file = cooper_ros_cv_file.cv_file:main',
        ],
    },
)
