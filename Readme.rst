Antec Display Service Setup
===========================

Tested on Fedora Linux and CachyOS.

Overview
--------

This project provides a native C daemon that reads CPU and GPU temperatures
from hwmon and sends them to a compatible Antec USB device.

The daemon runs as a systemd service and automatically updates the display.

Files
-----

- ``temp-monitor.c`` — Main C source code
- ``Makefile`` — Build and installation system
- ``antec_sensor`` — Compiled binary (generated after build)
- ``antec_display.service`` — systemd service file
- ``helper-sensors.sh`` — Interactive sensor discovery tool
- ``sensors.conf`` — Sensor configuration file

Build & Installation
--------------------

### 1. Build the project

Use the provided Makefile:

.. code-block:: bash

   make

This will generate the binary:

::

   antec_sensor

---

### 2. Configure sensors

Edit ``sensors.conf`` to match your hardware sensors, then install it to:

::

   /etc/antec/sensors.conf

Example:

.. code-block:: bash

   sudo mkdir -p /etc/antec
   sudo cp sensors.conf /etc/antec/

You can use the helper tool below to identify the correct sensor names.

---

### 3. Install the binary

Install the daemon system-wide:

.. code-block:: bash

   sudo make install

This installs the binary to:

::

   /usr/local/bin/antec_sensor

---

Systemd Setup
-------------

### 1. Install the service file

.. code-block:: bash

   sudo cp antec_display.service /etc/systemd/system/
   sudo systemctl daemon-reload

### 2. Enable the service

.. code-block:: bash

   sudo systemctl enable antec_display.service

### 3. Start the service

.. code-block:: bash

   sudo systemctl start antec_display.service

---

Sensor Detection
----------------

The daemon reads sensor definitions from:

::

   /etc/antec/sensors.conf

Example configuration:

.. code-block:: ini

   [cpu]
   sensor = k10temp
   name = Tctl

   [gpu]
   sensor = amdgpu
   name = junction

You can verify available hwmon devices manually:

.. code-block:: bash

   ls /sys/class/hwmon/

---

Sensor Selection Helper Tool
----------------------------

To identify the correct CPU and GPU temperature sources, use the included helper script:

.. code-block:: bash

   chmod +x helper-sensors.sh
   ./helper-sensors.sh

This tool will:

- List all hwmon devices
- Show sensor labels and current temperatures
- Help identify the correct CPU and GPU temperature sources

---

Troubleshooting
---------------

Check service status:

.. code-block:: bash

   systemctl status antec_display.service

View live logs:

.. code-block:: bash

   journalctl -u antec_display.service -f

USB debugging:

.. code-block:: bash

   lsusb
   dmesg | grep usb

---

Cleanup
-------

Remove the installed binary:

.. code-block:: bash

   sudo rm /usr/local/bin/antec_sensor

Disable and remove the service:

.. code-block:: bash

   sudo systemctl disable antec_display.service
   sudo rm /etc/systemd/system/antec_display.service
   sudo systemctl daemon-reload

Clean build files:

.. code-block:: bash

   make clean
