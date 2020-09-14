#include <algorithm>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "vendor/libelfin/dwarf/dwarf++.hh"
#include "vendor/libelfin/elf/elf++.hh"
#include "vendor/linenoise/linenoise.h"

enum class reg {
    rax, rbx, rcx, rdx,
    rdi, rsi, rbp, rsp,
    r8, r9, r10, r11,
    r12, r13, r14, r15,
    rip, rflags, cs,
    orig_rax, fs_base, gs_base,
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

enum class symbol_type {
    notype,
    object,
    func,
    section,
    file,
};

symbol_type to_symbol_type(elf::stt sym) {
    switch (sym) {
        case elf::stt::notype:
            return symbol_type::notype;

        case elf::stt::object:
            return symbol_type::object;

        case elf::stt::func:
            return symbol_type::func;

        case elf::stt::section:
            return symbol_type::section;

        case elf::stt::file:
            return symbol_type::file;

        default:
            return symbol_type::notype;
    }
}

std::string to_string(symbol_type st) {
    switch (st) {
        case symbol_type::notype:
            return "notype";

        case symbol_type::object:
            return "object";

        case symbol_type::func:
            return "func";

        case symbol_type::section:
            return "section";

        case symbol_type::file:
            return "file";

        default:
            return "notype";
    }
}

struct symbol {
    symbol_type type;
    std::string name;
    std::uintptr_t address;
};

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
        : m_prog_name{std::move(prog_name)}, m_pid{pid} {
        int fd = open(m_prog_name.c_str(), O_RDONLY);

        m_elf = elf::elf {elf::create_mmap_loader(fd)};
        m_dwarf = dwarf::dwarf {dwarf::elf::create_loader(m_elf)};
    }

    void run();

private:
    std::string m_prog_name;
    pid_t m_pid;
    std::unordered_map<std::intptr_t, breakpoint> m_breakpoints;
    elf::elf m_elf;
    dwarf::dwarf m_dwarf;

