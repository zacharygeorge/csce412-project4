# CSCE 412 Project 4 - Load Balancer Simulation

This repository models a single load balancer, a FIFO request queue, and a
dynamically sized collection of web servers. Runtime behavior is controlled by
an external configuration file and supports deterministic request generation,
firewall filtering, dynamic scaling, and quiet or verbose output.

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

## Simulation workflow

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

The simulator runs end to end. It loads and validates `config.txt`, creates and
filters randomized requests, assigns accepted work FIFO, processes clock cycles,
scales the server pool, and prints summaries derived from actual execution.
Focused automated tests cover the component and integration behavior.

## Configuration

The program reads `config.txt` by default, or a different file passed as its
only command-line argument. Blank lines and `#` comments are allowed. Scalar
keys must appear exactly once; `blocked_source_range` may be repeated.

| Setting | Meaning |
| --- | --- |
| `minimum_processing_time` | Inclusive minimum randomly generated request duration. |
| `maximum_processing_time` | Inclusive maximum randomly generated request duration. |
| `scale_up_requests_per_server` | Add a server when the queue is strictly greater than this value times the current server count. |
| `scale_down_requests_per_server` | Remove one idle server when the queue is strictly less than this value times the current server count. |
| `scaling_cooldown_cycles` | Minimum cycles between successful scaling changes. |
| `arrival_probability` | Probability from 0 through 1 that a cycle creates one arrival batch. |
| `maximum_requests_per_arrival` | Maximum uniformly selected batch size; the minimum is one. |
| `requests_per_starting_server` | Accepted initial requests requested per starting server. |
| `random_seed` | Seed for repeatable request generation and arrival decisions. |
| `default_output_mode` | Prompt default: `quiet` or `verbose`. |
| `blocked_source_range` | Inclusive IPv4 range written as `first-last`; repeat for more ranges. |

Filtering occurs before queue admission. A blocked candidate increments generated
and rejected counts, while an admitted candidate increments generated and accepted
counts. Quiet mode prints startup and final summaries plus compact scaling evidence.
Verbose mode prints those summaries and meaningful generation, rejection,
assignment, completion, and scaling events.

## Build

On this Windows development environment:

```powershell
mingw32-make
mingw32-make test
```

On a standard GNU Make environment:

```sh
make
make test
```

The executable accepts an optional configuration path; otherwise it uses
`config.txt`:

```powershell
.\load_balancer.exe
.\load_balancer.exe demo_config.txt
```

After starting, enter the starting server count, number of clock cycles, and
quiet or verbose output mode. Redirect output to a file when producing a
submission log.

For a short deterministic demonstration with frequent firewall rejections and
visible scaling, run `.\load_balancer.exe demo_config.txt`, then enter `2`, `25`,
and `verbose` at the prompts. The fixed seed makes the same configuration and
inputs reproducible.

Clean generated build products with:

```powershell
mingw32-make clean
```

## Automated tests

`mingw32-make test` builds and runs the Request/WebServer, LoadBalancer, and
configuration/firewall/logging suites. The tests use deterministic seeds and
directly constructed configurations where appropriate; they do not require
interactive input.

## Doxygen documentation

Doxygen 1.9.8 or later can generate the static documentation site from the
documented headers and `docs/mainpage.dox`:

```powershell
doxygen Doxyfile
```

The entry point is `docs\generated\html\index.html`. Publish the **contents** of
that `html` directory together so relative links to stylesheets, scripts, class
pages, images, navigation data, and the `search` directory remain valid. The
`Doxyfile`, source files, `docs\mainpage.dox`, and
`docs\generated\doxygen-warnings.log` are inputs or diagnostics and are not part
of the deployed website.
