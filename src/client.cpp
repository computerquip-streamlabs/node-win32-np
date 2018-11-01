#include "client.hpp"
#include "utility.hpp"

Napi::FunctionReference client::prototype =
	Napi::FunctionReference();

void client::Initialize(Napi::Env env, Napi::Object exports)
{
	Napi::Function client_func = DefineClass(
		env, "NamedPipeClient", {
			InstanceMethod("write",      &client::write),
			InstanceMethod("read",       &client::read),
			InstanceMethod("read_until", &client::read_until),
			InstanceMethod("close",      &client::close)
		}
	);

	client::prototype = Napi::Persistent(client_func);
	exports.Set("Client", client_func);
}

client::client(const Napi::CallbackInfo& info)
: Napi::ObjectWrap<client>(info)
{
	Napi::Env env = info.Env();
	static client_options default_options;
	std::wstring pipe_name;

	if (!info[0].IsString()) {
		std::string error_string = get_error_string(info, 0);
		auto error = Napi::TypeError::New(env, error_string.c_str());

		throw error;
	}

	pipe_name = get_wide_string(env, info[0]);

	client_options *options = &default_options;

	this->pipe = ::CreateFileW(
		pipe_name.c_str(),
		options->dwDesiredAccess,
		0, NULL,
		OPEN_EXISTING,
		0, NULL
	);

	if (this->pipe == INVALID_HANDLE_VALUE) {
		auto error = handle_os_error(env, "CreateFile");
		throw error;
	}
}