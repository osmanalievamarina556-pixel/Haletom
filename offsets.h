#pragma once
#include <cstddef>
#include <cstdint>

// Source: dump.cs (il2cpp metadata v31, libil2cpp.so arm64). Offsets are instance-field offsets
// taken from the dump's "// 0xNN" comments. "FILL" entries are runtime values that a dump cannot
// give; 0 means unresolved and the dependent feature stays off.
namespace off {
// ---- verified from dump.cs ----
constexpr std::size_t kUnityObj_CachedPtr = 0x10;     // UnityEngine.Object.m_CachedPtr
// PlayerSkeleton (TypeDefIndex 1845)
constexpr std::size_t kSkel_HeadBone      = 0xB0;     // Rigidbody _headBone
constexpr std::size_t kSkel_PelvisBone    = 0xC0;     // Rigidbody _pelvisBone
// zkk (TypeDefIndex 226) per-player holder
constexpr std::size_t kHolder_Player      = 0x10;     // PlayerController lzl
constexpr std::size_t kHolder_Team        = 0x18;     // Team lzm  (None=0, CT=1, TR=2)
constexpr std::size_t kHolder_Skeleton    = 0x20;     // PlayerSkeleton lzn
// PlayerController (TypeDefIndex 228)
constexpr std::size_t kPC_FpsCamera       = 0x28;
constexpr std::size_t kPC_MainCamera      = 0x30;
constexpr std::size_t kPC_PhotonView      = 0x80;
// managed List<T> and il2cpp array
constexpr std::size_t kList_Items         = 0x10;
constexpr std::size_t kList_Size          = 0x18;
constexpr std::size_t kArr_Length         = 0x18;
constexpr std::size_t kArr_Data           = 0x20;

// ---- FILL: resolve on your build ----
// Static block that holds PlayerSkeleton.nld (the Casters list). Get the RVA by breaking on
// PlayerSkeleton's static ctor or the Casters getter in IDA/Ghidra; the static block pointer is
// what sits at libil2cpp+RVA. kStatic_CasterList_Off = offset of nld inside that block.
constexpr std::uintptr_t kStatic_CasterList_RVA = 0;
constexpr std::size_t    kStatic_CasterList_Off = 0;
// Pointer to the float[16] view*proj matrix of the active camera. Find with CE: move the camera,
// pointer-scan the 16 floats that change with the view.
constexpr std::uintptr_t kViewMatrix_RVA        = 0;

// ---- native layout (defaults = common Unity 2019+ TransformAccess; VERIFY with CE) ----
constexpr std::size_t kNative_ComponentTransform = 0x30;   // native Component -> native Transform
constexpr std::size_t kNative_TransformAccess    = 0x38;   // native Transform -> TransformAccess
constexpr std::size_t kTA_Transforms             = 0x0;    // TransformAccess.pTransforms (stride 0x30)
constexpr std::size_t kTA_Hierarchy              = 0x8;    // TransformAccess.hierarchy
constexpr std::size_t kTA_Index                  = 0x10;   // int index of this transform
constexpr std::size_t kHier_Parents              = 0x18;   // int* parent index per transform
}  // namespace off
