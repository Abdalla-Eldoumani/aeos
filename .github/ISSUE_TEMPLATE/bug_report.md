---
name: Bug report
about: A reproducible failure in AEOS — crash, hang, wrong output, doc that lies about behaviour.
labels: bug
---

### What you ran

The exact command. If you ran a sequence inside the shell, paste the whole sequence.

```
make run-ramfb
```

### What you expected

What should have happened.

### What actually happened

What did happen. If the kernel halted on an exception, paste the contents of `build/crash.log`.

```
[paste boot log + EXCEPTION block here]
```

### Environment

- Host OS (e.g. Ubuntu 24.04 in WSL2 on Windows 11):
- QEMU version (`qemu-system-aarch64 --version`):
- Toolchain version (`aarch64-linux-gnu-gcc --version`):
- Commit you built from (`git rev-parse HEAD`):

### Reproducer

Smallest sequence of inputs that triggers the failure. Trim every step that isn't required.
