#include "../runtime/Briefcase.Admin/Transport.hpp"
#include <atomic>
#include <future>
#include <iostream>
using namespace bc::admin;
int main() {
    try {
        auto start = Clock::now();
        auto identity = create_identity();
        Credentials server(identity), client;
        std::atomic<bool> stop{};
        Listener listener("127.0.0.1", 0, stop);
        auto task = std::async(std::launch::async, [&] {
            for (;;) {
                if (auto s = listener.accept(server)) {
                    auto request = s->receive();
                    if (request != "hello")
                        throw std::runtime_error("request mismatch");
                    s->send(std::string(100000, 'x'));
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
        auto stream = Stream::connect(client, "127.0.0.1:" + std::to_string(listener.port()),
                                      identity.fingerprint, stop);
        stream.send("hello");
        if (stream.receive(1048576) != std::string(100000, 'x'))
            throw std::runtime_error("response mismatch");
        task.get();
        std::cout << "PASS " << stream.protocol()
                  << " pinned certificate + encrypted 100KB roundtrip, identity/transport "
                  << std::chrono::duration<double, std::milli>(Clock::now() - start).count() << " ms\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
