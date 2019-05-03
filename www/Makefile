data-y+=htdocs/ouiradio.html
htdocs/ouiradio.html_ALIAS+=htdocs/index.html
data-y+=htdocs/ouiradio.js
data-y+=htdocs/js/jsonrpc.js

data-y+=htdocs/css/bootstrap.min.css
data-y+=htdocs/css/bootstrap.min.css.map
data-y+=htdocs/fonts/glyphicons-halflings-regular.woff2
data-y+=htdocs/js/bootstrap.min.js
data-y+=htdocs/js/jquery.min.js

BOOTSTRAP_VERSION=3.3.7
bootstrap-$(BOOTSTRAP_VERSION)-dist.zip:
	wget https://github.com/twbs/bootstrap/releases/download/v$(BOOTSTRAP_VERSION)/bootstrap-$(BOOTSTRAP_VERSION)-dist.zip

bootstrap-$(BOOTSTRAP_VERSION)-dist: bootstrap-$(BOOTSTRAP_VERSION)-dist.zip
	unzip bootstrap-$(BOOTSTRAP_VERSION)-dist.zip

htdocs/css/bootstrap.min.css: bootstrap-$(BOOTSTRAP_VERSION)-dist
	mkdir -p htdocs/css
	mkdir -p htdocs/js
	mkdir -p htdocs/fonts
	mv -f bootstrap-$(BOOTSTRAP_VERSION)-dist/css/* htdocs/css/
	mv -f bootstrap-$(BOOTSTRAP_VERSION)-dist/js/* htdocs/js/
	mv -f bootstrap-$(BOOTSTRAP_VERSION)-dist/fonts/* htdocs/fonts/
	rm -rf bootstrap-$(BOOTSTRAP_VERSION)-dist

JQUERY_VERSION=3.3.1
jquery$(JQUERY_VERSION:%=-%).min.js:
	wget https://code.jquery.com/jquery$(JQUERY_VERSION:%=-%).min.js

htdocs/js/jquery.min.js: jquery$(JQUERY_VERSION:%=-%).min.js
	mkdir -p htdocs/js/
	cp -f jquery$(JQUERY_VERSION:%=-%).min.js htdocs/js/jquery.min.js
