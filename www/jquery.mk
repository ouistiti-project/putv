JQUERY_VERSION=3.6.0
JQUERY_FILES+=$(JQUERY_DOCS)/js/jquery-$(JQUERY_VERSION).min.js
data-y+=$(JQUERY_FILES)

$(JQUERY_FILES): $(JQUERY_DOCS)/%:
	mkdir -p $(dir $@)
	wget -O $@ https://code.jquery.com/$(notdir $*)

$(JQUERY_DOCS)/js/jquery-$(JQUERY_VERSION).min.js_ALIAS+=jquery.min.js
