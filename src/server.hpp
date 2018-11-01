#pragma once

#include "common.hpp"

struct server_options {
	DWORD dwOpenMode{PIPE_ACCESS_DUPLEX};
	DWORD dwPipeMode{PIPE_TYPE_BYTE | PIPE_READMODE_BYTE};
	DWORD nMaxInstances{PIPE_UNLIMITED_INSTANCES};
	DWORD nOutBufferSize{512};
	DWORD nInBufferSize{512};
	DWORD nDefaultTimeOut{NMPWAIT_USE_DEFAULT_WAIT};
};

class server : public Napi::ObjectWrap<server>, public common {
public:
	static Napi::FunctionReference prototype;
	static void Initialize(Napi::Env env, Napi::Object exports);

	server(const Napi::CallbackInfo& info);
	server() = delete;
	server(const server &) = delete;
	server(server &&) = delete;

	Napi::Value listen(const Napi::CallbackInfo& info);
};