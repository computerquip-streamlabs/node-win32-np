#include <codecvt>
#include <nan.h>
#include <windows.h>

static void handle_os_error(const char *context_msg)
{
	std::error_code ec(::GetLastError(), std::system_category());

	auto exception = Nan::ErrnoException(
		ec.value(),
		context_msg,
		ec.message().c_str()
	);

	Nan::ThrowError(exception);
}

static std::string get_error_string(
  const Nan::FunctionCallbackInfo<v8::Value>& info,
  int index
) {
	std::string error_string;
	v8::Isolate *isolate = info.GetIsolate();
	Nan::Utf8String arg_type(info[index]->TypeOf(isolate));

	error_string = "Argument ";
	error_string += std::to_string(index);
	error_string += " given bad type: ";
	error_string += std::string(*arg_type, arg_type.length());

	return error_string;
}

static std::wstring get_wide_string(v8::Local<v8::Value> arg)
{
	Nan::Utf8String tmp(arg);

	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>,wchar_t> convert;
	return convert.from_bytes(*tmp, (*tmp) + tmp.length());
}

static v8::Local<v8::String> field_name(const char* field_name)
{
	return Nan::New(field_name).ToLocalChecked();
}

static v8::MaybeLocal<v8::Value> get_object_field(
  v8::Local<v8::Object> object,
  const char* field
) {
	return Nan::Get(object, field_name(field));
}

static void free_callback(char *data, void *hint)
{
	delete data;
}

/* In this case, each "server" pertains to one
 * one handle. It's up to the frontend how it handles
 * multiples. */
struct server_options {
	DWORD dwOpenMode{PIPE_ACCESS_DUPLEX};
	DWORD dwPipeMode{PIPE_TYPE_BYTE | PIPE_READMODE_BYTE};
	DWORD nMaxInstances{PIPE_UNLIMITED_INSTANCES};
	DWORD nOutBufferSize{512};
	DWORD nInBufferSize{512};
	DWORD nDefaultTimeOut{NMPWAIT_USE_DEFAULT_WAIT};
};

struct client_options {
	DWORD dwDesiredAccess{GENERIC_READ | GENERIC_WRITE};
};

struct common {
	char *carryover_buffer{nullptr};
	size_t carryover_size{0};

	HANDLE pipe{INVALID_HANDLE_VALUE};
	std::error_code ec;

	v8::Local<v8::Value> use_carryover();
	v8::Local<v8::Value> use_partial_carryover(size_t size);

	v8::Local<v8::Value> use_partial_buffer(
	  char *buffer,
	  size_t size,
	  size_t offset
	);
};

v8::Local<v8::Value> common::use_carryover()
{
	auto result = Nan::NewBuffer(
		carryover_buffer, (uint32_t)carryover_size,
		free_callback, nullptr
	);

	carryover_buffer = nullptr;
	carryover_size = 0;

	return result.ToLocalChecked();
}

v8::Local<v8::Value> common::use_partial_carryover(size_t chunk_size)
{
	size_t remainder_size = carryover_size - chunk_size;

	char *new_carryover = new char[remainder_size];

	memcpy(
		new_carryover,
		&carryover_buffer[chunk_size],
		remainder_size
	);

	auto result = Nan::NewBuffer(
		carryover_buffer, (uint32_t)chunk_size,
		free_callback, nullptr
	);

	carryover_buffer = new_carryover;
	carryover_size = remainder_size;

	return result.ToLocalChecked();
}

/* This assumes the carryover is to be thrown away! */
v8::Local<v8::Value> common::use_partial_buffer(
  char *buffer,
  size_t size,
  size_t offset
) {
	free(carryover_buffer);

	size_t remainder = size - offset;
	char *new_carryover = new char[remainder];

	/* Copy the remainder to the new carryover */
	memcpy(new_carryover, &buffer[offset], remainder);

	auto result = Nan::NewBuffer(buffer, offset, free_callback, nullptr);

	carryover_size = remainder;
	carryover_buffer = new_carryover;

	return result.ToLocalChecked();
}

class server : public Nan::ObjectWrap, public common {
public:
	static Nan::Persistent<v8::FunctionTemplate> prototype;

	server(const wchar_t *lpName, server_options *options);
	server() = delete;
	server(const server &) = delete;
	server(server &&) = delete;

	static NAN_METHOD(create);
};

