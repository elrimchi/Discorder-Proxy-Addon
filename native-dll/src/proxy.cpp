// Discord process-local SOCKS5 transport. No TLS or DAVE decryption.
// C++17, Windows x64. MinHook is distributed under its upstream BSD license.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace dp {
thread_local bool bypass = false;
struct Bypass { bool previous = bypass; Bypass() { bypass = true; } ~Bypass() { bypass = previous; } };
#define REAL(name) decltype(&::name) real_##name = &::name
REAL(connect); REAL(WSAConnect); REAL(sendto); REAL(WSASendTo); REAL(send); REAL(WSASend);
REAL(recvfrom); REAL(WSARecvFrom); REAL(WSARecv); REAL(closesocket); REAL(getpeername);
REAL(WSAIoctl); REAL(WSAGetOverlappedResult); REAL(CreateIoCompletionPort);
REAL(GetQueuedCompletionStatus); REAL(GetQueuedCompletionStatusEx);
REAL(SetFileCompletionNotificationModes); REAL(CancelIoEx); REAL(GetOverlappedResult);
REAL(GetCommandLineW); REAL(GetCommandLineA);

struct Address {
    sockaddr_storage value{};
    int size = 0;
    const sockaddr* ptr() const { return reinterpret_cast<const sockaddr*>(&value); }
    sockaddr* ptr() { return reinterpret_cast<sockaddr*>(&value); }
    int family() const { return value.ss_family; }
    bool assign(const sockaddr* address, int length) {
        if (!address) return false;
        size = address->sa_family == AF_INET ? sizeof(sockaddr_in) :
               address->sa_family == AF_INET6 ? sizeof(sockaddr_in6) : 0;
        if (!size || length < size) return false;
        std::memcpy(&value, address, size);
        return true;
    }
    uint16_t port() const {
        if (family() == AF_INET) return ntohs(reinterpret_cast<const sockaddr_in*>(&value)->sin_port);
        if (family() == AF_INET6) return ntohs(reinterpret_cast<const sockaddr_in6*>(&value)->sin6_port);
        return 0;
    }
    bool local() const {
        if (family() == AF_INET) return (ntohl(reinterpret_cast<const sockaddr_in*>(&value)->sin_addr.s_addr) >> 24) == 127;
        if (family() == AF_INET6) {
            const auto* ip = &reinterpret_cast<const sockaddr_in6*>(&value)->sin6_addr;
            return IN6_IS_ADDR_LOOPBACK(ip) || (IN6_IS_ADDR_V4MAPPED(ip) && ip->u.Byte[12] == 127);
        }
        return true;
    }
    bool operator==(const Address& other) const {
        if (family() != other.family() || port() != other.port()) return false;
        if (family() == AF_INET) return reinterpret_cast<const sockaddr_in*>(&value)->sin_addr.s_addr ==
                                      reinterpret_cast<const sockaddr_in*>(&other.value)->sin_addr.s_addr;
        if (family() == AF_INET6) {
            const auto* a = reinterpret_cast<const sockaddr_in6*>(&value);
            const auto* b = reinterpret_cast<const sockaddr_in6*>(&other.value);
            return a->sin6_scope_id == b->sin6_scope_id && std::memcmp(&a->sin6_addr, &b->sin6_addr, 16) == 0;
        }
        return false;
    }
};
Address loopback(int family) {
    Address address;
    if (family == AF_INET6) {
        sockaddr_in6 a{}; a.sin6_family = AF_INET6; a.sin6_addr = in6addr_loopback;
        address.assign(reinterpret_cast<sockaddr*>(&a), sizeof(a));
    } else {
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.assign(reinterpret_cast<sockaddr*>(&a), sizeof(a));
    }
    return address;
}
struct NativeSocket {
    SOCKET value = INVALID_SOCKET;
    NativeSocket() = default;
    explicit NativeSocket(SOCKET s) : value(s) {}
    ~NativeSocket() { if (value != INVALID_SOCKET) real_closesocket(value); }
    NativeSocket(const NativeSocket&) = delete;
    NativeSocket& operator=(const NativeSocket&) = delete;
};
struct Relay;
struct Receive;
struct Context {
    SOCKET socket = INVALID_SOCKET;
    int type = 0;
    std::mutex lock;
    std::vector<std::shared_ptr<Relay>> relays;
    Address connected;
    bool proxied = false;
    std::atomic<bool> closed{false};
    HANDLE port = nullptr;
    ULONG_PTR key = 0;
    UCHAR notificationModes = 0;
};
struct State {
    std::mutex contextsLock, receivesLock;
    std::unordered_map<SOCKET, std::shared_ptr<Context>> contexts;
    std::unordered_map<LPOVERLAPPED, std::shared_ptr<Receive>> receives;
    Address proxy;
    bool tcp = true, udp = true, logging = false;
    std::wstring logPath;
    HANDLE logMutex = nullptr;
    std::string username, password;
    std::wstring commandLine;
    std::string commandLineAnsi;
};
// Process-lifetime storage: hooks and relay threads remain valid until process exit.
State& state() { static State* p = new State; return *p; }
void log(const char* message, int error = 0) {
    auto& s = state(); if (!s.logging) return;
    DWORD saved = GetLastError();
    char buffer[512]; SYSTEMTIME time{}; GetLocalTime(&time);
    int count = std::snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u.%03u pid=%lu %s (%d)\r\n",
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentProcessId(), message, error);
    OutputDebugStringA(buffer);
    // Never wait under the loader lock. Contended diagnostic records are dropped.
    DWORD locked = s.logMutex ? WaitForSingleObject(s.logMutex, 0) : WAIT_FAILED;
    if (locked == WAIT_OBJECT_0 || locked == WAIT_ABANDONED) {
        HANDLE file = CreateFileW(s.logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size{}, zero{}; DWORD written = 0;
            DWORD bytes = static_cast<DWORD>(std::max(0, std::min(count, 511)));
            if (GetFileSizeEx(file, &size)) {
                if (size.QuadPart + bytes > 2 * 1024 * 1024) {
                    SetFilePointerEx(file, zero, nullptr, FILE_BEGIN); SetEndOfFile(file);
                } else SetFilePointerEx(file, zero, nullptr, FILE_END);
                WriteFile(file, buffer, bytes, &written, nullptr);
            }
            CloseHandle(file);
        }
        ReleaseMutex(s.logMutex);
    }
    SetLastError(saved);
}
std::shared_ptr<Context> context(SOCKET socket, bool create = true) {
    auto& s = state();
    std::lock_guard<std::mutex> guard(s.contextsLock);
    auto it = s.contexts.find(socket);
    if (it != s.contexts.end()) return it->second;
    if (!create) return {};
    int type = 0, size = sizeof(type);
    if (getsockopt(socket, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &size) ||
        (type != SOCK_DGRAM && type != SOCK_STREAM)) return {};
    if (s.contexts.size() >= 4096) return {};
    auto result = std::make_shared<Context>(); result->socket = socket; result->type = type;
    s.contexts.emplace(socket, result);
    return result;
}
bool sendAll(SOCKET socket, const void* data, size_t length) {
    const auto* p = static_cast<const char*>(data);
    while (length) {
        int count = real_send(socket, p, static_cast<int>(std::min<size_t>(length, INT_MAX)), 0);
        if (count <= 0) return false;
        p += count; length -= count;
    }
    return true;
}
bool readAll(SOCKET socket, void* data, size_t length) {
    auto* p = static_cast<char*>(data);
    while (length) {
        int count = recv(socket, p, static_cast<int>(std::min<size_t>(length, INT_MAX)), 0);
        if (count <= 0) return false;
        p += count; length -= count;
    }
    return true;
}
std::vector<uint8_t> encode(const Address& address) {
    std::vector<uint8_t> bytes;
    if (address.family() == AF_INET) {
        const auto* a = reinterpret_cast<const sockaddr_in*>(&address.value);
        bytes.push_back(1);
        const auto* p = reinterpret_cast<const uint8_t*>(&a->sin_addr);
        bytes.insert(bytes.end(), p, p + 4);
    } else {
        const auto* a = reinterpret_cast<const sockaddr_in6*>(&address.value);
        bytes.push_back(4);
        const auto* p = reinterpret_cast<const uint8_t*>(&a->sin6_addr);
        bytes.insert(bytes.end(), p, p + 16);
    }
    bytes.push_back(static_cast<uint8_t>(address.port() >> 8));
    bytes.push_back(static_cast<uint8_t>(address.port()));
    return bytes;
}
bool decode(const uint8_t* data, size_t size, Address& result, size_t& consumed) {
    if (!size) return false;
    if (data[0] == 1 && size >= 7) {
        sockaddr_in a{}; a.sin_family = AF_INET;
        std::memcpy(&a.sin_addr, data + 1, 4); std::memcpy(&a.sin_port, data + 5, 2);
        result.assign(reinterpret_cast<sockaddr*>(&a), sizeof(a)); consumed = 7; return true;
    }
    if (data[0] == 4 && size >= 19) {
        sockaddr_in6 a{}; a.sin6_family = AF_INET6;
        std::memcpy(&a.sin6_addr, data + 1, 16); std::memcpy(&a.sin6_port, data + 17, 2);
        result.assign(reinterpret_cast<sockaddr*>(&a), sizeof(a)); consumed = 19; return true;
    }
    return false;
}
SOCKET socks(uint8_t command, const Address& target, Address& bound) {
    Bypass internal;
    NativeSocket control(::socket(state().proxy.family(), SOCK_STREAM, IPPROTO_TCP));
    if (control.value == INVALID_SOCKET) return INVALID_SOCKET;
    DWORD timeout = 6000;
    setsockopt(control.value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
    setsockopt(control.value, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
    // Nonblocking connect keeps an unreachable local SOCKS endpoint bounded.
    u_long nonblocking = 1; ioctlsocket(control.value, FIONBIO, &nonblocking);
    int connected = real_connect(control.value, state().proxy.ptr(), state().proxy.size);
    if (connected != 0 && WSAGetLastError() != WSAEWOULDBLOCK) return INVALID_SOCKET;
    if (connected != 0) {
        WSAPOLLFD p{control.value, POLLWRNORM, 0};
        if (WSAPoll(&p, 1, 6000) <= 0) return INVALID_SOCKET;
        int error = 0, length = sizeof(error);
        if (getsockopt(control.value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) || error) return INVALID_SOCKET;
    }
    nonblocking = 0; ioctlsocket(control.value, FIONBIO, &nonblocking);
    const bool authenticated = !state().username.empty();
    uint8_t greeting[] = {5, 1, static_cast<uint8_t>(authenticated ? 2 : 0)};
    uint8_t response[4]{};
    if (!sendAll(control.value, greeting, sizeof(greeting)) || !readAll(control.value, response, 2) ||
        response[0] != 5 || response[1] != greeting[2]) { log("SOCKS greeting rejected"); return INVALID_SOCKET; }
    if (authenticated) {
        std::vector<uint8_t> auth{1, static_cast<uint8_t>(state().username.size())};
        auth.insert(auth.end(), state().username.begin(), state().username.end());
        auth.push_back(static_cast<uint8_t>(state().password.size()));
        auth.insert(auth.end(), state().password.begin(), state().password.end());
        if (!sendAll(control.value, auth.data(), auth.size()) || !readAll(control.value, response, 2) ||
            response[0] != 1 || response[1] != 0) { log("SOCKS authentication rejected"); return INVALID_SOCKET; }
    }
    std::vector<uint8_t> request{5, command, 0};
    auto destination = encode(target); request.insert(request.end(), destination.begin(), destination.end());
    if (!sendAll(control.value, request.data(), request.size()) || !readAll(control.value, response, 4) ||
        response[0] != 5 || response[2] != 0 || response[1] != 0) {
        log(command == 3 ? "SOCKS UDP ASSOCIATE rejected" : "SOCKS CONNECT rejected", response[1]);
        return INVALID_SOCKET;
    }
    std::vector<uint8_t> address{response[3]};
    if (response[3] == 1 || response[3] == 4) {
        address.resize(response[3] == 1 ? 7 : 19);
        if (!readAll(control.value, address.data() + 1, address.size() - 1)) return INVALID_SOCKET;
        size_t consumed = 0;
        if (!decode(address.data(), address.size(), bound, consumed)) return INVALID_SOCKET;
    } else if (response[3] == 3) {
        uint8_t length = 0;
        if (!readAll(control.value, &length, 1) || !length) return INVALID_SOCKET;
        std::string host(length, '\0'); uint16_t port = 0;
        if (!readAll(control.value, &host[0], length) || !readAll(control.value, &port, 2)) return INVALID_SOCKET;
        addrinfo hints{}; hints.ai_socktype = SOCK_DGRAM; addrinfo* answers = nullptr;
        if (getaddrinfo(host.c_str(), std::to_string(ntohs(port)).c_str(), &hints, &answers)) return INVALID_SOCKET;
        bool ok = answers && bound.assign(answers->ai_addr, static_cast<int>(answers->ai_addrlen));
        freeaddrinfo(answers); if (!ok) return INVALID_SOCKET;
    } else return INVALID_SOCKET;
    if (command == 3) {
        // RFC 1928 permits a wildcard relay address: use the SOCKS server host.
        Address any = bound;
        if (any.family() == AF_INET && reinterpret_cast<sockaddr_in*>(&any.value)->sin_addr.s_addr == 0) {
            auto port = reinterpret_cast<sockaddr_in*>(&bound.value)->sin_port;
            bound = state().proxy; reinterpret_cast<sockaddr_in*>(&bound.value)->sin_port = port;
        } else if (any.family() == AF_INET6 && IN6_IS_ADDR_UNSPECIFIED(&reinterpret_cast<sockaddr_in6*>(&any.value)->sin6_addr)) {
            uint16_t port = bound.port(); bound = state().proxy;
            if (bound.family() == AF_INET) reinterpret_cast<sockaddr_in*>(&bound.value)->sin_port = htons(port);
            else reinterpret_cast<sockaddr_in6*>(&bound.value)->sin6_port = htons(port);
        }
        if (!bound.port()) return INVALID_SOCKET;
    }
    SOCKET result = control.value; control.value = INVALID_SOCKET; return result;
}

struct Relay {
    Address peer, local;
    NativeSocket listener;
    std::atomic<bool> stop{false}, alive{true};
    std::weak_ptr<Context> owner;
};
bool bindLoopback(NativeSocket& socket, Address& address, int type) {
    socket.value = ::socket(address.family(), type, type == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP);
    if (socket.value == INVALID_SOCKET) return false;
    BOOL exclusive = TRUE;
    setsockopt(socket.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char*>(&exclusive), sizeof(exclusive));
    if (bind(socket.value, address.ptr(), address.size)) return false;
    int length = sizeof(address.value);
    if (getsockname(socket.value, address.ptr(), &length)) return false;
    address.size = length; return true;
}
void udpWorker(std::shared_ptr<Relay> relay) {
    Bypass internal;
    auto done = [&] { relay->alive = false; };
    Address local = loopback(state().proxy.family());
    NativeSocket udp;
    if (!bindLoopback(udp, local, SOCK_DGRAM)) { log("UDP relay bind failed", WSAGetLastError()); done(); return; }
    Address bound;
    NativeSocket control(socks(3, local, bound));
    if (control.value == INVALID_SOCKET || real_connect(udp.value, bound.ptr(), bound.size)) {
        log("UDP relay establishment failed", WSAGetLastError()); done(); return;
    }
    auto header = encode(relay->peer); header.insert(header.begin(), {0, 0, 0});
    std::array<uint8_t, 65536> buffer{};
    Address application;
    log("UDP SOCKS association ready");
    while (!relay->stop) {
        WSAPOLLFD poll[] = {{relay->listener.value, POLLRDNORM, 0}, {udp.value, POLLRDNORM, 0}, {control.value, POLLRDNORM, 0}};
        if (WSAPoll(poll, 3, 100) < 0) break;
        if (poll[2].revents & (POLLRDNORM | POLLHUP | POLLERR | POLLNVAL)) break;
        if (poll[0].revents & POLLRDNORM) {
            Address sender; sender.size = sizeof(sender.value);
            int length = real_recvfrom(relay->listener.value, reinterpret_cast<char*>(buffer.data()) + header.size(),
                static_cast<int>(buffer.size() - header.size()), 0, sender.ptr(), &sender.size);
            auto owner = relay->owner.lock();
            if (!owner || owner->closed) break;
            Address expected; expected.size = sizeof(expected.value);
            if (getsockname(owner->socket, expected.ptr(), &expected.size)) break;
            if (length >= 0 && sender.local() && sender.port() == expected.port()) {
                application = sender;
                std::copy(header.begin(), header.end(), buffer.begin());
                real_send(udp.value, reinterpret_cast<char*>(buffer.data()), static_cast<int>(header.size()) + length, 0);
            }
        }
        if (poll[1].revents & POLLRDNORM) {
            int length = recv(udp.value, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
            if (length < 4 || buffer[0] || buffer[1] || buffer[2] || !application.size) continue;
            Address source; size_t used = 0;
            if (!decode(buffer.data() + 3, static_cast<size_t>(length - 3), source, used) || !(source == relay->peer)) continue;
            const size_t offset = used + 3;
            real_sendto(relay->listener.value, reinterpret_cast<char*>(buffer.data()) + offset,
                static_cast<int>(length - offset), 0, application.ptr(), application.size);
        }
    }
    done(); log("UDP SOCKS association closed");
}
// Bounded, bidirectional nonblocking pump. EOF is forwarded only after queued
// bytes drain, preserving TCP half-close and backpressure.
void pump(SOCKET a, SOCKET b, const std::shared_ptr<Relay>& relay) {
    u_long one = 1; ioctlsocket(a, FIONBIO, &one); ioctlsocket(b, FIONBIO, &one);
    struct Direction { std::vector<char> bytes; size_t offset = 0; bool eof = false, shut = false; } ab, ba;
    constexpr size_t limit = 128 * 1024;
    auto read = [&](SOCKET socket, Direction& d) {
        std::array<char, 32768> data{};
        int n = recv(socket, data.data(), static_cast<int>(data.size()), 0);
        if (n > 0) {
            if (d.offset) { d.bytes.erase(d.bytes.begin(), d.bytes.begin() + d.offset); d.offset = 0; }
            d.bytes.insert(d.bytes.end(), data.data(), data.data() + n);
        } else if (n == 0) d.eof = true;
        else if (WSAGetLastError() != WSAEWOULDBLOCK) return false;
        return true;
    };
    auto write = [&](SOCKET socket, Direction& d) {
        if (d.offset < d.bytes.size()) {
            int n = real_send(socket, d.bytes.data() + d.offset, static_cast<int>(d.bytes.size() - d.offset), 0);
            if (n > 0) d.offset += n;
            else if (n < 0 && WSAGetLastError() != WSAEWOULDBLOCK) return false;
        }
        if (d.offset == d.bytes.size()) { d.bytes.clear(); d.offset = 0; }
        if (d.eof && d.bytes.empty() && !d.shut) { shutdown(socket, SD_SEND); d.shut = true; }
        return true;
    };
    while (!relay->stop && !(ab.shut && ba.shut)) {
        WSAPOLLFD p[] = {{a, 0, 0}, {b, 0, 0}};
        if (!ab.eof && ab.bytes.size() - ab.offset < limit) p[0].events |= POLLRDNORM;
        if (!ba.eof && ba.bytes.size() - ba.offset < limit) p[1].events |= POLLRDNORM;
        if (!ba.bytes.empty()) p[0].events |= POLLWRNORM;
        if (!ab.bytes.empty()) p[1].events |= POLLWRNORM;
        if (WSAPoll(p, 2, 100) < 0) break;
        if ((p[0].revents | p[1].revents) & (POLLERR | POLLNVAL)) break;
        if ((p[0].revents & (POLLRDNORM | POLLHUP)) && !ab.eof && !read(a, ab)) break;
        if ((p[1].revents & (POLLRDNORM | POLLHUP)) && !ba.eof && !read(b, ba)) break;
        if (!write(b, ab) || !write(a, ba)) break;
    }
}
void tcpWorker(std::shared_ptr<Relay> relay) {
    Bypass internal;
    SOCKET accepted = INVALID_SOCKET;
    ULONGLONG deadline = GetTickCount64() + 10000;
    while (!relay->stop && GetTickCount64() < deadline) {
        WSAPOLLFD p{relay->listener.value, POLLRDNORM, 0};
        if (WSAPoll(&p, 1, 100) > 0) { accepted = accept(relay->listener.value, nullptr, nullptr); break; }
    }
    NativeSocket client(accepted);
    if (accepted == INVALID_SOCKET) { relay->alive = false; return; }
    Address bound;
    NativeSocket upstream(socks(1, relay->peer, bound));
    if (upstream.value != INVALID_SOCKET && !relay->stop) {
        log("TCP SOCKS tunnel ready");
        BOOL one = TRUE;
        setsockopt(upstream.value, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&one), sizeof(one));
        setsockopt(client.value, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&one), sizeof(one));
        pump(client.value, upstream.value, relay);
    }
    relay->alive = false;
}
std::shared_ptr<Relay> relayFor(const std::shared_ptr<Context>& owner, const Address& peer) {
    std::lock_guard<std::mutex> guard(owner->lock);
    if (owner->closed) return {};
    for (auto& relay : owner->relays) if (relay->peer == peer && relay->alive) return relay;
    // Old relays remain until their worker exits, but need not hold the mapping.
    owner->relays.erase(std::remove_if(owner->relays.begin(), owner->relays.end(),
        [](const auto& r) { return !r->alive; }), owner->relays.end());
    if (owner->relays.size() >= 32) return {};
    Bypass internal;
    auto result = std::make_shared<Relay>(); result->peer = peer; result->owner = owner;
    result->local = loopback(peer.family());
    if (!bindLoopback(result->listener, result->local, owner->type)) return {};
    if (owner->type == SOCK_STREAM && listen(result->listener.value, 1)) return {};
    owner->relays.push_back(result);
    try { std::thread(owner->type == SOCK_DGRAM ? udpWorker : tcpWorker, result).detach(); }
    catch (...) { owner->relays.pop_back(); return {}; }
    return result;
}
bool translateDestination(SOCKET socket, const sockaddr* name, int length, Address& translated, bool connected) {
    Address peer;
    if (!peer.assign(name, length) || peer.local()) return false;
    auto owner = context(socket);
    if (!owner) {
        int type = 0, size = sizeof(type);
        if (getsockopt(socket, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &size) == 0 &&
            ((type == SOCK_STREAM && state().tcp) || (type == SOCK_DGRAM && state().udp))) {
            translated.size = -1; WSASetLastError(WSAENOBUFS); return true;
        }
        return false;
    }
    if ((owner->type == SOCK_STREAM && !state().tcp) || (owner->type == SOCK_DGRAM && !state().udp)) return false;
    auto relay = relayFor(owner, peer);
    if (!relay) { WSASetLastError(WSAENOBUFS); translated.size = -1; return true; }
    translated = relay->local;
    if (connected) { std::lock_guard<std::mutex> guard(owner->lock); owner->connected = peer; owner->proxied = true; }
    return true;
}
void fixSource(const std::shared_ptr<Context>& owner, sockaddr* from, int* length) {
    if (!owner || !from || !length || *length <= 0) return;
    Address actual; if (!actual.assign(from, *length)) return;
    std::lock_guard<std::mutex> guard(owner->lock);
    for (const auto& relay : owner->relays) if (actual == relay->local) {
        if (*length >= relay->peer.size) { std::memcpy(from, relay->peer.ptr(), relay->peer.size); *length = relay->peer.size; }
        return;
    }
}
bool connectedUdpReady(SOCKET socket) {
    auto owner = context(socket, false);
    if (!owner || owner->type != SOCK_DGRAM || !state().udp) return true;
    Address peer;
    { std::lock_guard<std::mutex> guard(owner->lock); if (!owner->proxied) return true; peer = owner->connected; }
    auto relay = relayFor(owner, peer);
    if (!relay) { WSASetLastError(WSAENOBUFS); return false; }
    Address current; current.size = sizeof(current.value);
    if (real_getpeername(socket, current.ptr(), &current.size) == 0 && current == relay->local) return true;
    return real_connect(socket, relay->local.ptr(), relay->local.size) == 0;
}
int WSAAPI hook_connect(SOCKET socket, const sockaddr* name, int length) {
    if (bypass) return real_connect(socket, name, length);
    Address translated;
    if (!translateDestination(socket, name, length, translated, true)) {
        int result = real_connect(socket, name, length); int error = WSAGetLastError();
        if (!result) if (auto owner = context(socket, false); owner && owner->type == SOCK_DGRAM) {
            std::lock_guard<std::mutex> guard(owner->lock); owner->proxied = false; owner->connected = {};
            for (auto& relay : owner->relays) relay->stop = true;
            owner->relays.clear();
        }
        WSASetLastError(error); return result;
    }
    if (translated.size < 0) return SOCKET_ERROR;
    return real_connect(socket, translated.ptr(), translated.size);
}
int WSAAPI hook_WSAConnect(SOCKET socket, const sockaddr* name, int length, LPWSABUF caller, LPWSABUF callee, LPQOS sqos, LPQOS gqos) {
    Address translated;
    if (bypass) return real_WSAConnect(socket, name, length, caller, callee, sqos, gqos);
    if (!translateDestination(socket, name, length, translated, true)) {
        int result = real_WSAConnect(socket, name, length, caller, callee, sqos, gqos); int error = WSAGetLastError();
        if (!result) if (auto owner = context(socket, false); owner && owner->type == SOCK_DGRAM) {
            std::lock_guard<std::mutex> guard(owner->lock); owner->proxied = false; owner->connected = {};
            for (auto& relay : owner->relays) relay->stop = true;
            owner->relays.clear();
        }
        WSASetLastError(error); return result;
    }
    if (translated.size < 0) return SOCKET_ERROR;
    return real_WSAConnect(socket, translated.ptr(), translated.size, caller, callee, sqos, gqos);
}
BOOL PASCAL hook_ConnectEx(SOCKET socket, const sockaddr* name, int length, PVOID data, DWORD size, LPDWORD sent, LPOVERLAPPED overlap) {
    GUID guid = WSAID_CONNECTEX; LPFN_CONNECTEX function = nullptr; DWORD bytes = 0;
    if (real_WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &function, sizeof(function), &bytes, nullptr, nullptr)) return FALSE;
    Address translated;
    if (bypass || !translateDestination(socket, name, length, translated, true)) return function(socket, name, length, data, size, sent, overlap);
    if (translated.size < 0) return FALSE;
    return function(socket, translated.ptr(), translated.size, data, size, sent, overlap);
}
int WSAAPI hook_WSAIoctl(SOCKET socket, DWORD code, LPVOID input, DWORD inputSize, LPVOID output, DWORD outputSize,
                       LPDWORD bytes, LPWSAOVERLAPPED overlap, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    int result = real_WSAIoctl(socket, code, input, inputSize, output, outputSize, bytes, overlap, completion);
    if (!bypass && result == 0 && code == SIO_GET_EXTENSION_FUNCTION_POINTER && inputSize == sizeof(GUID) &&
        outputSize >= sizeof(LPFN_CONNECTEX) && input && output) {
        GUID guid = WSAID_CONNECTEX;
        if (std::memcmp(input, &guid, sizeof(guid)) == 0) *static_cast<LPFN_CONNECTEX*>(output) = hook_ConnectEx;
    }
    return result;
}
int WSAAPI hook_getpeername(SOCKET socket, sockaddr* name, int* length) {
    if (!bypass) if (auto owner = context(socket, false)) {
        std::lock_guard<std::mutex> guard(owner->lock);
        if (owner->proxied && owner->connected.size) {
            if (!name || !length || *length < owner->connected.size) { WSASetLastError(WSAEFAULT); return SOCKET_ERROR; }
            std::memcpy(name, owner->connected.ptr(), owner->connected.size); *length = owner->connected.size; return 0;
        }
    }
    return real_getpeername(socket, name, length);
}
int WSAAPI hook_sendto(SOCKET socket, const char* data, int size, int flags, const sockaddr* to, int length) {
    if (!bypass && !to && !connectedUdpReady(socket)) return SOCKET_ERROR;
    Address translated;
    int type = 0, typeSize = sizeof(type);
    if (!bypass && state().udp && getsockopt(socket, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &typeSize) == 0 &&
        type == SOCK_DGRAM && translateDestination(socket, to, length, translated, false)) {
        if (translated.size < 0) return SOCKET_ERROR;
        return real_sendto(socket, data, size, flags, translated.ptr(), translated.size);
    }
    return real_sendto(socket, data, size, flags, to, length);
}
int WSAAPI hook_recvfrom(SOCKET socket, char* data, int size, int flags, sockaddr* from, int* length) {
    int result = real_recvfrom(socket, data, size, flags, from, length);
    int error = WSAGetLastError();
    if (!bypass && (result >= 0 || error == WSAEMSGSIZE)) fixSource(context(socket, false), from, length);
    WSASetLastError(error); return result;
}

