#!/usr/bin/env python3
"""
Generate all figures for dpdk-itch5-feedhandler project.
Creates publication-quality diagrams for README.
"""

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle, Circle, Polygon
from matplotlib.lines import Line2D
import numpy as np
from pathlib import Path

# Set up professional style
plt.rcParams.update({
    'font.family': 'sans-serif',
    'font.sans-serif': ['Arial', 'Helvetica', 'DejaVu Sans'],
    'font.size': 11,
    'axes.titlesize': 14,
    'axes.titleweight': 'bold',
    'figure.facecolor': 'white',
    'axes.facecolor': 'white',
    'axes.edgecolor': '#333333',
    'axes.linewidth': 1.5,
    'grid.alpha': 0.3,
})

# Color palette
COLORS = {
    'primary': '#2E86AB',
    'secondary': '#A23B72',
    'accent': '#F18F01',
    'success': '#2E7D32',
    'dark': '#1B1B1E',
    'light': '#F5F5F5',
    'highlight': '#E94F37',
    'nic': '#1565C0',
    'ring': '#7B1FA2',
    'consumer': '#388E3C',
}


def create_architecture_diagram(output_path: str):
    """Create DPDK feed handler architecture diagram."""

    fig, ax = plt.subplots(figsize=(14, 8))
    ax.set_xlim(0, 14)
    ax.set_ylim(0, 8)
    ax.axis('off')

    # Title
    ax.text(7, 7.5, 'DPDK ITCH 5.0 Feed Handler Architecture',
            ha='center', va='center', fontsize=18, fontweight='bold', color=COLORS['dark'])

    # NIC Box
    nic_box = FancyBboxPatch((0.5, 3), 3, 2.5,
                             boxstyle="round,pad=0.05,rounding_size=0.2",
                             facecolor=COLORS['nic'], edgecolor=COLORS['dark'], linewidth=2)
    ax.add_patch(nic_box)
    ax.text(2, 5, 'NIC (10G)', ha='center', va='center',
            fontsize=14, fontweight='bold', color='white')
    ax.text(2, 4.4, 'DPDK Poll Mode', ha='center', va='center',
            fontsize=11, color='white')
    ax.text(2, 3.8, 'Zero-Copy RX', ha='center', va='center',
            fontsize=10, style='italic', color='#B3E5FC')
    ax.text(2, 3.3, 'Producer Core', ha='center', va='center',
            fontsize=10, color='white')

    # Arrow NIC to Ring Buffer
    ax.annotate('', xy=(5.3, 4.25), xytext=(3.7, 4.25),
               arrowprops=dict(arrowstyle='->', color=COLORS['dark'], lw=2.5))
    ax.text(4.5, 4.6, 'mbufs', ha='center', fontsize=10, style='italic')

    # Ring Buffer Box
    ring_box = FancyBboxPatch((5.5, 3), 3, 2.5,
                              boxstyle="round,pad=0.05,rounding_size=0.2",
                              facecolor=COLORS['ring'], edgecolor=COLORS['dark'], linewidth=2)
    ax.add_patch(ring_box)
    ax.text(7, 5, 'Ring Buffer', ha='center', va='center',
            fontsize=14, fontweight='bold', color='white')
    ax.text(7, 4.4, 'Lock-Free SPSC', ha='center', va='center',
            fontsize=11, color='white')
    ax.text(7, 3.8, 'Cache-Line Aligned', ha='center', va='center',
            fontsize=10, style='italic', color='#E1BEE7')
    ax.text(7, 3.3, '65K Entries', ha='center', va='center',
            fontsize=10, color='white')

    # Arrow Ring Buffer to Consumer
    ax.annotate('', xy=(10.3, 4.25), xytext=(8.7, 4.25),
               arrowprops=dict(arrowstyle='->', color=COLORS['dark'], lw=2.5))
    ax.text(9.5, 4.6, 'msgs', ha='center', fontsize=10, style='italic')

    # Consumer Box
    consumer_box = FancyBboxPatch((10.5, 3), 3, 2.5,
                                  boxstyle="round,pad=0.05,rounding_size=0.2",
                                  facecolor=COLORS['consumer'], edgecolor=COLORS['dark'], linewidth=2)
    ax.add_patch(consumer_box)
    ax.text(12, 5, 'Consumer', ha='center', va='center',
            fontsize=14, fontweight='bold', color='white')
    ax.text(12, 4.4, 'Strategy Logic', ha='center', va='center',
            fontsize=11, color='white')
    ax.text(12, 3.8, 'Order Book Update', ha='center', va='center',
            fontsize=10, style='italic', color='#C8E6C9')
    ax.text(12, 3.3, 'Consumer Core', ha='center', va='center',
            fontsize=10, color='white')

    # Feature labels below
    features_y = 1.8

    # NIC features
    ax.text(2, features_y, 'Zero-Copy Parsing', ha='center', fontsize=10,
            fontweight='bold', color=COLORS['nic'])
    ax.text(2, features_y - 0.4, 'Direct mbuf access', ha='center', fontsize=9,
            color='#666666')
    ax.text(2, features_y - 0.7, 'No kernel bypass', ha='center', fontsize=9,
            color='#666666')

    # Ring features
    ax.text(7, features_y, 'False Sharing Prevention', ha='center', fontsize=10,
            fontweight='bold', color=COLORS['ring'])
    ax.text(7, features_y - 0.4, 'alignas(64) padding', ha='center', fontsize=9,
            color='#666666')
    ax.text(7, features_y - 0.7, 'Separate cache lines', ha='center', fontsize=9,
            color='#666666')

    # Consumer features
    ax.text(12, features_y, 'CPU Core Pinning', ha='center', fontsize=10,
            fontweight='bold', color=COLORS['consumer'])
    ax.text(12, features_y - 0.4, 'Dedicated cores', ha='center', fontsize=9,
            color='#666666')
    ax.text(12, features_y - 0.7, 'No context switches', ha='center', fontsize=9,
            color='#666666')

    # Performance metrics at bottom
    metrics_y = 0.5
    ax.text(7, metrics_y, 'Performance: 445M ops/sec (single-threaded) | 15M msgs/sec (64-byte messages) | 2.2 ns/op latency',
            ha='center', fontsize=11, fontweight='bold', color=COLORS['dark'],
            bbox=dict(boxstyle='round,pad=0.3', facecolor=COLORS['light'], edgecolor=COLORS['dark']))

    plt.savefig(output_path, dpi=150, bbox_inches='tight', facecolor='white', edgecolor='none')
    plt.close()
    print(f"Created: {output_path}")


