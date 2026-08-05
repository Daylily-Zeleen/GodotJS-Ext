#pragma once

#include "jsb_module_loader.h"

namespace jsb {
// internal module 'godot-jsb'
class BridgeModuleLoader : public IModuleLoader {
public:
	virtual ~BridgeModuleLoader() override = default;

	virtual bool load(Environment *p_env, JavaScriptModule &p_module) override;
};

} //namespace jsb

