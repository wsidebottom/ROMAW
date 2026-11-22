/*+===================================================================
  File:      ppc-printf.cpp

  Summary:   Macintosh PPC varargs formatting; host va_list and pointers never cross ABI.

  Classes:   Declared: formatter; Used: ROMlib_ppc_registers, formatter, std::exception, std::runtime_error,
             std::string, std::vector

  Functions: ROMlib_ppc_format

  Origin:    Existing repository source in synppc/src; original attribution and provenance comments are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
             synppc/COPYING.
===================================================================+*/

/* Macintosh PPC varargs formatting; host va_list and pointers never cross ABI. */
#include "synppc/ppc.h"
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <climits>
#include <cctype>

namespace {
const size_t maximum_output = 16 * 1024 * 1024;

struct formatter {
    ROMlib_ppc_registers& r;
    ROMlib_ppc_memory_fn memory;
    void* opaque;
    unsigned slot;

    formatter(ROMlib_ppc_registers& regs, ROMlib_ppc_memory_fn fn, void* p, unsigned first)
        : r(regs), memory(fn), opaque(p), slot(first)
    {
    }

    void* span(uint64_t address, size_t size, bool write)
    {
        if (address > UINT32_MAX || size > UINT32_MAX || address + size > UINT64_C(0x100000000))
            throw std::runtime_error("formatted I/O address overflow");
        void* p = memory(opaque, (uint32_t)address, size, write);
        if (!p)
            throw std::runtime_error("formatted I/O memory fault");
        return p;
    }

    uint32_t word()
    {
        if (slot < 8)
            return r.gpr[3 + slot++];
        const unsigned char* p = (const unsigned char*)span((uint64_t)r.gpr[1] + 24 + (uint64_t)slot++ * 4, 4, false);
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    }

    uint64_t wide()
    {
        if (slot & 1)
            ++slot;
        uint64_t high = word();
        return (high << 32) | word();
    }

    std::string string(uint32_t address, int precision = -1)
    {
        std::string out;
        for (size_t n = 0; precision < 0 || n < (unsigned)precision; ++n) {
            if (n == maximum_output)
                throw std::runtime_error("formatted I/O string too long");
            char ch = *(char*)span((uint64_t)address + n, 1, false);
            if (!ch)
                break;
            out += ch;
        }
        return out;
    }

    void store(uint32_t address, uint64_t value, unsigned bytes)
    {
        unsigned char* p = (unsigned char*)span(address, bytes, true);
        for (unsigned i = bytes; i; --i) {
            p[i - 1] = value;
            value >>= 8;
        }
    }

    template <class T> void append(std::string& out, const std::string& spec, T value)
    {
        int n = std::snprintf(NULL, 0, spec.c_str(), value);
        if (n < 0 || (size_t)n > maximum_output - out.size())
            throw std::runtime_error("formatted I/O output too long");
        std::vector<char> piece((size_t)n + 1);
        std::snprintf(piece.data(), piece.size(), spec.c_str(), value);
        out.append(piece.data(), n);
    }

    int number(const std::string& format, size_t& i)
    {
        int n = 0;
        while (i < format.size() && std::isdigit((unsigned char)format[i])) {
            unsigned digit = format[i++] - '0';
            if (n > (maximum_output - digit) / 10)
                throw std::runtime_error("formatted I/O field too large");
            n = n * 10 + digit;
        }
        return n;
    }
};
}

