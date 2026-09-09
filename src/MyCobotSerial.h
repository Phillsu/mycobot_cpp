// MyCobotSerial.h
//
// Win32 COM-port transport + myCobot serial protocol framing, wrapped in a
// small synchronous request/response API. One transaction (write request,
// read matching reply) is atomic under an internal mutex, so it's safe to
// call from the UI thread (e.g. E-STOP) while a worker thread is mid-stream
// sending drawing waypoints.
#pragma once

#include <windows.h>
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mycobot {

// A pose in the robot's base coordinate frame: X,Y,Z in mm, RX,RY,RZ in degrees.
struct Coords {
    double x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0;

    double& operator[](int i) {
        switch (i) {
            case 0: return x; case 1: return y; case 2: return z;
            case 3: return rx; case 4: return ry; default: return rz;
        }
    }
};

using Angles = std::array<double, 6>;

// Tri-state result for boolean status queries (mirrors the firmware's own
// 0 / 1 / -1 "unknown or error" convention).
enum class TriState { False, True, Unknown };

class MyCobotSerial {
public:
    MyCobotSerial() = default;
    ~MyCobotSerial();

    MyCobotSerial(const MyCobotSerial&) = delete;
    MyCobotSerial& operator=(const MyCobotSerial&) = delete;

    // Opens the COM port (e.g. L"COM5") at the given baud rate (default matches
    // the Atom firmware's standard 115200 8N1). Returns "" on success, or a
    // human-readable error message.
    std::wstring Open(const std::wstring& portName, DWORD baud = 115200);
    void Close();
    bool IsOpen() const { return hPort_ != INVALID_HANDLE_VALUE; }

    // --- High level API -----------------------------------------------
    // All of these return false (or TriState::Unknown / nullopt) if the
    // transaction failed (timeout, not connected, malformed reply).

    bool PowerOn();
    bool PowerOff();
    TriState IsPowerOn();
    bool ReleaseAllServos();     // relax all joints -> arm can be hand-guided
    TriState IsControllerConnected();

    bool SetSpeed(int speedPct); // 0-100

    std::optional<Angles> GetAngles();
    bool SendAngles(const Angles& angles, int speedPct);

    std::optional<Coords> GetCoords();
    // mode: proto::MODE_LINEAR for straight-line TCP motion (used for drawing),
    // proto::MODE_ANGULAR for joint-space motion (used e.g. for a fast travel move).
    bool SendCoords(const Coords& coords, int speedPct, uint8_t mode);

    TriState IsMoving();
    bool Stop();
    bool SetColor(uint8_t r, uint8_t g, uint8_t b);

    // Last error text from the most recent failed call (for status/log display).
    std::wstring LastError() const { return lastError_; }

private:
    HANDLE hPort_ = INVALID_HANDLE_VALUE;
    std::mutex ioMutex_;
    std::wstring lastError_;

    // Sends one frame [FE FE LEN CMD data... FA] and, if expectReply, reads and
    // returns the reply frame's data payload. Caller must hold ioMutex_.
    std::optional<std::vector<uint8_t>> Transact(uint8_t cmd,
                                                   const std::vector<uint8_t>& data,
                                                   bool expectReply,
                                                   DWORD timeoutMs = 1500);

    bool WriteFrameLocked(uint8_t cmd, const std::vector<uint8_t>& data);
    std::optional<std::vector<uint8_t>> ReadFrameLocked(uint8_t expectedCmd, DWORD timeoutMs);
    bool ReadByteLocked(uint8_t& out, DWORD timeoutMs);
};

// --- encode/decode helpers (exposed for unit testing / reuse) --------------
int16_t Angle2Int(double deg);
double Int2Angle(int16_t v);
int16_t Coord2Int(double mmOrDeg);
double Int2Coord(int16_t v);

} // namespace mycobot
