Written by Cyrus Karsan

# Mulithreaded HTTP Server
This program is a multihtreaded HTTP server which is able to parse GET, HEAD, and PUT requests concurrently from multiple clients. Correct responses will return 200 or 201 status codes. If any other request is given, the server will return a 400 or the respective error code. 
When you run httpserver, you must specify the port, whether you would like to enable logging, and the number of threads. 

# How to run
To compile and run:
```sh
make
./httpserver [port] -l [true] -N [6]
