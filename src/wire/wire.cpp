#include "wire/wire.hpp"

#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>

#include "pcap/pcap.h"


namespace cs120 {
uint16_t complement_checksum(Slice<uint8_t> buffer) {
    return complement_checksum_complement(complement_checksum_sum(buffer));
}

uint32_t get_local_ip() {
    char pcap_error[PCAP_ERRBUF_SIZE]{};
    pcap_if_t *device = nullptr;
    if (pcap_findalldevs(&device, pcap_error) == PCAP_ERROR) { cs120_abort(pcap_error); }

    int pipe_fd[2] = {-1, -1};
    if (pipe(pipe_fd) != 0) { cs120_abort("pipe error"); }

    int child = fork();
    switch (child) {
        case -1:
            cs120_abort("fork error");
        case 0:
            dup2(pipe_fd[1], STDOUT_FILENO);
            if (execl("/sbin/ifconfig", "/sbin/ifconfig", device->name, nullptr) != 0) {
                cs120_abort("failed to execute ifconfig");
            }
        default:;
    }

    int child_state = 0;
    if (waitpid(child, &child_state, 0) != child) { cs120_abort("waitpid error"); }
    if (!WIFEXITED(child_state)) { cs120_abort("ifconfig execution failed!"); }

    char buffer[1024]{};
    ssize_t size = read(pipe_fd[0], buffer, 1024);
    if (size < 0) { cs120_abort("read error"); }

    uint32_t ip = inet_addr(strstr(buffer, "inet ") + sizeof("inet"));
    if (ip == static_cast<uint32_t>(-1)) { cs120_abort("invalid ip"); }

    pcap_freealldevs(device);
    close(pipe_fd[0]);
    close(pipe_fd[1]);

    return ip;
}

EndPoint parse_ip_address(const char *str) {
    uint8_t buffer[4]{};
    uint16_t lan_port = 0;

    if (sscanf(str, "%hhd.%hhd.%hhd.%hhd:%hd", &buffer[0], &buffer[1], &buffer[2],
               &buffer[3], &lan_port) != 5) { cs120_abort("input_format_error!"); }

    return EndPoint{*reinterpret_cast<uint32_t *>(buffer), lan_port};
}

void ETHHeader::format() const {
    printf("Ethernet Header {\n");
    printf("\tdestination address: ");
    print_mac_addr(destination_mac);
    printf(",\n");
    printf("\tsource address: ");
    print_mac_addr(source_mac);
    printf(",\n");
    printf("\tprotocol: %d,\n", protocol);
    printf("}\n");
}
}
