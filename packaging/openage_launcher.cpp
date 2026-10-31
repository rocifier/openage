// Copyright 2026 the openage authors. See copying.md for legal info.

#include <string>
#include <vector>

#include <windows.h>

namespace {

class WinHandle {
public:
	explicit WinHandle(HANDLE value) :
		value{value} {}

	~WinHandle() {
		if (this->value != nullptr && this->value != INVALID_HANDLE_VALUE) {
			CloseHandle(this->value);
		}
	}

	WinHandle(const WinHandle &) = delete;
	WinHandle &operator=(const WinHandle &) = delete;

	HANDLE get() const {
		return this->value;
	}

private:
	HANDLE value;
};


int show_error(const std::wstring &message) {
	MessageBoxW(nullptr, message.c_str(), L"openage", MB_OK | MB_ICONERROR);
	return 1;
}


bool get_install_dir(std::wstring &install_dir) {
	std::vector<wchar_t> executable_path(1024);
	while (true) {
		auto length = GetModuleFileNameW(nullptr,
		                                 executable_path.data(),
		                                 static_cast<DWORD>(executable_path.size()));
		if (length == 0) {
			return false;
		}
		if (length < executable_path.size() - 1) {
			executable_path.resize(length);
			break;
		}

		executable_path.resize(executable_path.size() * 2);
	}

	std::wstring full_path{executable_path.begin(), executable_path.end()};
	auto separator = full_path.find_last_of(L"\\/");
	if (separator == std::wstring::npos) {
		return false;
	}

	install_dir = full_path.substr(0, separator);
	return true;
}


bool configure_environment(const std::wstring &install_dir) {
	auto bin_dir = install_dir + L"\\bin";
	auto path_length = GetEnvironmentVariableW(L"PATH", nullptr, 0);
	std::wstring path{bin_dir};
	if (path_length > 0) {
		std::vector<wchar_t> old_path(path_length);
		if (GetEnvironmentVariableW(L"PATH", old_path.data(), path_length) == 0) {
			return false;
		}
		path += L";";
		path += old_path.data();
	}

	return SetEnvironmentVariableW(L"PATH", path.c_str()) != 0
	    && SetCurrentDirectoryW(install_dir.c_str()) != 0;
}

} // namespace


int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int) {
	std::wstring install_dir;
	if (not get_install_dir(install_dir) || not configure_environment(install_dir)) {
		return show_error(L"The openage installation directory could not be initialized.");
	}

	auto python_path = install_dir + L"\\python\\python.exe";
	auto asset_path = install_dir + L"\\share\\openage";
	auto log_path = install_dir + L"\\openage.log";
	SECURITY_ATTRIBUTES inheritable{};
	inheritable.nLength = sizeof(inheritable);
	inheritable.bInheritHandle = true;
	WinHandle log{CreateFileW(log_path.c_str(),
	                           GENERIC_WRITE,
	                           FILE_SHARE_READ,
	                           &inheritable,
	                           CREATE_ALWAYS,
	                           FILE_ATTRIBUTE_NORMAL,
	                           nullptr)};
	if (log.get() == INVALID_HANDLE_VALUE) {
		return show_error(L"openage.log could not be created next to openage.exe.");
	}

	std::wstring command = L"\"" + python_path + L"\" -m openage game --asset-dir \"";
	command += asset_path;
	command += L"\"";
	if (arguments != nullptr && arguments[0] != L'\0') {
		command += L" ";
		command += arguments;
	}
	std::vector<wchar_t> mutable_command{command.begin(), command.end()};
	mutable_command.push_back(L'\0');

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = nullptr;
	startup.hStdOutput = log.get();
	startup.hStdError = log.get();
	PROCESS_INFORMATION process{};
	if (not CreateProcessW(python_path.c_str(),
	                       mutable_command.data(),
	                       nullptr,
	                       nullptr,
	                       true,
	                       CREATE_NO_WINDOW,
	                       nullptr,
	                       install_dir.c_str(),
	                       &startup,
	                       &process)) {
		return show_error(L"The bundled Python runtime could not be started.");
	}

	WinHandle process_handle{process.hProcess};
	WinHandle thread_handle{process.hThread};
	if (WaitForSingleObject(process_handle.get(), INFINITE) != WAIT_OBJECT_0) {
		return show_error(L"Waiting for the game process failed.");
	}

	DWORD exit_code{};
	if (not GetExitCodeProcess(process_handle.get(), &exit_code)) {
		return show_error(L"The game process exit status could not be read.");
	}
	if (exit_code != 0) {
		return show_error(L"openage exited with an error. Details were written to:\n" + log_path);
	}

	return 0;
}