extern "C" int ROMlib_ppc_format(ROMlib_ppc_registers* regs, ROMlib_ppc_memory_fn memory, void* opaque, int bounded,
                                 char* error, size_t error_size)
{
    if (!regs || !memory || !error || !error_size)
        return 0;
    error[0] = 0;
    try {
        formatter f(*regs, memory, opaque, bounded ? 3 : 2);
        const uint32_t destination = regs->gpr[3], capacity = bounded ? regs->gpr[4] : UINT32_MAX;
        std::string format = f.string(regs->gpr[bounded ? 5 : 4]), out;
        for (size_t i = 0; i < format.size();) {
            if (format[i] != '%') {
                if (out.size() == maximum_output)
                    throw std::runtime_error("formatted I/O output too long");
                out += format[i++];
                continue;
            }
            ++i;
            if (i < format.size() && format[i] == '%') {
                if (out.size() == maximum_output)
                    throw std::runtime_error("formatted I/O output too long");
                out += '%';
                ++i;
                continue;
            }
            std::string spec = "%";
            while (i < format.size() && std::strchr("-+ #0", format[i])) {
                if (spec.size() > 16)
                    throw std::runtime_error("invalid printf flags");
                spec += format[i++];
            }
            int width = 0, precision = -1;
            if (i < format.size() && format[i] == '*') {
                ++i;
                int32_t value = f.word();
                if (value == INT32_MIN)
                    throw std::runtime_error("formatted I/O field too large");
                if (value < 0) {
                    spec += '-';
                    value = -value;
                }
                width = value;
            }
            else
                width = f.number(format, i);
            if ((unsigned)width > maximum_output)
                throw std::runtime_error("formatted I/O field too large");
            if (width)
                spec += std::to_string(width);
            if (i < format.size() && format[i] == '.') {
                ++i;
                if (i < format.size() && format[i] == '*') {
                    ++i;
                    precision = (int32_t)f.word();
                }
                else
                    precision = f.number(format, i);
                if (precision >= 0) {
                    if ((unsigned)precision > maximum_output)
                        throw std::runtime_error("formatted I/O field too large");
                    spec += '.';
                    spec += std::to_string(precision);
                }
            }
            std::string length;
            if (i < format.size() && std::strchr("hljztL", format[i])) {
                length += format[i++];
                if (i < format.size() && (length == "h" || length == "l") && format[i] == length[0])
                    length += format[i++];
            }
            if (i == format.size())
                throw std::runtime_error("incomplete printf conversion");
            char kind = format[i++];
            if (kind == 's' && length.empty()) {
                std::string value = f.string(f.word(), precision);
                f.append(out, spec + 's', value.c_str());
            }
            else if (kind == 'c' && length.empty())
                f.append(out, spec + 'c', (int)f.word());
            else if (kind == 'p' && length.empty())
                f.append(out, spec + 'p', (void*)(uintptr_t)f.word());
            else if (kind == 'n') {
                unsigned size = length == "hh" ? 1 : length == "h" ? 2 : (length == "ll" || length == "j") ? 8 : 4;
                f.store(f.word(), out.size(), size);
            }
            else if (std::strchr("diuoxX", kind)) {
                uint64_t value = (length == "ll" || length == "j") ? f.wide() : f.word();
                if (kind == 'd' || kind == 'i') {
                    int64_t signed_value = (length == "ll" || length == "j") ? (int64_t)value
                                           : length == "hh"                  ? (int8_t)value
                                           : length == "h"                   ? (int16_t)value
                                                                             : (int32_t)value;
                    f.append(out, spec + "ll" + kind, (long long)signed_value);
                }
                else {
                    if (length == "hh")
                        value = (uint8_t)value;
                    else if (length == "h")
                        value = (uint16_t)value;
                    f.append(out, spec + "ll" + kind, (unsigned long long)value);
                }
            }
            else if (std::strchr("aAeEfFgG", kind) && length != "L") {
                uint64_t bits = f.wide();
                double value;
                std::memcpy(&value, &bits, 8);
                f.append(out, spec + kind, value);
            }
            else
                throw std::runtime_error("unsupported printf conversion");
        }
        if (capacity) {
            size_t bytes = out.size() < capacity ? out.size() : (size_t)capacity - 1;
            char* p = (char*)f.span(destination, bytes + 1, true);
            if (bytes)
                std::memcpy(p, out.data(), bytes);
            p[bytes] = 0;
        }
        regs->gpr[3] = out.size();
        return 1;
    }
    catch (const std::exception& e) {
        std::snprintf(error, error_size, "%s", e.what());
    }
    catch (...) {
        std::snprintf(error, error_size, "formatted I/O failed");
    }
    regs->gpr[3] = UINT32_MAX;
    return 0;
}
