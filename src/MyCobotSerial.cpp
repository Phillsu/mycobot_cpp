#include "MyCobotSerial.h"
#include "ProtocolCode.h"

#include <cmath>

namespace mycobot {

int16_t Angle2Int(double deg) { return static_cast<int16_t>(std::lround(deg * 100.0)); }
double Int2Angle(int16_t v) { return static_cast<double>(v) / 100.0; }
int16_t Coord2Int(double mmOrDeg) { return static_cast<int16_t>(std::lround(mmOrDeg * 10.0)); }
double Int2Coord(int16_t v) { return static_cast<double>(v) / 10.0; }

namespace {
void PushInt16BE(std::vector<uint8_t>& out, int16_t v) {
    out.push_back(static_cast<uint8_t>((static_cast<uint16_t>(v) >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) & 0xFF));
}
int16_t ReadInt16BE(const uint8_t* p) {
    uint16_t u = (static_cast<uint16_t>(p[0]) << 8) | p[1];
    return static_cast<int16_t>(u);
}
} // namespace

MyCobotSerial::~MyCobotSerial() { Close(); }

std::wstring MyCobotSerial::Open(const std::wstring& portName, DWORD baud) {
    std::lock_guard<std::mutex> lock(ioMutex_);
    Close();

    // \\.\COMn form works for both COM1-9 and COM10+.
    std::wstring path = L"\\\\.\\" + portName;
    hPort_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                          OPEN_EXISTING, 0, nullptr);
    if (hPort_ == INVALID_HANDLE_VALUE) {
        lastError_ = L"CreateFile 失敗，找不到或無法開啟 " + portName;
        return lastError_;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(hPort_, &dcb)) {
        lastError_ = L"GetCommState 失敗";
        Close();
        return lastError_;
    }
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(hPort_, &dcb)) {
        lastError_ = L"SetCommState 失敗（鮑率可能不被支援）";
        Close();
        return lastError_;
    }

    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = 30; // per-ReadFile-call slice; overall deadline handled by caller
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 500;
    SetCommTimeouts(hPort_, &to);

    PurgeComm(hPort_, PURGE_RXCLEAR | PURGE_TXCLEAR);
    lastError_.clear();
    return L"";
}

void MyCobotSerial::Close() {
    if (hPort_ != INVALID_HANDLE_VALUE) {
        CloseHandle(hPort_);
        hPort_ = INVALID_HANDLE_VALUE;
    }
}

bool MyCobotSerial::ReadByteLocked(uint8_t& out, DWORD timeoutMs) {
    ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(hPort_, &out, 1, &read, nullptr)) return false;
        if (read == 1) return true;
        if (GetTickCount64() >= deadline) return false;
    }
}

bool MyCobotSerial::WriteFrameLocked(uint8_t cmd, const std::vector<uint8_t>& data) {
    if (hPort_ == INVALID_HANDLE_VALUE) {
        lastError_ = L"尚未連線";
        return false;
    }
    std::vector<uint8_t> frame;
    frame.reserve(data.size() + 5);
    frame.push_back(proto::HEADER);
    frame.push_back(proto::HEADER);
    frame.push_back(static_cast<uint8_t>(data.size() + 2)); // cmd(1) + data + footer(1)
    frame.push_back(cmd);
    frame.insert(frame.end(), data.begin(), data.end());
    frame.push_back(proto::FOOTER);

    DWORD written = 0;
    if (!WriteFile(hPort_, frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr) ||
        written != frame.size()) {
        lastError_ = L"寫入序列埠失敗";
        return false;
    }
    return true;
}

std::optional<std::vector<uint8_t>> MyCobotSerial::ReadFrameLocked(uint8_t expectedCmd,
                                                                     DWORD timeoutMs) {
    ULONGLONG deadline = GetTickCount64() + timeoutMs;
    auto timeLeft = [&]() -> DWORD {
        ULONGLONG now = GetTickCount64();
        return now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
    };

    // Sync to the two-byte header.
    int headerSeen = 0;
    while (headerSeen < 2) {
        DWORD tl = timeLeft();
        if (tl == 0) { lastError_ = L"讀取逾時（同步表頭）"; return std::nullopt; }
        uint8_t b;
        if (!ReadByteLocked(b, tl)) { lastError_ = L"讀取逾時（同步表頭）"; return std::nullopt; }
        headerSeen = (b == proto::HEADER) ? headerSeen + 1 : 0;
    }

    uint8_t lenByte;
    if (!ReadByteLocked(lenByte, timeLeft())) { lastError_ = L"讀取逾時（長度）"; return std::nullopt; }
    if (lenByte < 2) { lastError_ = L"回應長度異常"; return std::nullopt; }

    std::vector<uint8_t> rest(lenByte);
    for (uint8_t i = 0; i < lenByte; ++i) {
        DWORD tl = timeLeft();
        if (tl == 0 || !ReadByteLocked(rest[i], tl)) {
            lastError_ = L"讀取逾時（內容）";
            return std::nullopt;
        }
    }

    uint8_t gotCmd = rest[0];
    uint8_t footer = rest[lenByte - 1];
    if (footer != proto::FOOTER) {
        lastError_ = L"回應格式錯誤（結尾字元不符）";
        return std::nullopt;
    }
    if (expectedCmd != 0 && gotCmd != expectedCmd) {
        // Not the reply we're waiting for (could be an unrelated async status
        // frame) - treat as failure for this simple synchronous transport.
        lastError_ = L"回應指令碼不符";
        return std::nullopt;
    }

    return std::vector<uint8_t>(rest.begin() + 1, rest.end() - 1);
}

