std_msgs/Header header

# Class probabilities
vision_msgs/ObjectHypothesisWithPose[] results

vision_msgs/Point2D[] points

# ID used for consistency across multiple detection messages. Detections
# of the same object in different detection messages should have the same id.
# This field may be empty.
string id