class client : public Nan::ObjectWrap, public common {
public:
	static Nan::Persistent<v8::FunctionTemplate> prototype;

	client(const wchar_t *lpName, client_options *options);
	client() = delete;
	client(const server &) = delete;
	client(server &&) = delete;

	static NAN_METHOD(create);
};

Nan::Persistent<v8::FunctionTemplate> server::prototype =
	Nan::Persistent<v8::FunctionTemplate>();

Nan::Persistent<v8::FunctionTemplate> client::prototype =
	Nan::Persistent<v8::FunctionTemplate>();

server::server(const wchar_t *lpName, server_options *options)
{
	static server_options default_options;

	if (!options) options = &default_options;

	this->pipe = ::CreateNamedPipeW(
		lpName,
		options->dwOpenMode,
		options->dwPipeMode,
		options->nMaxInstances,
		options->nOutBufferSize,
		options->nInBufferSize,
		options->nDefaultTimeOut,
		NULL
	);

	if (this->pipe == INVALID_HANDLE_VALUE) {
		this->ec.assign(::GetLastError(), std::system_category());
	}
}

client::client(const wchar_t *lpName, client_options *options)
{
	static client_options default_options;

	if (!options) options = &default_options;

	this->pipe = ::CreateFileW(
		lpName,
		options->dwDesiredAccess,
		0, NULL,
		OPEN_EXISTING,
		0, NULL
	);

	if (this->pipe == INVALID_HANDLE_VALUE) {
		this->ec.assign(::GetLastError(), std::system_category());
	}
}

/*#########################################
 *# Exported Javascript Functions
 *#######################################*/

