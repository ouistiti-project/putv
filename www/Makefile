HTDOCS?=htdocs/apps/
TRUSTDOCS?=htdocs/trust/

data-y+=$(HTDOCS)ouiradio.html
$(HTDOCS)ouiradio.html_ALIAS-$(HTINDEX)+=$(HTDOCS)index.html
data-y+=$(HTDOCS)ouiradio.json
data-n+=$(HTDOCS)apps.json
data-y+=$(HTDOCS)js/jsonrpc.js

data-y+=$(TRUSTDOCS)css/bootstrap.min.css
data-y+=$(TRUSTDOCS)css/bootstrap.min.css.map
data-y+=$(TRUSTDOCS)fonts/glyphicons-halflings-regular.woff2
data-y+=$(TRUSTDOCS)js/bootstrap.min.js
data-y+=$(TRUSTDOCS)js/jquery.min.js

BOOTSTRAP_VERSION=3.3.7
bootstrap-$(BOOTSTRAP_VERSION)-dist.zip:
	wget https://github.com/twbs/bootstrap/releases/download/v$(BOOTSTRAP_VERSION)/bootstrap-$(BOOTSTRAP_VERSION)-dist.zip

bootstrap-$(BOOTSTRAP_VERSION)-dist: bootstrap-$(BOOTSTRAP_VERSION)-dist.zip
	unzip bootstrap-$(BOOTSTRAP_VERSION)-dist.zip

$(TRUSTDOCS)css/bootstrap.min.css: bootstrap-$(BOOTSTRAP_VERSION)-dist
	mkdir -p $(TRUSTDOCS)css
	mkdir -p $(TRUSTDOCS)js
	mkdir -p $(TRUSTDOCS)fonts
	mv -f bootstrap-$(BOOTSTRAP_VERSION)-dist/css/* $(TRUSTDOCS)css/
	mv -f bootstrap-$(BOOTSTRAP_VERSION)-dist/js/* $(TRUSTDOCS)js/
	mv -f bootstrap-$(BOOTSTRAP_VERSION)-dist/fonts/* $(TRUSTDOCS)fonts/
	rm -rf bootstrap-$(BOOTSTRAP_VERSION)-dist

JQUERY_VERSION=3.3.1
jquery$(JQUERY_VERSION:%=-%).min.js:
	wget https://code.jquery.com/jquery$(JQUERY_VERSION:%=-%).min.js

$(TRUSTDOCS)js/jquery.min.js: jquery$(JQUERY_VERSION:%=-%).min.js
	mkdir -p htdocs/js/
	cp -f jquery$(JQUERY_VERSION:%=-%).min.js $(TRUSTDOCS)js/jquery.min.js
