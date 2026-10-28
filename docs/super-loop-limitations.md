# Super Loop Architecture Limitations & Analysis

## Known Architectural Constraints
1. **Cooperative Blocking Hazard:**
   - Any task exceeding its execution budget directly delays subsequent tasks.
   - Standard task execution must stay within `< 10 ms` to avoid jitter on UART processing.

2. **Worst-Case Latency Bound:**
   - Maximum delay before a task is processed in the loop is:
     $$\text{Latency}_{\max} = \sum_{i=0}^{N-1} \text{ExecutionTime}(\text{Task}_i)$$
   - Long-running operations (e.g., Flash write) MUST be split into state-machine steps.

3. **Interrupt vs. Polling Trade-off:**
   - High-frequency events are captured via Interrupt Service Routines (ISRs) setting flags.
   - Actual processing happens asynchronously inside the main loop iteration.

## Mitigation Measures
- **State Machines:** Heavy tasks execute in non-blocking chunks per iteration.
- **Execution Watchdog:** Timing checks monitor task duration to detect blocking code during execution.
