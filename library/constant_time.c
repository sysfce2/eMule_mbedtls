/**
 *  Constant-time functions
 *
 *  Copyright The Mbed TLS Contributors
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * The following functions are implemented without using comparison operators, as those
 * might be translated to branches by some compilers on some platforms.
 */

#include "common.h"
#include "constant_time_internal.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/error.h"
#include "mbedtls/platform_util.h"

#if defined(MBEDTLS_BIGNUM_C)
#include "mbedtls/bignum.h"
#include "bignum_core.h"
#endif

#if defined(MBEDTLS_SSL_TLS_C)
#include "ssl_misc.h"
#endif

#if defined(MBEDTLS_RSA_C)
#include "mbedtls/rsa.h"
#endif

#if defined(MBEDTLS_BASE64_C)
#include "constant_time_invasive.h"
#endif

#include <string.h>
#if defined(MBEDTLS_USE_PSA_CRYPTO)
#define PSA_TO_MBEDTLS_ERR(status) PSA_TO_MBEDTLS_ERR_LIST(status,    \
                                                           psa_to_ssl_errors,              \
                                                           psa_generic_status_to_mbedtls)
#endif

/*
 * Define MBEDTLS_EFFICIENT_UNALIGNED_VOLATILE_ACCESS where assembly is present to
 * perform fast unaligned access to volatile data.
 *
 * This is needed because mbedtls_get_unaligned_uintXX etc don't support volatile
 * memory accesses.
 *
 * Some of these definitions could be moved into alignment.h but for now they are
 * only used here.
 */
#if defined(MBEDTLS_EFFICIENT_UNALIGNED_ACCESS) && defined(MBEDTLS_HAVE_ASM)
#if defined(__arm__) || defined(__thumb__) || defined(__thumb2__) || defined(__aarch64__)
#define MBEDTLS_EFFICIENT_UNALIGNED_VOLATILE_ACCESS
#endif
#endif

#if defined(MBEDTLS_EFFICIENT_UNALIGNED_VOLATILE_ACCESS)
static inline uint32_t mbedtls_get_unaligned_volatile_uint32(volatile const unsigned char *p)
{
    /* This is UB, even where it's safe:
     *    return *((volatile uint32_t*)p);
     * so instead the same thing is expressed in assembly below.
     */
    uint32_t r;
#if defined(__arm__) || defined(__thumb__) || defined(__thumb2__)
    asm volatile ("ldr %0, [%1]" : "=r" (r) : "r" (p) :);
#elif defined(__aarch64__)
    asm volatile ("ldr %w0, [%1]" : "=r" (r) : "r" (p) :);
#endif
    return r;
}
#endif /* MBEDTLS_EFFICIENT_UNALIGNED_VOLATILE_ACCESS */

int mbedtls_ct_memcmp(const void *a,
                      const void *b,
                      size_t n)
{
    size_t i = 0;
    /*
     * `A` and `B` are cast to volatile to ensure that the compiler
     * generates code that always fully reads both buffers.
     * Otherwise it could generate a test to exit early if `diff` has all
     * bits set early in the loop.
     */
    volatile const unsigned char *A = (volatile const unsigned char *) a;
    volatile const unsigned char *B = (volatile const unsigned char *) b;
    uint32_t diff = 0;

#if defined(MBEDTLS_EFFICIENT_UNALIGNED_VOLATILE_ACCESS)
    for (; (i + 4) <= n; i += 4) {
        uint32_t x = mbedtls_get_unaligned_volatile_uint32(A + i);
        uint32_t y = mbedtls_get_unaligned_volatile_uint32(B + i);
        diff |= x ^ y;
    }
#endif

    for (; i < n; i++) {
        /* Read volatile data in order before computing diff.
         * This avoids IAR compiler warning:
         * 'the order of volatile accesses is undefined ..' */
        unsigned char x = A[i], y = B[i];
        diff |= x ^ y;
    }

    return (int) diff;
}

