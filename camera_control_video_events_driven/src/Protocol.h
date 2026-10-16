#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// Implementation of the device output data format and communication protocol.

// Common packet layout:
//   header(1B=0xFD) | length(2B, little-endian, total packet size) |
//   type(1B) | body | XOR-checksum(1B)
// XOR convention: bitwise XOR of every byte from the 0xFD header up to but not
// including the checksum byte itself.

namespace proto
{

// Packet type codes
enum PacketType : uint8_t
{
    TypeQuery    = 0x01,  // Query packet (sent by this program)
    TypeAnswer   = 0x02,  // Answer packet (sent by the device server, processed here)
    TypeData     = 0x03,  // Data packet (only reply with confirm)
    TypeConfirm  = 0x04,  // Confirm packet (sent by this program)
    TypeSimCalib = 0x0B,  // Simulated time calibration (only reply with confirm)
    TypeSimRF    = 0x0C,  // Simulated RF detection (only reply with confirm)
    TypeStoreCar = 0x0D,  // Store car recall (only reply with confirm)
    TypeFaultMsg = 0x33,  // AEI fault message (only reply with confirm)
    TypeSelfTest = 0x36,  // Self-test information (only reply with confirm)
    TypeRealTag  = 0x51,  // Real-time tag (only reply with confirm)
};

// Maximum reasonable value of the length field (defends against malformed
// packets that would otherwise allocate unbounded memory)
inline constexpr int MAX_PACKET_LEN = 4096;

// XOR check: data[0..len) (does not include the checksum byte itself)
uint8_t xorChecksum(const uint8_t* data, std::size_t len);

// Value -> packed BCD (only supports 0-99)
uint8_t toBcd(int v);

// Packed BCD -> value
int fromBcd(uint8_t b);

// 7-byte time field (year 2B month day hour minute second, packed BCD)
//
// Year is 2-byte BCD in little-endian order:
//   - Example: 2026 -> toBcd(2026%100) = 0x26 (units+tens) = "26",
//     toBcd(2026/100) = 0x20 (hundreds+thousands) = "20".
//   - Byte order: [0x26, 0x20] = numeric low byte ("26") first = "little-endian"
//     in the BCD number sense.
//   - Matches the length field and device id field (both little-endian) for
//     consistent byte order.
// Month/day/hour/minute/second are each 1-byte BCD (no endianness concern).
struct BcdTime
{
    int year = 0;     // 4-digit
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    void encodeTo(uint8_t* out) const // out must be at least 7 bytes
    {
        // Year: BCD little-endian (numeric low byte first)
        out[0] = toBcd(year % 100);  // "26" first = units+tens = low digit
        out[1] = toBcd(year / 100);  // "20" second = hundreds+thousands = high digit
        // Month/day/hour/minute/second: 1-byte BCD each
        out[2] = toBcd(month);
        out[3] = toBcd(day);
        out[4] = toBcd(hour);
        out[5] = toBcd(minute);
        out[6] = toBcd(second);
    }

    // "YYYYMMDDHHMMSS"; returns an empty string for invalid time fields
    std::string toString() const
    {
        if (year < 1900 || year > 9999) return "";  // Tighten the year range check
                                                  // (avoid negative / huge values confusing
                                                  //    the snprintf below)
        char buf[64];  // 64-byte buffer is far more than needed (worst case 14+1 bytes),
                       // but leaves headroom and avoids gcc -Wformat-truncation
                       // (which cannot reason across function calls)
        std::snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d",
                      year, month, day, hour, minute, second);
        return buf;
    }
};

// Device encoding: the "device id" in the packet is the device number
//   (the AEIid field from the config, an integer).
// All packet types use a uniform 2-byte little-endian binary device field
//   (0-65535, 0x0001 - 0xFFFF).
//  - The length field is also little-endian (confirmed by packet capture).
//  - Keeping the same byte order as the length field for consistency.
inline void encodeStation2(uint16_t aeiId, uint8_t* out)
{
    out[0] = static_cast<uint8_t>(aeiId & 0xFF);          // low byte (sent first)
    out[1] = static_cast<uint8_t>((aeiId >> 8) & 0xFF);   // high byte (sent second)
    // i.e. low byte at low address = little-endian
}

