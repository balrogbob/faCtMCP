#include <array>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

std::string framed_message(const std::string& body) {
    std::ostringstream out;
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    return out.str();
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

#ifdef _WIN32
std::string run_command_with_input(const std::string& command, const std::string& input) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    HANDLE child_stdin_read = nullptr;
    HANDLE child_stdin_write = nullptr;
    HANDLE child_stdout_read = nullptr;
    HANDLE child_stdout_write = nullptr;

    if (!CreatePipe(&child_stdout_read, &child_stdout_write, &sa, 0)) return "";
    if (!SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0)) return "";
    if (!CreatePipe(&child_stdin_read, &child_stdin_write, &sa, 0)) return "";
    if (!SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0)) return "";

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdInput = child_stdin_read;
    si.hStdOutput = child_stdout_write;
    si.hStdError = child_stdout_write;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::string cmdline = command;
    std::vector<char> cmdline_buffer(cmdline.begin(), cmdline.end());
    cmdline_buffer.push_back('\0');
    if (!CreateProcessA(nullptr, cmdline_buffer.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        return "";
    }

    CloseHandle(child_stdin_read);
    CloseHandle(child_stdout_write);

    DWORD written = 0;
    WriteFile(child_stdin_write, input.data(), static_cast<DWORD>(input.size()), &written, nullptr);
    CloseHandle(child_stdin_write);

    std::string output;
    std::array<char, 4096> buffer{};
    DWORD read = 0;
    while (ReadFile(child_stdout_read, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read > 0) {
        output.append(buffer.data(), buffer.data() + read);
    }

    CloseHandle(child_stdout_read);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return output;
}
#else
std::string run_command_with_input(const std::string& command, const std::string& input) {
    int stdin_pipe[2];
    int stdout_pipe[2];
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
        return "";
    }

    pid_t pid = fork();
    if (pid < 0) {
        return "";
    }

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

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: test_stdio_client <server-exe>\n";
        return 1;
    }

    const std::string server = argv[1];
    const std::string input =
        framed_message(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})") +
        framed_message(R"({"jsonrpc":"2.0","method":"notifications/initialized","params":{}})") +
        framed_message(R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})");

#ifdef _WIN32
    const std::string output = run_command_with_input("\"" + server + "\"", input);
#else
    const std::string output = run_command_with_input(server, input);
#endif

    if (!contains(output, "Content-Length:")) {
        std::cerr << "Missing MCP stdio framing\n";
        return 2;
    }
    if (!contains(output, "protocolVersion")) {
        std::cerr << "Missing initialize response\n";
        return 3;
    }
    if (!contains(output, "tools")) {
        std::cerr << "Missing tools/list response\n";
        return 4;
    }

    return 0;
}