unsigned mbedtls_ct_uint_mask(unsigned value)
{
    /* MSVC has a warning about unary minus on unsigned, but this is
     * well-defined and precisely what we want to do here */
#if defined(_MSC_VER)
#pragma warning( push )
#pragma warning( disable : 4146 )
#endif
    return -((value | -value) >> (sizeof(value) * 8 - 1));
#if defined(_MSC_VER)
#pragma warning( pop )
#endif
}

#if defined(MBEDTLS_SSL_SOME_SUITES_USE_MAC)

size_t mbedtls_ct_size_mask(size_t value)
{
    /* MSVC has a warning about unary minus on unsigned integer types,
     * but this is well-defined and precisely what we want to do here. */
#if defined(_MSC_VER)
#pragma warning( push )
#pragma warning( disable : 4146 )
#endif
    return -((value | -value) >> (sizeof(value) * 8 - 1));
#if defined(_MSC_VER)
#pragma warning( pop )
#endif
}

#endif /* MBEDTLS_SSL_SOME_SUITES_USE_MAC */

#if defined(MBEDTLS_BIGNUM_C)

mbedtls_mpi_uint mbedtls_ct_mpi_uint_mask(mbedtls_mpi_uint value)
{
    /* MSVC has a warning about unary minus on unsigned, but this is
     * well-defined and precisely what we want to do here */
#if defined(_MSC_VER)
#pragma warning( push )
#pragma warning( disable : 4146 )
#endif
    return -((value | -value) >> (sizeof(value) * 8 - 1));
#if defined(_MSC_VER)
#pragma warning( pop )
#endif
}

#endif /* MBEDTLS_BIGNUM_C */

#if defined(MBEDTLS_SSL_SOME_SUITES_USE_TLS_CBC)

/** Constant-flow mask generation for "less than" comparison:
 * - if \p x < \p y, return all-bits 1, that is (size_t) -1
 * - otherwise, return all bits 0, that is 0
 *
 * This function can be used to write constant-time code by replacing branches
 * with bit operations using masks.
 *
 * \param x     The first value to analyze.
 * \param y     The second value to analyze.
 *
 * \return      All-bits-one if \p x is less than \p y, otherwise zero.
 */
static size_t mbedtls_ct_size_mask_lt(size_t x,
                                      size_t y)
{
    /* This has the most significant bit set if and only if x < y */
    const size_t sub = x - y;

    /* sub1 = (x < y) ? 1 : 0 */
    const size_t sub1 = sub >> (sizeof(sub) * 8 - 1);

    /* mask = (x < y) ? 0xff... : 0x00... */
    const size_t mask = mbedtls_ct_size_mask(sub1);

    return mask;
}

size_t mbedtls_ct_size_mask_ge(size_t x,
                               size_t y)
{
    return ~mbedtls_ct_size_mask_lt(x, y);
}

#endif /* MBEDTLS_SSL_SOME_SUITES_USE_TLS_CBC */

#if defined(MBEDTLS_BASE64_C)

/* Return 0xff if low <= c <= high, 0 otherwise.
 *
 * Constant flow with respect to c.
 */
unsigned char mbedtls_ct_uchar_mask_of_range(unsigned char low,
                                             unsigned char high,
                                             unsigned char c)
{
    /* low_mask is: 0 if low <= c, 0x...ff if low > c */
    unsigned low_mask = ((unsigned) c - low) >> 8;
    /* high_mask is: 0 if c <= high, 0x...ff if c > high */
    unsigned high_mask = ((unsigned) high - c) >> 8;
    return ~(low_mask | high_mask) & 0xff;
}

#endif /* MBEDTLS_BASE64_C */

