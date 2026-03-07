---
name: trafik-tasks
description: Task runner for the trafik hallway transit sign monorepo. Use this skill to run, test, build, flash, or deploy any part of the stack — backend (Go), frontend (Svelte), or firmware (ESP32-C3).
compatibility: Requires mask, go 1.26+, node 22+, platformio (pio), docker, gcloud CLI
---

Tasks are defined in `maskfile.md` at the project root and run with `mask <task>`.

To discover available tasks, either read `maskfile.md` or run:

```bash
mask --introspect
```

To run a task:

```bash
mask <task-name>
```
