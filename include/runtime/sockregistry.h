#pragma once
#include <mutex>

namespace WasmEdge {
namespace Runtime {

class SocketRegistry {
public:
    static SocketRegistry &getInstance(){
        static SocketRegistry Instance;
        return Instance;
    }

    void setVSocket(int v){
        std::lock_guard<std::mutex> lock(Mutex);
        vsocket = v;
    }
    void setSocket(int v){
        std::lock_guard<std::mutex> lock(Mutex);
        socket = v;
    }
    int getVSocket() const {
        std::lock_guard<std::mutex> lock(Mutex);
        return vsocket;
    }
    int getSocket() const {
        std::lock_guard<std::mutex> lock(Mutex);
        return socket;
    }
private:
    SocketRegistry() : socket(0) {}
    SocketRegistry(const SocketRegistry &) = delete;
    SocketRegistry &operator=(const SocketRegistry &) = delete;

    mutable std::mutex Mutex;
    int vsocket;
    int socket;
};

} // namespace Runtime
} // namespace WasmEdge