unsigned mbedtls_ct_size_bool_eq(size_t x,
                                 size_t y)
{
    /* diff = 0 if x == y, non-zero otherwise */
    const size_t diff = x ^ y;

    /* MSVC has a warning about unary minus on unsigned integer types,
     * but this is well-defined and precisely what we want to do here. */
#if defined(_MSC_VER)
#pragma warning( push )
#pragma warning( disable : 4146 )
#endif

    /* diff_msb's most significant bit is equal to x != y */
    const size_t diff_msb = (diff | (size_t) -diff);

#if defined(_MSC_VER)
#pragma warning( pop )
#endif

    /* diff1 = (x != y) ? 1 : 0 */
    const unsigned diff1 = diff_msb >> (sizeof(diff_msb) * 8 - 1);

    return 1 ^ diff1;
}

#if defined(MBEDTLS_PKCS1_V15) && defined(MBEDTLS_RSA_C) && !defined(MBEDTLS_RSA_ALT)

/** Constant-flow "greater than" comparison:
 * return x > y
 *
 * This is equivalent to \p x > \p y, but is likely to be compiled
 * to code using bitwise operation rather than a branch.
 *
 * \param x     The first value to analyze.
 * \param y     The second value to analyze.
 *
 * \return      1 if \p x greater than \p y, otherwise 0.
 */
static unsigned mbedtls_ct_size_gt(size_t x,
                                   size_t y)
{
    /* Return the sign bit (1 for negative) of (y - x). */
    return (y - x) >> (sizeof(size_t) * 8 - 1);
}

#endif /* MBEDTLS_PKCS1_V15 && MBEDTLS_RSA_C && ! MBEDTLS_RSA_ALT */

#if defined(MBEDTLS_BIGNUM_C)

unsigned mbedtls_ct_mpi_uint_lt(const mbedtls_mpi_uint x,
                                const mbedtls_mpi_uint y)
{
    mbedtls_mpi_uint ret;
    mbedtls_mpi_uint cond;

    /*
     * Check if the most significant bits (MSB) of the operands are different.
     */
    cond = (x ^ y);
    /*
     * If the MSB are the same then the difference x-y will be negative (and
     * have its MSB set to 1 during conversion to unsigned) if and only if x<y.
     */
    ret = (x - y) & ~cond;
    /*
     * If the MSB are different, then the operand with the MSB of 1 is the
     * bigger. (That is if y has MSB of 1, then x<y is true and it is false if
     * the MSB of y is 0.)
     */
    ret |= y & cond;


    ret = ret >> (sizeof(mbedtls_mpi_uint) * 8 - 1);

    return (unsigned) ret;
}

#endif /* MBEDTLS_BIGNUM_C */

unsigned mbedtls_ct_uint_if(unsigned condition,
                            unsigned if1,
                            unsigned if0)
{
    unsigned mask = mbedtls_ct_uint_mask(condition);
    return (mask & if1) | (~mask & if0);
}

#if defined(MBEDTLS_BIGNUM_C)

void mbedtls_ct_mpi_uint_cond_assign(size_t n,
                                     mbedtls_mpi_uint *dest,
                                     const mbedtls_mpi_uint *src,
                                     unsigned char condition)
{
    size_t i;

    /* MSVC has a warning about unary minus on unsigned integer types,
     * but this is well-defined and precisely what we want to do here. */
#if defined(_MSC_VER)
#pragma warning( push )
#pragma warning( disable : 4146 )
#endif

    /* all-bits 1 if condition is 1, all-bits 0 if condition is 0 */
    const mbedtls_mpi_uint mask = -condition;

#if defined(_MSC_VER)
#pragma warning( pop )
#endif

    for (i = 0; i < n; i++) {
        dest[i] = (src[i] & mask) | (dest[i] & ~mask);
    }
}

#endif /* MBEDTLS_BIGNUM_C */

#if defined(MBEDTLS_PKCS1_V15) && defined(MBEDTLS_RSA_C) && !defined(MBEDTLS_RSA_ALT)