// Async receive adapter. The payload stays in the application's original WSABUF;
// only the source address is translated, before any user completion is exposed.
// An internal event with low bit set suppresses native IOCP delivery for the
// private OVERLAPPED. We then deliver the original OVERLAPPED via IOCP/APC/event.
struct Receive {
    std::shared_ptr<Context> owner;
    LPOVERLAPPED user = nullptr;
    OVERLAPPED native{};
    HANDLE event = nullptr, doneEvent = nullptr, thread = nullptr;
    PTP_WAIT wait = nullptr;
    sockaddr_storage source{};
    int sourceSize = sizeof(source), capacity = 0;
    sockaddr* destination = nullptr;
    int* destinationSize = nullptr;
    DWORD bytes = 0, flags = 0, error = 0;
    LPWSAOVERLAPPED_COMPLETION_ROUTINE routine = nullptr;
    std::atomic<bool> done{false};
    std::atomic<bool> notified{false};
    ~Receive() { if (event) CloseHandle(event); if (doneEvent) CloseHandle(doneEvent); if (thread) CloseHandle(thread); }
};
std::shared_ptr<Receive> operation(LPOVERLAPPED overlap) {
    if (!overlap) return {};
    std::lock_guard<std::mutex> guard(state().receivesLock);
    auto it = state().receives.find(overlap);
    return it == state().receives.end() ? nullptr : it->second;
}
void forgetOld(LPOVERLAPPED overlap) {
    if (!overlap) return;
    std::lock_guard<std::mutex> guard(state().receivesLock);
    auto it = state().receives.find(overlap);
    if (it != state().receives.end() && it->second->done) state().receives.erase(it);
}
ULONG_PTR nativeStatus(DWORD error) {
    if (!error) return 0;
    if (error == WSA_OPERATION_ABORTED) return 0xC0000120UL;
    if (error == WSAEMSGSIZE) return 0x80000005UL;
    if (error == WSAECONNRESET) return 0xC000020DUL;
    return 0xC0000001UL;
}
DWORD win32Error(DWORD error) {
    if (error == WSAEMSGSIZE) return ERROR_MORE_DATA;
    if (error == WSAECONNRESET) return ERROR_NETNAME_DELETED;
    return error;
}
VOID CALLBACK userApc(ULONG_PTR parameter) {
    std::unique_ptr<std::shared_ptr<Receive>> holder(reinterpret_cast<std::shared_ptr<Receive>*>(parameter));
    auto op = *holder;
    op->routine(op->error, op->bytes, op->user, op->flags);
    op->notified = true;
}
void complete(const std::shared_ptr<Receive>& op, bool immediate) {
    if (!op->error || op->error == WSAEMSGSIZE) {
        int size = op->sourceSize;
        fixSource(op->owner, reinterpret_cast<sockaddr*>(&op->source), &size);
        if (op->destination) {
            if (op->capacity < size) { op->error = WSAEFAULT; op->bytes = 0; }
            else { std::memcpy(op->destination, &op->source, size); *op->destinationSize = size; }
        }
    }
    HANDLE port = nullptr; ULONG_PTR key = 0; UCHAR modes = 0;
    { std::lock_guard<std::mutex> guard(op->owner->lock); port = op->owner->port; key = op->owner->key; modes = op->owner->notificationModes; }
    // The caller may reuse OVERLAPPED as soon as completion becomes visible.
    const auto event = reinterpret_cast<ULONG_PTR>(op->user->hEvent);
    op->user->InternalHigh = op->bytes;
    InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&op->user->Internal), reinterpret_cast<PVOID>(nativeStatus(op->error)));
    op->done.store(true, std::memory_order_release);
    SetEvent(op->doneEvent);
    if (op->routine) {
        auto holder = new std::shared_ptr<Receive>(op);
        if (!QueueUserAPC(userApc, op->thread, reinterpret_cast<ULONG_PTR>(holder))) { delete holder; log("Receive APC delivery failed", GetLastError()); }
        return;
    }
    if (event & ~ULONG_PTR(1)) SetEvent(reinterpret_cast<HANDLE>(event & ~ULONG_PTR(1)));
    if (port && !(event & 1) && !(immediate && (modes & FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)))
        PostQueuedCompletionStatus(port, op->bytes, key, op->user);
}
VOID CALLBACK receiveReady(PTP_CALLBACK_INSTANCE, PVOID parameter, PTP_WAIT wait, TP_WAIT_RESULT) {
    Bypass internal;
    std::unique_ptr<std::shared_ptr<Receive>> holder(static_cast<std::shared_ptr<Receive>*>(parameter));
    auto op = *holder;
    if (op->owner->closed) {
        op->bytes = static_cast<DWORD>(op->native.InternalHigh); op->error = WSA_OPERATION_ABORTED;
    } else if (!real_WSAGetOverlappedResult(op->owner->socket, &op->native, &op->bytes, FALSE, &op->flags)) {
        op->error = WSAGetLastError();
        if (op->error == WSAENOTSOCK) op->error = WSA_OPERATION_ABORTED;
    }
    complete(op, false);
    CloseThreadpoolWait(wait);
}
int WSAAPI hook_WSARecvFrom(SOCKET socket, LPWSABUF buffers, DWORD count, LPDWORD received, LPDWORD flags,
                          sockaddr* from, LPINT length, LPWSAOVERLAPPED overlap, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    auto owner = bypass ? nullptr : context(socket);
    if (!owner || owner->type != SOCK_DGRAM || !state().udp || !overlap) {
        if (!bypass) forgetOld(overlap);
        int result = real_WSARecvFrom(socket, buffers, count, received, flags, from, length, overlap, completion);
        int error = WSAGetLastError();
        if (!bypass && !overlap && (result == 0 || error == WSAEMSGSIZE)) fixSource(owner, from, length);
        WSASetLastError(error); return result;
    }
    if (!flags || (from && (!length || *length < static_cast<int>(sizeof(sockaddr_in))))) { WSASetLastError(WSAEFAULT); return SOCKET_ERROR; }
    if (auto old = operation(overlap); old && !old->done) { WSASetLastError(WSAEINVAL); return SOCKET_ERROR; }
    auto op = std::make_shared<Receive>();
    op->owner = owner; op->user = overlap; op->flags = *flags; op->routine = completion;
    op->destination = from; op->destinationSize = length; op->capacity = from ? *length : 0;
    op->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    op->doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!op->event || !op->doneEvent) { WSASetLastError(WSAENOBUFS); return SOCKET_ERROR; }
    if (completion && !DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &op->thread, THREAD_SET_CONTEXT, FALSE, 0)) {
        WSASetLastError(WSAENOBUFS); return SOCKET_ERROR;
    }
    op->native.hEvent = reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(op->event) | 1);
    auto holder = new std::shared_ptr<Receive>(op);
    op->wait = CreateThreadpoolWait(receiveReady, holder, nullptr);
    if (!op->wait) { delete holder; WSASetLastError(WSAENOBUFS); return SOCKET_ERROR; }
    HANDLE event = reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(overlap->hEvent) & ~ULONG_PTR(1));
    if (!completion && event) ResetEvent(event);
    overlap->InternalHigh = 0; overlap->Internal = 0x103; // STATUS_PENDING
    { std::lock_guard<std::mutex> guard(state().receivesLock);
      if (state().receives.size() > 128) for (auto it = state().receives.begin(); it != state().receives.end();) {
          const auto& old = it->second;
          if (old->done && old->owner->closed && (old->notified || (!old->owner->port && !old->routine))) it = state().receives.erase(it);
          else ++it;
      }
      state().receives[overlap] = op; }
    int result = real_WSARecvFrom(socket, buffers, count, &op->bytes, &op->flags, reinterpret_cast<sockaddr*>(&op->source),
                                  &op->sourceSize, &op->native, nullptr);
    int error = result == 0 ? 0 : WSAGetLastError();
    if (error == WSA_IO_PENDING) {
        SetThreadpoolWait(op->wait, op->event, nullptr);
        WSASetLastError(WSA_IO_PENDING); return SOCKET_ERROR;
    }
    CloseThreadpoolWait(op->wait); delete holder;
    if (error) {
        { std::lock_guard<std::mutex> guard(state().receivesLock); state().receives.erase(overlap); }
        overlap->Internal = nativeStatus(error); WSASetLastError(error); return SOCKET_ERROR;
    }
    complete(op, true);
    if (received) *received = op->bytes; *flags = op->flags;
    if (op->error) { WSASetLastError(op->error); return SOCKET_ERROR; }
    return 0;
}
BOOL WSAAPI hook_WSAGetOverlappedResult(SOCKET socket, LPWSAOVERLAPPED overlap, LPDWORD transferred, BOOL wait, LPDWORD flags) {
    if (!bypass) if (auto op = operation(overlap); op && op->owner->socket == socket) {
        if (!op->done && wait) WaitForSingleObject(op->doneEvent, INFINITE);
        if (!op->done) { WSASetLastError(WSA_IO_INCOMPLETE); return FALSE; }
        op->notified = true;
        if (transferred) *transferred = op->bytes; if (flags) *flags = op->flags;
        if (op->error) { WSASetLastError(op->error); return FALSE; } return TRUE;
    }
    return real_WSAGetOverlappedResult(socket, overlap, transferred, wait, flags);
}
BOOL WINAPI hook_GetOverlappedResult(HANDLE handle, LPOVERLAPPED overlap, LPDWORD transferred, BOOL wait) {
    if (!bypass) if (auto op = operation(overlap); op && op->owner->socket == reinterpret_cast<SOCKET>(handle)) {
        DWORD flags = 0;
        BOOL result = hook_WSAGetOverlappedResult(op->owner->socket, overlap, transferred, wait, &flags);
        if (!result) SetLastError(win32Error(WSAGetLastError())); return result;
    }
    return real_GetOverlappedResult(handle, overlap, transferred, wait);
}
HANDLE WINAPI hook_CreateIoCompletionPort(HANDLE file, HANDLE existing, ULONG_PTR key, DWORD threads) {
    HANDLE result = real_CreateIoCompletionPort(file, existing, key, threads);
    DWORD error = GetLastError();
    if (!bypass && result && file != INVALID_HANDLE_VALUE) if (auto owner = context(reinterpret_cast<SOCKET>(file))) {
        std::lock_guard<std::mutex> guard(owner->lock); owner->port = result; owner->key = key;
    }
    SetLastError(error); return result;
}
BOOL WINAPI hook_SetFileCompletionNotificationModes(HANDLE file, UCHAR modes) {
    BOOL result = real_SetFileCompletionNotificationModes(file, modes); DWORD error = GetLastError();
    if (!bypass && result) if (auto owner = context(reinterpret_cast<SOCKET>(file))) {
        std::lock_guard<std::mutex> guard(owner->lock); owner->notificationModes = modes;
    }
    SetLastError(error); return result;
}
BOOL WINAPI hook_GetQueuedCompletionStatus(HANDLE port, LPDWORD transferred, PULONG_PTR key, LPOVERLAPPED* overlap, DWORD timeout) {
    BOOL result = real_GetQueuedCompletionStatus(port, transferred, key, overlap, timeout);
    DWORD error = GetLastError();
    if (!bypass && overlap && *overlap) if (auto op = operation(*overlap); op && op->done && op->owner->port == port) {
        op->notified = true;
        if (transferred) *transferred = op->bytes;
        if (op->error) { SetLastError(win32Error(op->error)); return FALSE; }
    }
    SetLastError(error); return result;
}
BOOL WINAPI hook_GetQueuedCompletionStatusEx(HANDLE port, LPOVERLAPPED_ENTRY entries, ULONG count, PULONG removed, DWORD timeout, BOOL alertable) {
    BOOL result = real_GetQueuedCompletionStatusEx(port, entries, count, removed, timeout, alertable);
    DWORD error = GetLastError();
    if (!bypass && result && entries && removed) for (ULONG i = 0; i < *removed; ++i)
        if (auto op = operation(entries[i].lpOverlapped); op && op->done && op->owner->port == port) {
            op->notified = true;
            entries[i].Internal = nativeStatus(op->error); entries[i].dwNumberOfBytesTransferred = op->bytes;
        }
    SetLastError(error); return result;
}
BOOL WINAPI hook_CancelIoEx(HANDLE file, LPOVERLAPPED overlap) {
    if (!bypass && overlap) if (auto op = operation(overlap); op && op->owner->socket == reinterpret_cast<SOCKET>(file))
        return real_CancelIoEx(file, &op->native);
    return real_CancelIoEx(file, overlap);
}
int WSAAPI hook_WSASendTo(SOCKET socket, LPWSABUF buffers, DWORD count, LPDWORD sent, DWORD flags, const sockaddr* to,
                        int length, LPWSAOVERLAPPED overlap, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    if (!bypass) forgetOld(overlap);
    if (!bypass && !to && !connectedUdpReady(socket)) return SOCKET_ERROR;
    Address translated;
    int type = 0, typeSize = sizeof(type);
    if (!bypass && state().udp && getsockopt(socket, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &typeSize) == 0 &&
        type == SOCK_DGRAM && translateDestination(socket, to, length, translated, false)) {
        if (translated.size < 0) return SOCKET_ERROR;
        return real_WSASendTo(socket, buffers, count, sent, flags, translated.ptr(), translated.size, overlap, completion);
    }
    return real_WSASendTo(socket, buffers, count, sent, flags, to, length, overlap, completion);
}
int WSAAPI hook_WSASend(SOCKET socket, LPWSABUF buffers, DWORD count, LPDWORD sent, DWORD flags,
                      LPWSAOVERLAPPED overlap, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    if (!bypass) forgetOld(overlap);
    if (!bypass && !connectedUdpReady(socket)) return SOCKET_ERROR;
    return real_WSASend(socket, buffers, count, sent, flags, overlap, completion);
}
int WSAAPI hook_send(SOCKET socket, const char* data, int size, int flags) {
    if (!bypass && !connectedUdpReady(socket)) return SOCKET_ERROR;
    return real_send(socket, data, size, flags);
}
LPWSTR WINAPI hook_GetCommandLineW() { return const_cast<LPWSTR>(state().commandLine.c_str()); }
LPSTR WINAPI hook_GetCommandLineA() { return const_cast<LPSTR>(state().commandLineAnsi.c_str()); }
int WSAAPI hook_WSARecv(SOCKET socket, LPWSABUF buffers, DWORD count, LPDWORD received, LPDWORD flags,
                      LPWSAOVERLAPPED overlap, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    if (!bypass) forgetOld(overlap);
    return real_WSARecv(socket, buffers, count, received, flags, overlap, completion);
}
int WSAAPI hook_closesocket(SOCKET socket) {
    auto owner = bypass ? nullptr : context(socket, false);
    int result = real_closesocket(socket); int error = WSAGetLastError();
    if (owner && result == 0) {
        { std::lock_guard<std::mutex> guard(state().contextsLock);
          auto it = state().contexts.find(socket); if (it != state().contexts.end() && it->second == owner) state().contexts.erase(it); }
        owner->closed = true;
        std::lock_guard<std::mutex> guard(owner->lock);
        for (auto& relay : owner->relays) relay->stop = true;
        owner->relays.clear();
    }
    WSASetLastError(error); return result;
}

