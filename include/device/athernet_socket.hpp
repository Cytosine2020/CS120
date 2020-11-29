#ifndef CS120_ATHERNET_SOCKET_HPP
#define CS120_ATHERNET_SOCKET_HPP


#include <unistd.h>

#include "utility.hpp"
#include "queue.hpp"
#include "mod.hpp"
#include "athernet.hpp"


namespace cs120 {
/// this socket if for mimicking athernet device through unix socket
class AthernetSocket : public BaseSocket {
private:
    pthread_t receiver, sender;
    SPSCQueue *receive_queue, *send_queue;
    int athernet;

public:
    AthernetSocket(size_t size);

    AthernetSocket(AthernetSocket &&other) noexcept = default;

    AthernetSocket &operator=(AthernetSocket &&other) noexcept = default;

    size_t get_mtu() final { return ATHERNET_MTU - 1; }

    SPSCQueueSenderSlotGuard try_send() final { return send_queue->try_send(); }

    SPSCQueueSenderSlotGuard send() final { return send_queue->send(); }

    SPSCQueueReceiverSlotGuard try_recv() final { return receive_queue->try_recv(); }

    SPSCQueueReceiverSlotGuard recv() final { return receive_queue->recv(); }

    ~AthernetSocket() override {
        if (athernet != -1) { close(athernet); }
    }
};
}


#endif //CS120_ATHERNET_SOCKET_HPP
