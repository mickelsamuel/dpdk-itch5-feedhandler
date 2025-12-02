#!/usr/bin/env python3
"""
ITCH Binary to PCAP Converter

Converts raw NASDAQ ITCH 5.0 binary files to PCAP format for replay testing.
The script wraps ITCH messages in MoldUDP64, UDP, IP, and Ethernet headers.

Usage:
    python3 itch_to_pcap.py input.itch output.pcap [--messages N]

The output PCAP can be replayed using tcpreplay:
    sudo tcpreplay -i eth1 --topspeed output.pcap
"""

import struct
import argparse
import gzip
from pathlib import Path


# Network header structures
ETHER_TYPE_IPV4 = 0x0800
IP_PROTO_UDP = 17

# Ethernet header (14 bytes)
# dst_mac (6) + src_mac (6) + ether_type (2)
def make_ethernet_header(dst_mac=None, src_mac=None):
    if dst_mac is None:
        # Multicast MAC for 233.54.12.111
        dst_mac = bytes([0x01, 0x00, 0x5e, 0x36, 0x0c, 0x6f])
    if src_mac is None:
        src_mac = bytes([0x00, 0x11, 0x22, 0x33, 0x44, 0x55])
    return dst_mac + src_mac + struct.pack('>H', ETHER_TYPE_IPV4)


# IPv4 header (20 bytes, no options)
def make_ipv4_header(total_length, src_ip='10.0.0.1', dst_ip='233.54.12.111'):
    version_ihl = (4 << 4) | 5  # IPv4, 5 words (20 bytes)
    tos = 0
    identification = 0
    flags_fragment = 0x4000  # Don't fragment
    ttl = 64
    protocol = IP_PROTO_UDP
    checksum = 0  # Will be computed

    src_bytes = bytes(map(int, src_ip.split('.')))
    dst_bytes = bytes(map(int, dst_ip.split('.')))

    header = struct.pack('>BBHHHBBH4s4s',
                         version_ihl, tos, total_length,
                         identification, flags_fragment,
                         ttl, protocol, checksum,
                         src_bytes, dst_bytes)

    # Compute checksum
    checksum = ip_checksum(header)
    header = header[:10] + struct.pack('>H', checksum) + header[12:]

    return header


def ip_checksum(header):
    """Compute IP header checksum"""
    if len(header) % 2 == 1:
        header += b'\x00'

    checksum = 0
    for i in range(0, len(header), 2):
        word = (header[i] << 8) + header[i + 1]
        checksum += word

    while checksum >> 16:
        checksum = (checksum & 0xFFFF) + (checksum >> 16)

    return ~checksum & 0xFFFF


# UDP header (8 bytes)
def make_udp_header(src_port, dst_port, length):
    checksum = 0  # UDP checksum optional for IPv4
    return struct.pack('>HHHH', src_port, dst_port, length, checksum)


# MoldUDP64 header (20 bytes)
def make_moldudp64_header(session, sequence_number, message_count):
    # Session is 10 bytes, space-padded
    session_bytes = session.encode('ascii')[:10].ljust(10, b' ')
    return session_bytes + struct.pack('>QH', sequence_number, message_count)


# PCAP file header
PCAP_MAGIC = 0xa1b2c3d4
PCAP_VERSION_MAJOR = 2
PCAP_VERSION_MINOR = 4
PCAP_THISZONE = 0
PCAP_SIGFIGS = 0
PCAP_SNAPLEN = 65535
PCAP_LINKTYPE_ETHERNET = 1


def write_pcap_header(f):
    header = struct.pack('<IHHiIII',
                         PCAP_MAGIC,
                         PCAP_VERSION_MAJOR,
                         PCAP_VERSION_MINOR,
                         PCAP_THISZONE,
                         PCAP_SIGFIGS,
                         PCAP_SNAPLEN,
                         PCAP_LINKTYPE_ETHERNET)
    f.write(header)


def write_pcap_packet(f, data, ts_sec=0, ts_usec=0):
    """Write a single packet to PCAP file"""
    incl_len = len(data)
    orig_len = len(data)
    header = struct.pack('<IIII', ts_sec, ts_usec, incl_len, orig_len)
    f.write(header)
    f.write(data)


def read_itch_messages(input_file, max_messages=None):
    """
    Read ITCH messages from binary file.
    ITCH files have 2-byte big-endian length prefix for each message.
    """
    messages = []
    count = 0

    # Handle gzipped files
    opener = gzip.open if str(input_file).endswith('.gz') else open

    with opener(input_file, 'rb') as f:
        while True:
            if max_messages and count >= max_messages:
                break

            # Read 2-byte length
            length_bytes = f.read(2)
            if len(length_bytes) < 2:
                break

            msg_length = struct.unpack('>H', length_bytes)[0]

            # Read message
            message = f.read(msg_length)
            if len(message) < msg_length:
                break

            messages.append(message)
            count += 1

            if count % 100000 == 0:
                print(f"Read {count} messages...")

    return messages


