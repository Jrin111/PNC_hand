Here is a comprehensive technical export of the RH56E2 robotic hand's communication protocols and register map.

### 1. Communication Protocols & Interfaces
The RH56E2 supports three primary hardware interfaces. For a ROS2_Control driver requiring a high-frequency control loop, **Modbus TCP** is generally recommended. The supported baud rates for the serial (RS485) and CAN protocols are specifically defined and can be configured via register **1002**.

*   **Modbus TCP (Ethernet):** Standard master/slave protocol. 
    *   Default IP: `192.168.11.210`.
    *   Default Port: `6000`.
    *   Supported Function Codes: `0x03` (Read Holding Registers), `0x06` (Write Single Register), `0x10` (Write Multiple Registers).
*   **RS485 (Custom & Modbus RTU):**
    *   Data format: 8 Data bits, 1 Stop bit, No Parity.
    *   **Supported Baud Rates (Register 1002)**:
        *   `0`: 115200 bps (Default)
        *   `1`: 57600 bps
        *   `2`: 19200 bps
        *   `3`: 921600 bps
    *   Max Devices: 254.
    *   Custom Frame: `[0xEB][0x90][Hand_ID][Length][Cmd: 0x11 Read / 0x12 Write][Addr_L][Addr_H][Data][Checksum]`. Endianness is Little-Endian.
*   **CAN 2.0:** Extended 29-bit identifier frame.
    *   **Supported Baud Rates (Register 1002)**:
        *   `0`: 1000 Kbps (Default)
        *   `1`: 500 Kbps
    *   ID Format: `bit0-13` = Hand ID, `bit14-25` = Register Address, `bit26-28` = R/W Flag (0=Read, 1=Write).
    *   Endianness: Little-Endian (Low byte first, High byte second).

### 2. Degree of Freedom (DOF) Mapping
For ROS2 joint state and command array indexing, the 6 DOFs map exactly in this order across all register groups:
0.  **Pinky**
1.  **Ring**
2.  **Middle**
3.  **Index**
4.  **Thumb Bending**
5.  **Thumb Rotation**

---

### 3. Core Register Map (ROS2 Implementation Target)

Registers are 16-bit (`short`) unless specified as 8-bit (`byte`). In Modbus, each address corresponds to a 16-bit holding register.

#### A. System & Configuration (Setup Phase)
These registers are primarily used during the `on_configure` or `on_activate` lifecycle states of the ROS2 hardware interface.

*   **1000: Hand ID** [1 byte, W/R]
    *   Range: `1` to `254` (Default: `1`). 
    *   *Note: Changes to this register can be saved to flash.*
*   **1002: Baud Rate Setting** [1 byte, W/R]
    *   Writes the corresponding index (`0`-`3` for RS485, `0`-`1` for CAN) to change the communication speed. 
    *   *Note: Requires a save to flash and a power cycle to take effect.*
*   **1004: Clear Error** [1 byte, W/R]
    *   Write `1` to clear recoverable hardware faults (e.g., motor stall, over-current, abnormal errors, and communication faults). 
    *   *Note: Over-temperature faults cannot be cleared manually with this register; they will clear automatically once the temperature drops back to a safe level*.
*   **1005: Save Parameters to Flash** [1 byte, W/R]
    *   Write `1` to commit current configuration changes (like ID, Baud Rate, and default startup parameters) to non-volatile memory so they persist after a power cycle.
*   **1006: Factory Reset** [1 byte, W/R]
    *   Write `1` to restore all hand parameters back to their original factory defaults.
*   **1009: Force Sensor Calibration** [1 byte, W/R]
    *   Write `1` to trigger a 6-second internal calibration routine.
    *   *Safety Note for ROS2 Driver:* Before sending this command, you **must** command the hand to fully open, and ensure the fingers are not touching any objects or experiencing any load.
*   **1700 - 1703: IPv4 Address Fields** [4x 1 byte, W/R]
    *   Configures the Modbus TCP IP address (e.g., 1700=192, 1701=168, 1702=11, 1703=210). Changes require a power cycle to take effect.

#### B. Hand Control Commands (Write / `hw_commands`)
These are dynamically written during the ROS2 `write()` control loop.

*   **1486 - 1497:** **Target Angles** (`ANGLE_SET`) [6x short, W/R].
    *   Range: `0` (Fully open) to `1000` (Fully closed). `-1` = No action.
