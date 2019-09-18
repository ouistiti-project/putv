include scripts.mk

package=putv
version=2.1

subdir-$(JSONRPC)+=lib/jsonrpc
subdir-$(TINYSVCMDNS)+=lib/tinysvcmdns
subdir-$(UPNPRENDERER)+=lib/gmrender
subdir-y+=src
subdir-y+=tests
subdir-y+=clients
subdir-y+=www
