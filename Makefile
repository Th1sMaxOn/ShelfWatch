IMAGE ?= shelfwatch-build:lab2

.PHONY: image build clean

image:
	docker build -t $(IMAGE) docker

build: image
	docker run --rm -v "$(CURDIR):/workspace" -w /workspace \
		--user "$(shell id -u):$(shell id -g)" $(IMAGE) \
		make -C firmware all

clean:
	$(MAKE) -C firmware clean

.PHONY: check test sanitize coverage test-linux
check:
	python3 tools/check_project.py
test: check
	$(MAKE) -C tests test
sanitize: check
	$(MAKE) -C tests sanitize
test-linux: image
	docker run --rm -v "$(CURDIR):/workspace" -w /workspace --user "$(shell id -u):$(shell id -g)" $(IMAGE) make test sanitize
coverage: image
	docker run --rm -v "$(CURDIR):/workspace" -w /workspace --user "$(shell id -u):$(shell id -g)" $(IMAGE) python3 tools/coverage.py
