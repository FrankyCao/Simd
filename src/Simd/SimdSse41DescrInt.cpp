/*
* Simd Library (http://ermig1979.github.io/Simd).
*
* Copyright (c) 2011-2023 Yermalayeu Ihar.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#include "Simd/SimdMemory.h"
#include "Simd/SimdStore.h"
#include "Simd/SimdExtract.h"
#include "Simd/SimdArray.h"
#include "Simd/SimdUnpack.h"
#include "Simd/SimdDescrInt.h"
#include "Simd/SimdDescrIntCommon.h"
#include "Simd/SimdCpu.h"
#include "Simd/SimdFloat16.h"

namespace Simd
{
#ifdef SIMD_SSE41_ENABLE    
    namespace Sse41
    {
        static void MinMax32f(const float* src, size_t size, float& min, float& max)
        {
            assert(size % 8 == 0);
            __m128 _min = _mm_set1_ps(FLT_MAX);
            __m128 _max = _mm_set1_ps(-FLT_MAX);
            size_t i = 0;
            for (; i < size; i += 4)
            {
                __m128 _src = _mm_loadu_ps(src + i);
                _min = _mm_min_ps(_src, _min);
                _max = _mm_max_ps(_src, _max);
            }
            MinVal32f(_min, min);
            MaxVal32f(_max, max);
        }

        //-------------------------------------------------------------------------------------------------

        static void MinMax16f(const uint16_t* src, size_t size, float& min, float& max)
        {
            assert(size % 8 == 0);
            __m128 _min = _mm_set1_ps(FLT_MAX);
            __m128 _max = _mm_set1_ps(-FLT_MAX);
            size_t i = 0;
            for (; i < size; i += 4)
            {
                __m128i f16 = _mm_loadl_epi64((__m128i*)(src + i));
                __m128 _src = Float16ToFloat32(UnpackU16<0>(f16));
                _min = _mm_min_ps(_src, _min);
                _max = _mm_max_ps(_src, _max);
            }
            MinVal32f(_min, min);
            MaxVal32f(_max, max);
        }

        //-------------------------------------------------------------------------------------------------

        SIMD_INLINE __m128i Encode32f(__m128 src, __m128 scale, __m128 min, __m128i& sum, __m128i& sqsum)
        {
            __m128i value = _mm_cvtps_epi32(_mm_mul_ps(_mm_sub_ps(src, min), scale));
            sum = _mm_add_epi32(value, sum);
            sqsum = _mm_add_epi32(_mm_madd_epi16(value, value), sqsum);
            return value;
        }

        SIMD_INLINE __m128i Encode32f(const float* src, __m128 scale, __m128 min, __m128i& sum, __m128i& sqsum)
        {
            return Encode32f(_mm_loadu_ps(src), scale, min, sum, sqsum);
        }

        static SIMD_INLINE __m128i Encode32f4(const float* src, __m128 scale, __m128 min, __m128i& sum, __m128i& sqsum)
        {
            __m128i i0 = Encode32f(src + 0, scale, min, sum, sqsum);
            __m128i i4 = Encode32f(src + 4, scale, min, sum, sqsum);
            return _mm_srli_epi32(_mm_mullo_epi16(_mm_packus_epi32(i0, i4), E4_MULLO), 12);
        }

        static SIMD_INLINE __m128i Encode32f4x8(const float* src, __m128 scale, __m128 min, __m128i& sum, __m128i& sqsum)
        {
            __m128i s0 = Encode32f4(src + 0 * 8, scale, min, sum, sqsum);
            return _mm_packus_epi16(_mm_packus_epi32(s0, K_ZERO), K_ZERO);
        }

        static SIMD_INLINE __m128i Encode32f4x16(const float* src, __m128 scale, __m128 min, __m128i& sum, __m128i& sqsum)
        {
            __m128i s0 = Encode32f4(src + 0 * 8, scale, min, sum, sqsum);
            __m128i s1 = Encode32f4(src + 1 * 8, scale, min, sum, sqsum);
            return _mm_packus_epi16(_mm_packus_epi32(s0, s1), K_ZERO);
        }

        static void Encode32f4(const float* src, float scale, float min, size_t size, int32_t& sum, int32_t& sqsum, uint8_t* dst)
        {
            assert(size % 8 == 0);
            size_t i = 0, size16 = AlignLo(size, 16);
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _min = _mm_set1_ps(min);
            __m128i _sum = _mm_setzero_si128();
            __m128i _sqsum = _mm_setzero_si128();
            for (; i < size16; i += 16, src += 16, dst += 8)
                _mm_storel_epi64((__m128i*)dst, Encode32f4x16(src, _scale, _min, _sum, _sqsum));
            for (; i < size; i += 8, src += 8, dst += 4)
                *(uint32_t*)(dst) = _mm_extract_epi32(Encode32f4x8(src, _scale, _min, _sum, _sqsum), 0);
            sum = ExtractInt32Sum(_sum);
            sqsum = ExtractInt32Sum(_sqsum);
        }

        static SIMD_INLINE __m128i Encode32f5(const float* src, __m128 scale, __m128 min, __m128i& sum, __m128i& sqsum)
        {
            __m128i i0 = Encode32f(src + 0, scale, min, sum, sqsum);
            __m128i i4 = Encode32f(src + 4, scale, min, sum, sqsum);
            __m128i s0 = _mm_mullo_epi16(_mm_packus_epi32(i0, i4), E5_MULLO);
            return _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(s0, E5_SHFL0), _mm_shuffle_epi8(s0, E5_SHFL1)), _mm_shuffle_epi8(s0, E5_SHFL2));
        }

        static void Encode32f5(const float* src, float scale, float min, size_t size, int32_t& sum, int32_t& sqsum, uint8_t* dst)
        {
            assert(size % 8 == 0);
            size_t i = 0, main = size - 8;
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _min = _mm_set1_ps(min);
            __m128i _sum = _mm_setzero_si128();
            __m128i _sqsum = _mm_setzero_si128();
            for (; i < main; i += 8, src += 8, dst += 5)
                _mm_storel_epi64((__m128i*)dst, Encode32f5(src, _scale, _min, _sum, _sqsum));
            for (; i < size; i += 8, src += 8, dst += 5)
            {
                __m128i d0 = Encode32f5(src, _scale, _min, _sum, _sqsum);
                *(uint32_t*)(dst + 0) = _mm_extract_epi32(d0, 0);
                *(uint8_t*)(dst + 4) = _mm_extract_epi8(d0, 4);
            }
            sum = ExtractInt32Sum(_sum);
            sqsum = ExtractInt32Sum(_sqsum);
        }

        static SIMD_INLINE __m128i Encode32f6(const float* src, __m128 scale, __m128 min, __m128i & sum, __m128i & sqsum)
        {
            __m128i i0 = Encode32f(src + 0, scale, min, sum, sqsum);
            __m128i i4 = Encode32f(src + 4, scale, min, sum, sqsum);
            __m128i s0 = _mm_mullo_epi16(_mm_packus_epi32(i0, i4), E6_MULLO);
            return _mm_or_si128(_mm_shuffle_epi8(s0, E6_SHFL0), _mm_shuffle_epi8(s0, E6_SHFL1));
        }

        static void Encode32f6(const float* src, float scale, float min, size_t size, int32_t& sum, int32_t& sqsum, uint8_t* dst)
        {
            assert(size % 8 == 0);
            size_t i = 0, main = size - 8;
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _min = _mm_set1_ps(min);
            __m128i _sum = _mm_setzero_si128();
            __m128i _sqsum = _mm_setzero_si128();
            for (; i < main; i += 8, src += 8, dst += 6)
                _mm_storel_epi64((__m128i*)dst, Encode32f6(src, _scale, _min, _sum, _sqsum));
            for (; i < size; i += 8, src += 8, dst += 6)
            {
                __m128i d0 = Encode32f6(src, _scale, _min, _sum, _sqsum);
                *(uint32_t*)(dst + 0) = _mm_extract_epi32(d0, 0);
                *(uint16_t*)(dst + 4) = _mm_extract_epi16(d0, 2);
            }
            sum = ExtractInt32Sum(_sum);
            sqsum = ExtractInt32Sum(_sqsum);
        }

        static SIMD_INLINE __m128i Encode32f7(const float* src, __m128 scale, __m128 min, __m128i& sum, __m128i& sqsum)
        {
            __m128i i0 = Encode32f(src + 0, scale, min, sum, sqsum);
            __m128i i4 = Encode32f(src + 4, scale, min, sum, sqsum);
            __m128i s0 = _mm_mullo_epi16(_mm_packus_epi32(i0, i4), E7_MULLO);
            return _mm_or_si128(_mm_shuffle_epi8(s0, E7_SHFL0), _mm_shuffle_epi8(s0, E7_SHFL1));
        }

        static void Encode32f7(const float* src, float scale, float min, size_t size, int32_t& sum, int32_t& sqsum, uint8_t* dst)
        {
            assert(size % 8 == 0);
            size_t i = 0, main = size - 8;
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _min = _mm_set1_ps(min);
            __m128i _sum = _mm_setzero_si128();
            __m128i _sqsum = _mm_setzero_si128();
            for (; i < main; i += 8, src += 8, dst += 7)
                _mm_storel_epi64((__m128i*)dst, Encode32f7(src, _scale, _min, _sum, _sqsum));
            for (; i < size; i += 8, src += 8, dst += 7)
            {
                __m128i d0 = Encode32f7(src, _scale, _min, _sum, _sqsum);
                *(uint32_t*)(dst + 0) = _mm_extract_epi32(d0, 0);
                *(uint16_t*)(dst + 4) = _mm_extract_epi16(d0, 2);
                *(uint8_t*)(dst + 6) = _mm_extract_epi8(d0, 6);
            }
            sum = ExtractInt32Sum(_sum);
            sqsum = ExtractInt32Sum(_sqsum);
        }

        static void Encode32f8(const float* src, float scale, float min, size_t size, int32_t& sum, int32_t& sqsum, uint8_t* dst)
        {
            assert(size % 8 == 0);
            size_t sizeA = AlignLo(size, A), i = 0;
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _min = _mm_set1_ps(min);
            __m128i _sum = _mm_setzero_si128();
            __m128i _sqsum = _mm_setzero_si128();
            for (; i < sizeA; i += A)
            {
                __m128i d0 = Encode32f(src + i + 0 * F, _scale, _min, _sum, _sqsum);
                __m128i d1 = Encode32f(src + i + 1 * F, _scale, _min, _sum, _sqsum);
                __m128i d2 = Encode32f(src + i + 2 * F, _scale, _min, _sum, _sqsum);
                __m128i d3 = Encode32f(src + i + 3 * F, _scale, _min, _sum, _sqsum);
                _mm_storeu_si128((__m128i*)(dst + i), _mm_packus_epi16(_mm_packus_epi32(d0, d1), _mm_packus_epi32(d2, d3)));
            }
            for (; i < size; i += F)
            {
                __m128i d0 = Encode32f(src + i, _scale, _min, _sum, _sqsum);
                *(uint32_t*)(dst + i) = _mm_cvtsi128_si32(_mm_packus_epi16(_mm_packus_epi32(d0, _mm_setzero_si128()), _mm_setzero_si128()));
            }
            sum = ExtractInt32Sum(_sum);
            sqsum = ExtractInt32Sum(_sqsum);
        }

        //-------------------------------------------------------------------------------------------------

        static SIMD_INLINE __m128i Encode16f4(const uint16_t* src, __m128 scale, __m128 min, __m128i& sum, __m128i& sqsum)
        {
            __m128i u0 = _mm_loadu_si128((__m128i*)(src));
            __m128i i0 = Encode32f(Float16ToFloat32(UnpackU16<0>(u0)), scale, min, sum, sqsum);
            __m128i i4 = Encode32f(Float16ToFloat32(UnpackU16<1>(u0)), scale, min, sum, sqsum);
            return _mm_srli_epi32(_mm_mullo_epi16(_mm_packus_epi32(i0, i4), E4_MULLO), 12);
        }

        static SIMD_INLINE __m128i Encode16f4x8(const uint16_t* src, __m128 scale, __m128 min, __m128i& sum, __m128i& sqsum)
        {
            __m128i s0 = Encode16f4(src + 0 * 8, scale, min, sum, sqsum);
            return _mm_packus_epi16(_mm_packus_epi32(s0, K_ZERO), K_ZERO);
        }

        static SIMD_INLINE __m128i Encode16f4x16(const uint16_t* src, __m128 scale, __m128 min, __m128i& sum, __m128i& sqsum)
        {
            __m128i s0 = Encode16f4(src + 0 * 8, scale, min, sum, sqsum);
            __m128i s1 = Encode16f4(src + 1 * 8, scale, min, sum, sqsum);
            return _mm_packus_epi16(_mm_packus_epi32(s0, s1), K_ZERO);
        }

        static void Encode16f4(const uint16_t* src, float scale, float min, size_t size, int32_t& sum, int32_t& sqsum, uint8_t* dst)
        {
            assert(size % 8 == 0);
            size_t i = 0, size16 = AlignLo(size, 16);
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _min = _mm_set1_ps(min);
            __m128i _sum = _mm_setzero_si128();
            __m128i _sqsum = _mm_setzero_si128();
            for (; i < size16; i += 16, src += 16, dst += 8)
                _mm_storel_epi64((__m128i*)dst, Encode16f4x16(src, _scale, _min, _sum, _sqsum));
            for (; i < size; i += 8, src += 8, dst += 4)
                *(uint32_t*)(dst) = _mm_extract_epi32(Encode16f4x8(src, _scale, _min, _sum, _sqsum), 0);
            sum = ExtractInt32Sum(_sum);
            sqsum = ExtractInt32Sum(_sqsum);
        }

        static SIMD_INLINE __m128i Encode16f5(const uint16_t* src, __m128 scale, __m128 min, __m128i& sum, __m128i& sqsum)
        {
            __m128i u0 = _mm_loadu_si128((__m128i*)(src));
            __m128i i0 = Encode32f(Float16ToFloat32(UnpackU16<0>(u0)), scale, min, sum, sqsum);
            __m128i i4 = Encode32f(Float16ToFloat32(UnpackU16<1>(u0)), scale, min, sum, sqsum);
            __m128i s0 = _mm_mullo_epi16(_mm_packus_epi32(i0, i4), E5_MULLO);
            return _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(s0, E5_SHFL0), _mm_shuffle_epi8(s0, E5_SHFL1)), _mm_shuffle_epi8(s0, E5_SHFL2));
        }

        static void Encode16f5(const uint16_t* src, float scale, float min, size_t size, int32_t& sum, int32_t& sqsum, uint8_t* dst)
        {
            assert(size % 8 == 0);
            size_t i = 0, main = size - 8;
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _min = _mm_set1_ps(min);
            __m128i _sum = _mm_setzero_si128();
            __m128i _sqsum = _mm_setzero_si128();
            for (; i < main; i += 8, src += 8, dst += 5)
                _mm_storel_epi64((__m128i*)dst, Encode16f5(src, _scale, _min, _sum, _sqsum));
            for (; i < size; i += 8, src += 8, dst += 5)
            {
                __m128i d0 = Encode16f5(src, _scale, _min, _sum, _sqsum);
                *(uint32_t*)(dst + 0) = _mm_extract_epi32(d0, 0);
                *(uint8_t*)(dst + 4) = _mm_extract_epi8(d0, 4);
            }
            sum = ExtractInt32Sum(_sum);
            sqsum = ExtractInt32Sum(_sqsum);
        }

        static SIMD_INLINE __m128i Encode16f6(const uint16_t* src, __m128 scale, __m128 min, __m128i& sum, __m128i& sqsum)
        {
            __m128i u0 = _mm_loadu_si128((__m128i*)(src));
            __m128i i0 = Encode32f(Float16ToFloat32(UnpackU16<0>(u0)), scale, min, sum, sqsum);
            __m128i i4 = Encode32f(Float16ToFloat32(UnpackU16<1>(u0)), scale, min, sum, sqsum);
            __m128i s0 = _mm_mullo_epi16(_mm_packus_epi32(i0, i4), E6_MULLO);
            return _mm_or_si128(_mm_shuffle_epi8(s0, E6_SHFL0), _mm_shuffle_epi8(s0, E6_SHFL1));
        }

        static void Encode16f6(const uint16_t* src, float scale, float min, size_t size, int32_t& sum, int32_t& sqsum, uint8_t* dst)
        {
            assert(size % 8 == 0);
            size_t i = 0, main = size - 8;
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _min = _mm_set1_ps(min);
            __m128i _sum = _mm_setzero_si128();
            __m128i _sqsum = _mm_setzero_si128();
            for (; i < main; i += 8, src += 8, dst += 6)
                _mm_storel_epi64((__m128i*)dst, Encode16f6(src, _scale, _min, _sum, _sqsum));
            for (; i < size; i += 8, src += 8, dst += 6)
            {
                __m128i d0 = Encode16f6(src, _scale, _min, _sum, _sqsum);
                *(uint32_t*)(dst + 0) = _mm_extract_epi32(d0, 0);
                *(uint16_t*)(dst + 4) = _mm_extract_epi16(d0, 2);
            }
            sum = ExtractInt32Sum(_sum);
            sqsum = ExtractInt32Sum(_sqsum);
        }

        static SIMD_INLINE __m128i Encode16f7(const uint16_t* src, __m128 scale, __m128 min, __m128i& sum, __m128i& sqsum)
        {
            __m128i u0 = _mm_loadu_si128((__m128i*)(src));
            __m128i i0 = Encode32f(Float16ToFloat32(UnpackU16<0>(u0)), scale, min, sum, sqsum);
            __m128i i4 = Encode32f(Float16ToFloat32(UnpackU16<1>(u0)), scale, min, sum, sqsum);
            __m128i s0 = _mm_mullo_epi16(_mm_packus_epi32(i0, i4), E7_MULLO);
            return _mm_or_si128(_mm_shuffle_epi8(s0, E7_SHFL0), _mm_shuffle_epi8(s0, E7_SHFL1));
        }

        static void Encode16f7(const uint16_t* src, float scale, float min, size_t size, int32_t& sum, int32_t& sqsum, uint8_t* dst)
        {
            assert(size % 8 == 0);
            size_t i = 0, main = size - 8;
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _min = _mm_set1_ps(min);
            __m128i _sum = _mm_setzero_si128();
            __m128i _sqsum = _mm_setzero_si128();
            for (; i < main; i += 8, src += 8, dst += 7)
                _mm_storel_epi64((__m128i*)dst, Encode16f7(src, _scale, _min, _sum, _sqsum));
            for (; i < size; i += 8, src += 8, dst += 7)
            {
                __m128i d0 = Encode16f7(src, _scale, _min, _sum, _sqsum);
                *(uint32_t*)(dst + 0) = _mm_extract_epi32(d0, 0);
                *(uint16_t*)(dst + 4) = _mm_extract_epi16(d0, 2);
                *(uint8_t*)(dst + 6) = _mm_extract_epi8(d0, 6);
            }
            sum = ExtractInt32Sum(_sum);
            sqsum = ExtractInt32Sum(_sqsum);
        }

        static void Encode16f8(const uint16_t* src, float scale, float min, size_t size, int32_t& sum, int32_t& sqsum, uint8_t* dst)
        {
            assert(size % 8 == 0);
            size_t sizeA = AlignLo(size, A), i = 0;
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _min = _mm_set1_ps(min);
            __m128i _sum = _mm_setzero_si128();
            __m128i _sqsum = _mm_setzero_si128();
            for (; i < sizeA; i += A)
            {
                __m128i u0 = _mm_loadu_si128((__m128i*)(src + i + 0 * F));
                __m128i d0 = Encode32f(Float16ToFloat32(UnpackU16<0>(u0)), _scale, _min, _sum, _sqsum);
                __m128i d1 = Encode32f(Float16ToFloat32(UnpackU16<1>(u0)), _scale, _min, _sum, _sqsum);
                __m128i u2 = _mm_loadu_si128((__m128i*)(src + i + 2 * F));
                __m128i d2 = Encode32f(Float16ToFloat32(UnpackU16<0>(u2)), _scale, _min, _sum, _sqsum);
                __m128i d3 = Encode32f(Float16ToFloat32(UnpackU16<1>(u2)), _scale, _min, _sum, _sqsum);
                _mm_storeu_si128((__m128i*)(dst + i), _mm_packus_epi16(_mm_packus_epi32(d0, d1), _mm_packus_epi32(d2, d3)));
            }
            for (; i < size; i += F)
            {
                __m128i u0 = _mm_loadl_epi64((__m128i*)(src + i));
                __m128i d0 = Encode32f(Float16ToFloat32(UnpackU16<0>(u0)), _scale, _min, _sum, _sqsum);
                *(uint32_t*)(dst + i) = _mm_cvtsi128_si32(_mm_packus_epi16(_mm_packus_epi32(d0, _mm_setzero_si128()), _mm_setzero_si128()));
            }
            sum = ExtractInt32Sum(_sum);
            sqsum = ExtractInt32Sum(_sqsum);
        }

        //-------------------------------------------------------------------------------------------------

        static void Decode32f4(const uint8_t* src, float scale, float shift, size_t size, float* dst)
        {
            assert(size % 8 == 0);
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _shift = _mm_set1_ps(shift);
            for (size_t i = 0; i < size; i += 8)
            {
                __m128i s4 = _mm_loadl_epi64((__m128i*)src);
                __m128i s16 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(s4, C4_SHFL0), C4_MULLO), 12);
                _mm_storeu_ps(dst + 0, _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<0>(s16)), _scale), _shift));
                _mm_storeu_ps(dst + 4, _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<1>(s16)), _scale), _shift));
                src += 4;
                dst += 8;
            }
        }

        static void Decode32f5(const uint8_t* src, float scale, float shift, size_t size, float* dst)
        {
            assert(size % 8 == 0);
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _shift = _mm_set1_ps(shift);
            for (size_t i = 0; i < size; i += 8)
            {
                __m128i s5 = _mm_loadl_epi64((__m128i*)src);
                __m128i s16 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(s5, C5_SHFL0), C5_MULLO), 11);
                _mm_storeu_ps(dst + 0, _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<0>(s16)), _scale), _shift));
                _mm_storeu_ps(dst + 4, _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<1>(s16)), _scale), _shift));
                src += 5;
                dst += 8;
            }
        }

        static void Decode32f6(const uint8_t* src, float scale, float shift, size_t size, float* dst)
        {
            assert(size % 8 == 0);
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _shift = _mm_set1_ps(shift);
            for (size_t i = 0; i < size; i += 8)
            {
                __m128i s6 = _mm_loadl_epi64((__m128i*)src);
                __m128i s16 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(s6, C6_SHFL0), C6_MULLO), 10);
                _mm_storeu_ps(dst + 0, _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<0>(s16)), _scale), _shift));
                _mm_storeu_ps(dst + 4, _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<1>(s16)), _scale), _shift));
                src += 6;
                dst += 8;
            }
        }

        static void Decode32f7(const uint8_t* src, float scale, float shift, size_t size, float* dst)
        {
            assert(size % 8 == 0);
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _shift = _mm_set1_ps(shift);
            for (size_t i = 0; i < size; i += 8)
            {
                __m128i s7 = _mm_loadl_epi64((__m128i*)src);
                __m128i s16 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(s7, C7_SHFL0), C7_MULLO), 9);
                _mm_storeu_ps(dst + 0, _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<0>(s16)), _scale), _shift));
                _mm_storeu_ps(dst + 4, _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<1>(s16)), _scale), _shift));
                src += 7;
                dst += 8;
            }
        }

        static void Decode32f8(const uint8_t* src, float scale, float shift, size_t size, float* dst)
        {
            assert(size % 8 == 0);
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _shift = _mm_set1_ps(shift);
            size_t i = 0;
            for (; i < size; i += 4)
            {
                __m128 _src = _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_cvtsi32_si128(*(uint32_t*)(src + i))));
                _mm_storeu_ps(dst + i, _mm_add_ps(_mm_mul_ps(_src, _scale), _shift));
            }
        }

        //-------------------------------------------------------------------------------------------------

        static void Decode16f4(const uint8_t* src, float scale, float shift, size_t size, uint16_t* dst)
        {
            assert(size % 8 == 0);
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _shift = _mm_set1_ps(shift);
            for (size_t i = 0; i < size; i += 8)
            {
                __m128i s4 = _mm_loadl_epi64((__m128i*)src);
                __m128i s16 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(s4, C4_SHFL0), C4_MULLO), 12);
                __m128i d0 = Float32ToFloat16(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<0>(s16)), _scale), _shift));
                __m128i d4 = Float32ToFloat16(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<1>(s16)), _scale), _shift));
                _mm_storeu_si128((__m128i*)dst, _mm_packus_epi32(d0, d4));
                src += 4;
                dst += 8;
            }
        }

        static void Decode16f5(const uint8_t* src, float scale, float shift, size_t size, uint16_t* dst)
        {
            assert(size % 8 == 0);
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _shift = _mm_set1_ps(shift);
            for (size_t i = 0; i < size; i += 8)
            {
                __m128i s5 = _mm_loadl_epi64((__m128i*)src);
                __m128i s16 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(s5, C5_SHFL0), C5_MULLO), 11);
                __m128i d0 = Float32ToFloat16(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<0>(s16)), _scale), _shift));
                __m128i d4 = Float32ToFloat16(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<1>(s16)), _scale), _shift));
                _mm_storeu_si128((__m128i*)dst, _mm_packus_epi32(d0, d4));
                src += 5;
                dst += 8;
            }
        }

        static void Decode16f6(const uint8_t* src, float scale, float shift, size_t size, uint16_t* dst)
        {
            assert(size % 8 == 0);
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _shift = _mm_set1_ps(shift);
            for (size_t i = 0; i < size; i += 8)
            {
                __m128i s6 = _mm_loadl_epi64((__m128i*)src);
                __m128i s16 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(s6, C6_SHFL0), C6_MULLO), 10);
                __m128i d0 = Float32ToFloat16(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<0>(s16)), _scale), _shift));
                __m128i d4 = Float32ToFloat16(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<1>(s16)), _scale), _shift));
                _mm_storeu_si128((__m128i*)dst, _mm_packus_epi32(d0, d4));
                src += 6;
                dst += 8;
            }
        }

        static void Decode16f7(const uint8_t* src, float scale, float shift, size_t size, uint16_t* dst)
        {
            assert(size % 8 == 0);
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _shift = _mm_set1_ps(shift);
            for (size_t i = 0; i < size; i += 8)
            {
                __m128i s7 = _mm_loadl_epi64((__m128i*)src);
                __m128i s16 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(s7, C7_SHFL0), C7_MULLO), 9);
                __m128i d0 = Float32ToFloat16(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<0>(s16)), _scale), _shift));
                __m128i d4 = Float32ToFloat16(_mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(UnpackU16<1>(s16)), _scale), _shift));
                _mm_storeu_si128((__m128i*)dst, _mm_packus_epi32(d0, d4));
                src += 7;
                dst += 8;
            }
        }

        static void Decode16f8(const uint8_t* src, float scale, float shift, size_t size, uint16_t* dst)
        {
            assert(size % 8 == 0);
            __m128 _scale = _mm_set1_ps(scale);
            __m128 _shift = _mm_set1_ps(shift);
            size_t i = 0;
            for (; i < size; i += 8)
            {
                __m128i s8 = _mm_loadl_epi64((__m128i*)(src + i));
                __m128 s0 = _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_srli_si128(s8, 0)));
                __m128 s4 = _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_srli_si128(s8, 4)));
                __m128i d0 = Float32ToFloat16(_mm_add_ps(_mm_mul_ps(s0, _scale), _shift));
                __m128i d4 = Float32ToFloat16(_mm_add_ps(_mm_mul_ps(s4, _scale), _shift));
                _mm_storeu_si128((__m128i*)(dst + i), _mm_packus_epi32(d0, d4));
            }
        }

        //-------------------------------------------------------------------------------------------------

        template<int bits> int32_t Correlation(const uint8_t* a, const uint8_t* b, size_t size);

        template<> int32_t Correlation<4>(const uint8_t* a, const uint8_t* b, size_t size)
        {
            assert(size % 8 == 0);
            __m128i ab32 = _mm_setzero_si128();
            size_t i = 0, size32 = AlignLo(size, 32);
            for (; i < size32; i += 32, a += 16, b += 16)
            {
                __m128i _a = _mm_loadu_si128((__m128i*)a);
                __m128i _b = _mm_loadu_si128((__m128i*)b);
                __m128i ab16 = _mm_maddubs_epi16(_mm_and_si128(_a, K8_0F), _mm_and_si128(_b, K8_0F));
                ab16 = _mm_add_epi16(ab16, _mm_maddubs_epi16(_mm_and_si128(_mm_srli_epi16(_a, 4), K8_0F), _mm_and_si128(_mm_srli_epi16(_b, 4), K8_0F)));
                ab32 = _mm_add_epi32(ab32, _mm_madd_epi16(ab16, K16_0001));
            }
            for (; i < size; i += 8, a += 4, b += 4)
            {
                __m128i _a = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)a), C4_SHFL0), C4_MULLO), 12);
                __m128i _b = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)b), C4_SHFL0), C4_MULLO), 12);
                ab32 = _mm_add_epi32(_mm_madd_epi16(_a, _b), ab32);
            }
            return ExtractInt32Sum(ab32);
        }

        template<> int32_t Correlation<5>(const uint8_t* a, const uint8_t* b, size_t size)
        {
            assert(size % 8 == 0);
            __m128i _ab = _mm_setzero_si128();
            size_t i = 0, sizeA = AlignLo(size, A);
            for (; i < sizeA; i += A, a += 10, b += 10)
            {
                __m128i _a = _mm_loadu_si128((__m128i*)a);
                __m128i _b = _mm_loadu_si128((__m128i*)b);
                __m128i a0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_a, C5_SHFL0), C5_MULLO), 11);
                __m128i b0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_b, C5_SHFL0), C5_MULLO), 11);
                _ab = _mm_add_epi32(_mm_madd_epi16(a0, b0), _ab);
                __m128i a1 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_a, C5_SHFL1), C5_MULLO), 11);
                __m128i b1 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_b, C5_SHFL1), C5_MULLO), 11);
                _ab = _mm_add_epi32(_mm_madd_epi16(a1, b1), _ab);
            }
            for (; i < size; i += 8, a += 5, b += 5)
            {
                __m128i _a = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)a), C5_SHFL0), C5_MULLO), 11);
                __m128i _b = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)b), C5_SHFL0), C5_MULLO), 11);
                _ab = _mm_add_epi32(_mm_madd_epi16(_a, _b), _ab);
            }
            return ExtractInt32Sum(_ab);
        }

        template<> int32_t Correlation<6>(const uint8_t* a, const uint8_t* b, size_t size)
        {
            assert(size % 8 == 0);
            __m128i _ab = _mm_setzero_si128();  
            size_t i = 0, sizeA = AlignLo(size, A);
            for (; i < sizeA; i += A, a += 12, b += 12)
            {
                __m128i _a = _mm_loadu_si128((__m128i*)a);
                __m128i _b = _mm_loadu_si128((__m128i*)b);
                __m128i a0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_a, C6_SHFL0), C6_MULLO), 10);
                __m128i b0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_b, C6_SHFL0), C6_MULLO), 10);
                _ab = _mm_add_epi32(_mm_madd_epi16(a0, b0), _ab);
                __m128i a1 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_a, C6_SHFL1), C6_MULLO), 10);
                __m128i b1 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_b, C6_SHFL1), C6_MULLO), 10);
                _ab = _mm_add_epi32(_mm_madd_epi16(a1, b1), _ab);
            }
            for (; i < size; i += 8, a += 6, b += 6)
            {
                __m128i _a = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)a), C6_SHFL0), C6_MULLO), 10);
                __m128i _b = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)b), C6_SHFL0), C6_MULLO), 10);
                _ab = _mm_add_epi32(_mm_madd_epi16(_a, _b), _ab);
            }
            return ExtractInt32Sum(_ab);
        }

        template<> int32_t Correlation<7>(const uint8_t* a, const uint8_t* b, size_t size)
        {
            assert(size % 8 == 0);
            __m128i _ab = _mm_setzero_si128();
            size_t i = 0, sizeA = AlignLo(size, A);
            for (; i < sizeA; i += A, a += 14, b += 14)
            {
                __m128i _a = _mm_loadu_si128((__m128i*)a);
                __m128i _b = _mm_loadu_si128((__m128i*)b);
                __m128i a0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_a, C7_SHFL0), C7_MULLO), 9);
                __m128i b0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_b, C7_SHFL0), C7_MULLO), 9);
                _ab = _mm_add_epi32(_mm_madd_epi16(a0, b0), _ab);
                __m128i a1 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_a, C7_SHFL1), C7_MULLO), 9);
                __m128i b1 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_b, C7_SHFL1), C7_MULLO), 9);
                _ab = _mm_add_epi32(_mm_madd_epi16(a1, b1), _ab);
            }
            for (; i < size; i += 8, a += 7, b += 7)
            {
                __m128i _a = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)a), C7_SHFL0), C7_MULLO), 9);
                __m128i _b = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)b), C7_SHFL0), C7_MULLO), 9);
                _ab = _mm_add_epi32(_mm_madd_epi16(_a, _b), _ab);
            }
            return ExtractInt32Sum(_ab);
        }

        template<> int32_t Correlation<8>(const uint8_t* a, const uint8_t* b, size_t size)
        {
            size_t i = 0, sizeA = AlignLo(size, A);
            __m128i _ab = _mm_setzero_si128();
            for (; i < sizeA; i += A)
            {
                __m128i _a = _mm_loadu_si128((__m128i*)(a + i));
                __m128i _b = _mm_loadu_si128((__m128i*)(b + i));
                _ab = _mm_add_epi32(_mm_madd_epi16(UnpackU8<0>(_a), UnpackU8<0>(_b)), _ab);
                _ab = _mm_add_epi32(_mm_madd_epi16(UnpackU8<1>(_a), UnpackU8<1>(_b)), _ab);
            }
            for (; i < size; i += 8)
            {
                __m128i _a = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i*)(a + i)));
                __m128i _b = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i*)(b + i)));
                _ab = _mm_add_epi32(_mm_madd_epi16(_a, _b), _ab);
            }
            return ExtractInt32Sum(_ab);
        }

        template<int bits> void CosineDistance(const uint8_t* a, const uint8_t* b, size_t size, float* distance)
        {
            float abSum = (float)Correlation<bits>(a + 16, b + 16, size);
            Base::DecodeCosineDistance(a, b, abSum, distance);
        }

        //-------------------------------------------------------------------------------------------------

        template<int bits> void MicroCosineDistancesDirect2x4(const uint8_t* const* A, const uint8_t* const* B, size_t size, float* distances, size_t stride);

        template<> void MicroCosineDistancesDirect2x4<4>(const uint8_t* const* A, const uint8_t* const* B, size_t size, float* distances, size_t stride)
        {
            size_t i = 0, size32 = AlignLo(size, 32), o = 16;
            __m128i a0, a1, b0;
            __m128i ab00 = _mm_setzero_si128();
            __m128i ab01 = _mm_setzero_si128();
            __m128i ab02 = _mm_setzero_si128();
            __m128i ab03 = _mm_setzero_si128();
            __m128i ab10 = _mm_setzero_si128();
            __m128i ab11 = _mm_setzero_si128();
            __m128i ab12 = _mm_setzero_si128();
            __m128i ab13 = _mm_setzero_si128();
            for (; i < size32; i += 32, o += 16)
            {
                a0 = _mm_and_si128(_mm_loadu_si128((__m128i*)(A[0] + o)), K8_0F);
                a1 = _mm_and_si128(_mm_loadu_si128((__m128i*)(A[1] + o)), K8_0F);

                b0 = _mm_and_si128(_mm_loadu_si128((__m128i*)(B[0] + o)), K8_0F);
                ab00 = _mm_add_epi32(ab00, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));
                ab10 = _mm_add_epi32(ab10, _mm_madd_epi16(_mm_maddubs_epi16(a1, b0), K16_0001));

                b0 = _mm_and_si128(_mm_loadu_si128((__m128i*)(B[1] + o)), K8_0F);
                ab01 = _mm_add_epi32(ab01, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));
                ab11 = _mm_add_epi32(ab11, _mm_madd_epi16(_mm_maddubs_epi16(a1, b0), K16_0001));

                b0 = _mm_and_si128(_mm_loadu_si128((__m128i*)(B[2] + o)), K8_0F);
                ab02 = _mm_add_epi32(ab02, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));
                ab12 = _mm_add_epi32(ab12, _mm_madd_epi16(_mm_maddubs_epi16(a1, b0), K16_0001));

                b0 = _mm_and_si128(_mm_loadu_si128((__m128i*)(B[3] + o)), K8_0F);
                ab03 = _mm_add_epi32(ab03, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));
                ab13 = _mm_add_epi32(ab13, _mm_madd_epi16(_mm_maddubs_epi16(a1, b0), K16_0001));

                a0 = _mm_and_si128(_mm_srli_epi16(_mm_loadu_si128((__m128i*)(A[0] + o)), 4), K8_0F);
                a1 = _mm_and_si128(_mm_srli_epi16(_mm_loadu_si128((__m128i*)(A[1] + o)), 4), K8_0F);

                b0 = _mm_and_si128(_mm_srli_epi16(_mm_loadu_si128((__m128i*)(B[0] + o)), 4), K8_0F);
                ab00 = _mm_add_epi32(ab00, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));
                ab10 = _mm_add_epi32(ab10, _mm_madd_epi16(_mm_maddubs_epi16(a1, b0), K16_0001));

                b0 = _mm_and_si128(_mm_srli_epi16(_mm_loadu_si128((__m128i*)(B[1] + o)), 4), K8_0F);
                ab01 = _mm_add_epi32(ab01, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));
                ab11 = _mm_add_epi32(ab11, _mm_madd_epi16(_mm_maddubs_epi16(a1, b0), K16_0001));

                b0 = _mm_and_si128(_mm_srli_epi16(_mm_loadu_si128((__m128i*)(B[2] + o)), 4), K8_0F);
                ab02 = _mm_add_epi32(ab02, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));
                ab12 = _mm_add_epi32(ab12, _mm_madd_epi16(_mm_maddubs_epi16(a1, b0), K16_0001));

                b0 = _mm_and_si128(_mm_srli_epi16(_mm_loadu_si128((__m128i*)(B[3] + o)), 4), K8_0F);
                ab03 = _mm_add_epi32(ab03, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));
                ab13 = _mm_add_epi32(ab13, _mm_madd_epi16(_mm_maddubs_epi16(a1, b0), K16_0001));
            }
            for (; i < size; i += 8, o += 4)
            {
                a0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(A[0] + o)), C4_SHFL0), C4_MULLO), 12);
                a1 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(A[1] + o)), C4_SHFL0), C4_MULLO), 12);

                b0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[0] + o)), C4_SHFL0), C4_MULLO), 12);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a0, b0), ab00);
                ab10 = _mm_add_epi32(_mm_madd_epi16(a1, b0), ab10);

                b0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[1] + o)), C4_SHFL0), C4_MULLO), 12);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a0, b0), ab01);
                ab11 = _mm_add_epi32(_mm_madd_epi16(a1, b0), ab11);

                b0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[2] + o)), C4_SHFL0), C4_MULLO), 12);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a0, b0), ab02);
                ab12 = _mm_add_epi32(_mm_madd_epi16(a1, b0), ab12);

                b0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[3] + o)), C4_SHFL0), C4_MULLO), 12);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a0, b0), ab03);
                ab13 = _mm_add_epi32(_mm_madd_epi16(a1, b0), ab13);
            }
            __m128 ab0 = _mm_cvtepi32_ps(Extract4Sums(ab00, ab01, ab02, ab03));
            __m128 ab1 = _mm_cvtepi32_ps(Extract4Sums(ab10, ab11, ab12, ab13));
            DecodeCosineDistances1x4(A[0], B, ab0, distances + 0 * stride);
            DecodeCosineDistances1x4(A[1], B, ab1, distances + 1 * stride);
        }

        template<> void MicroCosineDistancesDirect2x4<5>(const uint8_t* const* A, const uint8_t* const* B, size_t size, float* distances, size_t stride)
        {
            size_t i = 0, size16 = AlignLo(size, 16), o = 16;
            __m128i a00, a01, a10, a11, b00, b01;
            __m128i ab00 = _mm_setzero_si128();
            __m128i ab01 = _mm_setzero_si128();
            __m128i ab02 = _mm_setzero_si128();
            __m128i ab03 = _mm_setzero_si128();
            __m128i ab10 = _mm_setzero_si128();
            __m128i ab11 = _mm_setzero_si128();
            __m128i ab12 = _mm_setzero_si128();
            __m128i ab13 = _mm_setzero_si128();
            for (; i < size16; i += 16, o += 10)
            {
                a01 = _mm_loadu_si128((__m128i*)(A[0] + o));
                a00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(a01, C5_SHFL0), C5_MULLO), 11);
                a01 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(a01, 5), C5_SHFL0), C5_MULLO), 11);
                a11 = _mm_loadu_si128((__m128i*)(A[1] + o));
                a10 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(a11, C5_SHFL0), C5_MULLO), 11);
                a11 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(a11, 5), C5_SHFL0), C5_MULLO), 11);

                b01 = _mm_loadu_si128((__m128i*)(B[0] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C5_SHFL0), C5_MULLO), 11);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);
                ab10 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab10);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 5), C5_SHFL0), C5_MULLO), 11);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab00);
                ab10 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab10);

                b01 = _mm_loadu_si128((__m128i*)(B[1] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C5_SHFL0), C5_MULLO), 11);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);
                ab11 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab11);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 5), C5_SHFL0), C5_MULLO), 11);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab01);
                ab11 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab11);

                b01 = _mm_loadu_si128((__m128i*)(B[2] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C5_SHFL0), C5_MULLO), 11);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);
                ab12 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab12);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 5), C5_SHFL0), C5_MULLO), 11);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab02);
                ab12 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab12);

                b01 = _mm_loadu_si128((__m128i*)(B[3] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C5_SHFL0), C5_MULLO), 11);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
                ab13 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab13);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 5), C5_SHFL0), C5_MULLO), 11);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab03);
                ab13 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab13);
            }
            for (; i < size; i += 8, o += 5)
            {
                a00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(A[0] + o)), C5_SHFL0), C5_MULLO), 11);
                a10 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(A[1] + o)), C5_SHFL0), C5_MULLO), 11);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[0] + o)), C5_SHFL0), C5_MULLO), 11);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);
                ab10 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab10);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[1] + o)), C5_SHFL0), C5_MULLO), 11);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);
                ab11 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab11);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[2] + o)), C5_SHFL0), C5_MULLO), 11);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);
                ab12 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab12);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[3] + o)), C5_SHFL0), C5_MULLO), 11);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
                ab13 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab13);
            }
            __m128 ab0 = _mm_cvtepi32_ps(Extract4Sums(ab00, ab01, ab02, ab03));
            __m128 ab1 = _mm_cvtepi32_ps(Extract4Sums(ab10, ab11, ab12, ab13));
            DecodeCosineDistances1x4(A[0], B, ab0, distances + 0 * stride);
            DecodeCosineDistances1x4(A[1], B, ab1, distances + 1 * stride);
        }

        template<> void MicroCosineDistancesDirect2x4<6>(const uint8_t* const* A, const uint8_t* const* B, size_t size, float* distances, size_t stride)
        {
            size_t i = 0, size16 = AlignLo(size, 16), o = 16;
            __m128i a00, a01, a10, a11, b00, b01;
            __m128i ab00 = _mm_setzero_si128();
            __m128i ab01 = _mm_setzero_si128();
            __m128i ab02 = _mm_setzero_si128();
            __m128i ab03 = _mm_setzero_si128();
            __m128i ab10 = _mm_setzero_si128();
            __m128i ab11 = _mm_setzero_si128();
            __m128i ab12 = _mm_setzero_si128();
            __m128i ab13 = _mm_setzero_si128();
            for (; i < size16; i += 16, o += 12)
            {
                a01 = _mm_loadu_si128((__m128i*)(A[0] + o));
                a00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(a01, C6_SHFL0), C6_MULLO), 10);
                a01 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(a01, 6), C6_SHFL0), C6_MULLO), 10);
                a11 = _mm_loadu_si128((__m128i*)(A[1] + o));
                a10 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(a11, C6_SHFL0), C6_MULLO), 10);
                a11 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(a11, 6), C6_SHFL0), C6_MULLO), 10);

                b01 = _mm_loadu_si128((__m128i*)(B[0] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C6_SHFL0), C6_MULLO), 10);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);
                ab10 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab10);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 6), C6_SHFL0), C6_MULLO), 10);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab00);
                ab10 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab10);

                b01 = _mm_loadu_si128((__m128i*)(B[1] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C6_SHFL0), C6_MULLO), 10);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);
                ab11 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab11);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 6), C6_SHFL0), C6_MULLO), 10);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab01);
                ab11 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab11);

                b01 = _mm_loadu_si128((__m128i*)(B[2] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C6_SHFL0), C6_MULLO), 10);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);
                ab12 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab12);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 6), C6_SHFL0), C6_MULLO), 10);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab02);
                ab12 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab12);

                b01 = _mm_loadu_si128((__m128i*)(B[3] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C6_SHFL0), C6_MULLO), 10);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
                ab13 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab13);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 6), C6_SHFL0), C6_MULLO), 10);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab03);
                ab13 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab13);
            }
            for (; i < size; i += 8, o += 6)
            {
                a00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(A[0] + o)), C6_SHFL0), C6_MULLO), 10);
                a10 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(A[1] + o)), C6_SHFL0), C6_MULLO), 10);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[0] + o)), C6_SHFL0), C6_MULLO), 10);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);
                ab10 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab10);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[1] + o)), C6_SHFL0), C6_MULLO), 10);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);
                ab11 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab11);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[2] + o)), C6_SHFL0), C6_MULLO), 10);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);
                ab12 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab12);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[3] + o)), C6_SHFL0), C6_MULLO), 10);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
                ab13 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab13);
            }
            __m128 ab0 = _mm_cvtepi32_ps(Extract4Sums(ab00, ab01, ab02, ab03));
            __m128 ab1 = _mm_cvtepi32_ps(Extract4Sums(ab10, ab11, ab12, ab13));
            DecodeCosineDistances1x4(A[0], B, ab0, distances + 0 * stride);
            DecodeCosineDistances1x4(A[1], B, ab1, distances + 1 * stride);
        }

        template<> void MicroCosineDistancesDirect2x4<7>(const uint8_t* const* A, const uint8_t* const* B, size_t size, float* distances, size_t stride)
        {
            size_t i = 0, size16 = AlignLo(size, 16), o = 16;
            __m128i a00, a01, a10, a11, b00, b01;
            __m128i ab00 = _mm_setzero_si128();
            __m128i ab01 = _mm_setzero_si128();
            __m128i ab02 = _mm_setzero_si128();
            __m128i ab03 = _mm_setzero_si128();
            __m128i ab10 = _mm_setzero_si128();
            __m128i ab11 = _mm_setzero_si128();
            __m128i ab12 = _mm_setzero_si128();
            __m128i ab13 = _mm_setzero_si128();
            for (; i < size16; i += 16, o += 14)
            {
                a01 = _mm_loadu_si128((__m128i*)(A[0] + o));
                a00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(a01, C7_SHFL0), C7_MULLO), 9);
                a01 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(a01, 7), C7_SHFL0), C7_MULLO), 9);
                a11 = _mm_loadu_si128((__m128i*)(A[1] + o));
                a10 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(a11, C7_SHFL0), C7_MULLO), 9);
                a11 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(a11, 7), C7_SHFL0), C7_MULLO), 9);

                b01 = _mm_loadu_si128((__m128i*)(B[0] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C7_SHFL0), C7_MULLO), 9);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);
                ab10 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab10);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 7), C7_SHFL0), C7_MULLO), 9);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab00);
                ab10 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab10);

                b01 = _mm_loadu_si128((__m128i*)(B[1] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C7_SHFL0), C7_MULLO), 9);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);
                ab11 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab11);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 7), C7_SHFL0), C7_MULLO), 9);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab01);
                ab11 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab11);

                b01 = _mm_loadu_si128((__m128i*)(B[2] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C7_SHFL0), C7_MULLO), 9);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);
                ab12 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab12);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 7), C7_SHFL0), C7_MULLO), 9);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab02);
                ab12 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab12);

                b01 = _mm_loadu_si128((__m128i*)(B[3] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C7_SHFL0), C7_MULLO), 9);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
                ab13 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab13);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 7), C7_SHFL0), C7_MULLO), 9);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab03);
                ab13 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab13);
            }
            for (; i < size; i += 8, o += 7)
            {
                a00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(A[0] + o)), C7_SHFL0), C7_MULLO), 9);
                a10 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(A[1] + o)), C7_SHFL0), C7_MULLO), 9);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[0] + o)), C7_SHFL0), C7_MULLO), 9);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);
                ab10 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab10);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[1] + o)), C7_SHFL0), C7_MULLO), 9);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);
                ab11 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab11);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[2] + o)), C7_SHFL0), C7_MULLO), 9);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);
                ab12 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab12);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[3] + o)), C7_SHFL0), C7_MULLO), 9);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
                ab13 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab13);
            }
            __m128 ab0 = _mm_cvtepi32_ps(Extract4Sums(ab00, ab01, ab02, ab03));
            __m128 ab1 = _mm_cvtepi32_ps(Extract4Sums(ab10, ab11, ab12, ab13));
            DecodeCosineDistances1x4(A[0], B, ab0, distances + 0 * stride);
            DecodeCosineDistances1x4(A[1], B, ab1, distances + 1 * stride);
        }

        template<> void MicroCosineDistancesDirect2x4<8>(const uint8_t* const* A, const uint8_t* const* B, size_t size, float* distances, size_t stride)
        {
            size_t i = 0, size16 = AlignLo(size, 16), o = 16;
            __m128i a00, a01, a10, a11, b00, b01;
            __m128i ab00 = _mm_setzero_si128();
            __m128i ab01 = _mm_setzero_si128();
            __m128i ab02 = _mm_setzero_si128();
            __m128i ab03 = _mm_setzero_si128();
            __m128i ab10 = _mm_setzero_si128();
            __m128i ab11 = _mm_setzero_si128();
            __m128i ab12 = _mm_setzero_si128();
            __m128i ab13 = _mm_setzero_si128();
            for (; i < size16; i += 16, o += 16)
            {
                a01 = _mm_loadu_si128((__m128i*)(A[0] + o));
                a00 = UnpackU8<0>(a01);
                a01 = UnpackU8<1>(a01);
                a11 = _mm_loadu_si128((__m128i*)(A[1] + o));
                a10 = UnpackU8<0>(a11);
                a11 = UnpackU8<1>(a11);

                b01 = _mm_loadu_si128((__m128i*)(B[0] + o));
                b00 = UnpackU8<0>(b01);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);
                ab10 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab10);
                b00 = UnpackU8<1>(b01);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab00);
                ab10 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab10);

                b01 = _mm_loadu_si128((__m128i*)(B[1] + o));
                b00 = UnpackU8<0>(b01);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);
                ab11 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab11);
                b00 = UnpackU8<1>(b01);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab01);
                ab11 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab11);

                b01 = _mm_loadu_si128((__m128i*)(B[2] + o));
                b00 = UnpackU8<0>(b01);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);
                ab12 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab12);
                b00 = UnpackU8<1>(b01);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab02);
                ab12 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab12);

                b01 = _mm_loadu_si128((__m128i*)(B[3] + o));
                b00 = UnpackU8<0>(b01);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
                ab13 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab13);
                b00 = UnpackU8<1>(b01);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab03);
                ab13 = _mm_add_epi32(_mm_madd_epi16(a11, b00), ab13);
            }
            for (; i < size; i += 8, o += 8)
            {
                a00 = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i*)(A[0] + o)));
                a10 = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i*)(A[1] + o)));

                b00 = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i*)(B[0] + o)));
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);
                ab10 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab10);

                b00 = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i*)(B[1] + o)));
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);
                ab11 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab11);

                b00 = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i*)(B[2] + o)));
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);
                ab12 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab12);

                b00 = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i*)(B[3] + o)));
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
                ab13 = _mm_add_epi32(_mm_madd_epi16(a10, b00), ab13);
            }
            __m128 ab0 = _mm_cvtepi32_ps(Extract4Sums(ab00, ab01, ab02, ab03));
            __m128 ab1 = _mm_cvtepi32_ps(Extract4Sums(ab10, ab11, ab12, ab13));
            DecodeCosineDistances1x4(A[0], B, ab0, distances + 0 * stride);
            DecodeCosineDistances1x4(A[1], B, ab1, distances + 1 * stride);
        }

        template<int bits> void MicroCosineDistancesDirect1x4(const uint8_t* const* A, const uint8_t* const* B, size_t size, float* distances, size_t stride);

        template<> void MicroCosineDistancesDirect1x4<4>(const uint8_t* const* A, const uint8_t* const* B, size_t size, float* distances, size_t stride)
        {
            size_t i = 0, size32 = AlignLo(size, 32), o = 16;
            __m128i a0, b0;
            __m128i ab00 = _mm_setzero_si128();
            __m128i ab01 = _mm_setzero_si128();
            __m128i ab02 = _mm_setzero_si128();
            __m128i ab03 = _mm_setzero_si128();
            for (; i < size32; i += 32, o += 16)
            {
                a0 = _mm_and_si128(_mm_loadu_si128((__m128i*)(A[0] + o)), K8_0F);

                b0 = _mm_and_si128(_mm_loadu_si128((__m128i*)(B[0] + o)), K8_0F);
                ab00 = _mm_add_epi32(ab00, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));

                b0 = _mm_and_si128(_mm_loadu_si128((__m128i*)(B[1] + o)), K8_0F);
                ab01 = _mm_add_epi32(ab01, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));

                b0 = _mm_and_si128(_mm_loadu_si128((__m128i*)(B[2] + o)), K8_0F);
                ab02 = _mm_add_epi32(ab02, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));

                b0 = _mm_and_si128(_mm_loadu_si128((__m128i*)(B[3] + o)), K8_0F);
                ab03 = _mm_add_epi32(ab03, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));

                a0 = _mm_and_si128(_mm_srli_epi16(_mm_loadu_si128((__m128i*)(A[0] + o)), 4), K8_0F);

                b0 = _mm_and_si128(_mm_srli_epi16(_mm_loadu_si128((__m128i*)(B[0] + o)), 4), K8_0F);
                ab00 = _mm_add_epi32(ab00, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));

                b0 = _mm_and_si128(_mm_srli_epi16(_mm_loadu_si128((__m128i*)(B[1] + o)), 4), K8_0F);
                ab01 = _mm_add_epi32(ab01, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));

                b0 = _mm_and_si128(_mm_srli_epi16(_mm_loadu_si128((__m128i*)(B[2] + o)), 4), K8_0F);
                ab02 = _mm_add_epi32(ab02, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));

                b0 = _mm_and_si128(_mm_srli_epi16(_mm_loadu_si128((__m128i*)(B[3] + o)), 4), K8_0F);
                ab03 = _mm_add_epi32(ab03, _mm_madd_epi16(_mm_maddubs_epi16(a0, b0), K16_0001));
            }
            for (; i < size; i += 8, o += 4)
            {
                a0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(A[0] + o)), C4_SHFL0), C4_MULLO), 12);

                b0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[0] + o)), C4_SHFL0), C4_MULLO), 12);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a0, b0), ab00);

                b0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[1] + o)), C4_SHFL0), C4_MULLO), 12);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a0, b0), ab01);

                b0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[2] + o)), C4_SHFL0), C4_MULLO), 12);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a0, b0), ab02);

                b0 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[3] + o)), C4_SHFL0), C4_MULLO), 12);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a0, b0), ab03);
            }
            __m128 ab0 = _mm_cvtepi32_ps(Extract4Sums(ab00, ab01, ab02, ab03));
            DecodeCosineDistances1x4(A[0], B, ab0, distances + 0 * stride);
        }

        template<> void MicroCosineDistancesDirect1x4<5>(const uint8_t* const* A, const uint8_t* const* B, size_t size, float* distances, size_t stride)
        {
            size_t i = 0, size16 = AlignLo(size, 16), o = 16;
            __m128i a00, a01, b00, b01;
            __m128i ab00 = _mm_setzero_si128();
            __m128i ab01 = _mm_setzero_si128();
            __m128i ab02 = _mm_setzero_si128();
            __m128i ab03 = _mm_setzero_si128();
            for (; i < size16; i += 16, o += 10)
            {
                a01 = _mm_loadu_si128((__m128i*)(A[0] + o));
                a00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(a01, C5_SHFL0), C5_MULLO), 11);
                a01 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(a01, 5), C5_SHFL0), C5_MULLO), 11);

                b01 = _mm_loadu_si128((__m128i*)(B[0] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C5_SHFL0), C5_MULLO), 11);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 5), C5_SHFL0), C5_MULLO), 11);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab00);

                b01 = _mm_loadu_si128((__m128i*)(B[1] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C5_SHFL0), C5_MULLO), 11);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 5), C5_SHFL0), C5_MULLO), 11);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab01);

                b01 = _mm_loadu_si128((__m128i*)(B[2] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C5_SHFL0), C5_MULLO), 11);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 5), C5_SHFL0), C5_MULLO), 11);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab02);

                b01 = _mm_loadu_si128((__m128i*)(B[3] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C5_SHFL0), C5_MULLO), 11);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 5), C5_SHFL0), C5_MULLO), 11);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab03);
            }
            for (; i < size; i += 8, o += 5)
            {
                a00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(A[0] + o)), C5_SHFL0), C5_MULLO), 11);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[0] + o)), C5_SHFL0), C5_MULLO), 11);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[1] + o)), C5_SHFL0), C5_MULLO), 11);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[2] + o)), C5_SHFL0), C5_MULLO), 11);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[3] + o)), C5_SHFL0), C5_MULLO), 11);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
            }
            __m128 ab0 = _mm_cvtepi32_ps(Extract4Sums(ab00, ab01, ab02, ab03));
            DecodeCosineDistances1x4(A[0], B, ab0, distances + 0 * stride);
        }

        template<> void MicroCosineDistancesDirect1x4<6>(const uint8_t* const* A, const uint8_t* const* B, size_t size, float* distances, size_t stride)
        {
            size_t i = 0, size16 = AlignLo(size, 16), o = 16;
            __m128i a00, a01, b00, b01;
            __m128i ab00 = _mm_setzero_si128();
            __m128i ab01 = _mm_setzero_si128();
            __m128i ab02 = _mm_setzero_si128();
            __m128i ab03 = _mm_setzero_si128();
            for (; i < size16; i += 16, o += 12)
            {
                a01 = _mm_loadu_si128((__m128i*)(A[0] + o));
                a00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(a01, C6_SHFL0), C6_MULLO), 10);
                a01 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(a01, 6), C6_SHFL0), C6_MULLO), 10);

                b01 = _mm_loadu_si128((__m128i*)(B[0] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C6_SHFL0), C6_MULLO), 10);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 6), C6_SHFL0), C6_MULLO), 10);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab00);

                b01 = _mm_loadu_si128((__m128i*)(B[1] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C6_SHFL0), C6_MULLO), 10);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 6), C6_SHFL0), C6_MULLO), 10);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab01);

                b01 = _mm_loadu_si128((__m128i*)(B[2] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C6_SHFL0), C6_MULLO), 10);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 6), C6_SHFL0), C6_MULLO), 10);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab02);

                b01 = _mm_loadu_si128((__m128i*)(B[3] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C6_SHFL0), C6_MULLO), 10);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 6), C6_SHFL0), C6_MULLO), 10);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab03);
            }
            for (; i < size; i += 8, o += 6)
            {
                a00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(A[0] + o)), C6_SHFL0), C6_MULLO), 10);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[0] + o)), C6_SHFL0), C6_MULLO), 10);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[1] + o)), C6_SHFL0), C6_MULLO), 10);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[2] + o)), C6_SHFL0), C6_MULLO), 10);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[3] + o)), C6_SHFL0), C6_MULLO), 10);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
            }
            __m128 ab0 = _mm_cvtepi32_ps(Extract4Sums(ab00, ab01, ab02, ab03));
            DecodeCosineDistances1x4(A[0], B, ab0, distances + 0 * stride);
        }

        template<> void MicroCosineDistancesDirect1x4<7>(const uint8_t* const* A, const uint8_t* const* B, size_t size, float* distances, size_t stride)
        {
            size_t i = 0, size16 = AlignLo(size, 16), o = 16;
            __m128i a00, a01, b00, b01;
            __m128i ab00 = _mm_setzero_si128();
            __m128i ab01 = _mm_setzero_si128();
            __m128i ab02 = _mm_setzero_si128();
            __m128i ab03 = _mm_setzero_si128();
            for (; i < size16; i += 16, o += 14)
            {
                a01 = _mm_loadu_si128((__m128i*)(A[0] + o));
                a00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(a01, C7_SHFL0), C7_MULLO), 9);
                a01 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(a01, 7), C7_SHFL0), C7_MULLO), 9);

                b01 = _mm_loadu_si128((__m128i*)(B[0] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C7_SHFL0), C7_MULLO), 9);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 7), C7_SHFL0), C7_MULLO), 9);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab00);

                b01 = _mm_loadu_si128((__m128i*)(B[1] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C7_SHFL0), C7_MULLO), 9);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 7), C7_SHFL0), C7_MULLO), 9);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab01);

                b01 = _mm_loadu_si128((__m128i*)(B[2] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C7_SHFL0), C7_MULLO), 9);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 7), C7_SHFL0), C7_MULLO), 9);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab02);

                b01 = _mm_loadu_si128((__m128i*)(B[3] + o));
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(b01, C7_SHFL0), C7_MULLO), 9);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_srli_si128(b01, 7), C7_SHFL0), C7_MULLO), 9);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab03);
            }
            for (; i < size; i += 8, o += 7)
            {
                a00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(A[0] + o)), C7_SHFL0), C7_MULLO), 9);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[0] + o)), C7_SHFL0), C7_MULLO), 9);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[1] + o)), C7_SHFL0), C7_MULLO), 9);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[2] + o)), C7_SHFL0), C7_MULLO), 9);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);

                b00 = _mm_srli_epi16(_mm_mullo_epi16(_mm_shuffle_epi8(_mm_loadl_epi64((__m128i*)(B[3] + o)), C7_SHFL0), C7_MULLO), 9);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
            }
            __m128 ab0 = _mm_cvtepi32_ps(Extract4Sums(ab00, ab01, ab02, ab03));
            DecodeCosineDistances1x4(A[0], B, ab0, distances + 0 * stride);
        }

        template<> void MicroCosineDistancesDirect1x4<8>(const uint8_t* const* A, const uint8_t* const* B, size_t size, float* distances, size_t stride)
        {
            size_t i = 0, size16 = AlignLo(size, 16), o = 16;
            __m128i a00, a01, b00, b01;
            __m128i ab00 = _mm_setzero_si128();
            __m128i ab01 = _mm_setzero_si128();
            __m128i ab02 = _mm_setzero_si128();
            __m128i ab03 = _mm_setzero_si128();
            for (; i < size16; i += 16, o += 16)
            {
                a01 = _mm_loadu_si128((__m128i*)(A[0] + o));
                a00 = UnpackU8<0>(a01);
                a01 = UnpackU8<1>(a01);

                b01 = _mm_loadu_si128((__m128i*)(B[0] + o));
                b00 = UnpackU8<0>(b01);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);
                b00 = UnpackU8<1>(b01);
                ab00 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab00);

                b01 = _mm_loadu_si128((__m128i*)(B[1] + o));
                b00 = UnpackU8<0>(b01);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);
                b00 = UnpackU8<1>(b01);
                ab01 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab01);

                b01 = _mm_loadu_si128((__m128i*)(B[2] + o));
                b00 = UnpackU8<0>(b01);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);
                b00 = UnpackU8<1>(b01);
                ab02 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab02);

                b01 = _mm_loadu_si128((__m128i*)(B[3] + o));
                b00 = UnpackU8<0>(b01);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
                b00 = UnpackU8<1>(b01);
                ab03 = _mm_add_epi32(_mm_madd_epi16(a01, b00), ab03);
            }
            for (; i < size; i += 8, o += 8)
            {
                a00 = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i*)(A[0] + o)));

                b00 = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i*)(B[0] + o)));
                ab00 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab00);

                b00 = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i*)(B[1] + o)));
                ab01 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab01);

                b00 = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i*)(B[2] + o)));
                ab02 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab02);

                b00 = _mm_cvtepu8_epi16(_mm_loadl_epi64((__m128i*)(B[3] + o)));
                ab03 = _mm_add_epi32(_mm_madd_epi16(a00, b00), ab03);
            }
            __m128 ab0 = _mm_cvtepi32_ps(Extract4Sums(ab00, ab01, ab02, ab03));
            DecodeCosineDistances1x4(A[0], B, ab0, distances + 0 * stride);
        }

        template<int bits> void MacroCosineDistancesDirect(size_t M, size_t N, const uint8_t* const* A, const uint8_t* const* B, size_t size, float* distances, size_t stride)
        {
            size_t M2 = AlignLoAny(M, 2);
            size_t N4 = AlignLo(N, 4);
            size_t i = 0;
            for (; i < M2; i += 2)
            {
                size_t j = 0;
                for (; j < N4; j += 4)
                    MicroCosineDistancesDirect2x4<bits>(A + i, B + j, size, distances + j, stride);
                for (; j < N; j += 1)
                {
                    CosineDistance<bits>(A[i + 0], B[j], size, distances + j + 0 * stride);
                    CosineDistance<bits>(A[i + 1], B[j], size, distances + j + 1 * stride);
                }
                distances += 2 * stride;
            }
            for (; i < M; i++)
            {
                size_t j = 0;
                for (; j < N4; j += 4)
                    MicroCosineDistancesDirect1x4<bits>(A + i, B + j, size, distances + j, stride);
                for (; j < N; j += 1)
                    CosineDistance<bits>(A[i], B[j], size, distances + j);
                distances += 1 * stride;
            }
        }

        //-------------------------------------------------------------------------------------------------

        static void UnpackNormA(size_t count, const uint8_t* const* src, float* dst, size_t stride)
        {
            for (size_t i = 0; i < count; ++i)
                _mm_storeu_si128((__m128i*)dst + i, _mm_loadu_si128((__m128i*)src[i]));
        }

        //-------------------------------------------------------------------------------------------------


        static void UnpackNormB(size_t count, const uint8_t* const* src, float* dst, size_t stride)
        {
            size_t count4 = AlignLo(count, 4), i = 0;
            for (; i < count4; i += 4, src += 4, dst += 4)
            {
                __m128 s0 = _mm_loadu_ps((float*)src[0]);
                __m128 s1 = _mm_loadu_ps((float*)src[1]);
                __m128 s2 = _mm_loadu_ps((float*)src[2]);
                __m128 s3 = _mm_loadu_ps((float*)src[3]);
                __m128 s00 = _mm_unpacklo_ps(s0, s2);
                __m128 s01 = _mm_unpacklo_ps(s1, s3);
                __m128 s10 = _mm_unpackhi_ps(s0, s2);
                __m128 s11 = _mm_unpackhi_ps(s1, s3);
                _mm_storeu_ps(dst + 0 * stride, _mm_unpacklo_ps(s00, s01));
                _mm_storeu_ps(dst + 1 * stride, _mm_unpackhi_ps(s00, s01));
                _mm_storeu_ps(dst + 2 * stride, _mm_unpacklo_ps(s10, s11));
                _mm_storeu_ps(dst + 3 * stride, _mm_unpackhi_ps(s10, s11));
            }
            for (; i < count; i++, src++, dst++)
            {
                dst[0 * stride] = ((float*)src)[0];
                dst[1 * stride] = ((float*)src)[1];
                dst[2 * stride] = ((float*)src)[2];
                dst[3 * stride] = ((float*)src)[3];
            }
        }

        //-------------------------------------------------------------------------------------------------

        static void UnpackDataA8(size_t count, const uint8_t* const* src, size_t size, uint8_t* dst, size_t stride)
        {
            size_t size16 = AlignLo(size, 16);
            for (size_t i = 0, j; i < count; i++)
            {
                const uint8_t* ps = src[i] + 16;
                uint16_t* pd = (uint16_t*)dst + i * size;
                for (j = 0; j < size16; j += 16, ps += 16, pd += 16)
                {
                    __m128i s = _mm_loadu_si128((__m128i*)ps);
                    _mm_storeu_si128((__m128i*)pd + 0, UnpackU8<0>(s));
                    _mm_storeu_si128((__m128i*)pd + 1, UnpackU8<1>(s));
                }
                for (; j < size; j += 8, ps += 8, pd += 8)
                {
                    __m128i s = _mm_loadl_epi64((__m128i*)ps);
                    _mm_storeu_si128((__m128i*)pd, UnpackU8<0>(s));
                }
            }
        }

        //-------------------------------------------------------------------------------------------------

        SIMD_INLINE void UnpackDataB8x4(const uint8_t* const* src, size_t offset, uint8_t* dst)
        {
            __m128i a0 = UnpackU8<0>(_mm_loadl_epi64((__m128i*)(src[0] + offset)));
            __m128i a1 = UnpackU8<0>(_mm_loadl_epi64((__m128i*)(src[1] + offset)));
            __m128i a2 = UnpackU8<0>(_mm_loadl_epi64((__m128i*)(src[2] + offset)));
            __m128i a3 = UnpackU8<0>(_mm_loadl_epi64((__m128i*)(src[3] + offset)));
            __m128i b0 = _mm_unpacklo_epi32(a0, a2);
            __m128i b1 = _mm_unpacklo_epi32(a1, a3);
            __m128i b2 = _mm_unpackhi_epi32(a0, a2);
            __m128i b3 = _mm_unpackhi_epi32(a1, a3);
            _mm_storeu_si128((__m128i*)dst + 0, _mm_unpacklo_epi32(b0, b1));
            _mm_storeu_si128((__m128i*)dst + 2, _mm_unpackhi_epi32(b0, b1));
            _mm_storeu_si128((__m128i*)dst + 4, _mm_unpacklo_epi32(b2, b3));
            _mm_storeu_si128((__m128i*)dst + 6, _mm_unpackhi_epi32(b2, b3));
        }

        static void UnpackDataB8(size_t count, const uint8_t* const* src, size_t size, uint8_t* dst, size_t stride)
        {
            size_t count8 = AlignLo(count, 8), i;
            for (i = 0, size += 16; i < count8; i += 8, src += 8)
            {
                for (size_t j = 16; j < size; j += 8, dst += 8 * A)
                {
                    UnpackDataB8x4(src + 0, j, dst + 0);
                    UnpackDataB8x4(src + 4, j, dst + A);
                }
            }
            if (i < count)
            {
                const uint8_t* _src[8];
                for (size_t j = 0; j < 8; i++, j++)
                    _src[j] = i < count ? *src++ : src[-1];
                for (size_t j = 16; j < size; j += 8, dst += 8 * A)
                {
                    UnpackDataB8x4(_src + 0, j, dst + 0);
                    UnpackDataB8x4(_src + 4, j, dst + A);
                }
            }
        }

        //-------------------------------------------------------------------------------------------------

        SIMD_INLINE __m128i Set2(const int16_t* src)
        {
            return _mm_set1_epi32(*(int32_t*)src);
        }

        SIMD_INLINE void Madd2(__m128i& ab, __m128i a, __m128i b)
        {
            ab = _mm_add_epi32(ab, _mm_madd_epi16(a, b));
        }

        template<int M> void Correlation16_2xM(size_t N, size_t K, const int16_t* ad0, const int16_t* bd, const float *an, const float *bn, size_t bnStride, float* distances, size_t stride)
        {
            __m128i ab00, ab01, ab10, ab11, ab20, ab21, ab30, ab31, ab40, ab41, ab50, ab51, a0, b0, b1;
            const int16_t* ad1 = ad0 + 1 * K;
            const int16_t* ad2 = ad0 + 2 * K;
            const int16_t* ad3 = ad0 + 3 * K;
            const int16_t* ad4 = ad0 + 4 * K;
            const int16_t* ad5 = ad0 + 5 * K;
            if (N > 4)
            {
                if (M > 0) ab00 = _mm_setzero_si128(), ab01 = _mm_setzero_si128();
                if (M > 1) ab10 = _mm_setzero_si128(), ab11 = _mm_setzero_si128();
                if (M > 2) ab20 = _mm_setzero_si128(), ab21 = _mm_setzero_si128();
                if (M > 3) ab30 = _mm_setzero_si128(), ab31 = _mm_setzero_si128();
                if (M > 4) ab40 = _mm_setzero_si128(), ab41 = _mm_setzero_si128();
                if (M > 5) ab50 = _mm_setzero_si128(), ab51 = _mm_setzero_si128();
                for (size_t k = 0; k < K; k += 2)
                {
                    b0 = _mm_loadu_si128((__m128i*)bd + 0);
                    b1 = _mm_loadu_si128((__m128i*)bd + 1);
                    if (M > 0) a0 = Set2(ad0 + k), Madd2(ab00, a0, b0), Madd2(ab01, a0, b1);
                    if (M > 1) a0 = Set2(ad1 + k), Madd2(ab10, a0, b0), Madd2(ab11, a0, b1);
                    if (M > 2) a0 = Set2(ad2 + k), Madd2(ab20, a0, b0), Madd2(ab21, a0, b1);
                    if (M > 3) a0 = Set2(ad3 + k), Madd2(ab30, a0, b0), Madd2(ab31, a0, b1);
                    if (M > 4) a0 = Set2(ad4 + k), Madd2(ab40, a0, b0), Madd2(ab41, a0, b1);
                    if (M > 5) a0 = Set2(ad5 + k), Madd2(ab50, a0, b0), Madd2(ab51, a0, b1);
                    bd += 16;
                } 
                if (N == 8)
                {
                    if (M > 0) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab00, distances + 0), DecodeCosineDistances1x4(an, bn + 4, bnStride, ab01, distances + 4), an += 4, distances += stride;
                    if (M > 1) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab10, distances + 0), DecodeCosineDistances1x4(an, bn + 4, bnStride, ab11, distances + 4), an += 4, distances += stride;
                    if (M > 2) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab20, distances + 0), DecodeCosineDistances1x4(an, bn + 4, bnStride, ab21, distances + 4), an += 4, distances += stride;
                    if (M > 3) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab30, distances + 0), DecodeCosineDistances1x4(an, bn + 4, bnStride, ab31, distances + 4), an += 4, distances += stride;
                    if (M > 4) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab40, distances + 0), DecodeCosineDistances1x4(an, bn + 4, bnStride, ab41, distances + 4), an += 4, distances += stride;
                    if (M > 5) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab50, distances + 0), DecodeCosineDistances1x4(an, bn + 4, bnStride, ab51, distances + 4), an += 4, distances += stride;
                }
                else
                {
                    if (M > 0) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab00, distances + 0), DecodeCosineDistances1x4(an, bn + 4, bnStride, ab01, distances + 4, N - 4), an += 4, distances += stride;
                    if (M > 1) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab10, distances + 0), DecodeCosineDistances1x4(an, bn + 4, bnStride, ab11, distances + 4, N - 4), an += 4, distances += stride;
                    if (M > 2) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab20, distances + 0), DecodeCosineDistances1x4(an, bn + 4, bnStride, ab21, distances + 4, N - 4), an += 4, distances += stride;
                    if (M > 3) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab30, distances + 0), DecodeCosineDistances1x4(an, bn + 4, bnStride, ab31, distances + 4, N - 4), an += 4, distances += stride;
                    if (M > 4) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab40, distances + 0), DecodeCosineDistances1x4(an, bn + 4, bnStride, ab41, distances + 4, N - 4), an += 4, distances += stride;
                    if (M > 5) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab50, distances + 0), DecodeCosineDistances1x4(an, bn + 4, bnStride, ab51, distances + 4, N - 4), an += 4, distances += stride;
                }
            }
            else
            {
                if (M > 0) ab00 = _mm_setzero_si128();
                if (M > 1) ab10 = _mm_setzero_si128();
                if (M > 2) ab20 = _mm_setzero_si128();
                if (M > 3) ab30 = _mm_setzero_si128();
                if (M > 4) ab40 = _mm_setzero_si128();
                if (M > 5) ab50 = _mm_setzero_si128();
                for (size_t k = 0; k < K; k += 2)
                {
                    b0 = _mm_loadu_si128((__m128i*)bd + 0);
                    if (M > 0) a0 = Set2(ad0 + k), Madd2(ab00, a0, b0);
                    if (M > 1) a0 = Set2(ad1 + k), Madd2(ab10, a0, b0);
                    if (M > 2) a0 = Set2(ad2 + k), Madd2(ab20, a0, b0);
                    if (M > 3) a0 = Set2(ad3 + k), Madd2(ab30, a0, b0);
                    if (M > 4) a0 = Set2(ad4 + k), Madd2(ab40, a0, b0);
                    if (M > 5) a0 = Set2(ad5 + k), Madd2(ab50, a0, b0);
                    bd += 16;
                }
                if (N == 4)
                {
                    if (M > 0) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab00, distances + 0), an += 4, distances += stride;
                    if (M > 1) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab10, distances + 0), an += 4, distances += stride;
                    if (M > 2) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab20, distances + 0), an += 4, distances += stride;
                    if (M > 3) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab30, distances + 0), an += 4, distances += stride;
                    if (M > 4) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab40, distances + 0), an += 4, distances += stride;
                    if (M > 5) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab50, distances + 0), an += 4, distances += stride;
                }
                else
                {
                    if (M > 0) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab00, distances + 0, N), an += 4, distances += stride;
                    if (M > 1) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab10, distances + 0, N), an += 4, distances += stride;
                    if (M > 2) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab20, distances + 0, N), an += 4, distances += stride;
                    if (M > 3) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab30, distances + 0, N), an += 4, distances += stride;
                    if (M > 4) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab40, distances + 0, N), an += 4, distances += stride;
                    if (M > 5) DecodeCosineDistances1x4(an, bn + 0, bnStride, ab50, distances + 0, N), an += 4, distances += stride;
                }
            }
        }

        typedef void(*Correlation16_2xM_Ptr)(size_t N, size_t K, const int16_t* ad0, const int16_t* bd, const float* an, const float* bn, size_t bnStride, float* distances, size_t stride);

        SIMD_INLINE Correlation16_2xM_Ptr GetCorrelation16_2xM(size_t M)
        {
            switch (M)
            {
            case 0: return NULL;
            case 1: return Correlation16_2xM<1>;
            case 2: return Correlation16_2xM<2>;
            case 3: return Correlation16_2xM<3>;
            case 4: return Correlation16_2xM<4>;
            case 5: return Correlation16_2xM<5>;
            case 6: return Correlation16_2xM<6>;
            }
            assert(0);
            return NULL;
        }

        void MacroCorrelation16(size_t M, size_t N, size_t K, const uint8_t* ad, const float* an, const uint8_t* bd, const float* bn, float* distances, size_t stride)
        {
            size_t M6 = AlignLoAny(M, 6);
            Correlation16_2xM_Ptr correlation_2x6 = GetCorrelation16_2xM(6);
            Correlation16_2xM_Ptr correlation_2xT = GetCorrelation16_2xM(M - M6);
            const int16_t* a = (int16_t*)ad;
            const int16_t* b = (int16_t*)bd;
            for (size_t j = 0; j < N; j += 8)
            {
                size_t dN = Simd::Min<size_t>(8, N - j);
                size_t i = 0;
                for (; i < M6; i += 6)
                    correlation_2x6(dN, K, a + i * K, b, an + i * 4, bn, N, distances + i * stride, stride);
                if(i < M)
                    correlation_2xT(dN, K, a + i * K, b, an + i * 4, bn, N, distances + i * stride, stride);
                b += K * 8;
                bn += 8;
                distances += 8;
            }
        }

        //-------------------------------------------------------------------------------------------------

        DescrInt::DescrInt(size_t size, size_t depth)
            : Base::DescrInt(size, depth)
        {
            _minMax32f = MinMax32f;
            _minMax16f = MinMax16f;
            _unpackNormA = UnpackNormA;
            _unpackNormB = UnpackNormB;
            _microMd = 2;
            _microNd = 4;
            _unpSize = _size * (_depth == 8 ? 2 : 1);
            _microMu = 5;
            _microNu = 8;
            switch (depth)
            {
            case 4:
            {
                _encode32f = Encode32f4;
                _encode16f = Encode16f4;
                _decode32f = Decode32f4;
                _decode16f = Decode16f4;
                _cosineDistance = Sse41::CosineDistance<4>;
                _macroCosineDistancesDirect = Sse41::MacroCosineDistancesDirect<4>;
                break;
            }
            case 5:
            {
                _encode32f = Encode32f5;
                _encode16f = Encode16f5;
                _decode32f = Decode32f5;
                _decode16f = Decode16f5;
                _cosineDistance = Sse41::CosineDistance<5>;
                _macroCosineDistancesDirect = Sse41::MacroCosineDistancesDirect<5>;
                break;
            }
            case 6:
            {
                _encode32f = Encode32f6;
                _encode16f = Encode16f6;
                _decode32f = Decode32f6;
                _decode16f = Decode16f6;
                _cosineDistance = Sse41::CosineDistance<6>;
                _macroCosineDistancesDirect = Sse41::MacroCosineDistancesDirect<6>;
                break;
            }
            case 7:
            {
                _encode32f = Encode32f7;
                _encode16f = Encode16f7;
                _decode32f = Decode32f7;
                _decode16f = Decode16f7;
                _cosineDistance = Sse41::CosineDistance<7>;
                _macroCosineDistancesDirect = Sse41::MacroCosineDistancesDirect<7>;
                break;
            }
            case 8:
            {
                _encode32f = Encode32f8;
                _encode16f = Encode16f8;
                _decode32f = Decode32f8;
                _decode16f = Decode16f8;
                _cosineDistance = Sse41::CosineDistance<8>;
                _macroCosineDistancesDirect = Sse41::MacroCosineDistancesDirect<8>;
                _unpackDataA = UnpackDataA8;
                _unpackDataB = UnpackDataB8;
                _macroCosineDistancesUnpack = MacroCorrelation16;
                break;
            }
            default:
                assert(0);
            }
        }

        void DescrInt::CosineDistancesMxNa(size_t M, size_t N, const uint8_t* const* A, const uint8_t* const* B, float* distances) const
        {
            if(_unpSize * _microNu > Base::AlgCacheL1() || N * 2 < _microNu || 1)
                CosineDistancesDirect(M, N, A, B, distances);
            else
                CosineDistancesUnpack(M, N, A, B, distances);
        }

        void DescrInt::CosineDistancesMxNp(size_t M, size_t N, const uint8_t* A, const uint8_t* B, float* distances) const
        {
            Array8ucp a(M);
            for (size_t i = 0; i < M; ++i) 
                a[i] = A + i * _encSize;
            Array8ucp b(N);
            for (size_t j = 0; j < N; ++j)
                b[j] = B + j * _encSize;
            CosineDistancesMxNa(M, N, a.data, b.data, distances);
        }

        void DescrInt::CosineDistancesDirect(size_t M, size_t N, const uint8_t* const* A, const uint8_t* const* B, float* distances) const
        {
            const size_t L2 = Base::AlgCacheL2();
            size_t mN = AlignLoAny(L2 / _encSize, _microNd);
            size_t mM = AlignLoAny(L2 / _encSize, _microMd);
            for (size_t i = 0; i < M; i += mM)
            {
                size_t dM = Simd::Min(M, i + mM) - i;
                for (size_t j = 0; j < N; j += mN)
                {
                    size_t dN = Simd::Min(N, j + mN) - j;
                    _macroCosineDistancesDirect(dM, dN, A + i, B + j, _size, distances + i * N + j, N);
                }
            }
        }

        void DescrInt::CosineDistancesUnpack(size_t M, size_t N, const uint8_t* const* A, const uint8_t* const* B, float* distances) const
        {
            size_t macroM = AlignLoAny(Base::AlgCacheL2() / _unpSize, _microMu);
            size_t macroN = AlignLoAny(Base::AlgCacheL3() / _unpSize, _microNu);
            Array8u dA(Min(macroM, M) * _unpSize);
            Array8u dB(Min(macroN, N) * _unpSize);
            Array32f nA(Min(macroM, M) * 4);
            Array32f nB(AlignHi(Min(macroN, N), _microNu) * 4);
            for (size_t i = 0; i < M; i += macroM)
            {
                size_t dM = Simd::Min(M, i + macroM) - i;
                _unpackNormA(dM, A + i, nA.data, 1);
                _unpackDataA(dM, A + i, _size, dA.data, _unpSize);
                for (size_t j = 0; j < N; j += macroN)
                {
                    size_t dN = Simd::Min(N, j + macroN) - j;
                    _unpackNormB(dN, B + j, nB.data, dN);
                    _unpackDataB(dN, B + j, _size, dB.data, 1);
                    _macroCosineDistancesUnpack(dM, dN, _size, dA.data, nA.data, dB.data, nB.data, distances + i * N + j, N);
                }
            }
        }

        //-------------------------------------------------------------------------------------------------

        void* DescrIntInit(size_t size, size_t depth)
        {
            if (!Base::DescrInt::Valid(size, depth))
                return NULL;
            return new Sse41::DescrInt(size, depth);
        }
    }
#endif
}
