#include <iostream>
#include <sstream>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "vendor/linenoise.h"

class breakpoint {
public:
    breakpoint(pid_t pid, std::intptr_t address)
        : m_pid{pid}, m_address{address}, m_enabled{false}, m_data{} {}

    void enable();
    void disable();

    bool is_enabled() const {
        return m_enabled;
    }

    std::intptr_t get_address() const {
        return m_address;
    }

private:
    pid_t m_pid;
    std::intptr_t m_address;
    bool m_enabled;
    uint8_t m_data;
};

void breakpoint::enable() {
    long data = ptrace(PTRACE_PEEKDATA, m_pid, m_address, nullptr);
    m_data = static_cast<uint8_t>(data & 0xFF);
    uint64_t new_data = (data & ~0xFF) | 0xCC;
    ptrace(PTRACE_POKEDATA, m_pid, m_address, new_data);

    m_enabled = true;
}

void breakpoint::disable() {
    long data = ptrace(PTRACE_PEEKDATA, m_pid, m_address, nullptr);
    long new_data = (data & ~0xFF) | m_data;
    ptrace(PTRACE_POKEDATA, m_pid, m_address, new_data);

    m_enabled = false;
}

class debugger {
public:
    debugger(std::string prog_name, pid_t pid)
        : m_prog_name{std::move(prog_name)}, m_pid{pid} {}

    void run();
    void handle_command(const std::string &line);
    void continue_execution();
    void set_breakpoint(std::intptr_t address);

private:
    std::string m_prog_name;
    pid_t m_pid;
    std::unordered_map<std::intptr_t, breakpoint> m_breakpoints;
};

void debugger::run() {
    int wait_status;
    int options = 0;
    waitpid(m_pid, &wait_status, options);

    char *line = nullptr;
    while ((line = linenoise("dbg> ")) != nullptr) {
        handle_command(line);
        linenoiseHistoryAdd(line);
        linenoiseFree(line);
    }
}

std::vector<std::string> split(const std::string &str, char delimiter) {
    std::vector<std::string> out {};
    std::stringstream stream {str};
    std::string item;

    while (std::getline(stream, item, delimiter)) {
        out.push_back(item);
    }

    return out;
}

bool is_prefix(const std::string &str, const std::string &of) {
    if (str.size() > of.size()) {
        return false;
    }

    return std::equal(str.begin(), str.end(), of.begin());
}

void debugger::handle_command(const std::string &line) {
    std::vector<std::string> args = split(line, ' ');
    std::string command = args[0];

    if (is_prefix(command, "continue")) {
        continue_execution();
    } else if (is_prefix(command, "breakpoint")) {
        std::string address {args[1], 2};
        set_breakpoint(std::stol(address, 0, 16));
    } else {
        std::cerr << "Unknown command" << std::endl;
    }
}

void debugger::continue_execution() {
    ptrace(PTRACE_CONT, m_pid, nullptr, nullptr);

    int wait_status;
    int options = 0;
    waitpid(m_pid, &wait_status, options);
}

void debugger::set_breakpoint(std::intptr_t address) {
    breakpoint bp {m_pid, address};
    bp.enable();
    m_breakpoints.emplace(address, bp);
    std::cout << "Set breakpoint at address 0x" << std::hex << address << std::endl;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Program name not specified" << std::endl;
        return -1;
    }

    char *prog = argv[1];
    pid_t pid = fork();

    if (pid == -1) {
        std::cerr << "Program failed to start" << std::endl;
        return -1;
    }

    if (pid == 0) {
        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
        return execl(prog, prog, nullptr);
    }

    debugger dbg{prog, pid};
    dbg.run();

    return 0;
}