def create_protocol_stack_diagram(output_path: str):
    """Create MoldUDP64/ITCH protocol stack diagram."""

    fig, ax = plt.subplots(figsize=(10, 10))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis('off')

    # Title
    ax.text(5, 9.5, 'UDP/MoldUDP64/ITCH Protocol Stack',
            ha='center', va='center', fontsize=16, fontweight='bold', color=COLORS['dark'])

    # Layer colors
    layer_colors = ['#E3F2FD', '#BBDEFB', '#90CAF9', '#64B5F6', '#42A5F5']
    layer_names = ['ITCH 5.0 Messages', 'MoldUDP64 Header', 'UDP Header', 'IPv4 Header', 'Ethernet Header']
    layer_sizes = ['Variable', '20 bytes', '8 bytes', '20 bytes', '14 bytes']
    layer_details = [
        ['Message Type (1B)', 'Stock Locate (2B)', 'Timestamp (6B)', 'Payload (Variable)'],
        ['Session ID (10B)', 'Sequence Number (8B)', 'Message Count (2B)'],
        ['Source Port', 'Dest Port', 'Length', 'Checksum'],
        ['Version/IHL', 'Total Length', 'TTL', 'Protocol', 'Addresses'],
        ['Dest MAC', 'Source MAC', 'EtherType'],
    ]

    y_start = 8.5
    height = 1.3
    gap = 0.15

    for i, (name, size, details, color) in enumerate(zip(layer_names, layer_sizes, layer_details, layer_colors)):
        y = y_start - i * (height + gap)

        # Main layer box
        layer_box = FancyBboxPatch((1, y - height + 0.1), 8, height - 0.2,
                                   boxstyle="round,pad=0.05,rounding_size=0.1",
                                   facecolor=color, edgecolor=COLORS['dark'], linewidth=2)
        ax.add_patch(layer_box)

        # Layer name and size
        ax.text(1.3, y - 0.25, name, fontsize=12, fontweight='bold', color=COLORS['dark'])
        ax.text(8.7, y - 0.25, size, fontsize=10, fontweight='bold', color=COLORS['dark'], ha='right')

        # Details
        detail_text = ' | '.join(details)
        ax.text(5, y - 0.7, detail_text, ha='center', fontsize=9, color='#555555')

    # Add arrow showing data flow
    ax.annotate('', xy=(0.5, 2.2), xytext=(0.5, 8.3),
               arrowprops=dict(arrowstyle='<->', color=COLORS['highlight'], lw=2))
    ax.text(0.3, 5.3, 'Data\nFlow', ha='center', va='center', fontsize=10,
            fontweight='bold', color=COLORS['highlight'], rotation=90)

    # Bottom note
    ax.text(5, 0.7, 'Total Header Overhead: 62 bytes (before ITCH payload)',
            ha='center', fontsize=11, fontweight='bold', color=COLORS['dark'],
            bbox=dict(boxstyle='round,pad=0.3', facecolor=COLORS['light'], edgecolor=COLORS['dark']))

    plt.savefig(output_path, dpi=150, bbox_inches='tight', facecolor='white', edgecolor='none')
    plt.close()
    print(f"Created: {output_path}")


