#include "common.hpp"
#include "utility.hpp"

template <typename T>
static void buffer_finalizer(Napi::Env env, T *data)
{
	delete [] data;
}

Napi::Value common::use_carryover(Napi::Env env)
{
	auto result = Napi::Buffer<char>::New(
		env,
		carryover_buffer,
		carryover_size,
		buffer_finalizer<char>
	);

	carryover_buffer = nullptr;
	carryover_size = 0;

	return result;
}

Napi::Value common::use_partial_carryover(Napi::Env env, size_t chunk_size)
{
	size_t remainder_size = carryover_size - chunk_size;

	char *new_carryover = new char[remainder_size];

	memcpy(
		new_carryover,
		&carryover_buffer[chunk_size],
		remainder_size
	);

	auto result = Napi::Buffer<char>::New(
		env,
		carryover_buffer,
		chunk_size,
		buffer_finalizer<char>
	);

	carryover_buffer = new_carryover;
	carryover_size = remainder_size;

	return result;
}

/* This assumes the carryover is to be thrown away! */
Napi::Value common::use_partial_buffer(
  Napi::Env env,
  char *buffer,
  size_t size,
  size_t offset
) {
	delete [] carryover_buffer;

	size_t remainder = size - offset;
	char *new_carryover = new char[remainder];

	/* Copy the remainder to the new carryover */
	memcpy(new_carryover, &buffer[offset], remainder);

	auto result = Napi::Buffer<char>::New(
		env,
		buffer,
		offset,
		buffer_finalizer<char>
	);

	carryover_size = remainder;
	carryover_buffer = new_carryover;

	return result;
}

Napi::Value common::read(const Napi::CallbackInfo& info)
{
	DWORD bytes_read = 0;
	Napi::Env env = info.Env();

	uint32_t bytes_to_read = info[0].As<Napi::Number>().Uint32Value();
	auto result_array = Napi::Array::New(env);

	if (this->carryover_size == bytes_to_read) {
		result_array.Set((uint32_t)0, this->use_carryover(env));
		return result_array;
	}

	/* Split the carryover into two and keep the remainder */
	if (this->carryover_size > bytes_to_read) {
		result_array.Set(
			(uint32_t)0,
			this->use_partial_carryover(env, bytes_to_read)
		);

		return result_array;
	}

	int array_offset = 0;


	/* Either no carryover exists and size is 0 or
	 * there's enough to insert a value into the array. */
	if (this->carryover_size > 0) {
		++array_offset;
		bytes_to_read -= this->carryover_size;

		(result_array).Set((uint32_t)0, this->use_carryover(env));
	}

	char *result_buffer = new char[bytes_to_read];
	DWORD total_bytes_read = 0;

	while (total_bytes_read != bytes_to_read) {
		const DWORD needed_bytes = bytes_to_read - total_bytes_read;

		BOOL success = ::ReadFile(
			this->pipe,
			&result_buffer[total_bytes_read],
			needed_bytes,
			&bytes_read,
			NULL
		);

		if (success == FALSE) {
			delete [] result_buffer;
			auto error = handle_os_error(env, "ReadFile");
			throw error;
		}

		total_bytes_read += bytes_read;
	}

	auto new_buffer = Napi::Buffer<char>::New(
		env,
		result_buffer,
		bytes_to_read,
		buffer_finalizer<char>
	);

	result_array.Set(array_offset, new_buffer);

	return result_array;
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

Napi::Value common::read_until(const Napi::CallbackInfo& info)
{
	Napi::Env env = info.Env();

	std::string token = info[0].As<Napi::String>();
	auto result_array = Napi::Array::New(env);
	int array_offset = 0;

	int find_result = find_token(
		this->carryover_buffer,
		this->carryover_size,
		token.c_str()
	);

	/* If the token wasn't found in the
	 * carryover, use the entire buffer
	 * as the first buffer sent back */
	if (find_result != -1) {
		size_t chunk_size = find_result + token.size();
		result_array.Set(
			(uint32_t)0,
			this->use_partial_carryover(env, chunk_size)
		);

		return result_array;
	}

	for (;;) {
		/* FIXME chunk size should be configurable. */
		size_t chunk_size = 1024;
		char *chunk = new char[chunk_size];

		DWORD bytes_read = 0;

		BOOL success = ::ReadFile(
			this->pipe,
			chunk, chunk_size,
			&bytes_read,
			NULL
		);

		if (success == FALSE) {
			delete [] chunk;
			auto error = handle_os_error(env, "ReadFile");
			throw error;
		}

		int find_result = find_token(chunk, bytes_read, token.c_str());

		if (find_result == -1) {
			auto new_buffer = Napi::Buffer<char>::New(
				env,
				chunk,
				bytes_read,
				buffer_finalizer<char>
			);

			result_array.Set((uint32_t)array_offset, new_buffer);

			++array_offset;
			continue;
		}

		if (find_result == bytes_read - token.size()) {
			/* We have an exact match to this chunk. Give the entire chunk */
			auto new_buffer = Napi::Buffer<char>::New(
				env,
				chunk,
				bytes_read,
				buffer_finalizer<char>
			);

			result_array.Set((uint32_t)array_offset, new_buffer);
			break;
		}

		/* We have a partial match. We need a carryover value. */
		auto new_buffer = this->use_partial_buffer(
			env, chunk, bytes_read, find_result + token.size()
		);

		result_array.Set((uint32_t)array_offset, new_buffer);
		break;
	}

	return result_array;
}

Napi::Value common::write(const Napi::CallbackInfo& info)
{
	DWORD total_bytes_written = 0;
	DWORD bytes_to_write = 0;
	Napi::Env env = info.Env();

	if (!info[0].IsBuffer()) {
		std::string error_string = get_error_string(info, 0);
		auto error = Napi::TypeError::New(env, error_string.c_str());

		throw error;
	}

	Napi::Buffer<char> contents = info[0].As<Napi::Buffer<char>>();

	if (info.Length() >= 2 && info[1].IsNumber()) {
		bytes_to_write = info[1].As<Napi::Number>().Uint32Value();
	} else {
		bytes_to_write = (DWORD)contents.ByteLength();
	}

	while (total_bytes_written != bytes_to_write) {
		const DWORD needed_bytes = bytes_to_write - total_bytes_written;
		const char *buffer = (char*)contents.Data();

		DWORD bytes_written = 0;

		BOOL success = ::WriteFile(
			this->pipe,
			&buffer[total_bytes_written],
			needed_bytes,
			&bytes_written,
			NULL
		);

		if (success == FALSE) {
			auto error = handle_os_error(env, "WriteFile");
			throw error;
		}

		total_bytes_written += bytes_written;
	}

	return env.Undefined();
}

Napi::Value common::close(const Napi::CallbackInfo& info)
{
	::CloseHandle(this->pipe);
	this->pipe = INVALID_HANDLE_VALUE;
	return info.Env().Undefined();
}