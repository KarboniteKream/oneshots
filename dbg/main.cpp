#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "vendor/linenoise.h"

enum class reg {
    rax, rbx, rcx, rdx,
    rdi, rsi, rbp, rsp,
    r8, r9, r10, r11,
    r12, r13, r14, r15,
    rip, rflags, cs,
    orig_rax,
    fs_base, gs_base,
    fs, gs, ss, ds, es,
};

const std::size_t n_registers = 27;

struct reg_descriptor {
    reg r;
    int dwarf_r;
    std::string name;
};

const std::array<reg_descriptor, n_registers> g_register_descriptors = {{
    { reg::r15, 15, "r15" },
    { reg::r14, 14, "r14" },
    { reg::r13, 13, "r13" },
    { reg::r12, 12, "r12" },
    { reg::rbp, 6, "rbp" },
    { reg::rbx, 3, "rbx" },
    { reg::r11, 11, "r11" },
    { reg::r10, 10, "r10" },
    { reg::r9, 9, "r9" },
    { reg::r8, 8, "r8" },
    { reg::rax, 0, "rax" },
    { reg::rcx, 2, "rcx" },
    { reg::rdx, 1, "rdx" },
    { reg::rsi, 4, "rsi" },
    { reg::rdi, 5, "rdi" },
    { reg::orig_rax, -1, "orig_rax" },
    { reg::rip, -1, "rip" },
    { reg::cs, 51, "cs" },
    { reg::rflags, 49, "eflags" },
    { reg::rsp, 7, "rsp" },
    { reg::ss, 52, "ss" },
    { reg::fs_base, 58, "fs_base" },
    { reg::gs_base, 59, "gs_base" },
    { reg::ds, 53, "ds" },
    { reg::es, 50, "es" },
    { reg::fs, 54, "fs" },
    { reg::gs, 55, "gs" },
}};

uint64_t get_register_value(pid_t pid, reg r) {
    user_regs_struct regs;
    ptrace(PTRACE_GETREGS, pid, nullptr, &regs);

    const reg_descriptor *it =
        std::find_if(begin(g_register_descriptors), end(g_register_descriptors),
                     [r](auto &&rd) { return rd.r == r; });

    return *(reinterpret_cast<uint64_t *>(&regs) + (it - begin(g_register_descriptors)));
}

uint64_t get_register_value_from_dwarf_register(pid_t pid, int dwarf_r) {
    const reg_descriptor *it =
        std::find_if(begin(g_register_descriptors), end(g_register_descriptors),
                     [dwarf_r](auto &&rd) { return rd.dwarf_r == dwarf_r; });

    if (it == end(g_register_descriptors)) {
        throw std::out_of_range("Unknown DWARF register");
    }

    return get_register_value(pid, it->r);
}

std::string get_register_name(reg r) {
    const reg_descriptor *it =
        std::find_if(begin(g_register_descriptors), end(g_register_descriptors),
                     [r](auto &&rd) { return rd.r == r; });

    return it->name;
}

reg get_register_from_name(const std::string name) {
    const reg_descriptor *it =
        std::find_if(begin(g_register_descriptors), end(g_register_descriptors),
                     [name](auto &&rd) { return rd.name == name; });

    return it->r;
}

void set_register_value(pid_t pid, reg r, uint64_t value) {
    user_regs_struct regs;
    ptrace(PTRACE_GETREGS, pid, nullptr, &regs);

    const reg_descriptor *it =
        std::find_if(begin(g_register_descriptors), end(g_register_descriptors),
                     [r](auto &&rd) { return rd.r == r; });

    *(reinterpret_cast<uint64_t *>(&regs) + (it - begin(g_register_descriptors))) = value;
    ptrace(PTRACE_SETREGS, pid, nullptr, &regs);
}

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

private:
    std::string m_prog_name;
    pid_t m_pid;
    std::unordered_map<std::intptr_t, breakpoint> m_breakpoints;

    void handle_command(const std::string &line);
    void continue_execution();
    void set_breakpoint(std::intptr_t address);
    void dump_registers();
    uint64_t read_memory(uint64_t address);
    void write_memory(uint64_t address, uint64_t value);
    uint64_t get_pc();
    void set_pc(uint64_t pc);
    void step_over_breakpoint();
    void wait_for_signal();
};

void debugger::run() {
    wait_for_signal();

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
        set_breakpoint(std::stol(address, nullptr, 16));
    } else if (is_prefix(command, "register")) {
        if (is_prefix(args[1], "dump")) {
            dump_registers();
        } else if (is_prefix(args[1], "read")) {
            reg reg = get_register_from_name(args[2]);
            std::cout << get_register_value(m_pid, reg) << std::endl;
        } else if (is_prefix(args[1], "write")) {
            reg reg = get_register_from_name(args[2]);
            std::string str {args[3], 2};
            uint64_t value = std::stol(str, nullptr, 16);
            set_register_value(m_pid, reg, value);
        }
    } else if (is_prefix(command, "memory")) {
        std::string str {args[2], 2};
        uint64_t address = std::stol(str, nullptr, 16);

        if (is_prefix(args[1], "read")) {
            std::cout << std::hex << read_memory(address) << std::endl;
        } else if (is_prefix(args[1], "write")) {
            std::string str {args[3], 2};
            uint64_t value = std::stol(str, nullptr, 16);
            write_memory(address, value);
        }
    } else {
        std::cerr << "Unknown command" << std::endl;
    }
}

void debugger::continue_execution() {
    step_over_breakpoint();
    ptrace(PTRACE_CONT, m_pid, nullptr, nullptr);
    wait_for_signal();
}

void debugger::set_breakpoint(std::intptr_t address) {
    breakpoint bp {m_pid, address};
    bp.enable();
    m_breakpoints.emplace(address, bp);
    std::cout << "Set breakpoint at address 0x" << std::hex << address << std::endl;
}

void debugger::dump_registers() {
    for (const reg_descriptor &rd : g_register_descriptors) {
        std::cout << std::left << std::setfill(' ') << std::setw(8) << rd.name
                  << " 0x" << std::right << std::setfill('0') << std::setw(16)
                  << std::hex << get_register_value(m_pid, rd.r) << std::endl;
    }
}

uint64_t debugger::read_memory(uint64_t address) {
    return ptrace(PTRACE_PEEKDATA, m_pid, address, nullptr);
}

void debugger::write_memory(uint64_t address, uint64_t value) {
    ptrace(PTRACE_POKEDATA, m_pid, address, value);
}

uint64_t debugger::get_pc() {
    return get_register_value(m_pid, reg::rip);
}

void debugger::set_pc(uint64_t pc) {
    set_register_value(m_pid, reg::rip, pc);
}

void debugger::step_over_breakpoint() {
    uint64_t location = get_pc() - 1;

    if (m_breakpoints.count(location)) {
        breakpoint &bp = m_breakpoints.at(location);

        if (bp.is_enabled()) {
            set_pc(location);
            bp.disable();
            ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr);
            wait_for_signal();
            bp.enable();
        }
    }
}

void debugger::wait_for_signal() {
    int wait_status;
    int options = 0;
    waitpid(m_pid, &wait_status, options);
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
