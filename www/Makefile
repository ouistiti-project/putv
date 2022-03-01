HTDOCS?=htdocs/apps/
CGI_BIN?=cgi-bin/apps/
WEBSOCKET?=websocket/

data-y+=$(HTDOCS)ouiradio.html
$(HTDOCS)ouiradio.html_ALIAS-$(HTINDEX)+=index.html
data-y+=$(HTDOCS)ouiradio.json
data-n+=$(HTDOCS)apps.json
data-y+=$(HTDOCS)js/jsonrpc.js
data-y+=$(CGI_BIN)totem-start.cgi
data-y+=$(WEBSOCKET).unixsocket

BOOTSTRAP_DOCS?=htdocs/trust
JQUERY_DOCS?=htdocs/trust
export BOOTSTRAP_DOCS JQUERY_DOCS
subdir-$(BOOTSTRAP)+=bootstrap.mk
subdir-$(JQUERY)+=jquery.mk

sysconf-y=ouistiti.d/putv.conf
