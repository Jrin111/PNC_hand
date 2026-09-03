// Minimal ROS 2 message definitions for Foxglove Bridge's CDR publisher.
// Supplying definitions also works before a topic has another publisher.
export const ROS2_PUBLISH_OPTIONS = {
  datatypes: new Map([
    ["std_msgs/msg/MultiArrayDimension", { definitions: [
      { name: "label", type: "string" },
      { name: "size", type: "uint32" },
      { name: "stride", type: "uint32" },
    ] }],
    ["std_msgs/msg/MultiArrayLayout", { definitions: [
      { name: "dim", type: "std_msgs/msg/MultiArrayDimension", isComplex: true, isArray: true },
      { name: "data_offset", type: "uint32" },
    ] }],
    ["std_msgs/msg/Float64MultiArray", { definitions: [
      { name: "layout", type: "std_msgs/msg/MultiArrayLayout", isComplex: true },
      { name: "data", type: "float64", isArray: true },
    ] }],
    ["control_msgs/msg/GripperCommand", { definitions: [
      { name: "position", type: "float64" },
      { name: "max_effort", type: "float64" },
    ] }],
  ]),
};
