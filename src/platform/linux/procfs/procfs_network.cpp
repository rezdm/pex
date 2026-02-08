#include "../procfs_reader.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <charconv>
#include <format>
#include <arpa/inet.h>
#include <netinet/in.h>

namespace fs = std::filesystem;

namespace pex {

std::map<int, NetworkConnectionInfo> ProcfsReader::parse_net_file(const std::string& path, const std::string& protocol) {
    std::map<int, NetworkConnectionInfo> connections;

    std::ifstream file(path);
    if (!file) return connections;

    std::string line;
    std::getline(file, line); // Skip header

    auto parse_address = [](const std::string& hex_addr, const bool is_ipv6) -> std::string {
        const size_t colon_pos = hex_addr.find(':');
        if (colon_pos == std::string::npos) return hex_addr;

        const std::string ip_hex = hex_addr.substr(0, colon_pos);
        const std::string port_hex = hex_addr.substr(colon_pos + 1);

        unsigned int port = 0;
        std::from_chars(port_hex.data(), port_hex.data() + port_hex.size(), port, 16);

        if (is_ipv6) {
            if (ip_hex.size() != 32) {
                return "[::]:" + std::to_string(port);
            }

            struct in6_addr addr{};
            for (int i = 0; i < 4; i++) {
                uint32_t word = 0;
                std::from_chars(ip_hex.data() + i * 8, ip_hex.data() + i * 8 + 8, word, 16);
                addr.s6_addr32[i] = htonl(word);
            }

            char buf[INET6_ADDRSTRLEN];
            if (inet_ntop(AF_INET6, &addr, buf, sizeof(buf))) {
                return std::format("[{}]:{}", buf, port);
            }
            return "[::]:" + std::to_string(port);
        } else {
            unsigned int ip = 0;
            std::from_chars(ip_hex.data(), ip_hex.data() + ip_hex.size(), ip, 16);
            const auto bytes = reinterpret_cast<unsigned char*>(&ip);
            return std::format("{}.{}.{}.{}:{}", bytes[0], bytes[1], bytes[2], bytes[3], port);
        }
    };

    static const char* tcp_states[] = {
        "", "ESTABLISHED", "SYN_SENT", "SYN_RECV", "FIN_WAIT1", "FIN_WAIT2",
        "TIME_WAIT", "CLOSE", "CLOSE_WAIT", "LAST_ACK", "LISTEN", "CLOSING"
    };

    bool is_ipv6 = protocol.find('6') != std::string::npos;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string sl_str;
        std::string local_addr, remote_addr;
        std::string state_hex;
        std::string tx_rx, tr_tm, retrnsmt;
        int uid = 0;
        int timeout = 0;
        int inode = 0;

        iss >> sl_str >> local_addr >> remote_addr >> state_hex
            >> tx_rx >> tr_tm >> retrnsmt >> uid >> timeout >> inode;

        if (inode == 0) continue;

        int state = 0;
        std::from_chars(state_hex.data(), state_hex.data() + state_hex.size(), state, 16);

        NetworkConnectionInfo conn;
        conn.protocol = protocol;
        conn.local_endpoint = parse_address(local_addr, is_ipv6);
        conn.remote_endpoint = parse_address(remote_addr, is_ipv6);
        conn.inode = inode;

        if (protocol.starts_with("tcp")) {
            conn.state = (state < 12) ? tcp_states[state] : "UNKNOWN";
        } else {
            conn.state = "-";
        }

        connections[inode] = std::move(conn);
    }

    return connections;
}

std::vector<NetworkConnectionInfo> ProcfsReader::get_network_connections(const int pid) {
    std::vector<NetworkConnectionInfo> result;

    std::set<int> socket_inodes;
    const std::string fd_path = "/proc/" + std::to_string(pid) + "/fd";

    try {
        for (const auto& entry : fs::directory_iterator(fd_path)) {
            try {
                std::string link = read_symlink(entry.path().string());
                if (link.starts_with("socket:[")) {
                    int inode = 0;
                    const auto start = link.data() + 8;
                    const auto end = link.data() + link.size() - 1;
                    std::from_chars(start, end, inode);
                    if (inode > 0) socket_inodes.insert(inode);
                }
            } catch (const std::exception&) {
                continue;
            }
        }
    } catch (const std::exception&) {
        return result;
    }

    if (socket_inodes.empty()) return result;

    auto tcp = parse_net_file("/proc/net/tcp", "tcp");
    auto tcp6 = parse_net_file("/proc/net/tcp6", "tcp6");
    auto udp = parse_net_file("/proc/net/udp", "udp");
    auto udp6 = parse_net_file("/proc/net/udp6", "udp6");

    for (int inode : socket_inodes) {
        if (auto itTcp4 = tcp.find(inode); itTcp4 != tcp.end()) {
            result.push_back(itTcp4->second);
        } else if (auto itTcp6 = tcp6.find(inode); itTcp6 != tcp6.end()) {
            result.push_back(itTcp6->second);
        } else if (auto itUdp4 = udp.find(inode); itUdp4 != udp.end()) {
            result.push_back(itUdp4->second);
        } else if (auto itUdp6 = udp6.find(inode); itUdp6 != udp6.end()) {
            result.push_back(itUdp6->second);
        }
    }

    return result;
}

} // namespace pex
