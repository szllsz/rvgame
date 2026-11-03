# Reusable PYNQ ownership and retirement workflow

This is a decision workflow, not a universal board-control script. It comes from a reproduced Zynq native-frame deadlock and tests of safe and unsafe cleanup in rvgame. Register addresses and flags remain project-specific.

## Establish the dependency before issuing stop

Trace every bus master that can access the program, assets and framebuffer. Determine whether the producer must finish additional work before the stop request can be acknowledged. In the observed native-frame design, HOLD stops the producer immediately, while its response waits for the frame engine to become idle. An unfinished frame requires that stopped producer to submit the remaining band. Thus HOLD-first can deadlock an ARM MMIO access; a Python timeout cannot unwind that access.

## Apply the target project's proof

1. Rediscover boot ID, supervisor and owner command line/start time, active application and existing outputs. An old PID or controller timeout does not prove owner exit.
2. Use the current owner's normal final/exit protocol while the producer can still run. Validate current-run identity, application, checksum and completion. A short retirement packet is not a complete performance record.
3. Alternatively, use a specifically verified fatal-cancel path. Verify outstanding DMA has actually drained; posting abort alone is insufficient.
4. Only after that proof, apply the project's permitted HOLD/disable sequence, verify all bus users idle, and release buffers. Keep the original functional error even when cleanup succeeds.
5. If proof is unavailable, retain owner and buffer references and quarantine. Read-only inspection may continue. Do not add another loader, force-kill the owner, free CMA memory, or treat an SSH timeout as permission to reload.

## Evidence and limits

The motivating RTL simulation exercised the actual control/bridge/frame dependency, old deadlock, abort-before-HOLD, and outstanding-DMA drain. Host tests exercised bad/partial final packets, stale run state and unsafe cleanup. A later board test injected an observer failure and verified retirement/restoration while preserving the original failure. These validate those implementations and inputs; they do not establish that every PYNQ design has the same dependency.

When applying this workflow elsewhere, replace all protocol assumptions with the actual RTL/driver contract and test an unsafe case as well as a safe case. If a design requires a different sequence, preserve its verified sequence and explain the dependency. No power-cycle, reprogramming or external account action is implied by reading this file.
