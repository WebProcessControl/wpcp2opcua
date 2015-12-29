WPCP2OPCUA
==========

WPCP2OPCUA is a small gateway to access an [OPC UA](https://opcfoundation.org/about/opc-technologies/opc-ua/) server via [WPCP](http://wpcp.net). It allows full access to variables in the OPC UA address space. This includes browsing, reading, writing and subscribing. In addition to that, alarms can be subscribed and acknowledged too.

Requirements
------------

Since there is no free open source implementation of the official OPC UA stack available at writing this lines, it is not possible to provide pre-built binaries due to licensing restrictions. Nevertheless there are evaluation versions available, which allow unlimited usage on an evaluation basis.

To build WPCP2OPCUA you have to install meet the following software:

* [Visual Studio](https://www.visualstudio.com/)
* [CMake](https://cmake.org/)
* [OpenSSL](https://slproweb.com/products/Win32OpenSSL.html)
* [OPC UA Stack](https://www.unified-automation.com/products/client-sdk/ansi-c-ua-client-sdk.html)

Build
-----

If all requirements are met a build can be created by calling `cmake` in an empty directory via
```
cmake path/to/source
```
pointing to the source directory. Then the binaries can be created by opening the Visual Studio Solution file or by calling `cmake --build .` in the created directory.

Usage
-----

The generated can be started via the command line with a list of parameters, which are described in the next section. If started with valid arguments, it will run until terminated with `Ctrl+C`.

The simplest way to start the server is with the following command:
```
wpcp2opcua.exe --http.port 80 --opcua.url opc.tcp://localhost:4840/
```

Parameters
----------

`--debug.level`: A value between 0 and 7 declaring the level of log messages.

`--http.port`: Port where the server should bind to and listen for incoming connections.

`--http.rootdir`: Directory which will be used by the server for finding files requested via the HTTP interface. It usually contains files like `index.html`.

`--opcua.trace`: If this parameter is set, OPC UA tracing is enabled.

`--opcua.url`: The endpoint URL for creating the OPC UA session.

`--opcua.uri`: The server URI for creating the OPC UA session.

`--rwpcp.auth`: This parameter can be used to specify the `Authorization` header of the RWPCP connection request.

`--rwpcp.host`: The hostname of the RWPCP target server.

`--rwpcp.port`: The port of the RWPCP target server. Defaults to `80`.

`--rwpcp.ssl`: If this parameter is set, RWPCP will use a secure socket for communication with.

`--rwpcp.path`: If the RWPCP uses different endpoints on the same hostname, the path can be given with this parameter. Defaults to `/`.

`--rwpcp.origin`: This parameter can be used to specify the `Origin` header of the RWPCP connection request.

`--rwpcp.interval`: The number of seconds between two RWPCP connection attempts. Defaults to `10`.

Example Scenario
----------------

WPCP2OPCUA can be used in many scenarios. One example would be be to make a [PLC](https://en.wikipedia.org/wiki/Programmable_logic_controller) with an OPC UA server accessible via WPCP. This allows the user to access all variables via a native web interface and build a full visualization in plain web technology. For testing purpose, [WPCP GUI](https://github.com/WebProcessControl/wpcp-gui) can be used to access the data on the server, too.

```
+-----+              +------------+                 +----------+
| PLC |<-- OPC UA -->| WPCP2OPCUA |<-- HTTP+WPCP -->| WPCP GUI |
+-----+              +------------+                 +----------+
```
