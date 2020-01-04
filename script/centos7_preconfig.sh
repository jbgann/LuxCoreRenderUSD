#!/bin/bash

### CentOS 7 vanilla USD dependency installs 
yum install -y python-pyside python-tools python-pip python-devel zlib-devel glew-devel libXrandr-devel libXcursor-devel libXinerama-devel libXi-devel openssl-devel 

# Python dependencies for USD
pip install PyOpenGL
pip install Jinja2

# Build USD. The USD source should reside in ~/masters/USD-repo
cd ~/masters/USD-repo
python build_scripts/build_usd.py ~/masters/USD

# After the USD build completes the following env paths should be set
# PYTHONPATH: ~/masters/USD/lib/python
# PATH: ~/masters/USD/bin
