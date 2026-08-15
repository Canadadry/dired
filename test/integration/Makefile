PYTHON := python3
VENV := .venv
VENV_PYTHON := $(VENV)/bin/python
REPO_ROOT := ../..
DIREDD_BIN := $(REPO_ROOT)/build/diredd

.PHONY: test install venv clean

test: venv $(DIREDD_BIN)
	$(VENV_PYTHON) -m unittest discover -s . -p '*_test.py'
	$(VENV_PYTHON) run.py

$(DIREDD_BIN):
	cd $(REPO_ROOT) && ./builder debug

install: venv

venv: $(VENV)/.deps-installed

$(VENV)/.deps-installed: requirements.txt
	$(PYTHON) -m venv $(VENV)
	$(VENV_PYTHON) -m pip install --upgrade pip
	$(VENV_PYTHON) -m pip install -r requirements.txt
	touch $(VENV)/.deps-installed

clean:
	rm -rf $(VENV)
