#include "vectors.hpp"

namespace runner {

const Vector kVectors[] = {
#include "vectors.inc"
};

const std::uint32_t kVectorCount = sizeof(kVectors) / sizeof(kVectors[0]);

}  // namespace runner