def create_packet(messages, sequence_number, session='NASDAQ    '):
    """Create a complete network packet containing ITCH messages"""
    # Build MoldUDP64 payload (messages with length prefixes)
    payload = b''
    for msg in messages:
        payload += struct.pack('>H', len(msg)) + msg

    # MoldUDP64 header
    moldudp64 = make_moldudp64_header(session, sequence_number, len(messages))
    moldudp64_payload = moldudp64 + payload

    # UDP header
    udp_length = 8 + len(moldudp64_payload)
    udp = make_udp_header(26477, 26477, udp_length)

    # IP header
    ip_total_length = 20 + udp_length
    ip = make_ipv4_header(ip_total_length)

    # Ethernet header
    eth = make_ethernet_header()

    return eth + ip + udp + moldudp64_payload


def convert_itch_to_pcap(input_file, output_file, messages_per_packet=10,
                         max_messages=None):
    """Convert ITCH binary file to PCAP"""
    print(f"Reading ITCH messages from {input_file}...")
    messages = read_itch_messages(input_file, max_messages)
    print(f"Read {len(messages)} messages")

    print(f"Writing PCAP to {output_file}...")
    sequence_number = 1
    packets_written = 0
    timestamp_sec = 0
    timestamp_usec = 0

    with open(output_file, 'wb') as f:
        write_pcap_header(f)

        # Group messages into packets
        for i in range(0, len(messages), messages_per_packet):
            batch = messages[i:i + messages_per_packet]
            packet = create_packet(batch, sequence_number)
            write_pcap_packet(f, packet, timestamp_sec, timestamp_usec)

            sequence_number += len(batch)
            packets_written += 1

            # Increment timestamp slightly for each packet
            timestamp_usec += 100  # 100 microseconds between packets
            if timestamp_usec >= 1000000:
                timestamp_usec = 0
                timestamp_sec += 1

            if packets_written % 10000 == 0:
                print(f"Written {packets_written} packets...")

    print(f"Done! Written {packets_written} packets to {output_file}")


def analyze_itch_file(input_file, max_messages=1000):
    """Analyze ITCH file and print message type distribution"""
    message_types = {}
    count = 0

    opener = gzip.open if str(input_file).endswith('.gz') else open

    with opener(input_file, 'rb') as f:
        while count < max_messages:
            length_bytes = f.read(2)
            if len(length_bytes) < 2:
                break

            msg_length = struct.unpack('>H', length_bytes)[0]
            message = f.read(msg_length)
            if len(message) < msg_length:
                break

            msg_type = chr(message[0])
            message_types[msg_type] = message_types.get(msg_type, 0) + 1
            count += 1

    print(f"\nMessage type distribution (first {count} messages):")
    print("-" * 40)

    type_names = {
        'S': 'System Event',
        'R': 'Stock Directory',
        'H': 'Stock Trading Action',
        'Y': 'Reg SHO Restriction',
        'L': 'Market Participant Position',
        'V': 'MWCB Decline Level',
        'W': 'MWCB Status',
        'K': 'IPO Quoting Period',
        'J': 'LULD Auction Collar',
        'h': 'Operational Halt',
        'A': 'Add Order (No MPID)',
        'F': 'Add Order (MPID)',
        'E': 'Order Executed',
        'C': 'Order Executed With Price',
        'X': 'Order Cancel',
        'D': 'Order Delete',
        'U': 'Order Replace',
        'P': 'Trade (Non-Cross)',
        'Q': 'Cross Trade',
        'B': 'Broken Trade',
        'I': 'NOII',
        'N': 'RPII',
    }

    for msg_type, count in sorted(message_types.items()):
        name = type_names.get(msg_type, 'Unknown')
        print(f"  {msg_type} ({name}): {count}")


def main():
    parser = argparse.ArgumentParser(
        description='Convert NASDAQ ITCH 5.0 binary files to PCAP format')
    parser.add_argument('input', help='Input ITCH binary file (can be .gz)')
    parser.add_argument('output', nargs='?', help='Output PCAP file')
    parser.add_argument('--messages', '-m', type=int,
                        help='Maximum messages to convert')
    parser.add_argument('--per-packet', '-p', type=int, default=10,
                        help='Messages per UDP packet (default: 10)')
    parser.add_argument('--analyze', '-a', action='store_true',
                        help='Analyze input file instead of converting')

    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: Input file not found: {args.input}")
        return 1

    if args.analyze:
        analyze_itch_file(args.input, args.messages or 10000)
        return 0

    if not args.output:
        args.output = str(input_path.stem) + '.pcap'

    convert_itch_to_pcap(
        args.input,
        args.output,
        messages_per_packet=args.per_packet,
        max_messages=args.messages
    )

    return 0


if __name__ == '__main__':
    exit(main())
