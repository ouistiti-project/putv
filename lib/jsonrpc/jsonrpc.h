#ifndef __JSONRPC_H__
#define __JSONRPC_H__
#include <jansson.h>

#define JSONRPC_PARSE_ERROR -32700
#define JSONRPC_INVALID_REQUEST -32600
#define JSONRPC_METHOD_NOT_FOUND -32601
#define JSONRPC_INVALID_PARAMS -32602
#define JSONRPC_INTERNAL_ERROR -32603

typedef int (*jsonrpc_method_prototype)(json_t *json_params, json_t **result, void *userdata);

struct jsonrpc_method_entry_t
{
	const char type; /* 'r'=request , 'n'=notification */
	const char *name;
	jsonrpc_method_prototype funcptr;
	const char *params_spec;
	unsigned long id;
	struct jsonrpc_method_entry_t *next;
};

char *jsonrpc_handler(const char *input, size_t input_len,
	struct jsonrpc_method_entry_t method_table[],
	void *userdata);

char *jsonrpc_request(const char *method, int methodlen,
		struct jsonrpc_method_entry_t method_table[],
		char *userdata, unsigned long *pid);

json_t *jsonrpc_error_object(int code, const char *message, json_t *data);
json_t *jsonrpc_error_object_predefined(int code, json_t *data);

typedef json_t *(*jsonrpc_error_response_t)(json_t *json_id, json_t *json_error);
#define ERRORHANDLER_REQUEST 1
#define ERRORHANDLER_IGNORE 0
void jsonrpc_set_errorhandler(jsonrpc_error_response_t error_response);

#endif