    void handle_command(const std::string &line);
    void continue_execution();
    void set_breakpoint_at_address(std::intptr_t address);
    void set_breakpoint_at_function(const std::string &name);
    void set_breakpoint_at_line(const std::string &file, unsigned line);
    void remove_breakpoint(std::intptr_t address);
    void dump_registers();
    uint64_t read_memory(uint64_t address);
    void write_memory(uint64_t address, uint64_t value);
    uint64_t get_pc();
    void set_pc(uint64_t pc);
    void step_single_instruction();
    void step_single_instruction_with_breakpoint_check();
    void step_over_breakpoint();
    void step_in();
    void step_over();
    void step_out();
    void wait_for_signal();
    dwarf::die get_function_from_pc(uint64_t pc);
    dwarf::line_table::iterator get_line_entry_from_pc(uint64_t pc);
    void print_source(const std::string &filename, unsigned line, unsigned n_lines_context = 2);
    siginfo_t get_signal_info();
    void handle_sigtrap(siginfo_t info);
    std::vector<symbol> lookup_symbol(const std::string &name);
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

bool is_suffix(const std::string &str, const std::string &of) {
    if (str.size() > of.size()) {
        return false;
    }

    unsigned long diff = of.size() - str.size();
    return std::equal(str.begin(), str.end(), of.begin() + diff);
}

void debugger::handle_command(const std::string &line) {
    std::vector<std::string> args = split(line, ' ');
    std::string command = args[0];

    if (is_prefix(command, "continue")) {
        continue_execution();
    } else if (is_prefix(command, "breakpoint")) {
        if (args[1][0] == '0' && args[1][1] == 'x') {
            std::string str {args[1], 2};
            uint64_t address = std::stol(str, nullptr, 16);
            set_breakpoint_at_address(address);
        } else if (args[1].find(':') != std::string::npos) {
            std::vector<std::string> file_and_line = split(args[1], ':');
            set_breakpoint_at_line(file_and_line[0], stoi(file_and_line[1]));
        } else {
            set_breakpoint_at_function(args[1]);
        }
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
    } else if (is_prefix(command, "step")) {
        step_in();
    } else if (is_prefix(command, "stepi")) {
        step_single_instruction_with_breakpoint_check();
        dwarf::line_table::iterator line_entry = get_line_entry_from_pc(get_pc());
        print_source(line_entry->file->path, line_entry->line);
    } else if (is_prefix(command, "next")) {
        step_over();
    } else if (is_prefix(command, "finish")) {
        step_out();
    } else if (is_prefix(command, "symbol")) {
        std::vector<symbol> symbols = lookup_symbol(args[1]);

        for (symbol &sym : symbols) {
            std::cout << sym.name << ' ' << to_string(sym.type)
                      << " 0x" << std::hex << sym.address << std::endl;
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

void debugger::set_breakpoint_at_address(std::intptr_t address) {
    breakpoint bp {m_pid, address};
    bp.enable();
    m_breakpoints.emplace(address, bp);
    std::cout << "Set breakpoint at address 0x" << std::hex << address << std::endl;
}

void debugger::set_breakpoint_at_function(const std::string &name) {
    for (const dwarf::compilation_unit &cu : m_dwarf.compilation_units()) {
        for (const dwarf::die &die : cu.root()) {
            if (die.has(dwarf::DW_AT::name) && at_name(die) == name) {
                dwarf::taddr low_pc = at_low_pc(die);
                dwarf::line_table::iterator entry = get_line_entry_from_pc(low_pc);
                entry++; // Skip prologue.
                set_breakpoint_at_address(entry->address);
            }
        }
    }
}

void debugger::set_breakpoint_at_line(const std::string &file, unsigned line) {
    for (const dwarf::compilation_unit &cu : m_dwarf.compilation_units()) {
        if (is_suffix(file, at_name(cu.root()))) {
            const dwarf::line_table &lt = cu.get_line_table();

            for (const dwarf::line_table::entry &entry : lt) {
                if (entry.is_stmt && entry.line == line) {
                    set_breakpoint_at_address(entry.address);
                    return;
                }
            }
        }
    }
}

void debugger::remove_breakpoint(std::intptr_t address) {
    if (m_breakpoints.at(address).is_enabled()) {
        m_breakpoints.at(address).disable();
    }

    m_breakpoints.erase(address);
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

void debugger::step_single_instruction() {
    ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr);
    wait_for_signal();
}

void debugger::step_single_instruction_with_breakpoint_check() {
    if (m_breakpoints.count(get_pc())) {
        step_over_breakpoint();
    } else {
        step_single_instruction();
    }
}

void debugger::step_over_breakpoint() {
    uint64_t pc = get_pc();

    if (m_breakpoints.count(pc)) {
        breakpoint &bp = m_breakpoints.at(pc);

        if (bp.is_enabled()) {
            bp.disable();
            step_single_instruction();
            bp.enable();
        }
    }
}

void debugger::step_in() {
    unsigned line = get_line_entry_from_pc(get_pc())->line;

    while (get_line_entry_from_pc(get_pc())->line == line) {
        step_single_instruction_with_breakpoint_check();
    }

    dwarf::line_table::iterator line_entry = get_line_entry_from_pc(get_pc());
    print_source(line_entry->file->path, line_entry->line);
}

void debugger::step_over() {
    dwarf::die func = get_function_from_pc(get_pc());
    dwarf::taddr func_entry = at_low_pc(func);
    dwarf::taddr func_end = at_high_pc(func);

    dwarf::line_table::iterator line = get_line_entry_from_pc(func_entry);
    dwarf::line_table::iterator start_line = get_line_entry_from_pc(get_pc());

    std::vector<std::intptr_t> to_delete {};

    while (line->address < func_end) {
        auto address = line->address;

        if (address != start_line->address && !m_breakpoints.count(address)) {
            set_breakpoint_at_address(address);
            to_delete.push_back(address);
        }

        line++;
    }

    uint64_t frame_pointer = get_register_value(m_pid, reg::rbp);
    uint64_t return_address = read_memory(frame_pointer + sizeof(uint64_t));

    if (!m_breakpoints.count(return_address)) {
        set_breakpoint_at_address(return_address);
        to_delete.push_back(return_address);
    }

    continue_execution();

    for (auto address : to_delete) {
        remove_breakpoint(address);
    }
}

void debugger::step_out() {
    uint64_t frame_pointer = get_register_value(m_pid, reg::rbp);
    uint64_t return_address = read_memory(frame_pointer + sizeof(uint64_t));

    bool should_remove_breakpoint = false;
    if (!m_breakpoints.count(return_address)) {
        set_breakpoint_at_address(return_address);
        should_remove_breakpoint = true;
    }

    continue_execution();

    if (should_remove_breakpoint) {
        remove_breakpoint(return_address);
    }
}

void debugger::wait_for_signal() {
    int wait_status;
    int options = 0;
    waitpid(m_pid, &wait_status, options);

    siginfo_t info = get_signal_info();

    switch (info.si_signo) {
        case SIGTRAP:
            handle_sigtrap(info);
            break;

        case SIGSEGV:
            std::cout << "Segmentation fault. Reason: " << info.si_code << std::endl;
            break;

        default:
            std::cout << "Got signal: " << strsignal(info.si_signo) << std::endl;
            break;
    }
}

dwarf::die debugger::get_function_from_pc(uint64_t pc) {
    for (const dwarf::compilation_unit &cu : m_dwarf.compilation_units()) {
        if (!die_pc_range(cu.root()).contains(pc)) {
            continue;
        }

        for (const dwarf::die &die : cu.root()) {
            if (die.tag != dwarf::DW_TAG::subprogram) {
                continue;
            }

            if (!die_pc_range(die).contains(pc)) {
                continue;
            }

            return die;
        }
    }

    throw std::out_of_range("Unknown function");
}

dwarf::line_table::iterator debugger::get_line_entry_from_pc(uint64_t pc) {
    for (const dwarf::compilation_unit &cu : m_dwarf.compilation_units()) {
        if (!die_pc_range(cu.root()).contains(pc)) {
            continue;
        }

        const dwarf::line_table &lt = cu.get_line_table();
        dwarf::line_table::iterator it = lt.find_address(pc);

        if (it == lt.end()) {
            throw std::out_of_range("Unknown line entry");
        }

        return it;
    }

    throw std::out_of_range("Unknown line entry");
}

void debugger::print_source(const std::string &filename, unsigned line, unsigned n_lines_context) {
    std::ifstream file {filename};

    unsigned start_line = line <= n_lines_context ? 1 : line - n_lines_context;
    unsigned end_line = line + n_lines_context + 1 +
                        (line < n_lines_context ? n_lines_context - line : 0);

    char c {};
    unsigned current_line = 1;

    while (current_line != start_line && file.get(c)) {
        if (c == '\n') {
            current_line++;
        }
    }

    std::cout << (current_line == line ? "> " : "  ");

    while (current_line != end_line && file.get(c)) {
        std::cout << c;

        if (c == '\n') {
            current_line++;
            std::cout << (current_line == line ? "> " : "  ");
        }
    }

    std::cout << std::endl;
}

siginfo_t debugger::get_signal_info() {
    siginfo_t info;
    ptrace(PTRACE_GETSIGINFO, m_pid, nullptr, &info);
    return info;
}

void debugger::handle_sigtrap(siginfo_t info) {
    switch (info.si_code) {
        case SI_KERNEL:
        case TRAP_BRKPT: {
            uint64_t pc = get_pc() - 1;
            set_pc(pc);
            std::cout << "Hit breakpoint at address 0x" << std::hex << pc << std::endl;
            dwarf::line_table::iterator line_entry = get_line_entry_from_pc(pc);
            print_source(line_entry->file->path, line_entry->line);
            return;
        }

        case TRAP_TRACE:
            return;

        default:
            std::cout << "Unknown SIGTRAP code " << info.si_code << std::endl;
            return;
    }
}

std::vector<symbol> debugger::lookup_symbol(const std::string &name) {
    std::vector<symbol> symbols;

    for (const elf::section &section : m_elf.sections()) {
        elf::sht type = section.get_hdr().type;

        if (type != elf::sht::symtab && type != elf::sht::dynsym) {
            continue;
        }

        for (elf::sym sym : section.as_symtab()) {
            if (sym.get_name() == name) {
                const elf::Sym<> &data = sym.get_data();
                symbol_type st = to_symbol_type(data.type());
                symbols.push_back(symbol {st, sym.get_name(), data.value});
            }
        }
    }

    return symbols;
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

    std::cout << "Started " << prog << " with PID " << pid << std::endl;
    debugger dbg{prog, pid};
    dbg.run();

    return 0;
}
