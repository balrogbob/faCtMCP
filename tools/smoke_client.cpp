#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
typedef int SOCKET;
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
#endif

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string framed_message(const std::string& body) {
    std::ostringstream out;
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    return out.str();
}

#ifdef _WIN32
std::string run_stdio_server(const std::string& command, const std::string& input) {
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE stdin_read = nullptr, stdin_write = nullptr, stdout_read = nullptr, stdout_write = nullptr;
    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) return "";
    if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0)) return "";
    if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0)) return "";
    if (!SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0)) return "";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.hStdInput = stdin_read;
    si.hStdOutput = stdout_write;
    si.hStdError = stdout_write;
    si.dwFlags = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};
    std::vector<char> cmdline(command.begin(), command.end());
    cmdline.push_back('\0');
    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        return "";
    }

    CloseHandle(stdin_read);
    CloseHandle(stdout_write);

    DWORD written = 0;
    WriteFile(stdin_write, input.data(), static_cast<DWORD>(input.size()), &written, nullptr);
    CloseHandle(stdin_write);

    std::string output;
    std::array<char, 4096> buffer{};
    DWORD read = 0;
    while (ReadFile(stdout_read, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read > 0) {
        output.append(buffer.data(), buffer.data() + read);
    }
    CloseHandle(stdout_read);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return output;
}
#else
std::string run_stdio_server(const std::string& command, const std::string& input) {
    int stdin_pipe[2];
    int stdout_pipe[2];
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) return "";

    pid_t pid = fork();
    if (pid < 0) return "";

    if (pid == 0) {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        execl(command.c_str(), command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    write(stdin_pipe[1], input.data(), input.size());
    close(stdin_pipe[1]);

    std::string output;
    std::array<char, 4096> buffer{};
    ssize_t count = 0;
    while ((count = read(stdout_pipe[0], buffer.data(), buffer.size())) > 0) {
        output.append(buffer.data(), static_cast<size_t>(count));
    }
    close(stdout_pipe[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return output;
}
#endif

std::string http_request(int port, const std::string& request_text) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return "";

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return "";
    }

    send(sock, request_text.c_str(), static_cast<int>(request_text.size()), 0);
    std::string response;
    std::array<char, 4096> buffer{};
    int received = 0;
    while ((received = recv(sock, buffer.data(), static_cast<int>(buffer.size()), 0)) > 0) {
        response.append(buffer.data(), buffer.data() + received);
    }

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return response;
}

int smoke_stdio(const std::string& executable) {
    const std::string input =
        framed_message(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") +
        framed_message(R"({"jsonrpc":"2.0","method":"notifications/initialized","params":{}})") +
        framed_message(R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})");

#ifdef _WIN32
    const std::string output = run_stdio_server("\"" + executable + "\"", input);
#else
    const std::string output = run_stdio_server(executable, input);
#endif

    std::cout << output << "\n";
    return (contains(output, "protocolVersion") && contains(output, "tools")) ? 0 : 1;
}

int smoke_http(const std::string& executable, int port) {
#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

#ifdef _WIN32
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> cmdline(executable.begin(), executable.end());
    cmdline.push_back('\0');
    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        return 1;
    }
#else
    pid_t pid = fork();
    if (pid == 0) {
        execl(executable.c_str(), executable.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    if (pid < 0) return 1;
#endif

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << http_request(port, "GET /mcp HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n") << "\n";
    std::cout << http_request(port, "GET /tools HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n") << "\n";
    const std::string body = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    std::ostringstream post;
    post << "POST /mcp HTTP/1.1\r\n"
         << "Host: localhost\r\n"
         << "Content-Type: application/json\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n\r\n"
         << body;
    const std::string post_response = http_request(port, post.str());
    std::cout << post_response << "\n";

#ifdef _WIN32
    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    WSACleanup();
#else
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
#endif

    return contains(post_response, "200 OK") ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: smoke_client <stdio|http> <executable> [port]\n";
        return 1;
    }

    const std::string mode = argv[1];
    const std::string executable = argv[2];
    const int port = (argc >= 4) ? std::atoi(argv[3]) : 18080;

    if (mode == "stdio") {
        return smoke_stdio(executable);
    }
    if (mode == "http") {
        return smoke_http(executable, port);
    }

    std::cerr << "Unknown mode: " << mode << "\n";
    return 1;
}
