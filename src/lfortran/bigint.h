#ifndef LFORTRAN_BIGINT_H
#define LFORTRAN_BIGINT_H

#include <cstdint>

#include <lfortran/containers.h>

namespace LFortran {

namespace BigInt {

/*
 * Arbitrary size integer implementation.
 *
 * We use tagged signed 64bit integers with no padding bits and using 2's
 * complement for negative values (int64_t) as the underlying data structure.
 * Little-endian is assumed.
 *
 * Bits (from the left):
 * 1 ..... sign: 0 positive, 1 negative
 * 2 ..... tag: bits 1-2 equal to 01: pointer; otherwise integer
 * 3-64 .. if the tag is - integer: rest of the signed integer bits in 2's
 *                                  complement
 *                       - pointer: 64 bit pointer shifted by 2
 *                                  to the right (>> 2)
 *
 * The pointer must be aligned to 4 bytes (bits 63-64 must be 00).
 * Small signed integers are represented directly as integers in int64_t, large
 * integers are allocated on heap and a pointer to it is used as "tag pointer"
 * in int64_t.
 *
 * To check if the integer has a pointer tag, we check that the first two bits
 * (1-2) are equal to 01:
 */

// Returns true if "i" is a pointer and false if "i" is an integer
inline static bool is_int_ptr(int64_t i) {
    return (((uint64_t)i) >> (64 - 2)) == 1;
}

/*
 * A pointer is converted to integer by shifting by 2 to the right and adding
 * 01 to the first two bits to tag it as a pointer:
 */

// Converts a pointer "p" (must be aligned to 4 bytes) to a tagged int64_t
inline static int64_t ptr_to_int(void *p) {
    return (int64_t)( (((uint64_t)p) >> 2) | (1ULL << (64 - 2)) );
}

/* An integer with the pointer tag is converted to a pointer by shifting by 2
 * to the left, which erases the tag and puts 00 to bits 63-64:
 */

// Converts a tagged int64_t to a pointer (aligned to 4 bytes)
inline static void* int_to_ptr(int64_t i) {
    return (void *)(((uint64_t)i) << 2);
}

/* The maximum small int is 2^62-1
 */
const int64_t MAX_SMALL_INT = (int64_t)((1ULL << 62)-1);

/* The minimum small int is -2^63
 */
const int64_t MIN_SMALL_INT = (int64_t)(-(1ULL << 63));

// Returns true if "i" is a small int
inline static bool is_small_int(int64_t i) {
    return (MIN_SMALL_INT <= i && i <= MAX_SMALL_INT);
}

/* Arbitrary integer implementation
 * For now large integers are implemented as strings with decimal digits. The
 * only supported operation on this is converting to and from a string. Later
 * we will replace with an actual large integer implementation and add other
 * operations.
 */

// Converts a string to a large int (allocated on heap, returns a pointer)
inline static int64_t string_to_largeint(Allocator &al, const Str &s) {
    char *cs = s.c_str(al);
    return ptr_to_int(cs);
}

// Converts a large int to a string
inline static char* largeint_to_string(int64_t i) {
    LFORTRAN_ASSERT(is_int_ptr(i));
    void *p = int_to_ptr(i);
    char *cs = (char*)p;
    return cs;
}

struct BigInt {
    int64_t n;

    bool is_large() const {
        return is_int_ptr(n);
    }

    void from_small_int(int64_t i) {
        LFORTRAN_ASSERT(is_small_int(i));
        n = i;
    }

    void from_large_int(Allocator &al, const Str &s) {
        n = string_to_largeint(al, s);
    }

    std::string str() const {
        if (is_large()) {
            return std::string(largeint_to_string(n));
        } else {
            return std::to_string(n);
        }
    }

};

static_assert(std::is_standard_layout<BigInt>::value);
static_assert(std::is_trivial<BigInt>::value);
static_assert(sizeof(BigInt) == sizeof(int64_t));
static_assert(sizeof(BigInt) == 8);


} // BigInt
} // LFortran

#endif // LFORTRAN_BIGINT_H
