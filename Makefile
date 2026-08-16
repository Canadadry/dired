PYTHON := python3
VENV := test/integration/.venv
VENV_PYTHON := $(VENV)/bin/python

.PHONY: test unit install venv clean

test: unit venv
	$(VENV_PYTHON) -m unittest discover -s test/integration -p '*_test.py'
	$(VENV_PYTHON) test/integration/run.py

unit:
	./builder clean debug test

install: venv

venv: $(VENV)/.deps-installed

$(VENV)/.deps-installed: test/integration/requirements.txt
	$(PYTHON) -m venv $(VENV)
	$(VENV_PYTHON) -m pip install --upgrade pip
	$(VENV_PYTHON) -m pip install -r test/integration/requirements.txt
	touch $(VENV)/.deps-installed

clean:
	rm -rf $(VENV)
