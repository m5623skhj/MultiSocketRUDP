## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Standalone v3 waits for each response/timeout; mixed throughput includes the drain interval. RTT includes send queue delay. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 µs | p95 µs | p99 µs | Timeouts | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 100.000 | 30086.861 | 23.6 | 51.3 | 64.5 | 0.000 | -69.50% | 0.000 |
| reliable-baseline | reliable | 100.000 | 23839.607 | 35.0 | 67.4 | 78.7 | 0.000 | -63.96% | 0.000 |
| mixed | reliable | 100.000 | 40.065 | 69.8 | 129.8 | 193.4 | N/A | -49.04% | 0.000 |
| mixed | unreliable | 99.500 | 399.043 | 94.1 | 172.9 | 243.2 | N/A | -54.66% | -0.200 |
