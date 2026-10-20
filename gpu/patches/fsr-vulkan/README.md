# Local changes to gpu/third_party/fsr-vulkan

The submodule points at upstream FireBurn/FSR-Vulkan (`c64f093`). `build.sh` applies the
patches here to its working tree when they are not applied yet:

- `0001-...`: `BB_FSR4_PROFILE` (GPU time per FSR 4 pass) and `BB_FSR4_STATS` (driver
  statistics of each pass) in the FSR 4 v07 provider.
- `0002-...`: `#include <mutex>` in the FFX Vulkan backend; libc++ (the Windows MSYS2 CLANG64
  build) does not include it transitively.
- `0003-...` and `0004-...` (from ducvd89/Bloodborne-Android, GPL-2.0): device-local images on
  unified-memory GPUs (Adreno/Turnip): FFX skipped every host-visible type for device-local
  requests, and there every allocatable type is one, so FSR 3 context creation failed; and frame
  generation frees its per-frame descriptor pools and constant buffers (on Turnip each was a
  mapped buffer that was never released, until mapping failed and the game crashed).
