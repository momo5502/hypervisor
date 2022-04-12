#pragma once

namespace vmx
{
	class ept
	{
	public:
		ept();
		~ept();

		ept(ept&&) = delete;
		ept(const ept&) = delete;
		ept& operator=(ept&&) = delete;
		ept& operator=(const ept&) = delete;
	};
}
