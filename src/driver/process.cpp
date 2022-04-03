#include "std_include.hpp"
#include "process.hpp"
#include "type_traits.hpp"
#include "exception.hpp"

namespace process
{
	process_handle::process_handle(const PEPROCESS handle)
		: handle_(handle)
	{
	}

	process_handle::~process_handle()
	{
		this->release();
	}

	process_handle::process_handle(process_handle&& obj) noexcept
		: process_handle()
	{
		this->operator=(std::move(obj));
	}

	process_handle& process_handle::operator=(process_handle&& obj) noexcept
	{
		if (this != &obj)
		{
			this->release();
			this->handle_ = obj.handle_;
			obj.handle_ = nullptr;
		}

		return *this;
	}

	process_handle::operator bool() const
	{
		return this->handle_ != nullptr;
	}

	process_handle::operator PEPROCESS() const
	{
		return this->handle_;
	}

	bool process_handle::is_alive() const
	{
		LARGE_INTEGER zero_time{};
		zero_time.QuadPart = 0;

		return KeWaitForSingleObject(this->handle_, Executive, KernelMode, FALSE, &zero_time) != STATUS_WAIT_0;
	}

	void process_handle::release()
	{
		if (this->handle_)
		{
			ObDereferenceObject(this->handle_);
			this->handle_ = nullptr;
		}
	}

	process_handle find_process_by_id(const uint32_t process_id)
	{
		PEPROCESS process{};
		if (PsLookupProcessByProcessId(HANDLE(process_id), &process) != STATUS_SUCCESS)
		{
			return {};
		}

		return process_handle{process};
	}

	scoped_process_attacher::scoped_process_attacher(const process_handle& process)
	{
		if (!process || !process.is_alive())
		{
			throw std::runtime_error("Invalid process");
		}

		KeStackAttachProcess(process, &this->apc_state_);
	}

	scoped_process_attacher::~scoped_process_attacher()
	{
		KeUnstackDetachProcess(&this->apc_state_);
	}
}
