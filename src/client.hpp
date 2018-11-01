#pragma once

#include "common.hpp"

struct client_options {
	DWORD dwDesiredAccess{GENERIC_READ | GENERIC_WRITE};
};

class client : public Napi::ObjectWrap<client>, public common {
public:
	static Napi::FunctionReference prototype;
	static void Initialize(Napi::Env env, Napi::Object exports);

	client(const Napi::CallbackInfo& info);
	client() = delete;
	client(const client &) = delete;
	client(client &&) = delete;
};