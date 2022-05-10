#pragma once
#include "html/html_window.hpp"

class launcher final
{
public:
	launcher();

	void run() const;

private:
	html_window main_window_;

	void create_main_menu();
};
