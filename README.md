# CIPster Ethernet/IP Stack in C++

## Developer's Mailing List:

Colaborative discussion is here: https://www.freelists.org/list/cipster


## Welcome to CIPster!

CIPster is an EtherNet/IP&trade; stack for I/O adapter devices; supports multiple
I/O and explicit connections; includes objects and services to make EtherNet/IP&trade;-
compliant products defined in THE ETHERNET/IP SPECIFICATION and published by
ODVA (http://www.odva.org).

CIPster is a C++ port of C based OpENer with additional features. C++ is a
higher level language than C so many things are made easier. Easier means
faster.  Over time this will affect rate of evolution.

Thank yous go to the original authors of OpENer. See the AUTHORS. Since OpENer
code was assigned to Rockwell Automation as a copyright holder, then this
CIPster code has original portions copyright to Rockwell as well, however
additional contributions are owned by their respective contributors. This
project does not require copyright assignment, so the resulting code is under
a license to its licensees, and the licensors are its multiple contributors.
Should a question about ownership arise, the git version control system has a
full record of who did what.

## Requirements:

CIPster has been developed to be highly portable. The default version targets PCs
with a POSIX operating system and a BSD-socket network interface. To test this
version we recommend a Linux PC or Windows installed.

On Linux, you will need to have the following installed:

* CMake
* gcc
* make
* binutils
* cmake-curses-gui or cmake-gui

These should be in package repositories of most Linux distros.

On Windows, you will need MingW installed.

If you want to run the unit tests you will also have to download CppUTest via
https://github.com/cpputest/cpputest

I use JEdit as my text editor, you don't need an IDE, building from the command
line is not bad with the makefiles that CMake creates.

For configuring the project we recommend the use of a CMake GUI (i.e. the
cmake-curses-gui or cmake-gui package on Linux).

## Compiling on Linux for POSIX:

There are actually three CMake projects in the tree. The lowest level CMake
project is one for the library itself that creates libeip.a. Then there are two
others, one that creates a POSIX/linux executable and another a windows
executable. These two latter projects nest the lower level library project as a
sub project. For starters, and to get familiar with CMake's "out of tree" build
support, you might build the library alone at first. Since you are building out
of tree, (i.e. not in the source tree) you can simply delete the out of tree
directory at any time and try something else.

### Building the Library

This is not a mandatory step, but is provided as a learning experience.  If you
want to build a sample program, skip to *Building the Sample Programs*.

Let's create our out of tree directory in /tmp, which is erased on Ubuntu
after each reboot.  This means it will disappear, but we don't care because
building the library is easy and the out of tree build directory can be anywhere
after you get the hang of it.

    $ mkdir /tmp/build
    $ cd /tmp/build
    $ cmake DCMAKE_BUILD_TYPE=Debug <path-to-CIPster>/source

You will get a warning about a missing USER_INCLUDE_DIR setting.  That needs
to be a directory containing your modified open_user_conf.h file.  For this
first build, we can set it to &lt;path-to-CIPster&gt;/examples/POSIX/sample_application .

We can set this USER_INCLUDE_DIR after the CMakeCache.txt file is created
using ccmake from package cmake-curses-gui.

    $ sudo apt-get install cmake-curses-gui
    $ ccmake .

While inside ccmake, edit the field USER_INCLUDE_DIR to contain
"&lt;path-to-CIPster&gt;/examples/POSIX/sample_application". After changing the
field, press c for configure and g for generate.  Then quit.

Or you can delete the CMakeCache.txt file and start over by simply adding in the
missing USER_INCLUDE_DIR setting on the command line:

    $ cmake -DUSER_INCLUDE_DIR=<path-to-CIPster>/examples/POSIX/sample_application -DCMAKE_BUILD_TYPE=Debug <path-to-CIPster>/source
    $ make

You must delete CMakeCache.txt before running cmake, but not before running ccmake.

### Building the Sample Programs

Building either sample program (linux or windows) is also easy. These will each
be built in their own "out of tree" build directory. (With CMake, there's never
a good reason to build in the source tree, ever.) When building either sample
program, the library will also be built for you within a subdirectory called
build-CIPster of your build directory. It is a nested project that gets built
once. (You can later trigger a rebuild of this subproject by deleting this
subdirectory followed by a make of the parent project.)

For a Release build instead of a Debug build, substitute *Release* for *Debug* in
the following instructions. From another dedicated out of tree build directory:

    $ cmake -DCMAKE_BUILD_TYPE=Debug <path-to-CIPster>/examples/POSIX
    $ make

Wasn't that simple?  CMake is indeed king of the build tools.

Then you can run the resultant program.

    ./sample ipaddress subnetmask gateway domainname hostaddress macaddress
    e.g. ./sample 192.168.0.2 255.255.255.0 192.168.0.1 test.com testdevice 00 15 C5 BF D0 87

Recently USER_INCLUDE_DIR has become configurable in the outer sample projects also.
This makes it possible to experiment using different settings by supplying different
out of tree "cipster_user_conf.h" settings.

## Compiling on Linux for Windows

You can build 32 bit or 64 bit windows libraries or programs on linux using the
mingw tools with CMake. CMake supports toolchain files which can be passed in
the original invocation of CMake to identify the cross compiling toolchain set.
There is one supplied for 64 bit Windows, so here we build a 64 bit windows
sample program, and actually run in on Linux under Wine. (You can copy the
toolchain file and modify it for your toolchain. Or if building on Windows, omit
it. Truth be told, it is easier to build a Windows console binary on Linux than
on Windows.)  On Ubuntu or Debian the mingw tools are installed with the following
command:

    $ sudo apt install g++-mingw-w64-x86-64

Then follow these steps:

    $ mkdir /tmp/build-win-cip
    $ cd /tmp/build-win-cip
    $ cmake -DCMAKE_TOOLCHAIN_FILE=<path-to-CIPster>/source/buildsupport/Toolchain/toolchain-mingw64.cmake -DCMAKE_BUILD_TYPE=Debug <path-to-CIPster>/examples/WINDOWS
    $ make

Then if you have 64 bit Wine installed, simply run the program as if it were a linux binary on linux.

    $ ./sample.exe


Directory structure:
--------------------
- examples ...  The platform specific example programs.
- doc ...  Doxygen generated documentation (has to be generated for the SVN version) and Coding rules
- data ... EDS file for the default application
- source
    - src ... the production source code
        - cip ... the CIP layer of the stack
        - cip_objects ... additional CIP objects
        - enet_encap ... the Ethernet encapsulation layer
        - utils ... utility functions
    - tests ... the test source code
        - utils ... tests for utility functions

Documentation:
--------------
The documentation of the functions of CIPster is part of the source code. The source
packages contain the generated documentation in the directory api_doc. If you
use the GIT version you will need the program Doxygen for generating the HTML
documentation. You can generate the documentation by invoking doxygen from the
command line in the CIPster main directory.

Porting CIPster:
---------------
For porting CIPster to new platforms please see the porting section in the
Doxygen documentation.

