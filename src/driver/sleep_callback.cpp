#include "std_include.hpp"
#include "sleep_callback.hpp"
#include "exception.hpp"
#include "finally.hpp"

sleep_callback::sleep_callback(callback_function&& callback)
	: callback_(std::move(callback))
{
	PCALLBACK_OBJECT callback_object{};
	UNICODE_STRING callback_name = RTL_CONSTANT_STRING(L"\\Callback\\PowerState");
	OBJECT_ATTRIBUTES object_attributes = RTL_CONSTANT_OBJECT_ATTRIBUTES(
		&callback_name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE);

	const auto _ = utils::finally([&callback_object]()
	{
		ObDereferenceObject(callback_object);
	});

	const auto status = ExCreateCallback(&callback_object, &object_attributes, FALSE, TRUE);
	if (!NT_SUCCESS(status))
	{
		throw std::runtime_error("Unable to create callback object");
	}

	this->handle_ = ExRegisterCallback(callback_object, sleep_callback::static_callback, this);
	if (!this->handle_)
	{
		throw std::runtime_error("Unable to register callback");
	}
}

sleep_callback::~sleep_callback()
{
	if (this->handle_)
	{
		ExUnregisterCallback(this->handle_);
	}
}

void sleep_callback::dispatcher(const type type) const
{
	try
	{
		if (this->callback_)
		{
			this->callback_(type);
		}
	}
	catch (...)
	{
	}
}

_Function_class_(CALLBACK_FUNCTION)

void sleep_callback::static_callback(void* context, void* argument1, void* argument2)
{
	if (!context || argument1 != reinterpret_cast<PVOID>(PO_CB_SYSTEM_STATE_LOCK))
	{
		return;
	}

	auto type = type::sleep;
	if (ARGUMENT_PRESENT(argument2))
	{
		type = type::wakeup;
	}

	static_cast<sleep_callback*>(context)->dispatcher(type);
}
