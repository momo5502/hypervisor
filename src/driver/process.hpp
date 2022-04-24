#pragma once

namespace process
{
	class process_handle
	{
	public:
		process_handle() = default;
		process_handle(PEPROCESS handle, bool own = true);
		~process_handle();

		process_handle(process_handle&& obj) noexcept;
		process_handle& operator=(process_handle&& obj) noexcept;

		process_handle(const process_handle& obj);
		process_handle& operator=(const process_handle& obj);

		operator bool() const;
		operator PEPROCESS() const;

		bool is_alive() const;
		uint32_t get_id() const;

		const char* get_image_filename() const;

	private:
		bool own_{true};
		PEPROCESS handle_{nullptr};

		void release();
	};

	process_handle find_process_by_id(uint32_t process_id);
	process_handle get_current_process();

	class scoped_process_attacher
	{
	public:
		scoped_process_attacher(const process_handle& process);
		~scoped_process_attacher();

		scoped_process_attacher(scoped_process_attacher&& obj) noexcept = delete;
		scoped_process_attacher& operator=(scoped_process_attacher&& obj) noexcept = delete;

		scoped_process_attacher(const scoped_process_attacher&) = delete;
		scoped_process_attacher& operator=(const scoped_process_attacher&) = delete;

	private:
		KAPC_STATE apc_state_{};
	};
}
