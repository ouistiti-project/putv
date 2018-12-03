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

/**
 * @brief generate a response to a jsonRPC
 * 
 * This function unpack a string to get informations about the RPC.
 * The requested method is searched into the method_table and
 * the associated function is called.
 * The function continues with the response generation.
 *
 * @param input			string corresponding to a jsonRPC.
 * @param input_len		length of the input string.
 * @param method_table	table of methods to manage.
 * @param userdata		argument fot the callback associated to every methods.
 * 
 * @return the response string to send. The "answer" and the "notification"
 * don't generate string.
 */
char *jsonrpc_handler(const char *input, size_t input_len,
	struct jsonrpc_method_entry_t method_table[],
	void *userdata);
json_t *jsonrpc_jresponse(json_t *json_request,
	struct jsonrpc_method_entry_t method_table[],
	void *userdata);

/**
 * @brief generate a jsonRPC string
 *
 * This function search the method into the method_table.
 * It can generate only "request" or "notification".
 * To send a request the user needs to manage the answer.
 * This function will generate the request. jsonrpc_handler will manage
 * the response. To do that the method_table must contain an entry for
 * the request ("r" type) and an entry for the answer ("a" type).
 * 
 * @param method		name of the RPC to generate.
 * @param methodlen		length of the string.
 * @param method_table	table of methods to manage (same of method_handler).
 * @param userdata		argument fot the callback associated to every methods.
 * @param pid			id of the generated request.
 */
char *jsonrpc_request(const char *method, int methodlen,
		struct jsonrpc_method_entry_t method_table[],
		char *userdata, unsigned long *pid);
json_t *jsonrpc_jrequest(const char *method,
		struct jsonrpc_method_entry_t method_table[],
		char *userdata, unsigned long *pid);


json_t *jsonrpc_error_object(int code, const char *message, json_t *data);
json_t *jsonrpc_error_object_predefined(int code, json_t *data);

typedef json_t *(*jsonrpc_error_response_t)(json_t *json_id, json_t *json_error);
#define ERRORHANDLER_REQUEST 1
#define ERRORHANDLER_IGNORE 0
void jsonrpc_set_errorhandler(jsonrpc_error_response_t error_response);

#endif
