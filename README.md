# Multithreaded Web Server in C++

A web server built from scratch in C++, using raw POSIX sockets and a custom thread pool — no frameworks, no external libraries. Handles multiple client connections at the same time and serves real HTML files over HTTP.

## What it does

- Speaks raw HTTP — parses real requests and sends back valid HTTP responses
- Handles many clients concurrently using a fixed thread pool (not one thread per client)
- Routes different URL paths to different files, with proper 404 handling for missing pages
- Serves static HTML files from a `public/` directory

## Why I built this

I wanted to understand what's actually happening underneath frameworks like Flask or Express — how a server accepts connections, parses HTTP, and handles concurrency, all at the OS level. This project is a small, working version of what those frameworks do for you automatically.

## Tech / concepts used

- Raw POSIX sockets (`socket`, `bind`, `listen`, `accept`)
- Manual HTTP request parsing and response building
- `std::thread`, `std::mutex`, `std::condition_variable`
- Producer-consumer pattern (task queue + thread pool)
- Static file serving from disk

## Architecture

```
Client (browser/curl)
       │
       ▼
  accept() connection
       │
       ▼
  Push to task queue ──► [Worker 1] [Worker 2] [Worker 3] [Worker 4]
                                │         │         │         │
                                ▼         ▼         ▼         ▼
                          Parse request → Read file → Send HTTP response
```

The main thread only accepts connections and hands them off to a shared queue. A fixed pool of worker threads pulls jobs off that queue and handles them independently, so one slow client can't block anyone else.

## Performance

Benchmarked with Apache Bench (`ab`):

| Concurrency | Requests | Requests/sec | Failed |
|---|---|---|---|
| 20  | 1,000 | ~7,600/sec | 0 |
| 100 | 5,000 | ~8,500/sec | 0 |

## Running it

```bash
g++ server.cpp -o server -pthread
./server
```

Then visit `http://localhost:8080/` in a browser, or:

```bash
curl http://localhost:8080/
```

## What's next

- Support for more HTTP methods (POST, PUT)
- Keep-alive connections
- Configurable thread pool size
