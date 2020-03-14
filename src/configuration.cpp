#include <configuration.hpp>
#include <iostream>

std::vector<std::pair<int, const char *>> Configuration::m_Keynames;

inline bool startsWithSubstring(const std::string &a, const std::string &b, std::string &sub) {
	bool rv = a.substr(0, b.length()) == b;
	if (rv)
		sub = a.substr(b.length(), a.length() - b.length());
	return rv;
}

int Configuration::getKeycode(const std::string &name) {
	if (m_Keynames.empty()) {
		for (int i = -1; i < KEY_MAX; i++) {
			const char *r = keyname(i);
			if (r)
				m_Keynames.emplace_back(i, r);
		}
	}
	for (unsigned i = 0; i < m_Keynames.size(); i++)
		if (name == m_Keynames[i].second)
			return m_Keynames[i].first;
	return -1;
}

bool Configuration::pressKey(int code) {
	if (!m_Bindings.count(code))
		return false;

	for (unsigned i = 0; i < m_Bindings[code].size(); i++) {
		keybind_t kb = m_Bindings[code][i];
		kb.m_Function(kb.m_Argument);
	}
	return true;
}

void Configuration::set(const std::string &key, const std::string &value) {
	m_Variables[key] = value;
}

void Configuration::unset(const std::string &key) {
	m_Variables.erase(key);
}

void Configuration::bind(const std::string &key, const std::string &cmd, const std::string &args) {
	int code = getKeycode(key);
	if (code == -1) {
		fprintf(stderr, "Invalid keyname: %s\n", key.c_str());
		return;
	}
	keybind_t kbind;
	if (!cmd.compare("quit"))
		kbind.m_Function = &pamix_quit;
	else if (!cmd.compare("select-tab")) {
		int tab = 0;
		sscanf(args.c_str(), "%d", &tab);
		kbind.m_Function = &pamix_select_tab;
		kbind.m_Argument.i = tab;
	} else if (!cmd.compare("cycle-tab-next")) {
		kbind.m_Function = &pamix_cycle_tab_next;
	} else if (!cmd.compare("cycle-tab-prev")) {
		kbind.m_Function = &pamix_cycle_tab_prev;
	} else if (!cmd.compare("select-next")) {
		bool precise = !args.compare("channel");
		kbind.m_Function = &pamix_select_next;
		kbind.m_Argument.b = precise;
	} else if (!cmd.compare("select-prev")) {
		bool precise = !args.compare("channel");
		kbind.m_Function = &pamix_select_prev;
		kbind.m_Argument.b = precise;
	} else if (!cmd.compare("set-volume")) {
		double value = 0.0;
		sscanf(args.c_str(), "%lf", &value);
		kbind.m_Function = &pamix_set_volume;
		kbind.m_Argument.d = value;
	} else if (!cmd.compare("add-volume")) {
		double delta = 0.0;
		sscanf(args.c_str(), "%lf", &delta);
		kbind.m_Function = &pamix_add_volume;
		kbind.m_Argument.d = delta;
	} else if (!cmd.compare("cycle-next"))
		kbind.m_Function = &pamix_cycle_next;
	else if (!cmd.compare("cycle-prev"))
		kbind.m_Function = &pamix_cycle_prev;
	else if (!cmd.compare("toggle-lock"))
		kbind.m_Function = &pamix_toggle_lock;
	else if (!cmd.compare("set-lock")) {
		bool lock = args.compare("0") != 0;
		kbind.m_Function = &pamix_set_lock;
		kbind.m_Argument.b = lock;
	} else if (!cmd.compare("toggle-mute"))
		kbind.m_Function = &pamix_toggle_mute;
	else if (!cmd.compare("set-mute")) {
		bool mute = args.compare("0") != 0;
		kbind.m_Function = &pamix_set_mute;
		kbind.m_Argument.b = mute;
	}

	m_Bindings[code].emplace_back(kbind);
}

void Configuration::unbind(const std::string &key) {
	int code = getKeycode(key);
	if (code == -1) {
		fprintf(stderr, "Invalid keyname: %s\n", key.c_str());
		return;
	}
	m_Bindings.erase(code);
}

void Configuration::unbindall() {
	m_Bindings.clear();
}

bool Configuration::loadFile(Configuration *config, const std::string &path) {
	FILE *file = fopen(path.c_str(), "r");

	if (!file)
		return false;

	fseek(file, 0, SEEK_END);
	unsigned long length = ftell(file);

	if (length == LLONG_MAX)
		return false;

	char *data = new char[length + 1];
	std::memset(data, 0, length + 1);
	fseek(file, 0, SEEK_SET);
	unsigned read = fread(data, 1, length, file);

	if (read != length)
		return false;
	fclose(file);

	std::istringstream stream(data);
	std::string line;

	/*
	 * parse text line by line
	 * syntax: 
	 * ; comments out last line
	 * floats look like 0.0
	 *
	 * couple of example lines:
	 * bind ^L set_volume(1.5, other args)
	 * set autospawn_pulse=1
	 * */
	while (std::getline(stream, line)) {
		// clip comments
		size_t cpos = line.find(';');
		if (cpos != std::string::npos)
			line = line.substr(0, cpos);

		// set x=y
		std::string sub;
		if (startsWithSubstring(line, "set ", sub)) {
			size_t pos = sub.find('=');
			// TODO error message if pos==npos
			if (pos == std::string::npos)
				continue;
			config->set(sub.substr(0, pos), sub.substr(pos + 1, sub.length() - pos - 1));
		} else if (startsWithSubstring(line, "unbind ", sub)) {
			config->unbind(sub);
		} else if (startsWithSubstring(line, "unbind-all", sub))
			config->unbindall();
		else if (startsWithSubstring(line, "bind ", sub)) {
			size_t pos = sub.find(' ');
			if (pos == std::string::npos)
				continue;

			std::string key = sub.substr(0, pos);
			std::string cmd = sub.substr(pos + 1, sub.length() - pos - 1);

			// seperate command from arguments
			std::string args;
			size_t spos = cmd.find(' ');
			if (spos != std::string::npos) {
				args = cmd.substr(spos + 1, cmd.length() - spos - 1);
				cmd = cmd.substr(0, spos);
			}

			config->bind(key, cmd, args);

			continue;
		}
	}

	delete[] data;

	return true;
}

bool Configuration::has(const std::string &key) {
	return m_Variables.count(key);
}

std::string Configuration::getString(const std::string &key) {
	if (has(key))
		return m_Variables[key];
	return "";
}

int Configuration::getInt(const std::string &key) {
	if (!has(key))
		return 0;
	int rv = 0;
	sscanf(m_Variables[key].c_str(), "%d", &rv);
	return rv;
}

bool Configuration::getBool(const std::string &key) {
	if (!has(key))
		return false;
	return m_Variables[key].compare("false");
}
