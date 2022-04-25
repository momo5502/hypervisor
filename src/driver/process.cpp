#include "std_include.hpp"
#include "process.hpp"
#include "type_traits.hpp"
#include "exception.hpp"

namespace process
{
	process_handle::process_handle(const PEPROCESS handle, const bool own)
		: own_(own), handle_(handle)
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
			this->own_ = obj.own_;
			this->handle_ = obj.handle_;
			obj.own_ = false;
			obj.handle_ = nullptr;
		}

		return *this;
	}

	process_handle::process_handle(const process_handle& obj)
	{
		this->operator=(std::move(obj));
	}

	process_handle& process_handle::operator=(const process_handle& obj)
	{
		if (this != &obj)
		{
			this->release();
			this->own_ = obj.own_;
			this->handle_ = obj.handle_;

			if (this->own_ && this->handle_)
			{
				ObReferenceObject(this->handle_);
			}
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
		if (!this->handle_)
		{
			return false;
		}

		LARGE_INTEGER zero_time{};
		zero_time.QuadPart = 0;

		return KeWaitForSingleObject(this->handle_, Executive, KernelMode, FALSE, &zero_time) != STATUS_WAIT_0;
	}

	uint32_t process_handle::get_id() const
	{
		if (!this->handle_)
		{
			return 0;
		}

		return uint32_t(uint64_t(PsGetProcessId(this->handle_)));
	}

	const char* process_handle::get_image_filename() const
	{
		if (!this->handle_)
		{
			return nullptr;
		}

		return PsGetProcessImageFileName(this->handle_);
	}

	void process_handle::release()
	{
		if (this->own_ && this->handle_)
		{
			ObDereferenceObject(this->handle_);
		}

		this->handle_ = nullptr;
		this->own_ = false;
	}

	process_handle find_process_by_id(const uint32_t process_id)
	{
		PEPROCESS process{};
		const uint64_t process_id_long = process_id;
		if (PsLookupProcessByProcessId(HANDLE(process_id_long), &process) != STATUS_SUCCESS)
		{
			return {};
		}

		return process_handle{process, true};
	}

	process_handle get_current_process()
	{
		return process_handle{PsGetCurrentProcess(), false};
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
