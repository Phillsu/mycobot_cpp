// ProtocolCode.h
//
// myCobot serial communication protocol constants.
//
// Reverse-derived from the official open-source reference implementation
// (pymycobot), which is the canonical description of the protocol spoken
// by the Atom/M5Stack-Basic firmware that ships on myCobot 360 M5:
//   https://github.com/elephantrobotics/pymycobot
//   pymycobot/common.py   (ProtocolCode class, frame build/parse)
//   pymycobot/generate.py (command implementations)
//
// Frame format (no CRC on M5-based myCobot models):
//   [0xFE][0xFE][LEN][CMD_ID][...DATA...][0xFA]
//   LEN = number of bytes AFTER the LEN byte itself, i.e. 1 (CMD_ID) + len(DATA) + 1 (footer)
//
// Encoding:
//   angle  -> int16 big-endian, value = round(angle_deg * 100)
//   coord  -> int16 big-endian, value = round(coord_mm_or_deg * 10)
//   speed  -> single byte, 0-100
#pragma once
#include <cstdint>

namespace mycobot::proto {

constexpr uint8_t HEADER = 0xFE;
constexpr uint8_t FOOTER = 0xFA;

// System / power
constexpr uint8_t POWER_ON                 = 0x10;
constexpr uint8_t POWER_OFF                = 0x11;
constexpr uint8_t IS_POWER_ON              = 0x12;
constexpr uint8_t RELEASE_ALL_SERVOS       = 0x13;
constexpr uint8_t IS_CONTROLLER_CONNECTED  = 0x14;
constexpr uint8_t SET_FRESH_MODE           = 0x16;

// Angles
constexpr uint8_t GET_ANGLES   = 0x20;
constexpr uint8_t SEND_ANGLE   = 0x21; // single joint
constexpr uint8_t SEND_ANGLES  = 0x22; // all joints

// Coordinates
constexpr uint8_t GET_COORDS   = 0x23;
constexpr uint8_t SEND_COORD   = 0x24; // single axis
constexpr uint8_t SEND_COORDS  = 0x25; // all axes

// Motion state
constexpr uint8_t STOP          = 0x29;
constexpr uint8_t IS_IN_POSITION = 0x2A;
constexpr uint8_t IS_MOVING     = 0x2B;

// Jog (not used by this app - calibration uses free-drag teach instead)
constexpr uint8_t JOG_ANGLE = 0x30;
constexpr uint8_t JOG_COORD = 0x32;
constexpr uint8_t JOG_STOP  = 0x34;

// Speed
constexpr uint8_t GET_SPEED = 0x40;
constexpr uint8_t SET_SPEED = 0x41;

// Misc / feedback LED
constexpr uint8_t SET_COLOR = 0x6A;

// send_coords / send_angle interpolation mode
constexpr uint8_t MODE_ANGULAR = 0; // joints interpolate independently (curved TCP path)
constexpr uint8_t MODE_LINEAR  = 1; // TCP moves in a straight line - required for drawing

// Coordinate axis ids, used by GET_COORDS/SEND_COORDS ordering
enum class Axis : int { X = 0, Y = 1, Z = 2, RX = 3, RY = 4, RZ = 5 };

} // namespace mycobot::proto
