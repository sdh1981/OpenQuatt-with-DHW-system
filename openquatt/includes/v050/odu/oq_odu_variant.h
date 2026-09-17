#pragma once

// Uitgedund uit upstream OpenQuatt v0.50.0 (openquatt/includes/odu/oq_odu_generation.h):
// alleen de enum. De detectie daar (compressorcode, PCB-programma, klantmodel)
// is er voor V1/V1.5/V2-installaties door elkaar; deze fork draait vast op V1.5.
// Waarden en volgorde gelijk aan upstream, zodat overgenomen logica ze kan vergelijken.

#include <cstdint>

namespace oq_odu {

enum class Variant : uint8_t {
  UNKNOWN = 0,
  V1,
  V1_5,
  V2_OLD_MODEL,
  V2_NEW_MODEL,
};

}  // namespace oq_odu
