#include <pamix_functions.hpp>

#include <iostream>

static PAInterface *interface;

void pamix_set_interface(PAInterface *i)
{
	interface = i;
}

void pamix_quit(argument_t arg)
{
	quit();
}

void pamix_select_tab(argument_t arg)
{
	selectEntries(interface, (entry_type)arg.i);
	signal_update(true);
}

void pamix_select_next(argument_t arg)
{
	select_next(interface, arg.b);
	signal_update(true);
}

void pamix_select_prev(argument_t arg)
{
	select_previous(interface, arg.b);
	signal_update(true);
}

void pamix_set_volume(argument_t arg)
{
	set_volume(interface, arg.d);
}

void pamix_add_volume(argument_t arg)
{
	add_volume(interface, arg.d);
}

void pamix_cycle_next(argument_t arg)
{
	cycle_switch(interface, arg.b);
}

void pamix_cycle_prev(argument_t arg)
{
	cycle_switch(interface, arg.b);
}

void pamix_toggle_lock(argument_t arg)
{
	toggle_lock(interface);
	signal_update(true);
}
void pamix_set_lock(argument_t arg)
{
	set_lock(interface, arg.b);
	signal_update(true);
}

void pamix_toggle_mute(argument_t arg)
{
	toggle_mute(interface);
	signal_update(true);
}
void pamix_set_mute(argument_t arg)
{
	set_mute(interface, arg.b);
	signal_update(true);
}
