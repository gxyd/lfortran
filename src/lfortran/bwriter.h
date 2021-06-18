#ifndef LFORTRAN_BWRITER_H
#define LFORTRAN_BWRITER_H

#include <lfortran/exception.h>

namespace LFortran {

std::string static inline uint64_to_string(uint64_t i) {
    char bytes[4];
    bytes[0] = (i >> 24) & 0xFF;
    bytes[1] = (i >> 16) & 0xFF;
    bytes[2] = (i >>  8) & 0xFF;
    bytes[3] =  i        & 0xFF;
    return std::string(bytes, 4);
}

uint64_t static inline string_to_uint64(const char *s) {
    // The cast from signed char to unsigned char is important,
    // otherwise the signed char shifts return wrong value for negative numbers
    const uint8_t *p = (const unsigned char*)s;
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

uint64_t static inline string_to_uint64(const std::string &s) {
    return string_to_uint64(&s[0]);
}

// BinaryReader / BinaryWriter encapsulate access to the file by providing
// primitives that other classes just use.
class BinaryWriter
{
private:
    std::string s;
public:
    std::string get_str() {
        return s;
    }

    void write_int8(uint8_t i) {
        char c=i;
        s.append(std::string(&c, 1));
    }

    void write_int64(uint64_t i) {
        s.append(uint64_to_string(i));
    }

    void write_string(const std::string &t) {
        write_int64(t.size());
        s.append(t);
    }
};

class BinaryReader
{
private:
    std::string s;
    size_t pos;
public:
    BinaryReader(const std::string &s) : s{s}, pos{0} {}

    uint8_t read_int8() {
        if (pos+1 > s.size()) {
            throw LFortranException("read_int8: String is too short for deserialization.");
        }
        uint8_t n = s[pos];
        pos += 1;
        return n;
    }

    uint64_t read_int64() {
        if (pos+4 > s.size()) {
            throw LFortranException("read_int64: String is too short for deserialization.");
        }
        uint64_t n = string_to_uint64(&s[pos]);
        pos += 4;
        return n;
    }

    std::string read_string() {
        size_t n = read_int64();
        if (pos+n > s.size()) {
            throw LFortranException("read_string: String is too short for deserialization.");
        }
        std::string r = std::string(&s[pos], n);
        pos += n;
        return r;
    }
};

// TextReader / TextWriter encapsulate access to the file by providing
// primitives that other classes just use. The file is a human readable
// text file. These classes are useful for debugging.
class TextWriter
{
private:
    std::string s;
public:
    std::string get_str() {
        return s;
    }

    void write_int8(uint8_t i) {
        s.append(std::to_string(i));
        s += " ";
    }

    void write_int64(uint64_t i) {
        s.append(std::to_string(i));
        s += " ";
    }

    void write_string(const std::string &t) {
        write_int64(t.size());
        s.append(t);
        s += " ";
    }
};

class TextReader
{
private:
    std::string s;
    size_t pos;
public:
    TextReader(const std::string &s) : s{s}, pos{0} {}

    uint8_t read_int8() {
        uint64_t n = read_int64();
        if (n < 255) {
            return n;
        } else {
            throw LFortranException("read_int8: Integer too large to fit 8 bits.");
        }
    }

    uint64_t read_int64() {
        std::string tmp;
        while (s[pos] != ' ') {
            tmp += s[pos];
            pos++;
            if (pos >= s.size()) {
                throw LFortranException("read_int64: String is too short for deserialization.");
            }
        }
        pos++;
        uint64_t n = std::stoi(tmp);
        return n;
    }

    std::string read_string() {
        size_t n = read_int64();
        if (pos+n > s.size()) {
            throw LFortranException("read_string: String is too short for deserialization.");
        }
        std::string r = std::string(&s[pos], n);
        pos += n;
        if (s[pos] != ' ') {
            throw LFortranException("read_string: Space expected.");
        }
        pos ++;
        return r;
    }
};

} // namespace LFortran

#endif // LFORTRAN_BWRITER_H