/** Shift some data towards the left inside a buffer.
 *
 * `mbedtls_ct_mem_move_to_left(start, total, offset)` is functionally
 * equivalent to
 * ```
 * memmove(start, start + offset, total - offset);
 * memset(start + offset, 0, total - offset);
 * ```
 * but it strives to use a memory access pattern (and thus total timing)
 * that does not depend on \p offset. This timing independence comes at
 * the expense of performance.
 *
 * \param start     Pointer to the start of the buffer.
 * \param total     Total size of the buffer.
 * \param offset    Offset from which to copy \p total - \p offset bytes.
 */
static void mbedtls_ct_mem_move_to_left(void *start,
                                        size_t total,
                                        size_t offset)
{
    volatile unsigned char *buf = start;
    size_t i, n;
    if (total == 0) {
        return;
    }
    for (i = 0; i < total; i++) {
        unsigned no_op = mbedtls_ct_size_gt(total - offset, i);
        /* The first `total - offset` passes are a no-op. The last
         * `offset` passes shift the data one byte to the left and
         * zero out the last byte. */
        for (n = 0; n < total - 1; n++) {
            unsigned char current = buf[n];
            unsigned char next = buf[n+1];
            buf[n] = mbedtls_ct_uint_if(no_op, current, next);
        }
        buf[total-1] = mbedtls_ct_uint_if(no_op, buf[total-1], 0);
    }
}

#endif /* MBEDTLS_PKCS1_V15 && MBEDTLS_RSA_C && ! MBEDTLS_RSA_ALT */

#if defined(MBEDTLS_SSL_SOME_SUITES_USE_MAC)

void mbedtls_ct_memcpy_if_eq(unsigned char *dest,
                             const unsigned char *src,
                             size_t len,
                             size_t c1,
                             size_t c2)
{
    /* mask = c1 == c2 ? 0xff : 0x00 */
    const size_t equal = mbedtls_ct_size_bool_eq(c1, c2);

    /* dest[i] = c1 == c2 ? src[i] : dest[i] */
    size_t i = 0;
#if defined(MBEDTLS_EFFICIENT_UNALIGNED_ACCESS)
    const uint32_t mask32 = (uint32_t) mbedtls_ct_size_mask(equal);
    const unsigned char mask = (unsigned char) mask32 & 0xff;

    for (; (i + 4) <= len; i += 4) {
        uint32_t a = mbedtls_get_unaligned_uint32(src  + i) &  mask32;
        uint32_t b = mbedtls_get_unaligned_uint32(dest + i) & ~mask32;
        mbedtls_put_unaligned_uint32(dest + i, a | b);
    }
#else
    const unsigned char mask = (unsigned char) mbedtls_ct_size_mask(equal);
#endif /* MBEDTLS_EFFICIENT_UNALIGNED_ACCESS */
    for (; i < len; i++) {
        dest[i] = (src[i] & mask) | (dest[i] & ~mask);
    }
}

void mbedtls_ct_memcpy_offset(unsigned char *dest,
                              const unsigned char *src,
                              size_t offset,
                              size_t offset_min,
                              size_t offset_max,
                              size_t len)
{
    size_t offsetval;

    for (offsetval = offset_min; offsetval <= offset_max; offsetval++) {
        mbedtls_ct_memcpy_if_eq(dest, src + offsetval, len,
                                offsetval, offset);
    }
}

#endif /* MBEDTLS_SSL_SOME_SUITES_USE_MAC */

#if defined(MBEDTLS_BIGNUM_C)

#define MPI_VALIDATE_RET(cond)                                       \
    MBEDTLS_INTERNAL_VALIDATE_RET(cond, MBEDTLS_ERR_MPI_BAD_INPUT_DATA)

/*
 * Conditionally assign X = Y, without leaking information
 * about whether the assignment was made or not.
 * (Leaking information about the respective sizes of X and Y is ok however.)
 */