namespace js {

static NAN_METHOD(listen)
{
	server *ctx = Nan::ObjectWrap::Unwrap<server>(info.Holder());

	BOOL success = ::ConnectNamedPipe(ctx->pipe, NULL);

	if (success == 0) {
		handle_os_error("ConnectNamedPipe");
	}
}

/* read, write, and close can work on both client and server */
/* This will always return a buffer of the given size */
template <typename Unwrappable>
static NAN_METHOD(read)
{
	DWORD bytes_read = 0;

	Unwrappable *ctx =
		Nan::ObjectWrap::Unwrap<Unwrappable>(info.Holder());

	if (!info[0]->IsUint32()) {
		std::string error_string = get_error_string(info, 0);
		Nan::ThrowTypeError(error_string.c_str());
		return;
	}

	uint32_t bytes_to_read = Nan::To<uint32_t>(info[0]).FromJust();

	auto result_array = Nan::New<v8::Array>();

	if (ctx->carryover_size == bytes_to_read) {
		Nan::Set(result_array, 0, ctx->use_carryover());
		info.GetReturnValue().Set(result_array);

		return;
	}

	/* Split the carryover into two and keep the remainder */
	if (ctx->carryover_size > bytes_to_read) {
		Nan::Set(
			result_array, 0,
			ctx->use_partial_carryover(bytes_to_read)
		);

		info.GetReturnValue().Set(result_array);

		return;
	}

	int array_offset = 0;


	/* Either no carryover exists and size is 0 or
	 * there's enough to insert a value into the array. */
	if (ctx->carryover_size > 0) {
		++array_offset;
		bytes_to_read -= ctx->carryover_size;

		Nan::Set(result_array, 0, ctx->use_carryover());
	}

	char *result_buffer = new char[bytes_to_read];
	DWORD total_bytes_read = 0;

	while (total_bytes_read != bytes_to_read) {
		const DWORD needed_bytes = bytes_to_read - total_bytes_read;

		BOOL success = ::ReadFile(
			ctx->pipe,
			&result_buffer[total_bytes_read],
			needed_bytes,
			&bytes_read,
			NULL
		);

		if (success == FALSE) {
			handle_os_error("ReadFile");
			free(result_buffer);
			return;
		}

		total_bytes_read += bytes_read;
	}

	auto new_buffer = Nan::NewBuffer(
		result_buffer, (uint32_t)bytes_to_read,
		free_callback, nullptr
	);

	Nan::Set(result_array, array_offset, new_buffer.ToLocalChecked());

	info.GetReturnValue().Set(result_array);
}

static bool check_for_token(char *buffer, size_t offset, size_t sz, const char *token)
{
	for (const char *token_iter = token; token_iter[0] != '\0'; ++token_iter) {
		if (token_iter[0] != buffer[offset])
			return false;

		++offset;

		if (offset > sz)
			return false;
	}

	return true;
}

static int find_token(char *buffer, size_t sz, const char *token)
{
	for (size_t i = 0; i < sz; ++i) {
		if (check_for_token(buffer, i, sz, token))
			return i;
	}

	return -1;
}

template <typename Unwrappable>
static NAN_METHOD(read_until)
{
	Unwrappable *ctx =
		Nan::ObjectWrap::Unwrap<Unwrappable>(info.Holder());

	if (!info[0]->IsString()) {
		std::string error_string = get_error_string(info, 0);
		Nan::ThrowTypeError(error_string.c_str());
		return;
	}

	Nan::Utf8String token(info[0]);
	auto result_array = Nan::New<v8::Array>();
	int array_offset = 0;

	int find_result = find_token(
		ctx->carryover_buffer,
		ctx->carryover_size,
		*token
	);

	/* If the token wasn't found in the
	 * carryover, use the entire buffer
	 * as the first buffer sent back */
	if (find_result != -1) {
		Nan::Set(result_array, 0, ctx->use_partial_carryover(find_result));
		++array_offset;
	}

	for (;;) {
		/* FIXME chunk size should be configurable. */
		size_t chunk_size = 1024;
		char *chunk = new char[chunk_size];

		DWORD bytes_read = 0;

		BOOL success = ::ReadFile(
			ctx->pipe,
			chunk, chunk_size,
			&bytes_read,
			NULL
		);

		if (success == FALSE) {
			handle_os_error("ReadFile");
			free(chunk);
			return;
		}

		int find_result = find_token(chunk, bytes_read, *token);

		if (find_result == -1) {
			auto new_buffer = Nan::NewBuffer(
				chunk, bytes_read,
				free_callback, nullptr
			);

			Nan::Set(
				result_array,
				array_offset,
				new_buffer.ToLocalChecked()
			);

			++array_offset;
			continue;
		}

		if (find_result == bytes_read - token.length()) {
			/* We have an exact match to this chunk. Give the entire chunk */
			auto new_buffer = Nan::NewBuffer(chunk, bytes_read, free_callback, nullptr);

			Nan::Set(result_array, array_offset, new_buffer.ToLocalChecked());
			break;
		}

		/* We have a partial match. We need a carryover value. */
		auto new_buffer =
			ctx->use_partial_buffer(chunk, bytes_read, find_result);

		Nan::Set(result_array, array_offset, new_buffer);
		break;
	}

	info.GetReturnValue().Set(result_array);
}

template <typename Unwrappable>
static NAN_METHOD(write)
{
	DWORD total_bytes_written = 0;
	DWORD bytes_to_write = 0;

	Unwrappable *ctx =
		Nan::ObjectWrap::Unwrap<Unwrappable>(info.Holder());

	if (!info[0]->IsArrayBufferView()) {
		std::string error_string = get_error_string(info, 0);
		Nan::ThrowTypeError(error_string.c_str());
		return;
	}

	Nan::TypedArrayContents<char> contents(info[0]);

	if (info.Length() >= 2 && info[1]->IsUint32()) {
		bytes_to_write = Nan::To<uint32_t>(info[1]).FromJust();
	} else {
		bytes_to_write = (DWORD)contents.length();
	}

	while (total_bytes_written != bytes_to_write) {
		const DWORD needed_bytes = bytes_to_write - total_bytes_written;
		const char *buffer = *contents;

		DWORD bytes_written = 0;

		BOOL success = ::WriteFile(
			ctx->pipe,
			&buffer[total_bytes_written],
			needed_bytes,
			&bytes_written,
			NULL
		);

		if (success == FALSE) {
			handle_os_error("WriteFile");
			return;
		}

		total_bytes_written += bytes_written;
	}
}

template <typename Unwrappable>
static NAN_METHOD(close)
{
	Unwrappable *ctx = Nan::ObjectWrap::Unwrap<Unwrappable>(info.Holder());

	::CloseHandle(ctx->pipe);
	ctx->pipe = INVALID_HANDLE_VALUE;
}

} // namespace js

static server_options convert_server_options(v8::Local<v8::Object> options_obj)
{
	server_options result;

	auto openMode = get_object_field(options_obj, "openMode");
	auto pipeMode = get_object_field(options_obj, "pipeMode");
	auto maxInstances = get_object_field(options_obj, "maxInstances");
	auto outBufferSize = get_object_field(options_obj, "outBufferSize");
	auto inBufferSize = get_object_field(options_obj, "inBufferSize");
	auto timeout = get_object_field(options_obj, "timeout");

	/* TODO */
	return result;
}

