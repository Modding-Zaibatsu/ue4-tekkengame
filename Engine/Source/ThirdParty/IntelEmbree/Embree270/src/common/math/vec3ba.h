// ======================================================================== //
// Copyright 2009-2015 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "math.h"
#include "../simd/sse.h"

namespace embree
{
  ////////////////////////////////////////////////////////////////////////////////
  /// SSE Vec3ba Type
  ////////////////////////////////////////////////////////////////////////////////

  struct __aligned(16) Vec3ba
  {
    union {
      __m128 m128;
      struct { int x,y,z; int a; };
    };

    typedef int Scalar;
    enum { N = 3 };

    ////////////////////////////////////////////////////////////////////////////////
    /// Constructors, Assignment & Cast Operators
    ////////////////////////////////////////////////////////////////////////////////

    __forceinline Vec3ba( ) {}
    __forceinline Vec3ba( const __m128  input ) : m128(input) {}
    __forceinline Vec3ba( const Vec3ba& other ) : m128(other.m128) {}
    __forceinline Vec3ba& operator =(const Vec3ba& other) { m128 = other.m128; return *this; }

    __forceinline explicit Vec3ba ( bool  a )
      : m128(_mm_lookupmask_ps[(size_t(a) << 3) | (size_t(a) << 2) | (size_t(a) << 1) | size_t(a)]) {}
    __forceinline explicit Vec3ba ( bool  a, bool  b, bool  c)
      : m128(_mm_lookupmask_ps[(size_t(c) << 2) | (size_t(b) << 1) | size_t(a)]) {}

    __forceinline operator const __m128&( void ) const { return m128; }
    __forceinline operator       __m128&( void )       { return m128; }

    ////////////////////////////////////////////////////////////////////////////////
    /// Constants
    ////////////////////////////////////////////////////////////////////////////////

    __forceinline Vec3ba( FalseTy ) : m128(_mm_setzero_ps()) {}
    __forceinline Vec3ba( TrueTy  ) : m128(_mm_castsi128_ps(_mm_cmpeq_epi32(_mm_setzero_si128(), _mm_setzero_si128()))) {}

    ////////////////////////////////////////////////////////////////////////////////
    /// Array Access
    ////////////////////////////////////////////////////////////////////////////////

    __forceinline const int& operator []( const size_t index ) const { assert(index < 3); return (&x)[index]; }
    __forceinline       int& operator []( const size_t index )       { assert(index < 3); return (&x)[index]; }
  };


  ////////////////////////////////////////////////////////////////////////////////
  /// Unary Operators
  ////////////////////////////////////////////////////////////////////////////////

  __forceinline const Vec3ba operator !( const Vec3ba& a ) { return _mm_xor_ps(a.m128, Vec3ba(embree::True)); }

  ////////////////////////////////////////////////////////////////////////////////
  /// Binary Operators
  ////////////////////////////////////////////////////////////////////////////////

  __forceinline const Vec3ba operator &( const Vec3ba& a, const Vec3ba& b ) { return _mm_and_ps(a.m128, b.m128); }
  __forceinline const Vec3ba operator |( const Vec3ba& a, const Vec3ba& b ) { return _mm_or_ps (a.m128, b.m128); }
  __forceinline const Vec3ba operator ^( const Vec3ba& a, const Vec3ba& b ) { return _mm_xor_ps(a.m128, b.m128); }
  
  ////////////////////////////////////////////////////////////////////////////////
  /// Assignment Operators
  ////////////////////////////////////////////////////////////////////////////////
  
  __forceinline const Vec3ba operator &=( Vec3ba& a, const Vec3ba& b ) { return a = a & b; }
  __forceinline const Vec3ba operator |=( Vec3ba& a, const Vec3ba& b ) { return a = a | b; }
  __forceinline const Vec3ba operator ^=( Vec3ba& a, const Vec3ba& b ) { return a = a ^ b; }
  
  ////////////////////////////////////////////////////////////////////////////////
  /// Comparison Operators + Select
  ////////////////////////////////////////////////////////////////////////////////
  
  __forceinline bool operator ==( const Vec3ba& a, const Vec3ba& b ) { 
    return (_mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(_mm_castps_si128(a.m128), _mm_castps_si128(b.m128)))) & 7) == 7; 
  }
  __forceinline bool operator !=( const Vec3ba& a, const Vec3ba& b ) { 
    return (_mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(_mm_castps_si128(a.m128), _mm_castps_si128(b.m128)))) & 7) != 7; 
  }
  __forceinline bool operator < ( const Vec3ba& a, const Vec3ba& b ) {
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    if (a.z != b.z) return a.z < b.z;
    return false;
  }

  ////////////////////////////////////////////////////////////////////////////////
  /// Reduction Operations
  ////////////////////////////////////////////////////////////////////////////////
    
  __forceinline bool reduce_and( const Vec3ba& a ) { return (_mm_movemask_ps(a) & 0x7) == 0x7; }
  __forceinline bool reduce_or ( const Vec3ba& a ) { return (_mm_movemask_ps(a) & 0x7) != 0x0; }

  __forceinline bool all       ( const Vec3ba& b ) { return (_mm_movemask_ps(b) & 0x7) == 0x7; }
  __forceinline bool any       ( const Vec3ba& b ) { return (_mm_movemask_ps(b) & 0x7) != 0x0; }
  __forceinline bool none      ( const Vec3ba& b ) { return (_mm_movemask_ps(b) & 0x7) == 0x0; }

  ////////////////////////////////////////////////////////////////////////////////
  /// Output Operators
  ////////////////////////////////////////////////////////////////////////////////

  inline std::ostream& operator<<(std::ostream& cout, const Vec3ba& a) {
    return cout << "(" << (a.x ? "1" : "0") << ", " << (a.y ? "1" : "0") << ", " << (a.z ? "1" : "0") << ")";
  }
}