bool readCredential(const std::wstring& ini, const wchar_t* key, std::string& value) {
    std::vector<wchar_t> buffer(257);
    DWORD length = GetPrivateProfileStringW(L"proxy", key, L"", buffer.data(),
        static_cast<DWORD>(buffer.size()), ini.c_str());
    if (length >= buffer.size() - 1) return false;
    value.clear();
    if (!length) return true;
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(),
        static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0 || bytes > 255) return false;
    value.resize(bytes);
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(),
        static_cast<int>(length), &value[0], bytes, nullptr, nullptr) == bytes;
}

bool initialize(HMODULE module) {
    auto& s = state();
    // Windows can load/notify DLLs on small-stack capture-driver threads.
    // Long-path storage belongs on the heap, not in a loader callback frame.
    std::vector<wchar_t> path(32768);
    if (!GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()))) return false;
    std::wstring dir(path.data()); dir.resize(dir.find_last_of(L"\\/") + 1);
    std::wstring ini = dir + L"discord-proxy.ini";
    s.logging = GetPrivateProfileIntW(L"proxy", L"log", 0, ini.c_str()) != 0;
    if (s.logging) {
        DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        if (length && length <= 32768) {
            std::vector<wchar_t> local(length);
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), length)) {
                std::wstring folder = std::wstring(local.data()) + L"\\DiscordProcessProxy";
                CreateDirectoryW(folder.c_str(), nullptr);
                s.logPath = folder + L"\\discord-proxy.log";
                uint64_t hash = 14695981039346656037ull;
                for (wchar_t ch : s.logPath) { hash ^= static_cast<uint16_t>(ch); hash *= 1099511628211ull; }
                std::wstring name = L"Local\\DiscordProcessProxy.Log." + std::to_wstring(hash);
                s.logMutex = CreateMutexW(nullptr, FALSE, name.c_str());
            }
        }
    }
    if (!readCredential(ini, L"username", s.username) || !readCredential(ini, L"password", s.password) ||
        s.username.empty() != s.password.empty()) {
        log("Invalid SOCKS5 credentials: set both username and password, at most 255 UTF-8 bytes each");
        return false;
    }
    const bool authenticated = !s.username.empty();
    s.tcp = GetPrivateProfileIntW(L"proxy", L"tcp", 1, ini.c_str()) != 0;
    s.udp = GetPrivateProfileIntW(L"proxy", L"udp", 1, ini.c_str()) != 0;
    wchar_t host[256]{};
    GetPrivateProfileStringW(L"proxy", L"host", L"127.0.0.1", host, 256, ini.c_str());
    int port = GetPrivateProfileIntW(L"proxy", L"port", 10808, ini.c_str());
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(static_cast<u_short>(port));
    if (port <= 0 || port > 65535 || InetPtonW(AF_INET, host, &a.sin_addr) != 1) return false;
    s.proxy.assign(reinterpret_cast<sockaddr*>(&a), sizeof(a));
    if (!s.proxy.local()) return false;
    s.commandLine = real_GetCommandLineW();
    if (!authenticated && GetPrivateProfileIntW(L"proxy", L"chromium_proxy", 1, ini.c_str()) && s.commandLine.find(L"--proxy-server=") == std::wstring::npos)
        s.commandLine += L" --proxy-server=socks5://" + std::wstring(host) + L":" + std::to_wstring(port);
    int ansiSize = WideCharToMultiByte(CP_ACP, 0, s.commandLine.c_str(), -1, nullptr, 0, nullptr, nullptr);
    s.commandLineAnsi.resize(ansiSize);
    WideCharToMultiByte(CP_ACP, 0, s.commandLine.c_str(), -1, &s.commandLineAnsi[0], ansiSize, nullptr, nullptr);
    s.commandLineAnsi.resize(ansiSize - 1);
    if (MH_Initialize() != MH_OK) return false;
    struct Hook { const wchar_t* dll; const char* name; void* replacement; void** original; };
