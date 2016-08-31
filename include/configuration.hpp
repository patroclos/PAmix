#pragma once

#include <map>
#include <memory>
#include <pamix_functions.hpp>
#include <string>
#include <vector>

#define CONFIGURATION_AUTOSPAWN_PULSE "pulseaudio_autospawn"

struct keybind_t
{
	union argument_t m_Argument;
	void (*m_Function)(argument_t arg);
};

class Configuration
{
private:
	std::map<std::string, std::string>    m_Variables;
	std::map<int, std::vector<keybind_t>> m_Bindings;

	static std::vector<std::pair<int, const char *>> m_Keynames;
	static int getKeycode(const std::string &name);

public:
	bool has(const std::string &key);
	std::string getString(const std::string &key);
	int getInt(const std::string &key);
	bool getBool(const std::string &key);

	bool pressKey(int code);

	void set(const std::string &key, const std::string &value);
	void unset(const std::string &key);
	void bind(const std::string &key, const std::string &cmd, const std::string &arg = "");
	void unbind(const std::string &key);
	void unbindall();

	static bool loadFile(Configuration *config, const std::string &path);
};
