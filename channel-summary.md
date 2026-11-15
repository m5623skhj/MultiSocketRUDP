## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Standalone v3 waits for each response/timeout; mixed throughput includes the drain interval. RTT includes send queue delay. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 µs | p95 µs | p99 µs | Timeouts | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 100.000 | 6219.582 | 143.1 | 167.6 | 185.3 | 0.000 | +2.82% | 0.000 |
| reliable-baseline | reliable | 100.000 | 5202.353 | 160.1 | 187.9 | 217.0 | 0.000 | +0.64% | 0.000 |
| mixed | reliable | 100.000 | 40.786 | 179.3 | 302.2 | 354.3 | N/A | +18.46% | 0.000 |
| mixed | unreliable | 99.600 | 406.226 | 240.3 | 368.9 | 446.4 | N/A | -2.95% | 0.300 |
