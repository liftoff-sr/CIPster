CIPster Version 1.0
====================

Welcome to CIPster!
------------------

CIPster is an EtherNet/IP&trade; stack for I/O adapter devices; supports multiple
I/O and explicit connections; includes objects and services to make EtherNet/IP&trade;-
compliant products defined in THE ETHERNET/IP SPECIFICATION and published by
ODVA (http://www.odva.org).

CIPster is a C++ port of C based OpENer with additional features. C++ is a
higher level language than C so many things are made easier. Much credit goes to
original authors of OpENer. Since that code was assigned to Rockwell Automation
as a copyright holder, then this code has original portions copyright to Rockwell as
well, with additional contributions owned by their respective contributors.


Requirements:
-------------
CIPster has been developed to be highly portable. The default version targets PCs
with a POSIX operating system and a BSD-socket network interface. To test this
version we recommend a Linux PC or Windows installed.

On Linux, you will need to have the following installed:

* CMake
* gcc
* make
* binutils

These should be in package repositories of most Linux distros.

On Windows, you will need MingW installed.

If you want to run the unit tests you will also have to download CppUTest via
https://github.com/cpputest/cpputest

I use JEdit as my text editor, you don't need an IDE, building from the command
line is not bad with the makefiles that CMake creates.

For configuring the project we recommend the use of a CMake GUI (e.g.,the
cmake-gui oackage on Linux)

Compiling for POSIX:
-------------------


There are actually two CMake projects in the tree.  One for the library itself
that creates libeip.a, and another that create an executable which links to
that library.  The latter project nests for former as a sub project.
For starters, and to get familiar with CMake's "out of tree" build support,
you might build the library alone at first.  Since you are building out of tree,
you can simply delete the out of tree directory at any time and try something
else.

Let's create our out of tree directory in /tmp, which is erased on Ubuntu
after each reboot.  This means it will disappear, but we don't care because
building the library is easy and the out of tree build directory can be anywhere.

$ mkdir /tmp/build
$ cd /tmp/build
$ cmake -DCIPster_PLATFORM=POSIX DCMAKE_BUILD_TYPE=Debug &lt;path-to-CIPster&gt;/source

You will get a warning about a missing USER_INCLUDE_DIR setting.  That needs
to be a directory containing your modified open_user_conf.h file.  For this
first build, we can set it to &lt;path-to-CIPster&gt;/examples/POSIX/sample_application .

We can set this USER_INCLUDE_DIR after the CMakeCache.txt file is created
using ccmake from package cmake-curses-gui.

$ sudo apt-get install cmake-curses-gui

$ ccmake .

Press c for configure and g for generate.

Or you can delete the CMakeCache.txt file and simply add in the USER_INCLUDE_DIR
setting on the command line:

$ cmake -DUSER_INCLUDE_DIR=&lt;path-to-CIPster&gt;/examples/POSIX/sample_application -DCIPster_PLATFORM=POSIX -DCMAKE_BUILD_TYPE=Debug &lt;path-to-CIPster&gt;/source

$ make

Building the sample application is also easy but needs to be built in its own
out of tree build directory.  Never build in the source tree, ever.

From another directory:

$ cmake -DCMAKE_BUILD_TYPE=Debug <path-to-CIPster>/examples/POSIX
$ make

Then you can run the resultant program.

        ./sample ipaddress subnetmask gateway domainname hostaddress macaddress

        e.g. ./sample 192.168.0.2 255.255.255.0 192.168.0.1 test.com testdevice 00 15 C5 BF D0 87


Directory structure:
--------------------
- examples ...  The resulting binaries and make files for different ports
- doc ...  Doxygen generated documentation (has to be generated for the SVN version) and Coding rules
- data ... EDS file for the default application
- source
    - src ... the production source code
        - cip ... the CIP layer of the stack
        - cip_objects ... additional CIP objects
        - enet_encap ... the Ethernet encapsulation layer
        - ports ... the platform specific code
        - utils ... utility functions
    - tests ... the test source code
        - utils ... tests for utility functions

Documentation:
--------------
The documentation of the functions of CIPster is part of the source code. The source
packages contain the generated documentation in the directory api_doc. If you
use the GIT version you will need the program Doxygen for generating the HTML
documentation. You can generate the documentation by invoking doxygen from the
command line in the opener main directory.

Porting CIPster:
---------------
For porting CIPster to new platforms please see the porting section in the
Doxygen documentation.

