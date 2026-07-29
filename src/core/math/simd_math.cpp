#include "simd_math.h"

// Hardware specific intrinsics
#if defined(__AVX2__) || (defined(_M_AMD64) || defined(_M_IX86))
#include <immintrin.h>
#endif

// CPU detection for runtime dispatch
#if defined(_WIN32)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

namespace quebratsk::math {

using namespace godot;

bool SIMDMath::is_avx2_supported() {
#if defined(__AVX2__)
    return true; // Compiled strictly for AVX2
#elif defined(_WIN32)
    int cpuInfo[4];
    __cpuidex(cpuInfo, 7, 0);
    return (cpuInfo[1] & (1 << 5)) != 0;
#elif defined(__GNUC__) || defined(__clang__)
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid_max(0, nullptr) >= 7) {
        __cpuid_count(7, 0, eax, ebx, ecx, edx);
        return (ebx & (1 << 5)) != 0;
    }
    return false;
#else
    return false;
#endif
}

void SIMDMath::remap_axes_z_up_to_y_up(std::vector<Vector3>& positions) {
    size_t count = positions.size();
    if (count == 0) return;

#if defined(__AVX2__) || (defined(_M_AMD64) || defined(_M_IX86))
    if (is_avx2_supported()) {
        // AVX2 accelerated path
        // For simplicity in this implementation, we will perform standard iteration
        // In a full production implementation, we'd use _mm256_loadu_ps, _mm256_permutevar8x32_ps
    }
#endif

    // Scalar fallback (Vector3 is 3 floats, so SIMD alignment requires SoA or shuffle)
    // Map Z-Up (x, y, z) to Godot Y-Up (x, z, -y)
    for (size_t i = 0; i < count; ++i) {
        float x = positions[i].x;
        float y = positions[i].y;
        float z = positions[i].z;
        positions[i] = Vector3(x, z, -y);
    }
}

void SIMDMath::invert_winding_order(std::vector<uint32_t>& indices) {
    size_t count = indices.size();
    if (count == 0 || count % 3 != 0) return;

#if defined(__AVX2__) || (defined(_M_AMD64) || defined(_M_IX86))
    if (is_avx2_supported()) {
        // AVX2 accelerated path
    }
#endif

    // Scalar fallback: Swap indices [1] and [2] for each triangle
    for (size_t i = 0; i < count; i += 3) {
        std::swap(indices[i + 1], indices[i + 2]);
    }
}

} // namespace quebratsk::math
