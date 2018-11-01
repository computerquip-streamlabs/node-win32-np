#pragma once

#include <windows.h>
#include <napi.h>

struct common {
protected:
	char *carryover_buffer{nullptr};
	size_t carryover_size{0};

	HANDLE pipe{INVALID_HANDLE_VALUE};
	std::error_code ec{};

	Napi::Value use_carryover(Napi::Env env);
	Napi::Value use_partial_carryover(Napi::Env env, size_t size);

	Napi::Value use_partial_buffer(
	  Napi::Env env,
	  char *buffer,
	  size_t size,
	  size_t offset
	);

public:
	Napi::Value read_until(const Napi::CallbackInfo& info);
	Napi::Value read(const Napi::CallbackInfo& info);
	Napi::Value write(const Napi::CallbackInfo& info);
	Napi::Value close(const Napi::CallbackInfo& info);
};