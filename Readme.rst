Antec Display Service Setup
===========================

Tested on Fedora Linux and CachyOS.

Files
-----

- ``antec_display_service.py`` — Main service script
- ``antec_display.service`` — systemd service file
- ``sensors.conf`` — Sensor configuration

Installation
------------

Place ``antec_display_service.py`` somewhere appropriate for your system.

Example:

.. code-block:: bash

   sudo cp antec_display_service.py /bin/
   sudo chmod +x /usr/bin/antec_display_service.py

Install the systemd service:

.. code-block:: bash

   sudo cp antec_display.service /etc/systemd/system/
   sudo systemctl daemon-reload
   sudo systemctl enable antec_display

Sensor Setup
------------

Run the script manually first:

.. code-block:: bash

   python3 /usr/bin/antec_display_service.py

The script will walk you through selecting temperature sensors. Once the display is working, exit with ``Ctrl+C``.

Create the config directory:

.. code-block:: bash

   sudo mkdir -p /etc/antec

Edit ``sensors.conf`` to match the sensors you selected, then place it in:

::

   /etc/antec/sensors.conf

Example:

.. code-block:: bash

   sudo cp sensors.conf /etc/antec/

Start the service:

.. code-block:: bash

   sudo systemctl start antec_display

Troubleshooting
---------------

Check service status:

.. code-block:: bash

   systemctl status antec_display

View logs:

.. code-block:: bash

   journalctl -u antec_display -f

