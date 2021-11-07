BOOTSTRAP_VERSION=3.3.7
BOOTSTRAP_FILES+=$(BOOTSTRAP_DOCS)/css/bootstrap.min.css
BOOTSTRAP_FILES+=$(BOOTSTRAP_DOCS)/css/bootstrap.min.css.map
BOOTSTRAP_FILES+=$(BOOTSTRAP_DOCS)/fonts/glyphicons-halflings-regular.woff2
BOOTSTRAP_FILES+=$(BOOTSTRAP_DOCS)/js/bootstrap.min.js
data-y+=$(BOOTSTRAP_FILES)

bootstrap-v$(BOOTSTRAP_VERSION).zip:
	wget -c -O $@ https://github.com/twbs/bootstrap/archive/v$(BOOTSTRAP_VERSION).zip || if [ ! -s $@ ]; then rm $@; fi

bootstrap-$(BOOTSTRAP_VERSION)/: bootstrap-v$(BOOTSTRAP_VERSION).zip
	unzip -o $<

$(BOOTSTRAP_FILES): $(BOOTSTRAP_DOCS)/%: bootstrap-$(BOOTSTRAP_VERSION)/
	mkdir -p $(dir $@)
	cp $</dist/$* $@
