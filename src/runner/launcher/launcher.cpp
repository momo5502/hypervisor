#include "../std_include.hpp"
#include "../resource.hpp"
#include "launcher.hpp"

#include "../utils/nt.hpp"

launcher::launcher()
{
	this->create_main_menu();
}

void launcher::create_main_menu()
{
	this->main_window_.set_callback(
		[](window* window, const UINT message, const WPARAM w_param, const LPARAM l_param) -> LRESULT
		{
			if (message == WM_CLOSE)
			{
				window::close_all();
			}

			return DefWindowProcA(*window, message, w_param, l_param);
		});

	this->main_window_.create("S1x", 750, 420);
	this->main_window_.load_html(utils::nt::load_resource(MAIN_MENU));
	this->main_window_.show();
}

void launcher::run() const
{
	window::run();
}