inline uint16_t decodeStation2(const uint8_t* in)
{
    return static_cast<uint16_t>(in[0] | (in[1] << 8));  // little-endian read
}

// String AEIid (from config, e.g. "01") to uint16_t.
// The protocol requires a 2-byte little-endian device id, so the conversion
// happens at this boundary.
// Falls back to 0 on failure (should not happen; defensive only).
inline uint16_t aeiIdToUint16(const std::string& s)
{
    if (s.empty()) return 0;
    try { return static_cast<uint16_t>(std::stoi(s)); }
    catch (...) { return 0; }
}

// Build a query packet (19 bytes):
//   FD | 13 00 | 01 | device (2B LE) | 00 00 00 00 00 | time7(yr 2B LE) | XOR
//       |       |    |               |                 |
//       length  type  device          control/time       BCDTime
//
// Device is 2-byte little-endian binary (matches the length field order).
// The 5-byte control field must be all 0x00. We once wrote the second byte
// as 0xFF (a guess at a "no time-sync" flag), but the server immediately
// closed the connection with EOF; after reverting to 0x00 on 2026-10-14
// everything worked. The spec does not define this field; the values below
// reflect what we observed in practice.
inline std::vector<uint8_t> buildQueryPacket(uint16_t aeiId, const BcdTime& now)
{
    std::vector<uint8_t> pkt(19);
    pkt[0] = 0xFD;
    pkt[1] = 0x13;                       // length low byte = 19
    pkt[2] = 0x00;                       // length high byte
    pkt[3] = TypeQuery;                  // 0x01
    encodeStation2(aeiId, &pkt[4]);      // device id, 2-byte little-endian
    pkt[6]  = 0x00;                      // control field (5 bytes, all 0x00)
    pkt[7]  = 0x00;                      //   observed: the server validates 5B = 00 00 00 00 00
    pkt[8]  = 0x00;                      //   a previous attempt with 0xFF (a guess at a
    pkt[9]  = 0x00;                      //   "no time-sync" flag) caused the server to
    pkt[10] = 0x00;                      //   immediately close the connection (EOF).
                                          //   2026-10-14: confirmed must be 0x00, not 0xFF.
    now.encodeTo(&pkt[11]);              // 7-byte BCD time, little-endian (see BcdTime)
    pkt[18] = xorChecksum(pkt.data(), pkt.size() - 1);
    return pkt;
}

// Build a confirm packet (13 bytes):
//   FD 0D 00 04 [device 2B LE] [unused 6B] [XOR]
// Device is 2-byte little-endian binary (consistent with query / answer).
inline std::vector<uint8_t> buildConfirmPacket(uint16_t aeiId)
{
    std::vector<uint8_t> pkt(13, 0);
    pkt[0] = 0xFD;
    pkt[1] = 0x0D;
    pkt[2] = 0x00;
    pkt[3] = TypeConfirm;
    encodeStation2(aeiId, &pkt[4]);
    pkt[12] = xorChecksum(pkt.data(), pkt.size() - 1);
    return pkt;
}

// Extract a complete packet from the receive buffer (no XOR check, only
// structural check).
// Return: >0 = packet length (can be removed from the buffer);
//          0 = need more data;
//         -1 = resync failure (drop the first byte)
int tryExtractPacket(const uint8_t* data, std::size_t len, uint8_t& type, uint16_t& station);

// XOR check over the whole packet (pkt is the full packet including the
// checksum byte)
bool verifyPacket(const uint8_t* pkt, std::size_t len);

// Answer-packet parse result
struct AnswerInfo
{
    uint8_t status = 0;     // 0x01 / 0x02 = active (two different direction flags),
                          //   0x00 / 0xFF = idle
    BcdTime time;           // event time carried in the answer packet
    uint16_t station = 0;   // device id, read as 2-byte little-endian binary
    bool isUpDirection() const { return status == 0x01; }  // false = direction flag 0x02
};

// Parse an answer packet (pkt is a complete, already-verified packet)
bool parseAnswerPacket(const uint8_t* pkt, std::size_t len, AnswerInfo& out);

} // namespace proto
