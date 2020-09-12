Written by Cyrus Karsan

# Multithreaded HTTP Server with Logging
This program is a multitreaded HTTP server which is able to parse GET, HEAD, and PUT requests from multiple clients in parallel. In addition, the server is able to, concurrently, log all requests and perform a health check of the server\
Implemented using a dispatch worker model in which one thread is allocated as the client and the others are workers which parse requests simultaneously. All data hazards (RAW,WAR,WAW) have been accounted for.\ 
When you run httpserver you must specify the port, whether you would like to enable logging, and the number of threads. Logging and number of threads are optional parameters.

## Usage:
-l (true or false) determines whether to enable logging or not. If not provided, defaults to false\
-N (int) specifies the number of threads to allocate. If not provided, defaults to 4\
To compile and run:
```sh
make
./httpserver [port] -l [true] -N [6]