#if defined(_MSC_VER) && defined(_M_ARM64) && (_MSC_FULL_VER < 193131103)
/*
 * MSVC miscompiles this function if it's inlined prior to Visual Studio 2022 version 17.1. See:
 * https://developercommunity.visualstudio.com/t/c-compiler-miscompiles-part-of-mbedtls-library-on/1646989
 */
__declspec(noinline)
#endif
int mbedtls_mpi_safe_cond_assign(mbedtls_mpi *X,
                                 const mbedtls_mpi *Y,
                                 unsigned char assign)
{
    int ret = 0;
    MPI_VALIDATE_RET(X != NULL);
    MPI_VALIDATE_RET(Y != NULL);

    /* all-bits 1 if assign is 1, all-bits 0 if assign is 0 */
    mbedtls_mpi_uint limb_mask = mbedtls_ct_mpi_uint_mask(assign);

    MBEDTLS_MPI_CHK(mbedtls_mpi_grow(X, Y->n));

    X->s = (int) mbedtls_ct_uint_if(assign, Y->s, X->s);

    mbedtls_mpi_core_cond_assign(X->p, Y->p, Y->n, assign);

    for (size_t i = Y->n; i < X->n; i++) {
        X->p[i] &= ~limb_mask;
    }

cleanup:
    return ret;
}

/*
 * Conditionally swap X and Y, without leaking information
 * about whether the swap was made or not.
 * Here it is not ok to simply swap the pointers, which would lead to
 * different memory access patterns when X and Y are used afterwards.
 */
int mbedtls_mpi_safe_cond_swap(mbedtls_mpi *X,
                               mbedtls_mpi *Y,
                               unsigned char swap)
{
    int ret = 0;
    int s;
    MPI_VALIDATE_RET(X != NULL);
    MPI_VALIDATE_RET(Y != NULL);

    if (X == Y) {
        return 0;
    }

    MBEDTLS_MPI_CHK(mbedtls_mpi_grow(X, Y->n));
    MBEDTLS_MPI_CHK(mbedtls_mpi_grow(Y, X->n));

    s = X->s;
    X->s = (int) mbedtls_ct_uint_if(swap, Y->s, X->s);
    Y->s = (int) mbedtls_ct_uint_if(swap, s, Y->s);

    mbedtls_mpi_core_cond_swap(X->p, Y->p, X->n, swap);

cleanup:
    return ret;
}

/*
 * Compare unsigned values in constant time
 */
unsigned mbedtls_mpi_core_lt_ct(const mbedtls_mpi_uint *A,
                                const mbedtls_mpi_uint *B,
                                size_t limbs)
{
    unsigned ret, cond, done;

    /* The value of any of these variables is either 0 or 1 for the rest of
     * their scope. */
    ret = cond = done = 0;

    for (size_t i = limbs; i > 0; i--) {
        /*
         * If B[i - 1] < A[i - 1] then A < B is false and the result must
         * remain 0.
         *
         * Again even if we can make a decision, we just mark the result and
         * the fact that we are done and continue looping.
         */
        cond = mbedtls_ct_mpi_uint_lt(B[i - 1], A[i - 1]);
        done |= cond;

        /*
         * If A[i - 1] < B[i - 1] then A < B is true.
         *
         * Again even if we can make a decision, we just mark the result and
         * the fact that we are done and continue looping.
         */
        cond = mbedtls_ct_mpi_uint_lt(A[i - 1], B[i - 1]);
        ret |= cond & (1 - done);
        done |= cond;
    }

    /*
     * If all the limbs were equal, then the numbers are equal, A < B is false
     * and leaving the result 0 is correct.
     */

    return ret;
}

/*
 * Compare signed values in constant time
 */