*   **1498 - 1509:** **Force Control Thresholds** (`FORCE_SET`) [6x short, W/R].
    *   Range: `0` to `3000` grams (g). Dynamically limits grip strength per finger.
*   **1522 - 1533:** **Target Speeds** (`SPEED_SET`) [6x short, W/R].
    *   Range: `0` to `1000` (1000 = approx 600ms full travel).
*   **1625 - 1630:** **Motion Modes** [6x byte, W/R].
    *   `0`: Speed/Force Protection (Stops at target angle or force limit). *Recommended default for ROS2.*
    *   `1`: Force Control Mode (Closed-loop force maintenance).
    *   `2`: Load Retention Mode (Continuous current for static holds. Generates heat).

#### C. Real-Time Feedback (Read / `hw_states`)
These should be queried sequentially during the ROS2 `read()` control loop.

*   **1546 - 1557:** **Actual Angles** (`ANGLE_ACT`) [6x short, R].
    *   Range: `0` to `1000`.
*   **1582 - 1593:** **Actual Force** (`FORCE_ACT`) [6x short, R].
    *   Range: `-4000` to `4000` grams (g).
*   **1594 - 1605:** **Actual Current** [6x short, R].
    *   Range: `0` to `2000` mA.
*   **1612 - 1617:** **Status Info** [6x byte, R].
    *   `0`: Opening, `1`: Grasping, `2`: Stopped at Target Position, `3`: Stopped at Target Force, `5`: Current Protection Triggered, `6`: Motor Stall, `7`: Fault.
*   **1606 - 1611:** **Error Info** [6x byte, R].
    *   Bitmask: `Bit0`=Stall, `Bit1`=Over-temp, `Bit2`=Over-current, `Bit3`=Motor anomaly, `Bit4`=Comm fault.
*   **1618 - 1623:** **Motor Temperature** [6x byte, R].
    *   Range: `0` to `100` °C.

---

### 4. Tactile Sensor Data Structures
Depending on the specific variant of the RH56E2, the hand is equipped with either piezoresistive or capacitive tactile sensors. A ROS2 driver should publish this as a custom message or `sensor_msgs/PointCloud2`.

#### Variant A: Piezoresistive Sensors (Matrix layout)
Provides dense 16-bit integer (0-4095) pressure maps. Data is Little-Endian.
*   `3000-3369`: Pinky (Tip, Phalanges).
*   `3370-3739`: Ring.
*   `3740-4109`: Middle.
*   `4110-4479`: Index.
*   `4480-4899`: Thumb.
*   `4900-5123`: Palm (8x14 Matrix, 224 bytes).

#### Variant B: Capacitive Sensors (-C1 Models)
Provides a 58-byte struct per finger. Read starting at addresses: `3000` (Pinky), `3058` (Ring), `3116` (Middle), `3174` (Index), `3232` (Thumb).
*Each 58-byte block is structured as follows (all values Little-Endian)*:
1.  **Normal Force:** 32-bit Float (4 bytes)
2.  **Normal Force Change:** 32-bit Float (4 bytes)
3.  **Tangential Force:** 32-bit Float (4 bytes)
4.  **Tangential Force Change:** 32-bit Float (4 bytes)
5.  **Tangential Force Direction:** 16-bit Int (2 bytes, 0-359 degrees. `0xFFFF` = invalid)
6.  **Proximity Change:** 32-bit Float (4 bytes)
7.  *Remaining Bytes:* Checksum (2 bytes) + Reserved (2 bytes).

### 5. Implementation Notes
1.  **Safety & Initialization:** When building the `on_activate` lifecycle state in `ROS2_Control`, first verify communication, then read register `1606-1611` to ensure no errors exist. Write `0` to motion mode registers `1625-1630` to ensure safe operation before writing angle commands.
2.  **Unit Conversions for ROS2:** The hand operates internally in increments of 0-1000 for angles. The driver should convert these to radians based on the kinematics defined in the URDF, mapping 0 to the maximum extension limit and 1000 to the maximum flexion limit.
3.  **Exclusions:** Ignore any documentation references to "Action Sequence Memory" (Registers 2000-2324), as this is an older feature from the base RH56 series and is not supported in the RH56E2 architecture.