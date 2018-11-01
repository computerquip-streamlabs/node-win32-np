#pragma once

#include <codecvt>

static Napi::Error handle_os_error(Napi::Env env, const char *context_msg)
{
	std::error_code ec(::GetLastError(), std::system_category());

	auto error = Napi::Error::New(env, ec.message().c_str());

	return error;
}

static std::string get_error_string(const Napi::CallbackInfo& info, int index)
{
	return "Stay tuned folks";
}

static std::wstring get_wide_string(Napi::Env env, Napi::Value arg)
{
	std::string tmp(arg.As<Napi::String>());

	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>,wchar_t> convert;
	return convert.from_bytes(&tmp[0], &tmp[tmp.size()]);
}