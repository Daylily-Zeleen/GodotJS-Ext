#pragma once

#include <cstdint>

namespace jsb::internal {
namespace ELogSeverity {
enum Type : uint8_t {
#pragma push_macro("DEF")
#undef DEF
#define DEF(FieldName) FieldName,
#include "jsb_log_severity.def.h"

#pragma pop_macro("DEF")
};
} //namespace ELogSeverity
} //namespace jsb::internal

