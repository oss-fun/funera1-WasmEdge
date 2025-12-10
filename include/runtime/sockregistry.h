#pragma once
#include <mutex>
#include <vector>

namespace WasmEdge {
namespace Runtime {

struct SocketEntry {
    int vfd;
    int fd;
    int src;
};

class SocketRegistry {
public:
    static SocketRegistry &getInstance() {
        static SocketRegistry Instance;
        return Instance;
    }

    // 新しいエントリを追加
    void addSocket(int vfd, int fd, int src) {
        std::lock_guard<std::mutex> lock(Mutex);
        for (auto &e : entries) {
        if (e.fd == fd) {
            e.vfd = vfd;
            e.src = src;
            return;
        }
    }
        entries.push_back({vfd, fd, src});
    }

    // 全エントリ取得
    std::vector<SocketEntry> getAll() const {
        std::lock_guard<std::mutex> lock(Mutex);
        return entries; // コピーして返す
    }

    // vfd から fd を探す例
    int getFdByVfd(int vfd) const {
        std::lock_guard<std::mutex> lock(Mutex);
        for (const auto &e : entries) {
            if (e.vfd == vfd) return e.fd;
        }
        return -1; // 見つからなければ -1
    }

    int getVfdBySrc(int src) const {
        std::lock_guard<std::mutex> lock(Mutex);
        for (const auto &e : entries){
            if (e.src == src) return e.vfd;
        }
        return -1;
    }
    int getFdBySrc(int src) const {
        std::lock_guard<std::mutex> lock(Mutex);
        for (const auto &e : entries){
            if (e.src == src) return e.fd;
        }
        return -1;
    }


private:
    SocketRegistry() = default;
    SocketRegistry(const SocketRegistry &) = delete;
    SocketRegistry &operator=(const SocketRegistry &) = delete;

    mutable std::mutex Mutex;
    std::vector<SocketEntry> entries;
};

} // namespace Runtime
} // namespace WasmEdge