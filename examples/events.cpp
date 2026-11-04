/**
 * This programs handle events and dump them to console
 */

#include <iostream>

#include <i3ipc++/ipc.hpp>


int  main() {
	// First of all we need to connect to an i3 process
	i3ipc::connection  conn;

	// Then we subscribing on events (see i3ipc::EVENT_TYPE)
	conn.subscribe(i3ipc::ET_WORKSPACE | i3ipc::ET_WINDOW | i3ipc::ET_BINDING);

	// Handler of workspace_event
	conn.on_workspace_event = [](const i3ipc::workspace_event_t&  ev) {
		std::cout << "workspace_event: " << (char)ev.type << std::endl;
	};

	// Handler of window_event
	conn.on_window_event = [](const i3ipc::window_event_t&  ev) {
		std::cout << "window_event: " << (char)ev.type << std::endl;
	};

	// Handler of binding event
	conn.on_binding_event = [](const i3ipc::binding_t&  b) {
		std::cout << "binding_event:" << std::endl
			<< "\tcommand = \"" << b.command << '"' << std::endl
			<< "\tinput_code = " << b.input_code << std::endl
			<< "\tsymbol = " << b.symbol << std::endl
			<< "\tinput_type = " << static_cast<char>(b.input_type) << std::endl
			<< "\tevent_state_mask =" << std::endl;
		for (const std::string& s : b.event_state_mask) {
			std::cout << "\t\t\"" << s << '"' << std::endl;
		}
	};

	// And starting an event-handling loop. It ends with an exception when i3 closes the connection (e.g. on exit)
	try {
		while (true) {
			conn.handle_event();
		}
	} catch (const i3ipc::ipc_error&  e) {
		std::cerr << "Event loop finished: " << e.what() << std::endl;
		return 1;
	}

	return 0;
}
