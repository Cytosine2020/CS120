#include "device/raw_socket.hpp"

#include "pthread.h"
#include <numeric>

#include "pcap/pcap.h"
#include "libnet.h"

#include "wire/wire.hpp"
#include "wire/ipv4.hpp"


namespace {
using namespace cs120;

struct pcap_callback_args {
    pcap_t *pcap_handle;
    struct bpf_program filter;
    SPSCQueue *queue;
    uint32_t ip_addr;
};

void pcap_callback(u_char *args_, const struct pcap_pkthdr *info, const u_char *packet) {
    auto *args = reinterpret_cast<pcap_callback_args *>(args_);

    if (info->caplen != info->len) {
        cs120_warn("packet truncated!");
        return;
    }

    Slice<uint8_t> eth_datagram{packet, info->caplen};

    auto *eth_header = eth_datagram.buffer_cast<ETHHeader>();
    if (eth_header == nullptr) {
        cs120_warn("invalid package!");
        return;
    }

    if (eth_header->protocol != 8) { return; }

    auto eth_data = eth_datagram[Range(sizeof(ETHHeader))];

    auto *ip_header = eth_data.buffer_cast<IPV4Header>();
    if (ip_header == nullptr || complement_checksum(ip_header->into_slice()) != 0) {
        cs120_warn("invalid package!");
        return;
    }

    if (ip_header->get_src_ip() == args->ip_addr &&
            ip_header->get_dest_ip() != args->ip_addr) { return; }

    auto slot = args->queue->try_send();
    if (slot->empty()) {
        cs120_warn("package loss!");
    } else {
        auto ip_datagram = eth_data[Range{0, ip_header->get_total_length()}];
        (*slot)[Range{0, ip_datagram.size()}].copy_from_slice(ip_datagram);
    }
}

void *raw_socket_receiver(void *args_) {
    auto *args = reinterpret_cast<pcap_callback_args *>(args_);
    auto *pcap_args = reinterpret_cast<u_char *>(args);
    auto count = std::numeric_limits<int32_t>::max();

    for (;;) {
        if (pcap_loop(args->pcap_handle, count, pcap_callback, pcap_args) == PCAP_ERROR) {
            cs120_abort(pcap_geterr(args->pcap_handle));
        }
    }
}

struct raw_socket_sender_args {
    libnet_t *context;
    SPSCQueue *queue;
};

void *raw_socket_sender(void *args_) {
    auto *args = reinterpret_cast<raw_socket_sender_args *>(args_);

    for (;;) {
        auto buffer = args->queue->recv();

        auto[ip_header, ip_option, ip_data] = ipv4_split(*buffer); // todo
        if (ip_header == nullptr) {
            cs120_warn("invalid package!");
            continue;
        }

        if (libnet_build_ipv4(ip_header->get_total_length(), ip_header->get_type_of_service(),
                              ip_header->get_identification(), ip_header->get_fragment(),
                              ip_header->get_time_to_live(),
                              static_cast<uint8_t>(ip_header->get_protocol()),
                              ip_header->get_checksum(), ip_header->get_src_ip(),
                              ip_header->get_dest_ip(), ip_data.begin(), ip_data.size(),
                              args->context, 0) == -1) {
            cs120_abort(libnet_geterror(args->context));
        }

        if (!ip_option.empty() && libnet_build_ipv4_options(
                ip_option.begin(), ip_option.size(), args->context, 0) == -1) {
            cs120_abort(libnet_geterror(args->context));
        }

        if (libnet_write(args->context) == -1) { cs120_warn(libnet_geterror(args->context)); }

        libnet_clear_packet(args->context);
    }
}
}


namespace cs120 {
RawSocket::RawSocket(size_t size, uint32_t ip_addr) :
        receiver{}, sender{}, receive_queue{nullptr}, send_queue{nullptr} {
    char pcap_error[PCAP_ERRBUF_SIZE]{};
    pcap_if_t *device = nullptr;

    if (pcap_findalldevs(&device, pcap_error) == PCAP_ERROR) { cs120_abort(pcap_error); }

    pcap_t *pcap_handle = pcap_open_live(device->name, static_cast<int32_t>(get_mtu() + 100u),
                                         0, 1, pcap_error);
    if (pcap_handle == nullptr) { cs120_abort(pcap_error); }

    char err_buf[LIBNET_ERRBUF_SIZE];
    libnet_t *context = libnet_init(LIBNET_RAW4_ADV, device->name, err_buf);
    if (context == nullptr) { cs120_abort(err_buf); }

    pcap_freealldevs(device);

    receive_queue = new SPSCQueue{get_mtu(), size};
    send_queue = new SPSCQueue{get_mtu(), size};

    auto *recv_args = new pcap_callback_args{
            .pcap_handle = pcap_handle,
            .filter = (struct bpf_program) {},
            .queue = receive_queue,
            .ip_addr = ip_addr,
    };

    auto *send_args = new raw_socket_sender_args{
            .context = context,
            .queue = send_queue,
    };

    if (pcap_compile(pcap_handle, &recv_args->filter, "icmp or udp or tcp", 0,
                     PCAP_NETMASK_UNKNOWN) == PCAP_ERROR) {
        cs120_abort(pcap_geterr(pcap_handle));
    }

    pthread_create(&receiver, nullptr, raw_socket_receiver, recv_args);
    pthread_create(&sender, nullptr, raw_socket_sender, send_args);
}
}
