# DPDK ITCH 5.0 Feed Handler

A high-performance, kernel-bypass market data feed handler for NASDAQ TotalView-ITCH 5.0 protocol using DPDK (Data Plane Development Kit).

## Features

- **Zero-Copy Packet Parsing**: Direct pointer casting from DPDK mbufs without memory copies
- **Lock-Free SPSC Ring Buffer**: Cache-line aligned producer/consumer queue with false sharing prevention
- **MoldUDP64 Session Layer**: Full sequence tracking with gap detection
- **ITCH 5.0 Protocol Support**: All 22 message types with proper endianness handling
- **CPU Core Pinning**: Dedicated cores for packet reception and message processing
- **Hugepage Memory**: 2MB/1GB hugepage support for optimal memory access

## Architecture

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│   NIC (DPDK)    │────▶│  Ring Buffer     │────▶│   Consumer      │
│   Poll Mode     │     │  (Lock-Free)     │     │   (Strategy)    │
│   Producer Core │     │  SPSC Queue      │     │   Consumer Core │
└─────────────────┘     └──────────────────┘     └─────────────────┘
        │                        │
        │                        │
   Zero-Copy              Cache-Line
   Parsing                Aligned
```

## Project Structure

```
dpdk-itch5-feedhandler/
├── include/
│   ├── common/
│   │   ├── types.hpp          # Common type definitions
│   │   └── endian.hpp         # Byte-swapping utilities
│   ├── itch5/
│   │   ├── messages.hpp       # ITCH 5.0 message structures
│   │   └── parser.hpp         # Zero-copy parser
│   ├── moldudp64/
│   │   ├── header.hpp         # MoldUDP64 header parsing
│   │   └── session.hpp        # Session management & gap detection
│   ├── spsc/
│   │   └── ring_buffer.hpp    # Lock-free SPSC ring buffer
│   └── dpdk/
│       ├── config.hpp         # DPDK configuration
│       └── packet_handler.hpp # Packet processing
├── src/
│   ├── main.cpp               # Main application
│   └── feed_handler.hpp       # Feed handler implementation
├── tests/
│   ├── test_ring_buffer.cpp   # Ring buffer unit tests
│   ├── test_parser.cpp        # Parser unit tests
│   ├── test_moldudp64.cpp     # MoldUDP64 unit tests
│   ├── bench_ring_buffer.cpp  # Ring buffer benchmarks
│   └── bench_parser.cpp       # Parser benchmarks
├── scripts/
│   ├── setup_dpdk_env.sh      # DPDK environment setup
│   └── itch_to_pcap.py        # ITCH to PCAP converter
├── CMakeLists.txt
└── README.md
```

## Building

### Prerequisites

- CMake 3.16+
- C++17 compatible compiler (GCC 8+, Clang 7+)
- DPDK 21.11+ (optional, for live capture)

### Build without DPDK (File/PCAP mode)

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Build with DPDK

```bash
mkdir build && cd build
cmake -DUSE_DPDK=ON ..
make -j$(nproc)
```

### Run Tests

```bash
cd build
ctest --output-on-failure
```

### Run Benchmarks

```bash
./bench_ring_buffer
./bench_parser
```

## Usage

### Process ITCH Binary File

```bash
./feed_handler --itch-file 01302019.NASDAQ_ITCH50 --stats
```

### Process PCAP File

```bash
./feed_handler --pcap-file nasdaq_data.pcap --stats
```

### Live Capture (requires DPDK)

```bash
# Setup DPDK environment (as root)
sudo ./scripts/setup_dpdk_env.sh setup
sudo ./scripts/setup_dpdk_env.sh bind eth1

