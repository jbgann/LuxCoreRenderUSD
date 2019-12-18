LuxCore USD Hydra delegate
===========================

This plugin allows fast GPU or CPU accelerated viewport rendering by the open source LuxCore rendererer for the USD and Hydra system

For more details on USD, please visit the web site [here](http://openusd.org).

Prerequisites
-----------------------------

#### Common
* **LuxCore SDK 2.1**
* **USD build / tree**



----
## Current Status
Currently the delegate will build against an existing USD installation and has all the necessary placeholder classes and methods to respond to the calls made by the hydra framework.  At this time these placeholder methods make an entry into USD's logging system

##Further Work Required
The most pressing issue at this time is to parse USD scene objects into meshes readable by LuxCore, and to output the rendered meshes into a GL buffer displayable by the USD Viewport
