## Channel RTT / delivery

RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.
Values are medians across runs. Standalone v3 waits for each response/timeout; mixed throughput includes the drain interval. RTT includes send queue delay. Latency changes are informational.

| Scenario | Channel | Delivery % | Responses/s | p50 µs | p95 µs | p99 µs | Timeouts | Previous p95 delta | Delivery delta (pp) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| unreliable-only | unreliable | 100.000 | 14170.086 | 74.4 | 93.5 | 103.6 | 0.000 | -42.85% | 0.000 |
| reliable-baseline | reliable | 100.000 | 11735.863 | 73.4 | 106.9 | 122.6 | 0.000 | -43.20% | 0.000 |
| mixed | reliable | 100.000 | 39.988 | 125.4 | 170.3 | 211.8 | N/A | -32.45% | 0.000 |
| mixed | unreliable | 99.400 | 398.002 | 145.6 | 208.4 | 255.2 | N/A | -45.74% | -0.100 |