# Run feed handler
./feed_handler --port 0 --producer-core 1 --consumer-core 2
```

### Convert ITCH to PCAP

```bash
python3 scripts/itch_to_pcap.py input.itch output.pcap
```

## ITCH 5.0 Message Types Supported

| Type | Message | Size |
|------|---------|------|
| S | System Event | 12 |
| R | Stock Directory | 39 |
| H | Stock Trading Action | 25 |
| Y | Reg SHO Restriction | 20 |
| L | Market Participant Position | 26 |
| V | MWCB Decline Level | 35 |
| W | MWCB Status | 12 |
| K | IPO Quoting Period | 28 |
| J | LULD Auction Collar | 35 |
| h | Operational Halt | 21 |
| A | Add Order (No MPID) | 36 |
| F | Add Order (MPID) | 40 |
| E | Order Executed | 31 |
| C | Order Executed With Price | 36 |
| X | Order Cancel | 23 |
| D | Order Delete | 19 |
| U | Order Replace | 35 |
| P | Trade (Non-Cross) | 44 |
| Q | Cross Trade | 40 |
| B | Broken Trade | 19 |
| I | NOII | 50 |
| N | RPII | 20 |

## Performance Characteristics

*Benchmarks run on Apple M-series (results will vary by CPU)*

### Lock-Free Ring Buffer
- **Single-threaded**: ~440 million ops/sec
- **Producer-Consumer**: ~15 million msgs/sec (NormalizedMessage, 64 bytes)
- **Latency**: ~2-3 ns/op (single-threaded)

### ITCH Parser
- **AddOrder parsing**: ~100 million msgs/sec
- **Mixed messages**: ~60 million msgs/sec
- **Zero-copy overhead**: < 10ns per message
- **Endian conversion**: ~2.7 billion swaps/sec

## Key Design Decisions

### False Sharing Prevention

```cpp
struct RingBuffer {
    alignas(64) std::atomic<size_t> head_;  // Producer's cache line
    char padding_[64 - sizeof(std::atomic<size_t>)];
    alignas(64) std::atomic<size_t> tail_;  // Consumer's cache line
};
```

### Zero-Copy Parsing

```cpp
// Cast directly to struct - no memcpy
auto* msg = reinterpret_cast<const AddOrder*>(packet_data + offset);
uint64_t order_ref = endian::ntoh64(msg->order_reference_number);
```

### Endianness Handling

```cpp
// Use compiler intrinsics for single-instruction swap
inline uint32_t swap32(uint32_t val) noexcept {
    return __builtin_bswap32(val);  // Maps to BSWAP instruction
}
```

### Price Representation

```cpp
// Store prices as fixed-point integers to avoid floating-point
using Price = int64_t;  // 6 decimal places
constexpr int64_t PRICE_SCALE = 1'000'000;
// $150.25 stored as 150250000
```

## Sample Data

Download NASDAQ ITCH 5.0 sample data:
- Search for "NASDAQ ITCH 5.0 sample" or
- Use files from open-source repositories like `justinabate/nasdaq_itch_pcap`

## DPDK Environment Setup

The `setup_dpdk_env.sh` script handles:

1. **Hugepage Allocation**: 2MB and 1GB pages
2. **CPU Isolation**: Dedicated cores for polling
3. **Module Loading**: vfio-pci or igb_uio
4. **Interface Binding**: Bind NIC to DPDK driver

```bash
# Full setup
sudo ./scripts/setup_dpdk_env.sh setup

# Check status
./scripts/setup_dpdk_env.sh status

# Bind interface
sudo ./scripts/setup_dpdk_env.sh bind eth1
```

## UDP/MoldUDP64 Protocol Stack

```
┌────────────────────────────────────────────┐
│              ITCH 5.0 Messages             │
├────────────────────────────────────────────┤
│  MoldUDP64 Header (20 bytes)               │
│  - Session ID (10 bytes)                   │
│  - Sequence Number (8 bytes)               │
│  - Message Count (2 bytes)                 │
├────────────────────────────────────────────┤
│              UDP Header (8 bytes)          │
├────────────────────────────────────────────┤
│              IPv4 Header (20 bytes)        │
├────────────────────────────────────────────┤
│            Ethernet Header (14 bytes)      │
└────────────────────────────────────────────┘
```

## Gap Detection

The MoldUDP64 session layer tracks sequence numbers:

```cpp
if (header.sequence_number > expected_sequence_) {
    // Gap detected! Request retransmission
    Gap gap{expected_sequence_, header.sequence_number - 1};
    notify_gap(gap);
    state_ = SessionState::Stale;
}
```

## License

MIT License

## References

- [NASDAQ TotalView-ITCH 5.0 Specification](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHspecification.pdf)
- [MoldUDP64 Protocol Specification](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/moldudp64.pdf)
- [DPDK Documentation](https://doc.dpdk.org/)