#define HOOK(dll, name) {dll, #name, reinterpret_cast<void*>(hook_##name), reinterpret_cast<void**>(&real_##name)}
    Hook hooks[] = {
        HOOK(L"ws2_32.dll", connect), HOOK(L"ws2_32.dll", WSAConnect), HOOK(L"ws2_32.dll", WSAIoctl),
        HOOK(L"ws2_32.dll", sendto), HOOK(L"ws2_32.dll", WSASendTo), HOOK(L"ws2_32.dll", WSASend), HOOK(L"ws2_32.dll", send),
        HOOK(L"ws2_32.dll", recvfrom), HOOK(L"ws2_32.dll", WSARecvFrom), HOOK(L"ws2_32.dll", WSARecv),
        HOOK(L"ws2_32.dll", closesocket), HOOK(L"ws2_32.dll", getpeername), HOOK(L"ws2_32.dll", WSAGetOverlappedResult),
        HOOK(L"kernel32.dll", CreateIoCompletionPort), HOOK(L"kernel32.dll", GetQueuedCompletionStatus),
        HOOK(L"kernel32.dll", GetQueuedCompletionStatusEx), HOOK(L"kernel32.dll", SetFileCompletionNotificationModes),
        HOOK(L"kernel32.dll", CancelIoEx), HOOK(L"kernel32.dll", GetOverlappedResult)
        , HOOK(L"kernel32.dll", GetCommandLineW), HOOK(L"kernel32.dll", GetCommandLineA)
    };
    for (auto& hook : hooks) {
        HMODULE library = GetModuleHandleW(hook.dll);
        auto status = MH_CreateHook(GetProcAddress(library, hook.name), hook.replacement, hook.original);
        if (status != MH_OK) { log("Hook creation failed", status); MH_Uninitialize(); return false; }
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) { MH_Uninitialize(); return false; }
    log("Discorder-Proxy-Addon initialized"); return true;
}
} // namespace dp

