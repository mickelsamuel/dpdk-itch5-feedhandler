#!/bin/bash
#
# DPDK Environment Setup Script for HFT Feed Handler
#
# This script configures the Linux environment for optimal DPDK performance:
# 1. Hugepages allocation
# 2. CPU isolation for polling
# 3. DPDK module loading
# 4. Network interface binding
#
# Must be run as root!

set -e

# Configuration
HUGEPAGES_2MB=1024          # Number of 2MB hugepages (2GB total)
HUGEPAGES_1GB=2             # Number of 1GB hugepages (2GB total, if supported)
DPDK_INTERFACE=""           # Will be set by user or auto-detected
PRODUCER_CORE=1             # CPU core for packet reception (polling)
CONSUMER_CORE=2             # CPU core for message processing
DPDK_DIR="/usr/local/share/dpdk"
DPDK_DEVBIND="${DPDK_DIR}/usertools/dpdk-devbind.py"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root"
        exit 1
    fi
}

# Check if running on Linux
check_platform() {
    if [[ "$(uname)" != "Linux" ]]; then
        log_error "This script is designed for Linux systems"
        log_info "For macOS development, use PCAP PMD mode instead"
        exit 1
    fi
}

# Configure hugepages (required for DPDK zero-copy buffers)
setup_hugepages() {
    log_info "Configuring hugepages..."

    # Check for 1GB hugepage support
    if grep -q pdpe1gb /proc/cpuinfo; then
        log_info "1GB hugepages supported"

        # Mount hugetlbfs for 1GB pages
        mkdir -p /mnt/huge_1gb
        if ! mountpoint -q /mnt/huge_1gb; then
            mount -t hugetlbfs -o pagesize=1G none /mnt/huge_1gb
        fi

        # Allocate 1GB hugepages
        echo $HUGEPAGES_1GB > /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages
        log_info "Allocated $HUGEPAGES_1GB x 1GB hugepages"
    else
        log_warn "1GB hugepages not supported on this CPU"
    fi

    # Configure 2MB hugepages (always available)
    mkdir -p /mnt/huge_2mb
    if ! mountpoint -q /mnt/huge_2mb; then
        mount -t hugetlbfs -o pagesize=2M none /mnt/huge_2mb
    fi

    # Allocate 2MB hugepages
    echo $HUGEPAGES_2MB > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
    log_info "Allocated $HUGEPAGES_2MB x 2MB hugepages"

    # Verify allocation
    local allocated=$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages)
    if [[ $allocated -lt $HUGEPAGES_2MB ]]; then
        log_warn "Only $allocated hugepages allocated (requested $HUGEPAGES_2MB)"
        log_warn "You may need to free memory or reboot with hugepages=<N> kernel parameter"
    fi
}

