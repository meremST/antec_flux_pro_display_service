Antec Display Service Setup
===========================

Tested on Fedora Linux and CachyOS.

Overview
--------

This project provides a native C daemon that reads CPU/GPU temperatures from hwmon and sends them to an Antec USB device. It runs as a systemd service.

Files
-----

- ``temp-monitor.c`` — Main C source code
- ``Makefile`` — Build and install system
- ``antec_sensor`` — Compiled binary (generated)
- ``antec_display.service`` — systemd service file
- ``helper-sensors.sh`` — Interactive sensor discovery tool

Build & Installation
--------------------

### 1. Build the project

Use the provided Makefile:

.. code-block:: bash

   make

This will generate the binary:

::

   sudo ./antec_sensor


### 2. Install the binary (recommended)

Install it system-wide in a standard local system path:

.. code-block:: bash

   sudo make install

This installs the binary to:

::

   /usr/local/bin/

---

Systemd Setup
-------------

### 1. Install service file

.. code-block:: bash

   sudo cp antec_display.service /etc/systemd/system/
   sudo systemctl daemon-reload

### 2. Enable service

.. code-block:: bash

   sudo systemctl enable antec_display.service

### 3. Start service

.. code-block:: bash

   sudo systemctl start antec_display.service

---

Sensor Detection
----------------

On startup, the daemon automatically scans hwmon, for example:

- CPU: ``k10temp / Tctl``
- GPU: ``amdgpu / junction``

You can verify sensors manually:

.. code-block:: bash

   ls /sys/class/hwmon/

---

Sensor selection helper tool
----------------------------

To help identify the correct CPU/GPU temperature sources, use the included helper script:

.. code-block:: bash

   chmod +x helper-sensors.sh
   ./helper-sensors.sh

This tool will:

- List all hwmon devices
- Show sensor labels and current temperatures
- Help you identify correct CPU and GPU temperature sources

---

Troubleshooting
---------------

Check service status:

.. code-block:: bash

   systemctl status antec_display.service

View logs:

.. code-block:: bash

   journalctl -u antec_display.service -f

USB debugging:

.. code-block:: bash

   lsusb
   dmesg | grep usb

---

Cleanup
-------

To remove the binary:

.. code-block:: bash

   sudo rm /usr/local/bin/antec_sensor

To disable and remove the service:

.. code-block:: bash

   sudo systemctl disable antec_display.service
   sudo rm /etc/systemd/system/antec_display.service
   sudo systemctl daemon-reload

To clean build files:

.. code-block:: bash

   make clean
