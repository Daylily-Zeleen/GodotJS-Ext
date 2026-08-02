#include "jsb_callable.h"

namespace jsb
{
    String JSCallable::get_as_text() const
    {
        return vformat("[JSFunction: object_id=%s, callback_id=%s]", object_id_.operator uint64_t(), callback_id_.to_string());
    }

    JSCallable::~JSCallable()
    {
        if (callback_id_)
        {
            if (const std::shared_ptr<jsb::Environment> env = jsb::Environment::_access(env_id_))
            {
                env->release_function(callback_id_);
            }
        }
    }

    void JSCallable::call(const Variant** p_arguments, int p_argcount, Variant& r_return_value, GDExtensionCallError& r_call_error) const
    {
        const std::shared_ptr<jsb::Environment> env = jsb::Environment::_access(env_id_);
        if (!env)
        {
            r_call_error.error = GDEXTENSION_CALL_ERROR_INVALID_METHOD;
            return;
        }

        Object* object_ptr = object_id_.is_null() ? nullptr : godot::ObjectDB::get_instance(object_id_);
        r_return_value = env->call_function(object_ptr, callback_id_, p_arguments, p_argcount, r_call_error);
    }
}