# Isolate CPU cores for dedicated polling (reduces jitter)
setup_cpu_isolation() {
    log_info "Setting up CPU isolation..."

    # Check if cores are already isolated via kernel cmdline
    local cmdline=$(cat /proc/cmdline)
    if echo "$cmdline" | grep -q "isolcpus="; then
        log_info "CPU isolation already configured in kernel cmdline"
    else
        log_warn "CPU isolation not configured at boot time"
        log_warn "For optimal performance, add 'isolcpus=$PRODUCER_CORE,$CONSUMER_CORE' to kernel cmdline"
        log_warn "Edit /etc/default/grub and run update-grub"
    fi

    # Disable irqbalance (interferes with CPU affinity)
    if systemctl is-active --quiet irqbalance; then
        log_info "Stopping irqbalance service..."
        systemctl stop irqbalance
        systemctl disable irqbalance
    fi

    # Move IRQs away from isolated cores
    log_info "Moving IRQs away from cores $PRODUCER_CORE and $CONSUMER_CORE..."
    for irq_file in /proc/irq/*/smp_affinity_list; do
        if [[ -w $irq_file ]]; then
            # Set affinity to core 0 (or non-isolated cores)
            echo 0 > "$irq_file" 2>/dev/null || true
        fi
    done
}

# Load DPDK kernel modules
load_dpdk_modules() {
    log_info "Loading DPDK kernel modules..."

    # Load vfio-pci (preferred) or igb_uio
    if modprobe vfio-pci 2>/dev/null; then
        log_info "Loaded vfio-pci driver"
        DPDK_DRIVER="vfio-pci"

        # Enable unsafe IOMMU mode if IOMMU is not available
        if [[ ! -d /sys/kernel/iommu_groups/0 ]]; then
            log_warn "IOMMU not available, enabling unsafe mode"
            echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode
        fi
    elif modprobe igb_uio 2>/dev/null; then
        log_info "Loaded igb_uio driver"
        DPDK_DRIVER="igb_uio"
    elif modprobe uio_pci_generic 2>/dev/null; then
        log_info "Loaded uio_pci_generic driver"
        DPDK_DRIVER="uio_pci_generic"
    else
        log_error "No suitable DPDK driver found"
        log_error "Install DPDK and ensure kernel modules are available"
        exit 1
    fi
}

# Bind network interface to DPDK driver
bind_interface() {
    local iface=$1

    if [[ -z "$iface" ]]; then
        log_error "No interface specified"
        return 1
    fi

    log_info "Binding interface $iface to DPDK driver..."

    # Get PCI address of interface
    local pci_addr=$(ethtool -i "$iface" 2>/dev/null | grep "bus-info" | awk '{print $2}')

    if [[ -z "$pci_addr" ]]; then
        log_error "Could not find PCI address for $iface"
        return 1
    fi

    log_info "PCI address: $pci_addr"

    # Bring interface down
    ip link set "$iface" down

    # Bind to DPDK driver
    if [[ -x "$DPDK_DEVBIND" ]]; then
        $DPDK_DEVBIND --bind="$DPDK_DRIVER" "$pci_addr"
    else
        # Manual binding
        echo "$pci_addr" > /sys/bus/pci/drivers/${DPDK_DRIVER}/bind
    fi

    log_info "Interface $iface ($pci_addr) bound to $DPDK_DRIVER"
}

# Unbind interface from DPDK driver (restore to kernel driver)
unbind_interface() {
    local pci_addr=$1
    local kernel_driver=${2:-"ixgbe"}  # Default kernel driver

    log_info "Unbinding $pci_addr from DPDK driver..."

    if [[ -x "$DPDK_DEVBIND" ]]; then
        $DPDK_DEVBIND --bind="$kernel_driver" "$pci_addr"
    else
        echo "$pci_addr" > /sys/bus/pci/drivers/${DPDK_DRIVER}/unbind
        echo "$pci_addr" > /sys/bus/pci/drivers/${kernel_driver}/bind
    fi
}

# Display current status
show_status() {
    log_info "=== DPDK Environment Status ==="

    echo ""
    echo "Hugepages:"
    echo "  2MB pages: $(cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages) allocated, $(cat /sys/kernel/mm/hugepages/hugepages-2048kB/free_hugepages) free"
    if [[ -f /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages ]]; then
        echo "  1GB pages: $(cat /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages) allocated"
    fi

    echo ""
    echo "Mounted hugetlbfs:"
    mount | grep hugetlbfs || echo "  (none)"

    echo ""
    echo "CPU isolation:"
    if grep -q "isolcpus" /proc/cmdline; then
        grep -o "isolcpus=[^ ]*" /proc/cmdline
    else
        echo "  Not configured"
    fi

    echo ""
    echo "DPDK drivers loaded:"
    lsmod | grep -E "(vfio|igb_uio|uio)" || echo "  (none)"

    echo ""
    echo "Network devices:"
    if [[ -x "$DPDK_DEVBIND" ]]; then
        $DPDK_DEVBIND --status
    else
        echo "  dpdk-devbind.py not found"
    fi
}

# Setup for PCAP mode (development without real NIC)
setup_pcap_mode() {
    log_info "Setting up PCAP mode for development..."

    # PCAP PMD doesn't need special kernel setup
    # Just ensure hugepages for DPDK memory pools

    mkdir -p /mnt/huge_2mb
    if ! mountpoint -q /mnt/huge_2mb; then
        mount -t hugetlbfs -o pagesize=2M none /mnt/huge_2mb 2>/dev/null || true
    fi

    # Allocate minimal hugepages for PCAP mode
    echo 256 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages 2>/dev/null || true

    log_info "PCAP mode ready"
    log_info "Run feed handler with: --pcap-file <file.pcap>"
}

# Print usage
usage() {
    echo "Usage: $0 [OPTION]"
    echo ""
    echo "Options:"
    echo "  setup           Full DPDK environment setup (hugepages, CPU isolation, modules)"
    echo "  hugepages       Configure hugepages only"
    echo "  cpuiso          Configure CPU isolation only"
    echo "  bind <iface>    Bind interface to DPDK driver"
    echo "  unbind <pci>    Unbind PCI device from DPDK"
    echo "  status          Show current DPDK environment status"
    echo "  pcap            Setup for PCAP mode (development)"
    echo "  help            Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 setup                 # Full setup"
    echo "  $0 bind eth1             # Bind eth1 to DPDK"
    echo "  $0 unbind 0000:03:00.0   # Restore to kernel driver"
    echo ""
}

# Main
main() {
    case "${1:-help}" in
        setup)
            check_root
            check_platform
            setup_hugepages
            setup_cpu_isolation
            load_dpdk_modules
            show_status
            ;;
        hugepages)
            check_root
            check_platform
            setup_hugepages
            ;;
        cpuiso)
            check_root
            check_platform
            setup_cpu_isolation
            ;;
        bind)
            check_root
            check_platform
            load_dpdk_modules
            bind_interface "$2"
            ;;
        unbind)
            check_root
            check_platform
            unbind_interface "$2" "$3"
            ;;
        status)
            check_platform
            show_status
            ;;
        pcap)
            check_root
            setup_pcap_mode
            ;;
        help|--help|-h)
            usage
            ;;
        *)
            log_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
}

main "$@"
