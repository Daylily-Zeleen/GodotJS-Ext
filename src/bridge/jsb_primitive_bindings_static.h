#pragma once
#include "jsb_bridge_pch.h"

namespace jsb {
#if JSB_WITH_STATIC_BINDINGS
class Environment;

void register_primitive_bindings_static(Environment *p_env);
#endif
} //namespace jsb