extern "C" {
void* versionFunctions[17]{};
}
// Deliberately out-of-line: a compiler must not reserve initialization storage
// in DllMain's prologue before it checks DLL_THREAD_ATTACH/DETACH. Static CRT
// and thread_local need thread notifications; DisableThreadLibraryCalls is not
// a valid way to avoid them (see Microsoft's function contract).
__declspec(noinline) BOOL processAttach(HINSTANCE module) {
    try {
        std::vector<wchar_t> system(32768);
        if (!GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size()))) return FALSE;
        std::wstring name(system.data()); name += L"\\version.dll";
        HMODULE version = LoadLibraryW(name.c_str());
        const char* exports[] = {"GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA", "GetFileVersionInfoExW",
            "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA", "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",
            "GetFileVersionInfoW", "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW", "VerLanguageNameA",
            "VerLanguageNameW", "VerQueryValueA", "VerQueryValueW"};
        if (!version) return FALSE;
        for (size_t i = 0; i < 17; ++i) versionFunctions[i] = reinterpret_cast<void*>(GetProcAddress(version, exports[i]));
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(versionFunctions), &pinned);
        if (!dp::initialize(module)) { dp::log("DLL initialization failed; refusing to load"); return FALSE; }
        return TRUE;
    } catch (...) { return FALSE; }
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) return processAttach(module);
    return TRUE; // No buffers, allocations or network work for thread messages.
}

