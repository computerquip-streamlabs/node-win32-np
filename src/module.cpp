#include "client.hpp"
#include "server.hpp"

/*#########################################
 *# Module Registration
 *#######################################*/

Napi::Object Initialize(Napi::Env env, Napi::Object exports)
{
	server::Initialize(env, exports);
	client::Initialize(env, exports);
	return exports;
}

NODE_API_MODULE(node_win32_np, Initialize)