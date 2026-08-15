PYTHON := python3
VENV := .venv
VENV_PYTHON := $(VENV)/bin/python

.PHONY: test install venv clean

test: venv
	$(VENV_PYTHON) -m unittest discover -s . -p '*_test.py'

install: venv

venv: $(VENV)/.deps-installed

$(VENV)/.deps-installed: requirements.txt
	$(PYTHON) -m venv $(VENV)
	$(VENV_PYTHON) -m pip install --upgrade pip
	$(VENV_PYTHON) -m pip install -r requirements.txt
	touch $(VENV)/.deps-installed

clean:
	rm -rf $(VENV)