std::optional<std::vector<uint8_t>> MyCobotSerial::Transact(uint8_t cmd,
                                                              const std::vector<uint8_t>& data,
                                                              bool expectReply,
                                                              DWORD timeoutMs) {
    std::lock_guard<std::mutex> lock(ioMutex_);
    if (!WriteFrameLocked(cmd, data)) return std::nullopt;
    if (!expectReply) return std::vector<uint8_t>{};
    return ReadFrameLocked(cmd, timeoutMs);
}

// --- High level API ---------------------------------------------------

bool MyCobotSerial::PowerOn() { return Transact(proto::POWER_ON, {}, true).has_value(); }
bool MyCobotSerial::PowerOff() { return Transact(proto::POWER_OFF, {}, true).has_value(); }

TriState MyCobotSerial::IsPowerOn() {
    auto r = Transact(proto::IS_POWER_ON, {}, true);
    if (!r || r->empty()) return TriState::Unknown;
    int8_t v = static_cast<int8_t>((*r)[0]);
    if (v == 1) return TriState::True;
    if (v == 0) return TriState::False;
    return TriState::Unknown;
}

bool MyCobotSerial::ReleaseAllServos() {
    return Transact(proto::RELEASE_ALL_SERVOS, {}, true).has_value();
}

TriState MyCobotSerial::IsControllerConnected() {
    auto r = Transact(proto::IS_CONTROLLER_CONNECTED, {}, true);
    if (!r || r->empty()) return TriState::Unknown;
    int8_t v = static_cast<int8_t>((*r)[0]);
    if (v == 1) return TriState::True;
    if (v == 0) return TriState::False;
    return TriState::Unknown;
}

bool MyCobotSerial::SetSpeed(int speedPct) {
    if (speedPct < 0) speedPct = 0;
    if (speedPct > 100) speedPct = 100;
    std::vector<uint8_t> data{static_cast<uint8_t>(speedPct)};
    return Transact(proto::SET_SPEED, data, true).has_value();
}

std::optional<Angles> MyCobotSerial::GetAngles() {
    auto r = Transact(proto::GET_ANGLES, {}, true);
    if (!r || r->size() < 12) return std::nullopt;
    Angles a{};
    for (int i = 0; i < 6; ++i) a[i] = Int2Angle(ReadInt16BE(&(*r)[i * 2]));
    return a;
}

bool MyCobotSerial::SendAngles(const Angles& angles, int speedPct) {
    if (speedPct < 0) speedPct = 0;
    if (speedPct > 100) speedPct = 100;
    std::vector<uint8_t> data;
    data.reserve(13);
    for (double a : angles) PushInt16BE(data, Angle2Int(a));
    data.push_back(static_cast<uint8_t>(speedPct));
    return Transact(proto::SEND_ANGLES, data, true).has_value();
}

std::optional<Coords> MyCobotSerial::GetCoords() {
    auto r = Transact(proto::GET_COORDS, {}, true);
    if (!r || r->size() < 12) return std::nullopt;
    Coords c;
    c.x = Int2Coord(ReadInt16BE(&(*r)[0]));
    c.y = Int2Coord(ReadInt16BE(&(*r)[2]));
    c.z = Int2Coord(ReadInt16BE(&(*r)[4]));
    c.rx = Int2Coord(ReadInt16BE(&(*r)[6]));
    c.ry = Int2Coord(ReadInt16BE(&(*r)[8]));
    c.rz = Int2Coord(ReadInt16BE(&(*r)[10]));
    return c;
}

bool MyCobotSerial::SendCoords(const Coords& coords, int speedPct, uint8_t mode) {
    if (speedPct < 0) speedPct = 0;
    if (speedPct > 100) speedPct = 100;
    std::vector<uint8_t> data;
    data.reserve(14);
    PushInt16BE(data, Coord2Int(coords.x));
    PushInt16BE(data, Coord2Int(coords.y));
    PushInt16BE(data, Coord2Int(coords.z));
    PushInt16BE(data, Coord2Int(coords.rx));
    PushInt16BE(data, Coord2Int(coords.ry));
    PushInt16BE(data, Coord2Int(coords.rz));
    data.push_back(static_cast<uint8_t>(speedPct));
    data.push_back(mode);
    return Transact(proto::SEND_COORDS, data, true).has_value();
}

TriState MyCobotSerial::IsMoving() {
    auto r = Transact(proto::IS_MOVING, {}, true);
    if (!r || r->empty()) return TriState::Unknown;
    int8_t v = static_cast<int8_t>((*r)[0]);
    if (v == 1) return TriState::True;
    if (v == 0) return TriState::False;
    return TriState::Unknown;
}

bool MyCobotSerial::Stop() {
    // Best-effort emergency stop: fire without waiting long for a reply so
    // it's never blocked behind a slow/failed transaction.
    return Transact(proto::STOP, {}, true, 400).has_value();
}

bool MyCobotSerial::SetColor(uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> data{r, g, b};
    return Transact(proto::SET_COLOR, data, true).has_value();
}

} // namespace mycobot
