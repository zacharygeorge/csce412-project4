# CSCE 412 Project 4 - Load Balancer Simulation

This repository models a single load balancer, a FIFO request queue, and a
dynamically sized collection of web servers. Milestone A establishes the full
program workflow and every class/function contract. Detailed simulation behavior
is intentionally reserved for later milestones.

## Architecture

- `Request` is a value object containing source IP, destination IP, processing
  time, and a `P` or `S` job type.
- `WebServer` owns at most one current request and its remaining processing time.
- `LoadBalancer` owns `std::queue<Request>`, the web-server collection, the clock,
  the random engine, and all simulation statistics.
- `SimulationConfig` defines settings loaded from `config.txt` before the run.
- `main.cpp` owns only setup and orchestration: load configuration, read and
  validate user input, construct the load balancer, initialize the queue, call
  one load-balancer cycle per requested clock cycle, and request the final summary.

## Intended workflow

The initial queue is filled to `requests_per_starting_server * starting_servers`
accepted requests. A candidate blocked during initialization is counted as
generated and rejected, and generation continues until the accepted target is
reached. Startup makes at most 100 candidate attempts per requested queue entry;
if filtering prevents the target from being reached within that finite bound, the
program reports a configuration/runtime error instead of looping forever.

Each clock cycle has one fixed order:

1. Increment the simulation clock.
2. Perform one Bernoulli trial using `arrival_probability`. On success, select one
   uniformly random batch size from 1 through `maximum_requests_per_arrival`, then
   generate and filter that many independent requests. On failure, generate none.
3. Advance work that was already assigned and record any completions.
4. Assign queued requests FIFO to servers that are now idle.
5. If the cooldown allows, scale up when queue size is strictly greater than
   `scale_up_requests_per_server * server_count`, or scale down when it is strictly
   less than `scale_down_requests_per_server * server_count`.
6. Refresh current queue, processing, idle-server, and server-count statistics.

Only one server may be added or removed per eligible cycle. Scale-down removes an
idle server only and never reduces the pool below one server. A failed removal does
not begin a cooldown. A successful add/remove does. A newly added server begins
accepting work on the following cycle, preserving one assignment phase per cycle.
Equality at either threshold does not scale.

These rules preserve the central accounting invariant:

```text
requests generated = requests accepted + requests rejected
requests accepted = requests completed + requests queued + requests processing
ending servers = starting servers + servers added - servers removed
ending servers = busy servers + idle servers
```

Quiet mode prints starting and final summaries, including condensed scaling
evidence. Verbose mode prints the same summaries plus meaningful generation,
filtering, assignment, completion, and scaling events. Neither mode prints an
unhelpful message for every unchanged clock cycle.

## Current implementation status

The driver, ownership model, interfaces, contracts, configuration schema, and
basic build are present. Request construction, IPv4 validation, reproducible
random request generation, and one-request-at-a-time WebServer processing are
implemented and covered by focused automated tests.

The following operations deliberately throw a `NotImplementedError` naming the
unfinished method: configuration-file parsing, IP-range matching, load-balancer
queue initialization and submission, arrival generation, firewall filtering,
dynamic scaling, simulation cycles, and final summary output. The executable
therefore compiles but is not yet a working end-to-end simulation.

## Build

On this Windows development environment:

```powershell
mingw32-make
mingw32-make test
mingw32-make clean
```

On a standard GNU Make environment:

```sh
make
make test
make clean
```

The executable accepts an optional configuration path; otherwise it uses
`config.txt`:

```text
load_balancer [config-file]
```
