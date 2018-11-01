#include "server.hpp"
#include "utility.hpp"

Napi::FunctionReference server::prototype =
	Napi::FunctionReference();

void server::Initialize(Napi::Env env, Napi::Object exports)
{
	Napi::Function server_func = DefineClass(
		env, "NamedPipeServer", {
			InstanceMethod("write",      &server::write),
			InstanceMethod("read",       &server::read),
			InstanceMethod("read_until", &server::read_until),
			InstanceMethod("close",      &server::close),
			InstanceMethod("listen",     &server::listen)
		}
	);

	server::prototype = Napi::Persistent(server_func);
	exports.Set("Server", server_func);
}

Napi::Value server::listen(const Napi::CallbackInfo& info)
{
	BOOL success = ::ConnectNamedPipe(this->pipe, NULL);

	if (success == 0) {
		handle_os_error(info.Env(), "ConnectNamedPipe");
	}

	return info.Env().Undefined();
}

server::server(const Napi::CallbackInfo& info)
: Napi::ObjectWrap<server>(info)
{
	static server_options default_options;
	Napi::Env env = info.Env();
	std::wstring pipe_name;

	if (!info[0].IsString()) {
		std::string error_string = get_error_string(info, 0);
		auto error = Napi::TypeError::New(env, error_string.c_str());

		throw error;
	}

	pipe_name = get_wide_string(env, info[0]);

	server_options *options = &default_options;

	this->pipe = ::CreateNamedPipeW(
		pipe_name.c_str(),
		options->dwOpenMode,
		options->dwPipeMode,
		options->nMaxInstances,
		options->nOutBufferSize,
		options->nInBufferSize,
		options->nDefaultTimeOut,
		NULL
	);

	if (this->pipe == INVALID_HANDLE_VALUE) {
		Napi::Error::New(env, this->ec.message().c_str())
			.ThrowAsJavaScriptException();
	}
}