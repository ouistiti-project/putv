include scripts.mk

package=putv
version=2.1

subdir-$(JSONRPC)+=lib/jsonrpc
subdir-y+=src
subdir-y+=tests
subdir-y+=clients
subdir-$(WEBAPP)+=www