int mbedtls_mpi_lt_mpi_ct(const mbedtls_mpi *X,
                          const mbedtls_mpi *Y,
                          unsigned *ret)
{
    size_t i;
    /* The value of any of these variables is either 0 or 1 at all times. */
    unsigned cond, done, X_is_negative, Y_is_negative;

    MPI_VALIDATE_RET(X != NULL);
    MPI_VALIDATE_RET(Y != NULL);
    MPI_VALIDATE_RET(ret != NULL);

    if (X->n != Y->n) {
        return MBEDTLS_ERR_MPI_BAD_INPUT_DATA;
    }

    /*
     * Set sign_N to 1 if N >= 0, 0 if N < 0.
     * We know that N->s == 1 if N >= 0 and N->s == -1 if N < 0.
     */
    X_is_negative = (X->s & 2) >> 1;
    Y_is_negative = (Y->s & 2) >> 1;

    /*
     * If the signs are different, then the positive operand is the bigger.
     * That is if X is negative (X_is_negative == 1), then X < Y is true and it
     * is false if X is positive (X_is_negative == 0).
     */
    cond = (X_is_negative ^ Y_is_negative);
    *ret = cond & X_is_negative;

    /*
     * This is a constant-time function. We might have the result, but we still
     * need to go through the loop. Record if we have the result already.
     */
    done = cond;

    for (i = X->n; i > 0; i--) {
        /*
         * If Y->p[i - 1] < X->p[i - 1] then X < Y is true if and only if both
         * X and Y are negative.
         *
         * Again even if we can make a decision, we just mark the result and
         * the fact that we are done and continue looping.
         */
        cond = mbedtls_ct_mpi_uint_lt(Y->p[i - 1], X->p[i - 1]);
        *ret |= cond & (1 - done) & X_is_negative;
        done |= cond;

        /*
         * If X->p[i - 1] < Y->p[i - 1] then X < Y is true if and only if both
         * X and Y are positive.
         *
         * Again even if we can make a decision, we just mark the result and
         * the fact that we are done and continue looping.
         */
        cond = mbedtls_ct_mpi_uint_lt(X->p[i - 1], Y->p[i - 1]);
        *ret |= cond & (1 - done) & (1 - X_is_negative);
        done |= cond;
    }

    return 0;
}

#endif /* MBEDTLS_BIGNUM_C */

#if defined(MBEDTLS_PKCS1_V15) && defined(MBEDTLS_RSA_C) && !defined(MBEDTLS_RSA_ALT)