def create_ring_buffer_diagram(output_path: str):
    """Create lock-free SPSC ring buffer diagram."""

    fig, ax = plt.subplots(figsize=(12, 8))
    ax.set_xlim(0, 12)
    ax.set_ylim(0, 8)
    ax.axis('off')

    # Title
    ax.text(6, 7.5, 'Lock-Free SPSC Ring Buffer with False Sharing Prevention',
            ha='center', va='center', fontsize=16, fontweight='bold', color=COLORS['dark'])

    # Draw ring buffer as circular segments
    center = (6, 4)
    outer_radius = 2.5
    inner_radius = 1.5

    n_slots = 16
    head_idx = 3
    tail_idx = 11

    for i in range(n_slots):
        angle_start = 90 - i * (360 / n_slots)
        angle_end = 90 - (i + 1) * (360 / n_slots)

        # Determine color based on position
        if head_idx <= tail_idx:
            is_filled = head_idx <= i < tail_idx
        else:
            is_filled = i >= head_idx or i < tail_idx

        if i == head_idx:
            color = COLORS['success']  # Head (producer writes here)
        elif i == tail_idx:
            color = COLORS['highlight']  # Tail (consumer reads here)
        elif is_filled:
            color = COLORS['primary']  # Contains data
        else:
            color = '#E0E0E0'  # Empty

        # Create wedge
        theta1, theta2 = np.radians(angle_end), np.radians(angle_start)
        wedge = mpatches.Wedge(center, outer_radius, angle_end, angle_start,
                               width=outer_radius - inner_radius,
                               facecolor=color, edgecolor='white', linewidth=2)
        ax.add_patch(wedge)

        # Add index label
        mid_angle = np.radians((angle_start + angle_end) / 2)
        label_r = (outer_radius + inner_radius) / 2
        lx = center[0] + label_r * np.cos(mid_angle)
        ly = center[1] + label_r * np.sin(mid_angle)
        ax.text(lx, ly, str(i), ha='center', va='center', fontsize=9,
                fontweight='bold', color='white' if color != '#E0E0E0' else '#666666')

    # Head/Tail labels
    head_angle = np.radians(90 - head_idx * (360 / n_slots) - (360 / n_slots / 2))
    tail_angle = np.radians(90 - tail_idx * (360 / n_slots) - (360 / n_slots / 2))

    ax.annotate('HEAD\n(Producer)', xy=(center[0] + outer_radius * 1.15 * np.cos(head_angle),
                                        center[1] + outer_radius * 1.15 * np.sin(head_angle)),
               fontsize=10, fontweight='bold', ha='center', color=COLORS['success'])

    ax.annotate('TAIL\n(Consumer)', xy=(center[0] + outer_radius * 1.15 * np.cos(tail_angle),
                                         center[1] + outer_radius * 1.15 * np.sin(tail_angle)),
               fontsize=10, fontweight='bold', ha='center', color=COLORS['highlight'])

    # Cache line diagram on the right
    cache_x = 10
    cache_y = 5.5

    ax.text(cache_x, cache_y + 1.2, 'Memory Layout', ha='center', fontsize=12, fontweight='bold')
    ax.text(cache_x, cache_y + 0.8, '(False Sharing Prevention)', ha='center', fontsize=10, style='italic', color='#666666')

    # Cache line boxes
    for i, (label, color) in enumerate([('head_', COLORS['success']), ('padding', '#CCCCCC'), ('tail_', COLORS['highlight'])]):
        y = cache_y - i * 0.7
        box = FancyBboxPatch((cache_x - 1, y - 0.25), 2, 0.5,
                             boxstyle="round,pad=0.02",
                             facecolor=color, edgecolor=COLORS['dark'], linewidth=1.5)
        ax.add_patch(box)
        ax.text(cache_x, y, label, ha='center', va='center', fontsize=9, fontweight='bold',
                color='white' if color != '#CCCCCC' else '#333333')

    # Cache line annotations
    ax.annotate('64B Cache Line', xy=(cache_x + 1.2, cache_y), fontsize=9,
               xytext=(cache_x + 1.5, cache_y + 0.3),
               arrowprops=dict(arrowstyle='->', color='#666666', lw=1))
    ax.annotate('64B Cache Line', xy=(cache_x + 1.2, cache_y - 1.4), fontsize=9,
               xytext=(cache_x + 1.5, cache_y - 1.1),
               arrowprops=dict(arrowstyle='->', color='#666666', lw=1))

    # Legend
    legend_y = 1
    legend_items = [
        (COLORS['primary'], 'Data (filled slots)'),
        ('#E0E0E0', 'Empty slots'),
        (COLORS['success'], 'Head (write position)'),
        (COLORS['highlight'], 'Tail (read position)'),
    ]

    for i, (color, label) in enumerate(legend_items):
        x = 1 + (i % 2) * 5
        y = legend_y - (i // 2) * 0.4
        box = Rectangle((x, y - 0.1), 0.3, 0.25, facecolor=color, edgecolor=COLORS['dark'])
        ax.add_patch(box)
        ax.text(x + 0.45, y, label, fontsize=10, va='center')

    # Performance note
    ax.text(6, 0.3, '445M ops/sec single-threaded | 2.2 ns/op | Power-of-2 size for fast modulo',
            ha='center', fontsize=10, fontweight='bold', color=COLORS['dark'],
            bbox=dict(boxstyle='round,pad=0.3', facecolor=COLORS['light'], edgecolor=COLORS['dark']))

    plt.savefig(output_path, dpi=150, bbox_inches='tight', facecolor='white', edgecolor='none')
    plt.close()
    print(f"Created: {output_path}")


def create_benchmark_chart(output_path: str):
    """Create benchmark results bar chart."""

    fig, axes = plt.subplots(1, 2, figsize=(14, 6))

    # Left: Ring Buffer benchmarks
    ax1 = axes[0]
    categories = ['Single-\nThreaded', 'Concurrent\nP/C', 'Normalized\nMessage']
    values = [445.95, 6.79, 15.19]
    colors = [COLORS['primary'], COLORS['secondary'], COLORS['accent']]

    bars = ax1.bar(categories, values, color=colors, edgecolor='white', linewidth=2)
    ax1.set_ylabel('Throughput (Million ops/sec)', fontsize=12, fontweight='bold')
    ax1.set_title('Ring Buffer Performance', fontsize=14, fontweight='bold', pad=15)

    for bar, val in zip(bars, values):
        ax1.annotate(f'{val:.1f}M', xy=(bar.get_x() + bar.get_width()/2, bar.get_height()),
                    xytext=(0, 5), textcoords='offset points', ha='center', fontsize=11, fontweight='bold')

    ax1.grid(True, alpha=0.3, axis='y')
    ax1.set_axisbelow(True)

    # Right: Parser benchmarks
    ax2 = axes[1]
    categories = ['AddOrder\nParsing', 'Mixed\nMessages', 'Endian\nConversion']
    values = [98.87, 57.73, 2920]  # Last one is billion, will scale
    display_values = [98.87, 57.73, 2920]

    bars = ax2.bar(categories, values, color=[COLORS['primary'], COLORS['secondary'], COLORS['accent']],
                   edgecolor='white', linewidth=2)
    ax2.set_ylabel('Throughput (Million ops/sec)', fontsize=12, fontweight='bold')
    ax2.set_title('ITCH Parser Performance', fontsize=14, fontweight='bold', pad=15)

    labels = ['98.9M', '57.7M', '2.9B']
    for bar, label in zip(bars, labels):
        ax2.annotate(label, xy=(bar.get_x() + bar.get_width()/2, bar.get_height()),
                    xytext=(0, 5), textcoords='offset points', ha='center', fontsize=11, fontweight='bold')

    ax2.grid(True, alpha=0.3, axis='y')
    ax2.set_axisbelow(True)

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight', facecolor='white', edgecolor='none')
    plt.close()
    print(f"Created: {output_path}")


def main():
    docs_dir = Path(__file__).parent / 'docs'
    docs_dir.mkdir(exist_ok=True)

    print("Generating figures for dpdk-itch5-feedhandler...")
    print("=" * 50)

    create_architecture_diagram(docs_dir / 'architecture_diagram.png')
    create_protocol_stack_diagram(docs_dir / 'protocol_stack.png')
    create_ring_buffer_diagram(docs_dir / 'ring_buffer_diagram.png')
    create_benchmark_chart(docs_dir / 'benchmark_chart.png')

    print("=" * 50)
    print("All figures generated successfully!")


if __name__ == "__main__":
    main()