static client_options convert_client_options(v8::Local<v8::Object> options_obj)
{
	client_options result;

	auto desiredAccess = get_object_field(options_obj, "desiredAccess");

	/* TODO */
	return result;
}

NAN_METHOD(server::create)
{
	std::wstring pipe_name;

	v8::Local<v8::FunctionTemplate> templ =
		Nan::New<v8::FunctionTemplate>(server::prototype);

	v8::Local<v8::Object> result =
		Nan::NewInstance(templ->InstanceTemplate()).ToLocalChecked();

	server *ctx = nullptr;

	if (!info[0]->IsString()) {
		std::string error_string = get_error_string(info, 0);
		Nan::ThrowTypeError(error_string.c_str());
		goto type_error;
	}

	pipe_name = get_wide_string(info[0]);

	if (info.Length() >= 2 && info[1]->IsObject()) {
		server_options options =
			convert_server_options(Nan::To<v8::Object>(info[1]).ToLocalChecked());

		ctx = new server(pipe_name.c_str(), &options);
	} else {
		ctx = new server(pipe_name.c_str(), nullptr);
	}

	if (ctx->ec) {
		Nan::ThrowError(ctx->ec.message().c_str());
		goto bad_handle;
	}

	Nan::SetMethod(result, "write",      js::write<server>);
	Nan::SetMethod(result, "read",       js::read<server>);
	Nan::SetMethod(result, "read_until", js::read_until<server>);
	Nan::SetMethod(result, "listen",     js::listen);
	Nan::SetMethod(result, "close",      js::close<server>);

	ctx->Wrap(result);

	info.GetReturnValue().Set(result);

	return;

bad_handle:
type_error:
	delete ctx;
}

NAN_METHOD(client::create)
{
	std::wstring pipe_name;

	v8::Local<v8::FunctionTemplate> templ =
		Nan::New<v8::FunctionTemplate>(client::prototype);

	v8::Local<v8::Object> result =
		Nan::NewInstance(templ->InstanceTemplate()).ToLocalChecked();

	client *ctx = nullptr;

	if (!info[0]->IsString()) {
		std::string error_string = get_error_string(info, 0);
		Nan::ThrowTypeError(error_string.c_str());
		goto type_error;
	}

	pipe_name = get_wide_string(info[0]);

	if (info.Length() >= 2 && info[1]->IsObject()) {
		client_options options =
			convert_client_options(Nan::To<v8::Object>(info[1]).ToLocalChecked());

		ctx = new client(pipe_name.c_str(), &options);
	} else {
		ctx = new client(pipe_name.c_str(), nullptr);
	}

	if (ctx->ec) {
		Nan::ThrowError(ctx->ec.message().c_str());
		goto bad_handle;
	}

	Nan::SetMethod(result, "write",      js::write<client>);
	Nan::SetMethod(result, "read",       js::read<client>);
	Nan::SetMethod(result, "read_until", js::read_until<server>);
	Nan::SetMethod(result, "close",      js::close<client>);

	ctx->Wrap(result);

	info.GetReturnValue().Set(result);

	return;

bad_handle:
type_error:
	delete ctx;
}

/*#########################################
 *# Module Registration
 *#######################################*/

NAN_MODULE_INIT(Initialize)
{
	v8::Local<v8::FunctionTemplate> server_tpl = Nan::New<v8::FunctionTemplate>();
	server_tpl->SetClassName(Nan::New("NodeWin32NamedPipeServer").ToLocalChecked());
	server_tpl->InstanceTemplate()->SetInternalFieldCount(1);
	server::prototype.Reset(server_tpl);

	v8::Local<v8::FunctionTemplate> client_tpl = Nan::New<v8::FunctionTemplate>();
	client_tpl->SetClassName(Nan::New("NodeWin32NamedPipeClient").ToLocalChecked());
	client_tpl->InstanceTemplate()->SetInternalFieldCount(1);
	client::prototype.Reset(client_tpl);

	Nan::SetMethod(target, "createServer", server::create);
	Nan::SetMethod(target, "createClient", client::create);
}

NODE_MODULE(node_win32_np, Initialize)