int mbedtls_ct_rsaes_pkcs1_v15_unpadding(unsigned char *input,
                                         size_t ilen,
                                         unsigned char *output,
                                         size_t output_max_len,
                                         size_t *olen)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    size_t i, plaintext_max_size;

    /* The following variables take sensitive values: their value must
     * not leak into the observable behavior of the function other than
     * the designated outputs (output, olen, return value). Otherwise
     * this would open the execution of the function to
     * side-channel-based variants of the Bleichenbacher padding oracle
     * attack. Potential side channels include overall timing, memory
     * access patterns (especially visible to an adversary who has access
     * to a shared memory cache), and branches (especially visible to
     * an adversary who has access to a shared code cache or to a shared
     * branch predictor). */
    size_t pad_count = 0;
    unsigned bad = 0;
    unsigned char pad_done = 0;
    size_t plaintext_size = 0;
    unsigned output_too_large;

    plaintext_max_size = (output_max_len > ilen - 11) ? ilen - 11
                                                        : output_max_len;

    /* Check and get padding length in constant time and constant
     * memory trace. The first byte must be 0. */
    bad |= input[0];


    /* Decode EME-PKCS1-v1_5 padding: 0x00 || 0x02 || PS || 0x00
     * where PS must be at least 8 nonzero bytes. */
    bad |= input[1] ^ MBEDTLS_RSA_CRYPT;

    /* Read the whole buffer. Set pad_done to nonzero if we find
     * the 0x00 byte and remember the padding length in pad_count. */
    for (i = 2; i < ilen; i++) {
        pad_done  |= ((input[i] | (unsigned char) -input[i]) >> 7) ^ 1;
        pad_count += ((pad_done | (unsigned char) -pad_done) >> 7) ^ 1;
    }


    /* If pad_done is still zero, there's no data, only unfinished padding. */
    bad |= mbedtls_ct_uint_if(pad_done, 0, 1);

    /* There must be at least 8 bytes of padding. */
    bad |= mbedtls_ct_size_gt(8, pad_count);

    /* If the padding is valid, set plaintext_size to the number of
     * remaining bytes after stripping the padding. If the padding
     * is invalid, avoid leaking this fact through the size of the
     * output: use the maximum message size that fits in the output
     * buffer. Do it without branches to avoid leaking the padding
     * validity through timing. RSA keys are small enough that all the
     * size_t values involved fit in unsigned int. */
    plaintext_size = mbedtls_ct_uint_if(
        bad, (unsigned) plaintext_max_size,
        (unsigned) (ilen - pad_count - 3));

    /* Set output_too_large to 0 if the plaintext fits in the output
     * buffer and to 1 otherwise. */
    output_too_large = mbedtls_ct_size_gt(plaintext_size,
                                          plaintext_max_size);

    /* Set ret without branches to avoid timing attacks. Return:
     * - INVALID_PADDING if the padding is bad (bad != 0).
     * - OUTPUT_TOO_LARGE if the padding is good but the decrypted
     *   plaintext does not fit in the output buffer.
     * - 0 if the padding is correct. */
    ret = -(int) mbedtls_ct_uint_if(
        bad, -MBEDTLS_ERR_RSA_INVALID_PADDING,
        mbedtls_ct_uint_if(output_too_large,
                           -MBEDTLS_ERR_RSA_OUTPUT_TOO_LARGE,
                           0));

    /* If the padding is bad or the plaintext is too large, zero the
     * data that we're about to copy to the output buffer.
     * We need to copy the same amount of data
     * from the same buffer whether the padding is good or not to
     * avoid leaking the padding validity through overall timing or
     * through memory or cache access patterns. */
    bad = mbedtls_ct_uint_mask(bad | output_too_large);
    for (i = 11; i < ilen; i++) {
        input[i] &= ~bad;
    }

    /* If the plaintext is too large, truncate it to the buffer size.
     * Copy anyway to avoid revealing the length through timing, because
     * revealing the length is as bad as revealing the padding validity
     * for a Bleichenbacher attack. */
    plaintext_size = mbedtls_ct_uint_if(output_too_large,
                                        (unsigned) plaintext_max_size,
                                        (unsigned) plaintext_size);

    /* Move the plaintext to the leftmost position where it can start in
     * the working buffer, i.e. make it start plaintext_max_size from
     * the end of the buffer. Do this with a memory access trace that
     * does not depend on the plaintext size. After this move, the
     * starting location of the plaintext is no longer sensitive
     * information. */
    mbedtls_ct_mem_move_to_left(input + ilen - plaintext_max_size,
                                plaintext_max_size,
                                plaintext_max_size - plaintext_size);

    /* Finally copy the decrypted plaintext plus trailing zeros into the output
     * buffer. If output_max_len is 0, then output may be an invalid pointer
     * and the result of memcpy() would be undefined; prevent undefined
     * behavior making sure to depend only on output_max_len (the size of the
     * user-provided output buffer), which is independent from plaintext
     * length, validity of padding, success of the decryption, and other
     * secrets. */
    if (output_max_len != 0) {
        memcpy(output, input + ilen - plaintext_max_size, plaintext_max_size);
    }

    /* Report the amount of data we copied to the output buffer. In case
     * of errors (bad padding or output too large), the value of *olen
     * when this function returns is not specified. Making it equivalent
     * to the good case limits the risks of leaking the padding validity. */
    *olen = plaintext_size;

    return ret;
}

#endif /* MBEDTLS_PKCS1_V15 && MBEDTLS_RSA_C && ! MBEDTLS_RSA_ALT */
